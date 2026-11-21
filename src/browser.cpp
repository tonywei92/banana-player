#include "browser.h"
#include "pagebook.h"
#include "playlist.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/mount.h>
#endif

namespace bp
{
	bool isHiddenFolder(const std::string& dir)
	{
		struct stat st;	// a file, not a folder of that name
		return stat((dir + "/.hidden").c_str(), &st) == 0 && S_ISREG(st.st_mode);
	}

	bool insideHiddenFolder(const std::string& path)
	{
		for (std::string d = parentFolder(path); d != "/" && d != "."; d = parentFolder(d))
			if (isHiddenFolder(d)) return true;
		return false;
	}

	std::vector<BrowseEntry> listFolder(const std::string& dir, bool& ok, bool showHidden)
	{
		std::vector<BrowseEntry> dirs, videos;
		DIR* d = opendir(dir.c_str());
		ok = d != nullptr;
		if (!d) return {};
		std::string prefix = dir == "/" ? "/" : dir + "/";
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] == '.') continue;
			BrowseEntry b{e->d_name, prefix + e->d_name, false};
			// exFAT and some others don't fill d_type: ask
			bool isDir = e->d_type == DT_DIR, isFile = e->d_type == DT_REG;
			if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK)
			{
				struct stat st;
				if (stat(b.path.c_str(), &st) != 0) continue;
				isDir = S_ISDIR(st.st_mode);
				isFile = S_ISREG(st.st_mode);
			}
			if (isDir)
			{
				if (!showHidden && isHiddenFolder(b.path)) continue;
				b.dir = true;
				dirs.push_back(b);
			}
			else if (isFile) videos.push_back(b);	// every file: what can't be opened says so when picked
		}
		closedir(d);
		auto byName = [](const BrowseEntry& a, const BrowseEntry& b) { return naturalLess(a.name, b.name); };
		std::sort(dirs.begin(), dirs.end(), byName);
		std::sort(videos.begin(), videos.end(), byName);
		dirs.insert(dirs.end(), videos.begin(), videos.end());
		return dirs;
	}

	bool movedPath(const std::string& p, const std::string& from, const std::string& to, std::string& out)
	{
		if (from.empty() || p.size() < from.size() || p.compare(0, from.size(), from) != 0) return false;
		if (p.size() == from.size()) { out = to; return true; }
		if (p[from.size()] != '/') return false;
		out = to + p.substr(from.size());
		return true;
	}

	std::string parentFolder(const std::string& path)
	{
		std::string p = path;
		while (p.size() > 1 && p.back() == '/') p.pop_back();
		size_t slash = p.rfind('/');
		if (slash == std::string::npos) return ".";
		return slash == 0 ? "/" : p.substr(0, slash);
	}

	static bool isDir(const std::string& p)
	{
		struct stat st;
		return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
	}

	// the second card is in when something is mounted there (its folder is there either way)
	static bool mounted(const std::string& p)
	{
		std::ifstream m("/proc/mounts");
		std::string dev, at;
		while (m >> dev >> at)
		{
			if (at == p) return true;
			m.ignore(4096, '\n');
		}
		return false;
	}

	bool cardOut(const std::string& path)
	{
		const std::string sd2 = "/run/sd2";
		bool on = path == sd2 || path.rfind(sd2 + "/", 0) == 0;
		return on && !mounted(sd2);
	}

	std::vector<RemovableCard> removableCards()
	{
		if (const char* d = getenv("BP_CARD2")) return {{d, "Card 2", isDir(d)}};
		if (!isDir("/data/banana")) return {};
		return {{"/run/sd2", "Card 2", mounted("/run/sd2")}};
	}

	// the kind of device mounted at `at`, from the kernel ("SD", "MMC"; "" when it isn't an mmc device or can't be read)
	static std::string mountDeviceType(const std::string& at)
	{
		std::ifstream m("/proc/mounts");
		std::string dev, where, found;
		while (m >> dev >> where)
		{
			if (where == at) found = dev;	// (the last mount there is the one seen)
			m.ignore(4096, '\n');
		}
		const std::string pre = "/dev/mmcblk";
		if (found.rfind(pre, 0) != 0) return "";
		std::string disk = found.substr(5);	// "mmcblk1p3" -> "mmcblk1"
		size_t part = disk.find('p', 6);
		if (part != std::string::npos) disk.resize(part);
		std::ifstream t("/sys/block/" + disk + "/device/type");
		std::string type;
		t >> type;
		return type;
	}

	std::string storageLabel(const std::string& mount, const std::string& deviceType)
	{
		if (mount == "/run/sd2") return "SD card 2";
		if (mount == "/run/sd1") return "SD card 1";
		return deviceType == "SD" ? "SD card 1" : "Internal storage";
	}

	std::string capacityWords(uint64_t bytes)
	{
		static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
		double v = (double)bytes;
		int u = 0;
		while (v >= 999.5 && u < 4) { v /= 1000.0; u++; }
		char out[32];
		if (u >= 3 && v < 9.95) snprintf(out, sizeof out, "%.1f %s", v, units[u]);
		else snprintf(out, sizeof out, "%.0f %s", v, units[u]);
		return out;
	}

	Capacity capacityOf(uint64_t freeBytes, uint64_t totalBytes)
	{
		Capacity c;
		if (totalBytes == 0) return c;
		freeBytes = std::min(freeBytes, totalBytes);
		c.known = true;
		c.used = (double)(totalBytes - freeBytes) / (double)totalBytes;
		c.full = freeBytes < totalBytes / 10 || freeBytes < 1000000000ull;
		c.freeBytes = freeBytes;
		c.totalBytes = totalBytes;
		c.free = capacityWords(freeBytes) + " free";
		c.detail = capacityWords(totalBytes - freeBytes) + " used of " + capacityWords(totalBytes);
		return c;
	}

	MountFacts mountFactsFrom(const std::string& mountsText, const std::string& path)
	{
		MountFacts out;
		size_t best = 0;
		std::istringstream lines(mountsText);
		std::string line;
		while (std::getline(lines, line))
		{
			std::istringstream in(line);
			std::string dev, where, type, opts;
			if (!(in >> dev >> where >> type >> opts)) continue;
			for (size_t at; (at = where.find("\\040")) != std::string::npos;) where.replace(at, 4, " ");
			bool holds = where == "/" || path == where || path.rfind(where + "/", 0) == 0;
			if (!holds || (out.found && where.size() < best)) continue;
			best = where.size();
			out.found = true;
			out.fs = type;
			// "ro" is one whole option ("rw,errors=remount-ro" is not read-only)
			out.readOnly = ("," + opts + ",").find(",ro,") != std::string::npos;
		}
		return out;
	}

	MountFacts mountFacts(const std::string& path)
	{
		std::ifstream m("/proc/mounts");
		if (m)
		{
			std::stringstream all;
			all << m.rdbuf();
			return mountFactsFrom(all.str(), path);
		}
#ifdef __APPLE__
		struct statfs sf;
		if (statfs(path.c_str(), &sf) == 0)
		{
			MountFacts out;
			out.found = true;
			out.fs = sf.f_fstypename;
			out.readOnly = (sf.f_flags & MNT_RDONLY) != 0;
			return out;
		}
#endif
		return {};
	}

	std::string fsLabel(const std::string& fs)
	{
		static const char* const names[][2] = {{"exfat", "exFAT"}, {"vfat", "FAT32"}, {"msdos", "FAT32"}, {"ntfs", "NTFS"},
			{"ntfs3", "NTFS"}, {"fuseblk", "NTFS"}, {"f2fs", "F2FS"}, {"btrfs", "Btrfs"}, {"apfs", "APFS"}, {"hfs", "Mac OS Extended"}};
		for (auto& n : names)
			if (fs == n[0]) return n[1];
		return fs;
	}

	std::vector<StorageKind> storageFill(uint64_t usedBytes, uint64_t totalBytes, const std::vector<KindUse>& kinds)
	{
		std::vector<StorageKind> out;
		if (totalBytes == 0) return out;
		usedBytes = std::min(usedBytes, totalBytes);
		uint64_t left = usedBytes;	// the used room not yet given to a kind
		for (auto& k : kinds)
		{
			if (k.files <= 0) continue;
			// (a file may have grown since the library read it: a kind never takes more than is used)
			uint64_t bytes = std::min(k.bytes, left);
			left -= bytes;
			StorageKind s;
			s.name = k.name;
			s.count = std::to_string(k.files) + " " + (k.files == 1 ? k.one : k.many);
			s.size = capacityWords(bytes);
			s.share = (double)bytes / (double)totalBytes;
			out.push_back(std::move(s));
		}
		if (left > 0)
		{
			StorageKind s;
			s.name = "Other";
			s.size = capacityWords(left);
			s.share = (double)left / (double)totalBytes;
			s.other = true;
			out.push_back(std::move(s));
		}
		return out;
	}

	std::string placeWhere(const std::vector<BrowseRoot>& roots, const BrowseRoot& place)
	{
		const BrowseRoot* on = nullptr;
		for (auto& c : roots)
			if (c.card && place.path.rfind(c.path + "/", 0) == 0 && (!on || c.path.size() > on->path.size())) on = &c;
		if (!on) return place.path;
		std::string out = on->label;
		for (char ch : place.path.substr(on->path.size()))
		{
			if (ch == '/') out += " / ";
			else out += ch;
		}
		return out;
	}

	std::vector<BrowseRoot> browseRoots()
	{
		std::vector<BrowseRoot> roots;
		if (const char* env = getenv("BP_BROWSE"))
		{
			const char* cards = getenv("BP_BROWSE_CARDS");
			int asCards = cards ? atoi(cards) : 0;	// the first N stand in for storages
			std::string list = env;
			size_t a = 0;
			while (a <= list.size())
			{
				size_t b = list.find(':', a);
				if (b == std::string::npos) b = list.size();
				std::string p = list.substr(a, b - a);
				if (!p.empty() && isDir(p)) roots.push_back({p.substr(p.rfind('/') + 1).empty() ? p : p.substr(p.rfind('/') + 1), p, (int)roots.size() < asCards});
				a = b + 1;
			}
			return roots;
		}
		if (isDir("/data/banana"))
		{
			if (isDir("/data/roms/videos")) roots.push_back({"Videos", "/data/roms/videos"});
			// (the system's device doesn't change while it runs: read once, and said once in the log)
			static const std::string dataType = [] {
				std::string t = mountDeviceType("/data");
				fprintf(stderr, "storage: /data is on %s: \"%s\"\n", t.empty() ? "an unknown device" : t.c_str(),
					storageLabel("/data", t).c_str());
				return t;
			}();
			roots.push_back({storageLabel("/data", dataType), "/data", true});
			if (mounted("/run/sd2")) roots.push_back({storageLabel("/run/sd2", "SD"), "/run/sd2", true});
			return roots;
		}
		if (const char* home = getenv("HOME")) roots.push_back({"Home", home});
		roots.push_back({"Computer", "/"});
		return roots;
	}
}
