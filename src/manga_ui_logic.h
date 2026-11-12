#pragma once
// The manga reader's two small decisions, apart from the window: a generated cover's colours from a series' title,
// which chapter Resume opens, and how old a History row reads. The pages give it titles and the store's marks; it
// answers, so both can be tested on their own (tests/core_test.cpp).
#include <cstdint>
#include <string>
#include <vector>

namespace bp
{
	// a generated cover's gradient: a dark top and a mid bottom (one of the eight in manga_ui_logic.cpp)
	struct MangaCoverColors
	{
		unsigned char topR, topG, topB, bottomR, bottomG, bottomB;
	};

	// the same title always the same pair; different titles spread over the eight
	MangaCoverColors mangaCoverColors(const std::string& title);

	// what the store knows of one chapter, as Resume reads it
	struct MangaChapterMark
	{
		bool read = false;
		bool started = false;	// opened and not finished (a page turned, or its length known)
		int page = 0;
	};

	struct MangaResumePoint
	{
		int chapter = -1;	// -1: no chapters
		int page = 0;
	};

	// the first started-but-unread chapter, at its page; else the first unread, at its start; else the first
	MangaResumePoint mangaResume(const std::vector<MangaChapterMark>& chapters);

	// "now", "12 min ago", "3 h ago", "2 days ago", "5 weeks ago", "10 months ago", "2 years ago"; "" for no time
	std::string mangaAgo(int64_t at, int64_t now);

	// the item table a source's details()/chapters() get back when the series opened from the Library keeps no copy
	// of it: its key and title, escaped as JSON (the source still needs those fields to answer)
	std::string mangaSourceItemJson(const std::string& key, const std::string& title);

	// A series' own reading mode as the store keeps it (MangaSeries::readingMode): "rtl", "ltr" or "strip", and Book
	// after a slash ("rtl/book-left", "ltr/book-right"), so the places that copy the string carry Book along.
	// Parsing: false for "" or anything unknown (the setting's mode then; book off); Book only with a paged mode.
	bool parseMangaReadingMode(const std::string& s, int& mode, int& book);
	std::string mangaReadingModeId(int mode, int book);

	// Book's geometry, held as `book` says (MANGA_BOOK_LEFT or _RIGHT), reading right to left or not:
	// the spread's earlier page (its first) goes on the top panel, or on the bottom one
	bool mangaBookEarlierOnTop(int book, bool rtl);
	// the page turn in degrees clockwise that stands a page upright as held: 90 with the top panel on the left
	int mangaBookAngle(int book);
	// a sideways press as held, from the D-pad's UP (-1) or DOWN (+1): +1 a spread forward, -1 back. Forward is the
	// reader's left in right to left, the right in left to right; swap (Swap Left/Right) turns that round
	int mangaBookStep(int book, bool rtl, bool swap, int dpadDy);
	// a finger's swipe on the bottom panel (dy: its movement down the panel, in panel units): as the press that
	// pulls the next page the way the finger went (a finger moving to the reader's left is the right key's turn)
	int mangaBookSwipe(int book, bool rtl, bool swap, double dy);
}
