#pragma once
// "On this handheld" (docs/design/manga.md, sections 2 and 5.1): what series and chapters one Manga folder on the
// card holds. Pure directory reads with opendir and stat, no threads and no window: the caller (the Browse page, on
// the manga thread) decides when to scan. symlinks are never followed into directories (lstat), so a loop on a card
// can't hang or repeat the scan.

#include <string>
#include <vector>

namespace bp
{
	// one chapter: a folder of images or a .cbz/.zip archive. path: the folder or the archive file; title: its name,
	// without the extension for an archive. number/volume: chapterNumber/volumeNumber of that name, or for an archive
	// its ComicInfo.xml's Number/Volume when the name carries none; -1 is not known. pages: the images of a folder or
	// the archive's page count, -1 when it could not be read.
	struct LocalChapter
	{
		std::string title;
		std::string path;
		bool archive = false;
		double number = -1;
		double volume = -1;
		int pages = -1;
	};

	// one series: its chapters in reading order. cover: the first chapter's first page — a path for a folder chapter,
	// "archive.cbz#entry" for an archive, "" when none could be read. title: the folder or file name, unless every
	// chapter is an archive whose ComicInfo.xml says the same Series (a downloaded chapter links back that way).
	struct LocalSeries
	{
		std::string title;
		std::string path;
		std::string cover;
		std::vector<LocalChapter> chapters;
	};

	// what one Manga folder holds (root; "" and a folder that isn't there give nothing). A folder of images directly
	// in root is a one-chapter series named after the folder; a folder whose children are chapter folders and/or
	// .cbz/.zip files is a series of those chapters; an archive directly in root is a one-chapter series named after
	// the file. Nested deeper is ignored except one level of volume folders ("Vol 1/Chapter 3" is chapters, the volume
	// from the volume folder's name). Hidden entries and __MACOSX are skipped; a folder of images that also holds
	// chapter folders reads as a series, its own images ignored. At most 2000 series and 5000 chapters per series,
	// because a messy card should not build a list nobody can scroll.
	std::vector<LocalSeries> scanMangaFolder(const std::string& root);

	// the Manga folders of the Files places (each a card root like /data or /run/sd2): the child folder named Manga,
	// manga or MANGA, in the places' order, without duplicates. The name is matched as readdir writes it, so a place
	// on a case-insensitive file system gives back the case it really has.
	std::vector<std::string> mangaRoots(const std::vector<std::string>& places);
}
