#include "playlist.h"
#include "photo.h"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <initializer_list>

namespace bp
{
	static bool hasExtension(const std::string& name, std::initializer_list<const char*> exts)
	{
		size_t dot = name.rfind('.');
		if (dot == std::string::npos || name[0] == '.') return false;
		std::string ext = name.substr(dot + 1);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
		for (auto* e : exts)
			if (ext == e) return true;
		return false;
	}

	bool isVideoFile(const std::string& name)
	{
		return hasExtension(name, {"mp4", "m4v", "mkv", "webm", "avi", "mov", "ts", "m2ts", "mts", "mpg", "mpeg", "wmv",
			"flv", "3gp", "ogv", "vob", "divx", "rm", "rmvb", "asf"});
	}

	bool isAudioFile(const std::string& name)
	{
		return hasExtension(name, {"mp3", "flac", "ogg", "oga", "wav", "m4a", "m4b", "aac", "opus", "wma", "aiff", "aif", "ape",
			"wv", "mka"});
	}

	bool isAviFile(const std::string& name)
	{
		return hasExtension(name, {"avi", "divx"});
	}

	bool isMediaFile(const std::string& name)
	{
		return isVideoFile(name) || isAudioFile(name) || isPhotoFile(name);
	}

	bool folderHasCover(const std::string& path)
	{
		size_t slash = path.rfind('/');
		std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
		DIR* d = opendir(dir.c_str());
		if (!d) return false;
		bool found = false;
		while (dirent* e = readdir(d))
		{
			std::string n = e->d_name;
			size_t dot = n.rfind('.');
			if (dot == std::string::npos || dot == 0) continue;
			std::string base = n.substr(0, dot);
			std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return std::tolower(c); });
			// mpv's names (its cover-art-auto=exact list), as an image
			bool named = false;
			for (const char* b : {"albumartsmall", "albumart", "album", "cover", "front", "folder", "thumb"})
				if (base == b) named = true;
			if (named && hasExtension(n, {"jpg", "jpeg", "png", "webp", "gif", "bmp"})) { found = true; break; }
		}
		closedir(d);
		return found;
	}

	bool naturalLess(const std::string& a, const std::string& b)
	{
		size_t i = 0, j = 0;
		while (i < a.size() && j < b.size())
		{
			unsigned char ca = a[i], cb = b[j];
			if (std::isdigit(ca) && std::isdigit(cb))
			{
				// whole numbers, leading zeros aside
				size_t si = i, sj = j;
				while (si < a.size() && a[si] == '0') si++;
				while (sj < b.size() && b[sj] == '0') sj++;
				size_t ei = si, ej = sj;
				while (ei < a.size() && std::isdigit((unsigned char)a[ei])) ei++;
				while (ej < b.size() && std::isdigit((unsigned char)b[ej])) ej++;
				if (ei - si != ej - sj) return ei - si < ej - sj;
				int c = a.compare(si, ei - si, b, sj, ej - sj);
				if (c) return c < 0;
				i = ei;
				j = ej;
				continue;
			}
			int la = std::tolower(ca), lb = std::tolower(cb);
			if (la != lb) return la < lb;
			i++;
			j++;
		}
		if (a.size() - i != b.size() - j) return a.size() - i < b.size() - j;
		return a < b;	// the same but for case or zeros: still one order
	}

	std::vector<std::string> siblingVideos(const std::string& path, int& index)
	{
		index = 0;
		size_t slash = path.rfind('/');
		std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
		std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
		std::vector<std::string> all;
		if (DIR* d = isMediaFile(name) ? opendir(dir.c_str()) : nullptr)
		{
			while (dirent* e = readdir(d)) all.push_back(e->d_name);
			closedir(d);
		}
		std::vector<std::string> names = siblingNames(name, all, index);
		if (names.empty()) return {path};
		std::string prefix = slash == std::string::npos ? "" : dir == "/" ? "/" : dir + "/";
		for (auto& n : names) n = prefix + n;
		return names;
	}

	std::vector<std::string> siblingNames(const std::string& name, const std::vector<std::string>& folderNames, int& index)
	{
		index = 0;
		// a photo's are the photos; a video's or a song's, the videos and songs (a slideshow doesn't start a film)
		bool photos = isPhotoFile(name);
		std::vector<std::string> names;
		if (isMediaFile(name))
			for (auto& n : folderNames)
				if (isMediaFile(n) && isPhotoFile(n) == photos) names.push_back(n);
		if (std::find(names.begin(), names.end(), name) == names.end()) return {};
		std::sort(names.begin(), names.end(), naturalLess);
		index = (int)(std::find(names.begin(), names.end(), name) - names.begin());
		return names;
	}

	int playingRow(const std::vector<std::string>& paths, int hint, const std::string& path)
	{
		if (path.empty()) return -1;
		if (hint >= 0 && hint < (int)paths.size() && paths[hint] == path) return hint;
		for (size_t i = 0; i < paths.size(); i++)
			if (paths[i] == path) return (int)i;
		return -1;
	}
}
