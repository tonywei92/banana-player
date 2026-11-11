// The network folder's names as a manga series: see manganetfolders.h. The local scan's rules are repeated here for
// names alone, because a remote listing has no stat and no file to open; the ordering comes from the same helpers
// (manga_names and naturalLess), so a chapter reads in the order it would from the card.
#include "manganetfolders.h"
#include "manga_names.h"
#include "netpath.h"
#include "playlist.h"	// naturalLess, the order sortPages is built on
#include <algorithm>
#include <cctype>

namespace bp
{
	namespace
	{
		// the design's caps, as mangalocal uses them (a messy share shouldn't build a list nobody can scroll)
		constexpr size_t maxChapters = 5000;

		std::string lower(std::string s)
		{
			for (char& c : s) c = (char)std::tolower((unsigned char)c);
			return s;
		}

		// hidden entries and macOS's metadata are not manga (as mangalocal's hidden())
		bool hidden(const std::string& name)
		{
			return !name.empty() && (name[0] == '.' || name == "__MACOSX");
		}

		// the lowercased extension without the dot, "" when there is none
		std::string extOf(const std::string& name)
		{
			size_t dot = name.rfind('.');
			if (dot == std::string::npos || dot == 0) return "";
			return lower(name.substr(dot + 1));
		}

		bool isArchiveName(const std::string& name)
		{
			std::string e = extOf(name);
			return e == "cbz" || e == "zip";
		}

		std::string stripArchiveExt(const std::string& name)
		{
			std::string e = extOf(name);
			return (e == "cbz" || e == "zip") ? name.substr(0, name.size() - e.size() - 1) : name;
		}

		struct Found
		{
			NetMangaChapter chapter;
			double number = -1;
			double volume = -1;
		};

		// naturalLess has no tie rule of its own for equal names, and two chapters of one folder can't share a path
		bool natLess(const std::string& a, const std::string& b, const std::string& pa, const std::string& pb)
		{
			if (naturalLess(a, b)) return true;
			return !naturalLess(b, a) && naturalLess(pa, pb);
		}

		// the chapters in reading order, as mangalocal's makeSeries sorts them
		void sortChapters(std::vector<Found>& chs)
		{
			bool allNumbered = !chs.empty();
			for (const Found& c : chs)
				if (c.number < 0) { allNumbered = false; break; }
			if (allNumbered)
				std::stable_sort(chs.begin(), chs.end(), [](const Found& a, const Found& b) {
					if (a.volume != b.volume) return a.volume < b.volume;
					if (a.number != b.number) return a.number < b.number;
					return natLess(a.chapter.title, b.chapter.title, a.chapter.path, b.chapter.path);
				});
			else
				std::stable_sort(chs.begin(), chs.end(), [](const Found& a, const Found& b) {
					return natLess(a.chapter.title, b.chapter.title, a.chapter.path, b.chapter.path);
				});
		}
	}

	NetMangaListing netMangaListing(const std::vector<NetEntry>& entries)
	{
		NetMangaListing out;
		for (const NetEntry& e : entries)
		{
			if (e.name.empty() || hidden(e.name)) continue;
			if (e.dir) out.dirs.push_back(e.name);
			else if (isPageImage(e.name)) out.images.push_back(e.name);
			else if (isArchiveName(e.name)) out.archives.push_back(e.name);
		}
		sortPages(out.images);
		sortPages(out.archives);
		sortPages(out.dirs);
		return out;
	}

	NetMangaSeries netMangaSeriesFrom(const std::string& dir, const std::string& name,
		const std::vector<NetEntry>& own, const std::map<std::string, std::vector<NetEntry>>& sub)
	{
		NetMangaSeries series;
		series.path = dir;
		series.title = name;
		NetMangaListing ownListing = netMangaListing(own);
		std::vector<Found> chapters;
		for (const std::string& d : ownListing.dirs)
		{
			if (chapters.size() >= maxChapters) break;
			std::string path = netJoin(dir, d);
			auto it = sub.find(path);
			if (it == sub.end()) continue;	// the child couldn't be listed: not a chapter we can read
			NetMangaListing one = netMangaListing(it->second);
			if (one.images.empty()) continue;	// (no volume folders here: the design's series folder is direct)
			Found f;
			f.chapter.title = d;
			f.chapter.path = path;
			f.chapter.pages = (int)one.images.size();
			f.number = chapterNumber(d);
			f.volume = volumeNumber(d);
			chapters.push_back(std::move(f));
		}
		for (const std::string& a : ownListing.archives)
		{
			if (chapters.size() >= maxChapters) break;
			Found f;
			f.chapter.title = stripArchiveExt(a);
			f.chapter.path = netJoin(dir, a);
			f.chapter.archive = true;
			f.chapter.pages = -1;
			f.number = chapterNumber(f.chapter.title);
			f.volume = volumeNumber(f.chapter.title);
			chapters.push_back(std::move(f));
		}
		// a folder of pictures alone is a one-chapter series, as the local scan reads it; a folder that also has
		// chapter folders is a series first, its own pictures ignored
		if (chapters.empty() && !ownListing.images.empty())
		{
			Found f;
			f.chapter.title = name;
			f.chapter.path = dir;
			f.chapter.pages = (int)ownListing.images.size();
			chapters.push_back(std::move(f));
		}
		sortChapters(chapters);
		series.chapters.reserve(chapters.size());
		for (const Found& f : chapters) series.chapters.push_back(f.chapter);
		return series;
	}
}
