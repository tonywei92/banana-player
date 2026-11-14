#include "xfer.h"
#include "treecopy.h"
#include "netresolve.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <random>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <thread>
#include <unistd.h>
#include <vector>
#ifdef __linux__
#include <sys/vfs.h>
#endif

namespace bp
{
	namespace
	{
		// a thing to copy: its place under the top ("" the top itself), a folder or a file and its size
		struct Item
		{
			std::string rel;
			bool dir = false;
			uint64_t size = 0;
		};

		std::string joinRel(const std::string& base, const std::string& rel)
		{
			if (rel.empty()) return base;
			return (base == "/" ? "" : base) + "/" + rel;
		}

		std::string parentOf(const std::string& p)
		{
			size_t s = p.rfind('/');
			return s == std::string::npos || s == 0 ? "/" : p.substr(0, s);
		}

		std::string baseOf(const std::string& p)
		{
			size_t s = p.rfind('/');
			return s == std::string::npos ? p : p.substr(s + 1);
		}

		std::string sizeText(uint64_t b)
		{
			char out[32];
			if (b >= (1ull << 30)) snprintf(out, sizeof out, "%.1f GB", b / 1073741824.0);
			else if (b >= (1ull << 20)) snprintf(out, sizeof out, "%.1f MB", b / 1048576.0);
			else snprintf(out, sizeof out, "%llu KB", (unsigned long long)(b / 1024));
			return out;
		}

		std::string hiddenName()
		{
			static std::mt19937_64 rng((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
			char b[40];
			snprintf(b, sizeof b, ".bp-part-%llx", (unsigned long long)(rng() & 0xffffffffffull));
			return b;
		}

		// the cards' side: every folder and plain file under path (false: a link or a device in it, or unreadable)
		bool walkLocal(const std::string& path, const std::string& rel, std::vector<Item>& out, std::string& bad)
		{
			struct stat st;
			if (lstat(path.c_str(), &st) != 0) { bad = rel; return false; }
			if (S_ISREG(st.st_mode)) { out.push_back({rel, false, (uint64_t)st.st_size}); return true; }
			if (!S_ISDIR(st.st_mode)) { bad = rel; return false; }
			out.push_back({rel, true, 0});
			DIR* d = opendir(path.c_str());
			if (!d) { bad = rel; return false; }
			std::vector<std::string> names;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n != "." && n != "..") names.push_back(n);
			}
			closedir(d);
			for (const auto& n : names)
				if (!walkLocal(path + "/" + n, rel.empty() ? n : rel + "/" + n, out, bad)) return false;
			return true;
		}

		// a server's side: a file's size comes from its folder's listing
		NetResult walkNet(NetBackend& be, const std::string& path, bool dir, uint64_t size, const std::string& rel,
			std::vector<Item>& out, const std::atomic<bool>* cancel)
		{
			if (!dir) { out.push_back({rel, false, size}); return NetResult(); }
			out.push_back({rel, true, 0});
			std::vector<NetEntry> v;
			NetResult r = be.list(path, v);
			if (!r.ok()) return r;
			for (const NetEntry& e : v)
			{
				if (cancel && cancel->load()) { r.error = NetError::Cancelled; return r; }
				r = walkNet(be, joinRel(path, e.name), e.dir, e.size > 0 ? (uint64_t)e.size : 0,
					rel.empty() ? e.name : rel + "/" + e.name, out, cancel);
				if (!r.ok()) return r;
			}
			return r;
		}

		// is the top a folder, and its size: from its parent's listing
		NetResult statNet(NetBackend& be, const std::string& path, bool& found, bool& dir, uint64_t& size)
		{
			found = false;
			std::vector<NetEntry> v;
			NetResult r = be.list(parentOf(path), v);
			if (!r.ok()) return r;
			std::string name = baseOf(path);
			for (const NetEntry& e : v)
				if (e.name == name)
				{
					found = true;
					dir = e.dir;
					size = e.size > 0 ? (uint64_t)e.size : 0;
				}
			return r;
		}

		// a free name in a server folder: itself, else "name (2).ext"...
		NetResult freeNetName(NetBackend& be, const std::string& dir, std::string& name, bool folder)
		{
			std::vector<NetEntry> v;
			NetResult r = be.list(dir, v);
			if (!r.ok()) return r;
			auto taken = [&](const std::string& n) { return std::any_of(v.begin(), v.end(), [&](const NetEntry& e) { return e.name == n; }); };
			if (!taken(name)) return r;
			size_t dot = folder ? std::string::npos : name.rfind('.');
			if (dot == 0) dot = std::string::npos;
			std::string base = dot == std::string::npos ? name : name.substr(0, dot), ext = dot == std::string::npos ? "" : name.substr(dot);
			for (int n = 2; n < 1000; n++)
			{
				std::string c = base + " (" + std::to_string(n) + ")" + ext;
				if (!taken(c)) { name = c; return r; }
			}
			r.error = NetError::Other;
			r.detail = "no free name";
			return r;
		}

