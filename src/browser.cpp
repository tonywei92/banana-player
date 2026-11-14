#include "browser.h"
#include "pagebook.h"
#include "playlist.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>

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

	std::vector<BrowseRoot> browseRoots()
	{
		std::vector<BrowseRoot> roots;
		if (const char* env = getenv("BP_BROWSE"))
		{
			std::string list = env;
			size_t a = 0;
			while (a <= list.size())
			{
				size_t b = list.find(':', a);
				if (b == std::string::npos) b = list.size();
				std::string p = list.substr(a, b - a);
				if (!p.empty() && isDir(p)) roots.push_back({p.substr(p.rfind('/') + 1).empty() ? p : p.substr(p.rfind('/') + 1), p});
				a = b + 1;
			}
			return roots;
		}
		if (isDir("/data/banana"))
		{
			if (isDir("/data/roms/videos")) roots.push_back({"Videos", "/data/roms/videos"});
			roots.push_back({"This card", "/data"});
			if (mounted("/run/sd2")) roots.push_back({"Second card", "/run/sd2"});
			return roots;
		}
		if (const char* home = getenv("HOME")) roots.push_back({"Home", home});
		roots.push_back({"Computer", "/"});
		return roots;
	}
}
