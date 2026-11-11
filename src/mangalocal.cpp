// The card's Manga folders read as series and chapters: see mangalocal.h. One pass of opendir/lstat per level, the
// reading order from manga_names, and archives asked through Cbz (its central directory only, plus one small
// ComicInfo.xml per archive), so a full card costs listings and not page bytes.
#include "mangalocal.h"
#include "cbz.h"
#include "comicinfo.h"
#include "manga_names.h"
#include "playlist.h"	// naturalLess, the order sortPages is built on
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		// the design's caps (docs/design/manga.md, section 2, as given to this scanner)
		constexpr size_t maxSeries = 2000, maxChapters = 5000;

		std::string join(const std::string& dir, const std::string& name)
		{
			return dir == "/" ? "/" + name : dir + "/" + name;
		}

		// strip a place's trailing slashes once, so join reads well
		std::string trimTrailing(const std::string& p)
		{
			std::string s = p;
			while (s.size() > 1 && s.back() == '/') s.pop_back();
			return s;
		}

		char lower(char c)
		{
			return (char)std::tolower((unsigned char)c);
		}

		// hidden entries and macOS's metadata are not manga, in listings and in archives alike
		bool hidden(const std::string& name)
		{
			return !name.empty() && (name[0] == '.' || name == "__MACOSX");
		}

		// the lowercased extension without the dot, "" when there is none
		std::string extOf(const std::string& name)
		{
			size_t dot = name.rfind('.');
			if (dot == std::string::npos || dot == 0) return "";
			std::string e = name.substr(dot + 1);
			for (char& c : e) c = lower(c);
			return e;
		}

		// the archives this scanner can open: CBZ is a ZIP, and so is a .zip someone renamed
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

		std::string baseName(const std::string& path)
		{
			size_t slash = path.rfind('/');
			return slash == std::string::npos ? path : path.substr(slash + 1);
		}

		struct Listing
		{
			bool ok = false;
			std::vector<std::string> images;	// natural order: the first is a chapter's page one and its cover
			std::vector<std::string> dirs;
			std::vector<std::string> archives;
		};

		// one level of a folder. lstat and nothing else: a symlink is neither a directory we walk into nor a file we
		// read, so a card's self-linked folder can't send the scan round in circles.
		Listing listOne(const std::string& dir)
		{
			Listing l;
			DIR* d = opendir(dir.c_str());
			if (!d) return l;
			l.ok = true;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (hidden(n)) continue;
				struct stat st;
				if (lstat(join(dir, n).c_str(), &st) != 0) continue;
				if (S_ISDIR(st.st_mode)) l.dirs.push_back(n);
				else if (!S_ISREG(st.st_mode)) continue;
				else if (isPageImage(n)) l.images.push_back(n);
				else if (isArchiveName(n)) l.archives.push_back(n);
			}
			closedir(d);
			sortPages(l.images);
			return l;
		}

		// ComicInfo's Number is text ("10.5", "Extra"): a double when the whole of it is one, else nothing
		double numberFromText(const std::string& s)
		{
			const char* p = s.c_str();
			char* end = nullptr;
			double v = std::strtod(p, &end);
			return (end != p && *end == '\0') ? v : -1;
		}

		// a chapter plus what the scan learned to build the series with: its cover and its archive's ComicInfo Series
		struct Found : LocalChapter
		{
			std::string cover;
			std::string ciSeries;
		};

		// volOfParent: a volume folder's number, for a chapter whose own name says nothing about volumes
		Found makeFolder(const std::string& path, const std::string& name, const Listing& l, double volOfParent)
		{
			Found c;
			c.title = name;
			c.path = path;
			c.number = chapterNumber(name);
			c.volume = volumeNumber(name);
			if (c.volume < 0) c.volume = volOfParent;
			c.pages = (int)l.images.size();	// never empty: a folder chapter is a chapter because of its images
			if (!l.images.empty()) c.cover = join(path, l.images[0]);
			return c;
		}

		Found makeArchive(const std::string& path, double volOfParent)
		{
			Found c;
			c.title = stripArchiveExt(baseName(path));
			c.path = path;
			c.archive = true;
			c.number = chapterNumber(c.title);
			c.volume = volumeNumber(c.title);
			if (c.volume < 0) c.volume = volOfParent;
			Cbz z;
			if (!z.open(path)) return c;	// unreadable: pages stays -1, and there is no metadata to read
			std::vector<std::string> p = z.pages();
			c.pages = (int)p.size();
			if (!p.empty()) c.cover = path + "#" + p[0];
			// ComicInfo.xml as the downloads write it: one look, only to fill what the name doesn't carry
			for (const CbzEntry& e : z.entries())
			{
				std::string ln;
				for (char ch : e.name) ln += lower(ch);
				if (ln != "comicinfo.xml") continue;
				std::vector<uint8_t> raw;
				ComicInfo ci;
				if (z.read(e.name, raw) && parseComicInfo(std::string(raw.begin(), raw.end()), ci))
				{
					if (c.number < 0) c.number = numberFromText(ci.number);
					if (c.volume < 0 && ci.volume >= 0) c.volume = ci.volume;
					c.ciSeries = ci.series;
					c.ciSeriesKey = ci.seriesKey;
				}
				break;
			}
			return c;
		}

		// the chapters of a series folder: its chapter subfolders, its archives, and one level down through folders
		// that hold neither but whose own children do — the volume folders of docs/design/manga.md, section 2
		void collectChapters(const std::string& dir, const Listing& l, std::vector<Found>& out)
		{
			for (const std::string& n : l.dirs)
			{
				std::string sub = join(dir, n);
				Listing s = listOne(sub);
				if (!s.ok) continue;
				if (!s.images.empty())
				{
					out.push_back(makeFolder(sub, n, s, -1));
					if (out.size() >= maxChapters) return;
					continue;
				}
				double vol = volumeNumber(n);
				for (const std::string& g : s.dirs)
				{
					std::string vg = join(sub, g);
					Listing v = listOne(vg);
					if (v.ok && !v.images.empty()) out.push_back(makeFolder(vg, g, v, vol));
					if (out.size() >= maxChapters) return;
				}
				for (const std::string& a : s.archives)
				{
					out.push_back(makeArchive(join(sub, a), vol));
					if (out.size() >= maxChapters) return;
				}
			}
			for (const std::string& a : l.archives)
			{
				out.push_back(makeArchive(join(dir, a), -1));
				if (out.size() >= maxChapters) return;
			}
		}

		// naturalLess has no tie rule of its own for equal names, and two chapters of one folder can't share a path
		bool natLess(const std::string& a, const std::string& b, const std::string& pa, const std::string& pb)
		{
			if (naturalLess(a, b)) return true;
			return !naturalLess(b, a) && naturalLess(pa, pb);
		}

		// chapters in reading order, the series' cover from the first of them, and its title: the folder's name
		// unless every chapter is an archive whose ComicInfo agrees on one Series
		LocalSeries makeSeries(const std::string& path, const std::string& name, std::vector<Found>& chs)
		{
			bool allNumbered = !chs.empty();
			for (const Found& c : chs)
				if (c.number < 0) { allNumbered = false; break; }
			if (allNumbered)
				std::stable_sort(chs.begin(), chs.end(), [](const Found& a, const Found& b) {
					if (a.volume != b.volume) return a.volume < b.volume;
					if (a.number != b.number) return a.number < b.number;
					return natLess(a.title, b.title, a.path, b.path);
				});
			else
				std::stable_sort(chs.begin(), chs.end(), [](const Found& a, const Found& b) {
					return natLess(a.title, b.title, a.path, b.path);
				});
			LocalSeries s;
			s.path = path;
			s.title = name;
			bool agrees = !chs.empty();
			for (const Found& c : chs)
				if (!c.archive || c.ciSeries != chs.front().ciSeries || c.ciSeries.empty()) { agrees = false; break; }
			if (agrees) s.title = chs.front().ciSeries;
			s.cover = chs.front().cover;
			for (const Found& c : chs) s.chapters.push_back(c);	// the scan's extras don't travel
			return s;
		}
	}

	std::vector<LocalSeries> scanMangaFolder(const std::string& root)
	{
		std::vector<LocalSeries> out;
		if (root.empty()) return out;
		std::string base = trimTrailing(root);
		Listing top = listOne(base);
		if (!top.ok) return out;
		for (const std::string& n : top.dirs)
		{
			if (out.size() >= maxSeries) break;
			std::string path = join(base, n);
			Listing l = listOne(path);
			if (!l.ok) continue;
			std::vector<Found> chs;
			collectChapters(path, l, chs);
			// the folder's own images make it a chapter only when nothing inside makes it a series (then they are
			// ignored, as the design says: series first)
			if (chs.empty() && !l.images.empty()) chs.push_back(makeFolder(path, n, l, -1));
			if (chs.empty()) continue;	// empty, or no manga to read in it
			out.push_back(makeSeries(path, n, chs));
		}
		for (const std::string& n : top.archives)
		{
			if (out.size() >= maxSeries) break;
			std::string path = join(base, n);
			std::vector<Found> chs;
			chs.push_back(makeArchive(path, -1));
			out.push_back(makeSeries(path, stripArchiveExt(n), chs));
		}
		std::stable_sort(out.begin(), out.end(), [](const LocalSeries& a, const LocalSeries& b) {
			return natLess(a.title, b.title, a.path, b.path);
		});
		return out;
	}

	std::vector<std::string> mangaRoots(const std::vector<std::string>& places)
	{
		std::vector<std::string> out;
		for (const std::string& p : places)
		{
			if (p.empty() || p[0] != '/') continue;	// the places are absolute card paths
			std::string dir = trimTrailing(p);
			DIR* d = opendir(dir.c_str());
			if (!d) continue;
			const char* want[3] = {"Manga", "manga", "MANGA"};	// preference, not a set: the first hit that exists wins
			int best = 3;
			while (dirent* e = readdir(d))
			{
				for (int k = 0; k < best; k++)
				{
					if (std::string(e->d_name) != want[k]) continue;
					struct stat st;
					if (lstat(join(dir, want[k]).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) best = k;
					break;
				}
			}
			closedir(d);
			if (best >= 3) continue;
			std::string path = join(dir, want[best]);
			if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
		}
		return out;
	}
}