		std::string freeLocalName(const std::string& dir, const std::string& name, bool folder)
		{
			struct stat st;
			if (lstat((dir + "/" + name).c_str(), &st) != 0) return name;
			size_t dot = folder ? std::string::npos : name.rfind('.');
			if (dot == 0) dot = std::string::npos;
			std::string base = dot == std::string::npos ? name : name.substr(0, dot), ext = dot == std::string::npos ? "" : name.substr(dot);
			for (int n = 2; n < 1000; n++)
			{
				std::string c = base + " (" + std::to_string(n) + ")" + ext;
				if (lstat((dir + "/" + c).c_str(), &st) != 0) return c;
			}
			return "";
		}

		// a server's file or folder and all in it, gone (files first, the deepest folders before their parents)
		NetResult removeNet(NetBackend& be, const std::string& top, const std::vector<Item>& items)
		{
			NetResult last;
			for (auto it = items.rbegin(); it != items.rend(); ++it)
			{
				NetResult r = be.remove(joinRel(top, it->rel), it->dir);
				if (!r.ok()) last = r;
			}
			return last;
		}

		std::unique_ptr<NetBackend> backendFor(const XferEnd& e, std::string& err)
		{
			// a computer found by its name ("TONY-UBUNTU", "nas.local"): its address now, as the browser and the
			// player do (getaddrinfo alone doesn't know the names a Windows or Samba machine answers to)
			NetServer s = e.server;
			s.host = hostAddress(s.host);
			auto be = makeBackend(s, e.password, 15000);
			if (!be) err = "Network folders aren't available in this build";
			return be;
		}
	}

	std::string xferErrorText(const NetResult& r)
	{
		switch (r.error)
		{
		case NetError::None: return "";
		case NetError::Cancelled: return "Cancelled: nothing was changed";
		case NetError::NoAccess: return "The server refused it: this folder is read only for this account (or the name is taken)";
		case NetError::NotFound: return "It (or its server) can't be found: is the computer on, and on this Wi-Fi?";
		case NetError::Auth: return "The server didn't accept the user name or password";
		case NetError::NoNetwork: return "No network: nothing was changed";
		case NetError::Timeout: case NetError::Gone: return "The connection was lost: nothing was changed";
		default: return "The server couldn't do it (" + r.detail + ")";
		}
	}

