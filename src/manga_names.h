// Manga reader names: which entries of a folder or archive are pages, the order they read in, and the chapter and
// volume numbers a chapter's name carries. All strings, so a listing from the card and one from a server sort alike.
#pragma once
#include <string>
#include <vector>

namespace bp
{
	// the picture formats a page can be; hidden entries (".", "__MACOSX/") are never pages, as archive listings carry
	// both those and folders
	bool isPageImage(const std::string& name);

	// the reading order of a chapter: the browser's naturalLess (playlist.h), so pages 2 and 10 aren't swapped and a
	// chapter's pages sort as its folder does in Files
	void sortPages(std::vector<std::string>& names);

	// the chapter a chapter folder or archive name carries ("Chapter 12", "Ch.12", "c012", "#12", "Ep 3"), else its last
	// number ("Series Name 045.cbz" -> 45); -1 when there is none, or the only number is a volume's ("Vol 3")
	double chapterNumber(const std::string& name);

	// the volume a name carries ("Vol.2", "Vol 2", "Volume 02", "v2"), -1 when none
	double volumeNumber(const std::string& name);
}
