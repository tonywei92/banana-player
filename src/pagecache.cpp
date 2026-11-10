#include "pagecache.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		const char* INDEX_HEADER = "# banana-player page cache v1 (last use: nanoseconds since the epoch)";

		// FNV-1a over the key's bytes: the same key gives the same name on every device and every run, unlike
		// std::hash, which is free to change between builds.
		uint64_t fnv1a(const std::string& s)
		{
			uint64_t h = 14695981039346656037ull;
			for (unsigned char c : s)
			{
				h ^= c;
				h *= 1099511628211ull;
			}
			return h;
		}

		std::string hexOf(uint64_t v)
		{
			char buf[17];
			snprintf(buf, sizeof buf, "%016llx", (unsigned long long)v);
			return buf;
		}

		// A key is anything (a URL with a tab in it), so the fields are escaped: a line stays a line and a field
		// stays a field.
		std::string escapeField(const std::string& s)
		{
			std::string o;
			o.reserve(s.size());
			for (char c : s)
			{
				if (c == '\t') o += "\\t";
				else if (c == '\n') o += "\\n";
				else if (c == '\r') o += "\\r";
				else if (c == '\\') o += "\\\\";
				else o += c;
			}
			return o;
		}

		std::string unescapeField(const std::string& s)
		{
			std::string o;
			o.reserve(s.size());
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] != '\\' || i + 1 >= s.size())
				{
					o += s[i];
					continue;
				}
				char n = s[++i];
				if (n == 't') o += '\t';
				else if (n == 'n') o += '\n';
				else if (n == 'r') o += '\r';
				else if (n == '\\') o += '\\';
				else
				{
					o += '\\';	// an escape we don't know stays as it was written
					o += n;
				}
			}
			return o;
		}

		std::vector<std::string> splitTabs(const std::string& line)
		{
			std::vector<std::string> f;
			size_t pos = 0;
			for (;;)
			{
				size_t t = line.find('\t', pos);
				if (t == std::string::npos)
				{
					f.push_back(line.substr(pos));
					break;
				}
				f.push_back(line.substr(pos, t - pos));
				pos = t + 1;
			}
			return f;
		}

		bool toUInt(const std::string& s, uint64_t& out)
		{
			if (s.empty()) return false;
			char* end = nullptr;
			unsigned long long v = strtoull(s.c_str(), &end, 10);
			if (!end || *end) return false;
			out = (uint64_t)v;
			return true;
		}

		uint64_t nowNs()
		{
			struct timespec ts;
			clock_gettime(CLOCK_REALTIME, &ts);
			return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
		}

		// The file's own time, in the same unit as the stamps: a page that was never in an index is as old as its
		// file says it is.
		uint64_t mtimeNs(const struct stat& st)
		{
#ifdef __APPLE__
			return (uint64_t)st.st_mtimespec.tv_sec * 1000000000ull + (uint64_t)st.st_mtimespec.tv_nsec;
#else
			return (uint64_t)st.st_mtim.tv_sec * 1000000000ull + (uint64_t)st.st_mtim.tv_nsec;
#endif
		}

		bool endsWith(const std::string& s, const std::string& tail)
		{
			return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
		}

		// The folder and every folder above it: the cache is made when the reader is first switched on, and the
		// state dir's own folders may not be there yet.
		bool makeDirs(const std::string& path)
		{
			if (path.empty()) return false;
			std::string cur;
			size_t i = 0;
			if (path[0] == '/')
			{
				cur = "/";
				i = 1;
			}
			while (i <= path.size())
			{
				size_t j = path.find('/', i);
				if (j == std::string::npos) j = path.size();
				std::string part = path.substr(i, j - i);
				if (!part.empty())
				{
					if (cur.empty() || cur.back() == '/') cur += part;
					else cur += "/" + part;
					if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
				}
				if (j == path.size()) break;
				i = j + 1;
			}
			return true;
		}

		// A file written beside its place and renamed over it: a half page never looks whole.
		bool writeFile(const std::string& path, const uint8_t* data, size_t n)
		{
			std::string tmp = path + ".tmp";
			int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd < 0) return false;
			size_t left = n;
			const uint8_t* p = data;
			while (left > 0)
			{
				ssize_t w = write(fd, p, left);
				if (w <= 0)
				{
					close(fd);
					unlink(tmp.c_str());
					return false;
				}
				p += w;
				left -= size_t(w);
			}
			close(fd);
			if (rename(tmp.c_str(), path.c_str()) != 0)
			{
				unlink(tmp.c_str());
				return false;
			}
			return true;
		}

		bool readFile(const std::string& path, std::vector<uint8_t>& out)
		{
			int fd = open(path.c_str(), O_RDONLY);
			if (fd < 0) return false;
			struct stat st;
			if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
			{
				close(fd);
				return false;
			}
			out.clear();
			out.reserve(size_t(st.st_size));
			uint8_t buf[65536];
			for (;;)
			{
				ssize_t r = read(fd, buf, sizeof buf);
				if (r < 0)
				{
					close(fd);
					out.clear();
					return false;
				}
				if (r == 0) break;
				out.insert(out.end(), buf, buf + r);
			}
			close(fd);
			return true;
		}
	}

	PageCache::~PageCache()
	{
		// The index goes out with the pages: the order they were used in survives a restart, so the reader doesn't
		// have to remember to save it.
		if (dirty_) save();
	}

	bool PageCache::open(const std::string& dir, uint64_t limitBytes)
	{
		entries_.clear();
		total_ = 0;
		stamp_ = 0;
		dirty_ = false;
		limit_ = limitBytes;
		dir_ = dir;
		while (dir_.size() > 1 && dir_.back() == '/') dir_.pop_back();
		if (dir_.empty()) return false;
		if (!makeDirs(dir_)) return false;
		loadIndex();
		scanFiles();
		// The cap is a cap: a folder that was left over a smaller one gives its oldest pages up at once.
		evictToLimit();
		return true;
	}

	void PageCache::setLimit(uint64_t limitBytes)
	{
		limit_ = limitBytes;
		evictToLimit();
	}

	std::string PageCache::pathFor(const std::string& key) const
	{
		// The name is the key's hash and nothing else, so a key can hold any character at all and the path stays
		// inside the folder.
		std::string hex = hexOf(fnv1a(key));
		return dir_ + "/" + hex.substr(0, 2) + "/" + hex + ".bin";
	}

	bool PageCache::has(const std::string& key)
	{
		if (dir_.empty() || key.empty()) return false;
		std::string path = pathFor(key);
		struct stat st;
		if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
		{
			// The page is gone (a file removed, a card pulled): the record of it goes too.
			auto it = entries_.find(path);
			if (it != entries_.end())
			{
				total_ -= it->second.bytes;
				entries_.erase(it);
				dirty_ = true;
			}
			return false;
		}
		auto it = entries_.find(path);
		if (it == entries_.end())
		{
			// The index was lost, but the page is there: the name is the key's hash, so it is found anyway.
			Entry e;
			e.key = key;
			e.bytes = (uint64_t)st.st_size;
			e.lastUse = stamp();
			entries_.emplace(path, e);
			total_ += e.bytes;
		}
		else
		{
			if (it->second.key.empty())
			{
				it->second.key = key;	// the key was lost with the index: it is known again now
			}
			it->second.lastUse = stamp();
		}
		dirty_ = true;
		return true;
	}

	bool PageCache::put(const std::string& key, const uint8_t* data, size_t n)
	{
		if (dir_.empty() || key.empty()) return false;
		if (n && !data) return false;
		// A page bigger than the whole cap would never fit: it is refused instead of emptying the cache for it.
		if ((uint64_t)n > limit_) return false;
		std::string path = pathFor(key);
		size_t slash = path.find_last_of('/');
		if (!makeDirs(path.substr(0, slash))) return false;
		if (!writeFile(path, data, n)) return false;
		Entry e;
		e.key = key;
		e.bytes = (uint64_t)n;
		e.lastUse = stamp();
		auto it = entries_.find(path);
		if (it == entries_.end())
		{
			entries_.emplace(path, e);
			total_ += e.bytes;
		}
		else
		{
			total_ -= it->second.bytes;	// the rename replaced the old page
			it->second = e;
			total_ += e.bytes;
		}
		dirty_ = true;
		evictToLimit();
		return true;
	}

	bool PageCache::get(const std::string& key, std::vector<uint8_t>& out)
	{
		out.clear();
		if (dir_.empty() || key.empty()) return false;
		std::string path = pathFor(key);
		std::vector<uint8_t> data;
		if (!readFile(path, data))
		{
			auto it = entries_.find(path);
			if (it != entries_.end())
			{
				total_ -= it->second.bytes;
				entries_.erase(it);
				dirty_ = true;
			}
			return false;
		}
		out.swap(data);
		auto it = entries_.find(path);
		if (it == entries_.end())
		{
			// The index was lost, but the page is there: the name is the key's hash, so it is found anyway.
			Entry e;
			e.key = key;
			e.bytes = (uint64_t)out.size();
			e.lastUse = stamp();
			entries_.emplace(path, e);
			total_ += e.bytes;
		}
		else
		{
			if (it->second.key.empty())
			{
				it->second.key = key;	// the key was lost with the index: it is known again now
			}
			total_ -= it->second.bytes;	// the file is what it is, not what the index said
			it->second.bytes = (uint64_t)out.size();
			it->second.lastUse = stamp();
			total_ += it->second.bytes;
		}
		dirty_ = true;
		return true;
	}

	void PageCache::clear()
	{
		removeContents(dir_);
		entries_.clear();
		total_ = 0;
		// The index is gone with the pages and stays gone: an empty index would only say the same.
		dirty_ = false;
	}

	uint64_t PageCache::size() const
	{
		return total_;
	}

	size_t PageCache::count() const
	{
		return entries_.size();
	}

	bool PageCache::save() const
	{
		if (dir_.empty()) return false;
		// The pages in the order they were used, the oldest first: the file says what goes first when the cap is
		// reached.
		std::vector<const Entry*> order;
		order.reserve(entries_.size());
		for (const auto& kv : entries_) order.push_back(&kv.second);
		std::sort(order.begin(), order.end(), [](const Entry* a, const Entry* b) {
			if (a->lastUse != b->lastUse) return a->lastUse < b->lastUse;
			return a->key < b->key;
		});
		std::string text = INDEX_HEADER;
		text += "\n";
		for (const Entry* e : order)
		{
			// A page whose index was lost keeps its file: its key is only known again when the page is asked for.
			if (e->key.empty()) continue;
			text += escapeField(e->key) + "\t" + std::to_string(e->bytes) + "\t" + std::to_string(e->lastUse) + "\n";
		}
		if (!writeFile(dir_ + "/index.tsv", (const uint8_t*)text.data(), text.size())) return false;
		dirty_ = false;
		return true;
	}

	void PageCache::loadIndex()
	{
		std::vector<uint8_t> raw;
		if (!readFile(dir_ + "/index.tsv", raw)) return;	// no index: the files themselves are what we have
		std::string text((const char*)raw.data(), raw.size());
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			std::vector<std::string> f = splitTabs(line);
			if (f.size() < 3) continue;	// a line we can't read is skipped: a damaged index never keeps the reader from starting
			std::string key = unescapeField(f[0]);
			if (key.empty()) continue;
			uint64_t bytes = 0, lastUse = 0;
			if (!toUInt(f[1], bytes)) continue;
			std::string path = pathFor(key);
			struct stat st;
			if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;	// the page is gone: it is dropped
			Entry e;
			e.key = key;
			e.bytes = (uint64_t)st.st_size;	// the file says how big it is, not the index
			if (toUInt(f[2], lastUse)) e.lastUse = lastUse;
			else e.lastUse = mtimeNs(st);	// a last use we can't read comes from the file's own time
			if (e.lastUse > stamp_) stamp_ = e.lastUse;
			auto it = entries_.find(path);
			if (it != entries_.end())
			{
				total_ -= it->second.bytes;
				it->second = e;
				total_ += e.bytes;
			}
			else
			{
				entries_.emplace(path, e);
				total_ += e.bytes;
			}
		}
	}

	void PageCache::scanFiles()
	{
		// The files on disk are what the folder holds: the index may be lost, and a page the index doesn't know
		// still counts (and is given up like any other when the cap is reached).
		std::vector<std::string> subs;
		DIR* d = opendir(dir_.c_str());
		if (!d) return;
		while (struct dirent* e = readdir(d))
		{
			std::string name = e->d_name;
			if (name == "." || name == "..") continue;
			std::string path = dir_ + "/" + name;
			struct stat st;
			if (stat(path.c_str(), &st) != 0) continue;
			if (S_ISDIR(st.st_mode))
			{
				subs.push_back(path);
				continue;
			}
			if (!S_ISREG(st.st_mode) || !endsWith(name, ".bin")) continue;
			addFile(path, (uint64_t)st.st_size, mtimeNs(st));
		}
		closedir(d);
		for (const std::string& sub : subs)
		{
			DIR* sd = opendir(sub.c_str());
			if (!sd) continue;
			while (struct dirent* e = readdir(sd))
			{
				std::string name = e->d_name;
				if (name == "." || name == "..") continue;
				if (!endsWith(name, ".bin")) continue;
				std::string path = sub + "/" + name;
				struct stat st;
				if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
				addFile(path, (uint64_t)st.st_size, mtimeNs(st));
			}
			closedir(sd);
		}
	}

	void PageCache::addFile(const std::string& path, uint64_t bytes, uint64_t lastUse)
	{
		if (entries_.count(path)) return;	// the index knew it already
		Entry e;
		e.bytes = bytes;
		e.lastUse = lastUse;
		if (e.lastUse > stamp_) stamp_ = e.lastUse;
		entries_.emplace(path, e);
		total_ += e.bytes;
	}

	void PageCache::evictToLimit()
	{
		while (total_ > limit_ && !entries_.empty())
		{
			// The least recently used goes first. The path breaks a tie, so the same pages go every time.
			auto oldest = entries_.end();
			for (auto it = entries_.begin(); it != entries_.end(); ++it)
			{
				if (oldest == entries_.end() || it->second.lastUse < oldest->second.lastUse ||
				    (it->second.lastUse == oldest->second.lastUse && it->first < oldest->first))
					oldest = it;
			}
			unlink(oldest->first.c_str());
			total_ -= oldest->second.bytes;
			entries_.erase(oldest);
			dirty_ = true;
		}
	}

	void PageCache::removeContents(const std::string& dir)
	{
		DIR* d = opendir(dir.c_str());
		if (!d) return;
		std::vector<std::string> subs;
		while (struct dirent* e = readdir(d))
		{
			std::string name = e->d_name;
			if (name == "." || name == "..") continue;
			std::string path = dir + "/" + name;
			struct stat st;
			if (stat(path.c_str(), &st) != 0) continue;
			if (S_ISDIR(st.st_mode))
			{
				subs.push_back(path);
				continue;
			}
			unlink(path.c_str());
		}
		closedir(d);
		for (const std::string& sub : subs)
		{
			removeContents(sub);
			rmdir(sub.c_str());
		}
	}

	uint64_t PageCache::stamp()
	{
		// Every use is later than the one before, so the order is exact even inside the same second: the clock
		// alone would call two pages used in the same instant equally old.
		uint64_t now = nowNs();
		if (now <= stamp_) now = stamp_ + 1;
		stamp_ = now;
		return now;
	}
}
