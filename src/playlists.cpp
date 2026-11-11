#include "playlists.h"
#include <fcntl.h>
#include "netpath.h"
#include "photo.h"
#include "playlist.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
		}

		std::string lower(std::string s)
		{
			for (auto& c : s)
				if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
			return s;
		}

		bool hasControl(const std::string& s)
		{
			for (unsigned char c : s)
				if (c < 0x20 || c == 0x7f) return true;
			return false;
		}

		bool startsWithNoCase(const std::string& s, const char* p)
		{
			size_t n = strlen(p);
			return s.size() >= n && lower(s.substr(0, n)) == p;
		}

		std::string baseName(const std::string& p)
		{
			size_t i = p.find_last_of('/');
			return i == std::string::npos ? p : p.substr(i + 1);
		}

		std::string stemOf(const std::string& name)
		{
			size_t dot = name.find_last_of('.');
			return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
		}

		int hexVal(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}

		std::string percentDecode(const std::string& s)
		{
			std::string o;
			for (size_t i = 0; i < s.size(); i++)
			{
				int h, l;
				if (s[i] == '%' && i + 2 < s.size() && (h = hexVal(s[i + 1])) >= 0 && (l = hexVal(s[i + 2])) >= 0)
				{
					o += char(h * 16 + l);
					i += 2;
				}
				else o += s[i];
			}
			return o;
		}

		bool isMedia(const std::string& path)
		{
			return isVideoFile(path) || isAudioFile(path);
		}

		// The comma that ends #EXTINF's attributes: the first one outside quotes
		size_t extinfComma(const std::string& s)
		{
			bool q = false;
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] == '"') q = !q;
				else if (s[i] == ',' && !q) return i;
			}
			return std::string::npos;
		}

		std::string oneLine(std::string s)
		{
			for (auto& c : s)
				if ((unsigned char)c < 0x20 || c == 0x7f) c = ' ';
			return s;
		}

		// Cuts at `max` bytes without splitting a UTF-8 sequence
		std::string cutUtf8(const std::string& s, size_t max)
		{
			if (s.size() <= max) return s;
			size_t n = max;
			while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
			return s.substr(0, n);
		}

		std::vector<int> identity(size_t n)
		{
			std::vector<int> r(n);
			for (size_t i = 0; i < n; i++) r[i] = int(i);
			return r;
		}

		// Rebuilds `v` in the order `order` (old indexes) and gives the remap
		std::vector<int> reorder(std::vector<PlaylistEntry>& v, const std::vector<int>& order)
		{
			std::vector<PlaylistEntry> nv;
			nv.reserve(v.size());
			std::vector<int> remap(v.size(), -1);
			for (size_t k = 0; k < order.size(); k++)
			{
				nv.push_back(std::move(v[order[k]]));
				remap[order[k]] = int(k);
			}
			v = std::move(nv);
			return remap;
		}

		bool inRange(int i, size_t n) { return i >= 0 && size_t(i) < n; }

		bool readFile(const std::string& path, std::string& out)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) return false;
			std::ostringstream ss;
			ss << f.rdbuf();
			out = ss.str();
			return true;
		}

		bool exists(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0;
		}

		bool isM3uName(const std::string& n)
		{
			if (n.empty() || n[0] == '.') return false;
			std::string l = lower(n);
			auto ends = [&](const char* e) { size_t k = strlen(e); return l.size() > k && l.compare(l.size() - k, k, e) == 0; };
			return ends(".m3u") || ends(".m3u8");
		}
	}

	std::string entryTitle(const std::string& path)
	{
		return stemOf(isNetPath(path) ? netBaseName(path) : baseName(path));
	}

	bool parseM3u(const std::string& textIn, const std::string& m3uDir, Playlist& out, int& skipped)
	{
		skipped = 0;
		out.entries.clear();
		out.name.clear();
		if (textIn.size() > PL_MAX_FILE_BYTES) return false;
		std::string text = textIn;
		if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);

		bool haveInf = false;
		double infDur = -1;
		std::string infTitle;
		size_t pos = 0;
		while (pos < text.size())
		{
			size_t nl = text.find('\n', pos);
			if (nl == std::string::npos) nl = text.size();
			std::string line = text.substr(pos, nl - pos);
			pos = nl + 1;
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.size() > PL_MAX_LINE_BYTES)
			{
				skipped++;
				haveInf = false;
				continue;
			}
			line = trim(line);
			if (line.empty()) continue;
			if (line[0] == '#')
			{
				if (line.compare(0, 9, "#PLAYLIST") == 0 && line.size() > 9 && line[9] == ':')
					out.name = trim(line.substr(10));
				else if (line.compare(0, 8, "#EXTINF:") == 0)
				{
					std::string rest = line.substr(8);
					size_t c = extinfComma(rest);
					std::string head = c == std::string::npos ? rest : rest.substr(0, c);
					infTitle = c == std::string::npos ? std::string() : trim(rest.substr(c + 1));
					infDur = strtod(head.c_str(), nullptr);
					if (head.empty() || infDur < 0) infDur = -1;
					haveInf = true;
				}
				continue;
			}

			std::string path = line;
			bool ok = true;
			if (startsWithNoCase(path, "file://"))
			{
				path = path.substr(7);
				if (!path.empty() && path[0] != '/')
				{
					size_t s = path.find('/');	// a host such as "localhost"
					path = s == std::string::npos ? std::string() : path.substr(s);
				}
				path = percentDecode(path);
			}
			else if (startsWithNoCase(path, "bpnet://"))
				ok = isNetPath(path);
			else if (path.find("://") != std::string::npos && path.find("://") < path.find('/'))
				ok = false;
			else if (path[0] != '/' && !m3uDir.empty())
				path = m3uDir + (m3uDir.back() == '/' ? "" : "/") + path;

			if (ok && (path.empty() || !isMedia(path))) ok = false;	// photos and the rest
			if (ok && out.entries.size() >= PL_MAX_ENTRIES) ok = false;
			if (!ok)
			{
				skipped++;
				haveInf = false;
				continue;
			}
			PlaylistEntry e;
			e.path = path;
			e.title = haveInf && !infTitle.empty() ? infTitle : entryTitle(path);
			e.duration = haveInf ? infDur : -1;
			out.entries.push_back(std::move(e));
			haveInf = false;
		}
		if (out.name.empty() && !out.file.empty()) out.name = stemOf(baseName(out.file));
		return true;
	}

	std::string serializeM3u(const Playlist& p)
	{
		std::string o = "#EXTM3U\n#PLAYLIST:" + oneLine(p.name) + "\n";
		char buf[48];
		for (const auto& e : p.entries)
		{
			snprintf(buf, sizeof buf, "%ld", e.duration >= 0 ? long(e.duration + 0.5) : -1L);
			o += "#EXTINF:";
			o += buf;
			o += "," + oneLine(e.title) + "\n" + oneLine(e.path) + "\n";
		}
		return o;
	}

	PlNameError checkPlaylistName(const std::string& nameIn, const std::vector<std::string>& existing, const std::string& renaming)
	{
		std::string name = trim(nameIn);
		if (name.empty()) return PlNameError::Empty;
		if (name.size() > PL_NAME_MAX_BYTES) return PlNameError::TooLong;
		if (hasControl(name) || name.find('/') != std::string::npos) return PlNameError::BadChar;
		std::string l = lower(name), own = lower(renaming);
		for (const auto& e : existing)
		{
			std::string el = lower(e);
			if (el == l && !(!renaming.empty() && el == own)) return PlNameError::Taken;
		}
		return PlNameError::None;
	}

	std::string playlistFileName(const std::string& name)
	{
		std::string s;
		for (unsigned char c : name)
		{
			if (c < 0x20 || c == 0x7f || strchr("/\\:*?\"<>|", c)) s += '_';
			else s += char(c);
		}
		auto trimEnds = [](std::string& t)
		{
			size_t a = t.find_first_not_of(" .");
			t = a == std::string::npos ? std::string() : t.substr(a);
			size_t b = t.find_last_not_of(" .");
			t = b == std::string::npos ? std::string() : t.substr(0, b + 1);
		};
		trimEnds(s);
		s = cutUtf8(s, 60);
		trimEnds(s);
		if (s.empty()) s = "Playlist";
		return s + ".m3u";
	}

	std::vector<int> addEntries(std::vector<PlaylistEntry>& v, const std::vector<PlaylistEntry>& add, int at, size_t cap, int& dropped)
	{
		dropped = 0;
		size_t room = cap > v.size() ? cap - v.size() : 0;
		size_t n = std::min(room, add.size());
		dropped = int(add.size() - n);
		size_t pos = at < 0 || size_t(at) > v.size() ? v.size() : size_t(at);
		std::vector<int> remap(v.size());
		for (size_t i = 0; i < v.size(); i++) remap[i] = int(i < pos ? i : i + n);
		v.insert(v.begin() + pos, add.begin(), add.begin() + n);
		return remap;
	}

	std::vector<int> removeEntries(std::vector<PlaylistEntry>& v, const std::set<int>& sel)
	{
		std::vector<int> order;
		for (size_t i = 0; i < v.size(); i++)
			if (!sel.count(int(i))) order.push_back(int(i));
		return reorder(v, order);
	}

	std::vector<int> moveSelection(std::vector<PlaylistEntry>& v, const std::set<int>& sel, int dir)
	{
		size_t n = v.size();
		std::vector<int> order = identity(n);
		std::vector<char> on(n, 0);
		for (int i : sel)
			if (inRange(i, n)) on[i] = 1;
		if (dir < 0)
		{
			for (size_t i = 1; i < n; i++)
				if (on[i] && !on[i - 1])
				{
					std::swap(on[i], on[i - 1]);
					std::swap(order[i], order[i - 1]);
				}
		}
		else
		{
			for (size_t i = n; i-- > 1;)
				if (on[i - 1] && !on[i])
				{
					std::swap(on[i], on[i - 1]);
					std::swap(order[i], order[i - 1]);
				}
		}
		return reorder(v, order);
	}

	std::vector<int> moveEntriesTo(std::vector<PlaylistEntry>& v, const std::set<int>& sel, int at)
	{
		std::vector<int> rest, picked;
		for (size_t i = 0; i < v.size(); i++)
			(sel.count(int(i)) ? picked : rest).push_back(int(i));
		size_t pos = at < 0 ? 0 : std::min(size_t(at), rest.size());
		std::vector<int> order(rest.begin(), rest.begin() + pos);
		order.insert(order.end(), picked.begin(), picked.end());
		order.insert(order.end(), rest.begin() + pos, rest.end());
		return reorder(v, order);
	}

	std::vector<int> gatherSelection(std::vector<PlaylistEntry>& v, const std::set<int>& sel)
	{
		size_t n = v.size();
		int first = -1;
		for (int i : sel)
			if (inRange(i, n)) { first = i; break; }
		if (first < 0) return identity(n);
		std::vector<int> order;
		for (int i = 0; i < first; i++)
			if (!sel.count(i)) order.push_back(i);
		for (int i : sel)
			if (inRange(i, n)) order.push_back(i);
		for (size_t i = first; i < n; i++)
			if (!sel.count(int(i))) order.push_back(int(i));
		return reorder(v, order);
	}

	std::vector<int> removeDuplicates(std::vector<PlaylistEntry>& v, int& removed)
	{
		std::set<std::string> seen;
		std::vector<int> order;
		for (size_t i = 0; i < v.size(); i++)
			if (seen.insert(v[i].path).second) order.push_back(int(i));
		removed = int(v.size() - order.size());
		return reorder(v, order);
	}

	std::set<int> remapSelection(const std::set<int>& sel, const std::vector<int>& remap)
	{
		std::set<int> o;
		for (int i : sel)
			if (inRange(i, remap.size()) && remap[i] >= 0) o.insert(remap[i]);
		return o;
	}

	int remapIndex(int i, const std::vector<int>& remap)
	{
		if (remap.empty()) return -1;
		if (i < 0) i = 0;
		if (size_t(i) >= remap.size()) i = int(remap.size()) - 1;
		for (size_t j = i; j < remap.size(); j++)
			if (remap[j] >= 0) return remap[j];
		for (size_t j = i; j-- > 0;)
			if (remap[j] >= 0) return remap[j];
		return -1;
	}

	std::vector<int> shuffleOrder(int n, int first, uint64_t seed)
	{
		std::vector<int> r;
		if (n <= 0) return r;
		for (int i = 0; i < n; i++)
			if (i != first) r.push_back(i);
		// splitmix64: the same order for a seed on every standard library, which std::shuffle doesn't promise
		uint64_t s = seed;
		auto next = [&s]()
		{
			uint64_t z = (s += 0x9E3779B97F4A7C15ull);
			z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
			z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
			return z ^ (z >> 31);
		};
		for (size_t i = r.size(); i > 1; i--) std::swap(r[i - 1], r[next() % i]);
		if (first >= 0 && first < n) r.insert(r.begin(), first);
		return r;
	}

	std::vector<PlaylistEntry> folderEntries(const std::string& dir, const std::vector<std::string>& names, int& skippedPhotos)
	{
		skippedPhotos = 0;
		std::vector<std::string> keep;
		for (const auto& n : names)
		{
			if (n.empty() || n[0] == '.') continue;
			if (isVideoFile(n) || isAudioFile(n)) keep.push_back(n);
			else if (isPhotoFile(n)) skippedPhotos++;
		}
		std::sort(keep.begin(), keep.end(), naturalLess);
		std::vector<PlaylistEntry> out;
		for (const auto& n : keep)
		{
			PlaylistEntry e;
			e.path = isNetPath(dir) ? netJoin(dir, n) : (dir.empty() || dir.back() == '/' ? dir : dir + "/") + n;
			e.title = stemOf(n);
			out.push_back(std::move(e));
		}
		return out;
	}

	bool writeFileAtomic(const std::string& path, const std::string& text)
	{
		std::string tmp = path + ".tmp";
		{
			std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
			if (!f) return false;
			f.write(text.data(), std::streamsize(text.size()));
			f.flush();
			if (!f) { unlink(tmp.c_str()); return false; }
		}
		if (rename(tmp.c_str(), path.c_str()) != 0)
		{
			unlink(tmp.c_str());
			return false;
		}
		return true;
	}

	bool writeFileDurable(const std::string& path, const std::string& text)
	{
		// as writeFileAtomic, and on the card before it returns: the file's bytes synced before the rename, the folder
		// after it, so a reset (a battery pulled, a crash of the whole handheld) finds the old file or the new one whole
		std::string tmp = path + ".tmp";
		int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0) return false;
		size_t off = 0;
		while (off < text.size())
		{
			ssize_t n = ::write(fd, text.data() + off, text.size() - off);
			if (n <= 0) { ::close(fd); unlink(tmp.c_str()); return false; }
			off += (size_t)n;
		}
		bool ok = ::fsync(fd) == 0;
		if (::close(fd) != 0) ok = false;
		if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
		{
			unlink(tmp.c_str());
			return false;
		}
		std::string dir = path.substr(0, path.rfind('/'));
		int dfd = ::open(dir.empty() ? "/" : dir.c_str(), O_RDONLY);
		if (dfd >= 0)
		{
			::fsync(dfd);
			::close(dfd);
		}
		return true;
	}

	PlaylistStore::PlaylistStore(std::string dir) : dir_(std::move(dir))
	{
		while (dir_.size() > 1 && dir_.back() == '/') dir_.pop_back();
	}

	bool PlaylistStore::load(const std::string& file, Playlist& out, int& skipped) const
	{
		std::string text;
		out = Playlist();
		out.file = file;
		skipped = 0;
		if (!readFile(file, text)) return false;
		size_t s = file.find_last_of('/');
		return parseM3u(text, s == std::string::npos ? std::string() : file.substr(0, s), out, skipped);
	}

	std::vector<PlaylistStore::Info> PlaylistStore::list() const
	{
		std::vector<Info> out;
		if (dir_.empty()) return out;
		DIR* d = opendir(dir_.c_str());
		std::vector<std::string> files;
		if (d)
		{
			while (dirent* e = readdir(d))
				if (isM3uName(e->d_name)) files.push_back(e->d_name);
			closedir(d);
		}
		std::sort(files.begin(), files.end());	// a stable pick of which 500 when there are more
		if (files.size() > PL_MAX_LISTED) files.resize(PL_MAX_LISTED);
		for (const auto& f : files)
		{
			Playlist p;
			int skipped;
			std::string path = dir_ + "/" + f;
			if (!load(path, p, skipped)) continue;
			p.name = trim(cutUtf8(p.name, PL_NAME_MAX_BYTES));
			if (p.name.empty()) p.name = stemOf(f);
			out.push_back({path, p.name, int(p.entries.size())});
		}
		std::stable_sort(out.begin(), out.end(), [](const Info& a, const Info& b) { return naturalLess(a.name, b.name); });
		if (dir_.empty()) return out;
		Playlist liked;
		int skipped;
		load(likedFile(), liked, skipped);	// no file yet is an empty one
		out.insert(out.begin(), {likedFile(), LIKED_NAME, int(liked.entries.size()), true});
		return out;
	}

	std::vector<PlaylistStore::Info> PlaylistStore::addTargets() const
	{
		std::vector<Info> out = list();
		out.erase(std::remove_if(out.begin(), out.end(), [](const Info& i) { return i.builtin; }), out.end());
		return out;
	}

	bool PlaylistStore::isBuiltinFile(const std::string& file) const { return !dir_.empty() && file == likedFile(); }

	bool PlaylistStore::likable(const std::string& path)
	{
		return !path.empty() && !isPhotoFile(path) && (isVideoFile(path) || isAudioFile(path)) &&
			(isNetPath(path) || path[0] == '/');
	}

	void PlaylistStore::likedRefresh() const
	{
		if (likedSerial_ == serial_) return;
		likedSet_.clear();
		Playlist p;
		int skipped;
		if (!dir_.empty() && load(likedFile(), p, skipped))
			for (const auto& e : p.entries) likedSet_.insert(e.path);
		likedSerial_ = serial_;
	}

	bool PlaylistStore::isLiked(const std::string& path) const
	{
		likedRefresh();
		return likedSet_.count(path) > 0;
	}

	int PlaylistStore::likedCount() const
	{
		likedRefresh();
		return int(likedSet_.size());
	}

	bool PlaylistStore::setLiked(const std::string& path, const std::string& title, double duration, bool on, int at,
		std::string* dropped)
	{
		if (dropped) dropped->clear();
		if (dir_.empty()) return false;
		if (on && !likable(path)) return false;
		Playlist p;
		int skipped;
		if (!load(likedFile(), p, skipped)) { p = Playlist(); p.file = likedFile(); }
		p.name = LIKED_NAME;
		bool has = std::any_of(p.entries.begin(), p.entries.end(), [&](const PlaylistEntry& e) { return e.path == path; });
		if (on == has) return true;
		if (on)
		{
			PlaylistEntry e;
			e.path = path;
			e.title = title.empty() ? entryTitle(path) : title;
			e.duration = duration;
			p.entries.insert(p.entries.begin() + std::clamp(at, 0, (int)p.entries.size()), e);
			if (p.entries.size() > PL_MAX_ENTRIES)
			{
				if (dropped) *dropped = p.entries.back().path;
				p.entries.resize(PL_MAX_ENTRIES);
			}
		}
		else
			p.entries.erase(std::remove_if(p.entries.begin(), p.entries.end(), [&](const PlaylistEntry& e) { return e.path == path; }),
				p.entries.end());
		return save(p);
	}

	bool PlaylistStore::containsPath(const std::string& file, const std::string& path) const
	{
		Playlist p;
		int skipped;
		if (!load(file, p, skipped)) return false;
		return std::any_of(p.entries.begin(), p.entries.end(), [&](const PlaylistEntry& e) { return e.path == path; });
	}

	bool PlaylistStore::save(const Playlist& p)
	{
		if (dir_.empty() || p.file.empty()) return false;
		mkdir(dir_.c_str(), 0755);
		bool ok = writeFileAtomic(p.file, serializeM3u(p));
		serial_++;
		return ok;
	}

	// A file name for `name` that isn't taken; `allowSame` (a full path) doesn't count as taken
	std::string PlaylistStore::uniquePath(const std::string& name, const std::string& allowSame) const
	{
		std::string base = playlistFileName(name);
		base.resize(base.size() - 4);
		for (int n = 1;; n++)
		{
			std::string stem = n == 1 ? base : base + " (" + std::to_string(n) + ")";
			std::string path = dir_ + "/" + stem + ".m3u";
			if (path == allowSame || !exists(path)) return path;
		}
	}

	PlNameError PlaylistStore::create(const std::string& nameIn, std::string& fileOut)
	{
		std::vector<std::string> names;
		for (const auto& i : list()) names.push_back(i.name);
		PlNameError e = checkPlaylistName(nameIn, names);
		if (e != PlNameError::None) return e;
		Playlist p;
		p.name = trim(nameIn);
		p.file = uniquePath(p.name, "");
		if (!save(p)) return PlNameError::Empty;	// nothing better to say: the caller shows its own failure
		fileOut = p.file;
		return PlNameError::None;
	}

	PlNameError PlaylistStore::rename(const std::string& file, const std::string& newName, std::string& newFileOut)
	{
		if (isBuiltinFile(file)) return PlNameError::Builtin;
		Playlist p;
		int skipped;
		if (!load(file, p, skipped)) return PlNameError::Empty;
		std::vector<std::string> names;
		for (const auto& i : list()) names.push_back(i.name);
		PlNameError e = checkPlaylistName(newName, names, p.name);
		if (e != PlNameError::None) return e;
		p.name = trim(newName);
		std::string oldFile = file;
		p.file = uniquePath(p.name, oldFile);
		if (!save(p)) return PlNameError::Empty;
		if (p.file != oldFile) unlink(oldFile.c_str());
		newFileOut = p.file;
		return PlNameError::None;
	}

	bool PlaylistStore::remove(const std::string& file)
	{
		if (isBuiltinFile(file)) return false;
		bool ok = unlink(file.c_str()) == 0;
		serial_++;
		return ok;
	}
}
