#pragma once
// Network folders as a manga source (docs/design/manga.md, sections 2 and 5.1): a remote folder laid out the way the
// card's Manga folders are (mangalocal.*), decided from the names a listing gives — no file is opened. The same rules
// as the local scan: hidden entries and __MACOSX are skipped, pictures sort naturally, an archive is a .cbz or .zip,
// a folder of chapter folders (or archives) is a series, and a folder of pictures alone is a one-chapter series. The
// caller does the listing on the manga worker; nothing here touches a server, a window or a thread.
#include "net.h"
#include <map>
#include <string>
#include <vector>

namespace bp
{
	// One remote folder's entries as the scan reads them: its pictures in reading order, its archives (.cbz/.zip,
	// name only), and its child folders (natural order).
	struct NetMangaListing
	{
		std::vector<std::string> images;
		std::vector<std::string> dirs;
		std::vector<std::string> archives;
	};

	NetMangaListing netMangaListing(const std::vector<NetEntry>& entries);

	// One chapter of a remote series: a folder of pictures or an archive. path is the bpnet:// path of the folder or
	// archive. pages is the pictures in the folder, -1 for an archive (its central directory isn't read here).
	struct NetMangaChapter
	{
		std::string title;
		std::string path;
		bool archive = false;
		int pages = -1;
	};

	struct NetMangaSeries
	{
		std::string title;
		std::string path;
		std::vector<NetMangaChapter> chapters;
	};

	// The series a remote folder holds, from its own listing (own) and the listings of its child folders (sub, keyed
	// by bpnet path; a child that isn't there is skipped). Direct only, as the design's "a series folder of chapter
	// folders of pictures, or of CBZ/ZIP archives": a child folder with pictures is a chapter, an archive is one, and a
	// folder of pictures with no chapters is a one-chapter series. Chapters sort as mangalocal's do: numbered ones by
	// volume then number, the rest naturally. An empty chapters list means the folder isn't a series.
	NetMangaSeries netMangaSeriesFrom(const std::string& dir, const std::string& name,
		const std::vector<NetEntry>& own, const std::map<std::string, std::vector<NetEntry>>& sub);
}