	std::string xferCopy(const XferEnd& src, const XferEnd& dest, std::string& name, bool move, XferProgress& pr)
	{
		std::string err;
		std::unique_ptr<NetBackend> sb, db;
		if (src.net && !(sb = backendFor(src, err))) return err;
		if (dest.net && !(db = backendFor(dest, err))) return err;
		auto cancelled = [&] { return pr.cancel && pr.cancel->load(); };
		auto netFail = [&](const NetResult& r) { return xferErrorText(r); };

		// what there is to copy
		std::vector<Item> items;
		bool topDir = false;
		if (src.net)
		{
			bool found = false;
			uint64_t size = 0;
			NetResult r = statNet(*sb, src.path, found, topDir, size);
			if (!r.ok()) return netFail(r);
			if (!found) return "It isn't there any more";
			r = walkNet(*sb, src.path, topDir, size, "", items, pr.cancel);
			if (!r.ok()) return netFail(r);
		}
		else
		{
			std::string bad;
			if (!walkLocal(src.path, "", items, bad))
				return bad.empty() ? "It can't be read" : "\"" + bad + "\" in it can't be copied (a link, or unreadable)";
			topDir = !items.empty() && items[0].dir;
		}
		uint64_t total = 0, biggest = 0;
		for (const Item& i : items) { total += i.size; biggest = std::max(biggest, i.size); }
		if (pr.total) *pr.total = total;

		// one server, a move: a rename does it at once (within one SMB share)
		if (move && src.net && dest.net && src.server.id == dest.server.id)
		{
			NetResult r = freeNetName(*db, dest.path, name, topDir);
			if (!r.ok()) return netFail(r);
			r = db->rename(src.path, joinRel(dest.path, name));
			if (r.ok()) { if (pr.done) *pr.done = total; return ""; }
			if (r.error != NetError::Other) return netFail(r);
		}

		// the name it gets there, and room for it on a card
		if (dest.net)
		{
			NetResult r = freeNetName(*db, dest.path, name, topDir);
			if (!r.ok()) return netFail(r);
		}
		else
		{
			name = freeLocalName(dest.path, name, topDir);
			if (name.empty()) return "No free name for it here";
			struct statvfs vs;
			if (statvfs(dest.path.c_str(), &vs) == 0)
			{
				uint64_t room = (uint64_t)vs.f_bavail * vs.f_frsize, margin = 64ull << 20;
				if (total + margin > room) return "Not enough room: it needs " + sizeText(total) + ", " + sizeText(room > margin ? room - margin : 0) + " is free";
			}
#ifdef __linux__
			struct statfs fs;
			if (biggest >= (4ull << 30) && statfs(dest.path.c_str(), &fs) == 0 && fs.f_type == 0x4d44)
				return "That card (FAT) can't hold a file of 4 GB or more";
#endif
		}

		// written under a hidden name, then put in place
		std::string tmp = joinRel(dest.path, hiddenName());
		std::vector<Item> made;	// what was written, to take away again on a failure
		std::string fail;
		std::vector<char> buf(1 << 20);
		static const int slow = getenv("BP_COPY_SLOW_MS") ? atoi(getenv("BP_COPY_SLOW_MS")) : 0;
		for (const Item& it : items)
		{
			if (cancelled()) { fail = "Cancelled: nothing was changed"; break; }
			std::string from = joinRel(src.path, it.rel), to = joinRel(tmp, it.rel);
			if (pr.current) pr.current(it.rel.empty() ? name : it.rel);
			if (it.dir)
			{
				if (dest.net)
				{
					NetResult r = db->makeDir(to);
					if (!r.ok()) { fail = netFail(r); break; }
				}
				else if (mkdir(to.c_str(), 0755) != 0) { fail = std::string("It couldn't be copied: ") + strerror(errno); break; }
				made.push_back(it);
				continue;
			}
			// a file: read from one end, written to the other, a piece at a time
			std::unique_ptr<NetFile> nf;
			int in = -1;
			if (src.net)
			{
				NetResult r;
				nf = sb->open(from, r);
				if (!nf) { fail = netFail(r); break; }
			}
			else if ((in = open(from.c_str(), O_RDONLY)) < 0) { fail = "\"" + it.rel + "\" can't be read"; break; }
			int64_t off = 0;
			bool readFailed = false;
			auto more = [&](char* b, int64_t n) -> int64_t {
				if (cancelled()) return -1;
				if (slow > 0) std::this_thread::sleep_for(std::chrono::milliseconds(slow));
				int64_t got = src.net ? nf->read(b, n, off) : (int64_t)read(in, b, (size_t)n);
				if (got < 0) { readFailed = true; return -1; }
				off += got;
				if (pr.done) *pr.done += (uint64_t)got;
				return got;
			};
			if (dest.net)
			{
				NetResult r = db->put(to, more);
				if (!r.ok()) fail = readFailed ? (src.net ? netFail(nf->lastResult()) : "\"" + it.rel + "\" couldn't be read") : cancelled() ? "Cancelled: nothing was changed" : netFail(r);
			}
			else
			{
				int out = open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
				if (out < 0) fail = std::string("It couldn't be copied: ") + strerror(errno);
				else
				{
					for (;;)
					{
						int64_t n = more(buf.data(), (int64_t)buf.size());
						if (n < 0) { fail = readFailed ? (src.net ? netFail(nf->lastResult()) : "\"" + it.rel + "\" couldn't be read") : "Cancelled: nothing was changed"; break; }
						if (n == 0) break;
						int64_t w = 0;
						while (w < n)
						{
							ssize_t k = write(out, buf.data() + w, (size_t)(n - w));
							if (k < 0) { if (errno == EINTR) continue; fail = std::string("It couldn't be copied: ") + strerror(errno); break; }
							w += k;
						}
						if (!fail.empty()) break;
					}
					if (fail.empty() && fsync(out) != 0) fail = std::string("It couldn't be copied: ") + strerror(errno);
					close(out);
					if (!fail.empty()) unlink(to.c_str());
				}
			}
			if (in >= 0) close(in);
			if (!fail.empty()) break;
			made.push_back(it);
		}

		// in place, or taken away again
		std::string target = joinRel(dest.path, name);
		if (fail.empty())
		{
			if (dest.net)
			{
				NetResult r = db->rename(tmp, target);
				if (!r.ok()) fail = netFail(r);
			}
			else
			{
				int r = renameNoReplace(tmp, target);
				if (r != 0) fail = std::string("It couldn't be copied: ") + strerror(r);
			}
		}
		if (!fail.empty())
		{
			if (dest.net) removeNet(*db, tmp, made);
			else removeTree(tmp);
			return fail;
		}
		// a move: the original goes, now that the copy is whole
		if (move)
		{
			if (src.net)
			{
				NetResult r = removeNet(*sb, src.path, items);
				if (!r.ok()) return "Copied, but the original couldn't be deleted: " + netFail(r);
			}
			else removeTree(src.path);
		}
		return "";
	}

	std::string xferRename(const XferEnd& target, const std::string& newName)
	{
		std::string err;
		auto be = backendFor(target, err);
		if (!be) return err;
		NetResult r = be->rename(target.path, joinRel(parentOf(target.path), newName));
		return r.ok() ? "" : xferErrorText(r);
	}

	std::string xferDelete(const XferEnd& target, const std::atomic<bool>* cancel)
	{
		std::string err;
		auto be = backendFor(target, err);
		if (!be) return err;
		bool found = false, dir = false;
		uint64_t size = 0;
		NetResult r = statNet(*be, target.path, found, dir, size);
		if (!r.ok()) return xferErrorText(r);
		if (!found) return "It isn't there any more";
		std::vector<Item> items;
		r = walkNet(*be, target.path, dir, size, "", items, cancel);
		if (!r.ok()) return xferErrorText(r);
		r = removeNet(*be, target.path, items);
		return r.ok() ? "" : xferErrorText(r);
	}
}
