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

	// what a pressed Resume does with the series page as it is: open now, keep waiting (the engine, the chapter list),
	// say why it can't, or nothing to open (no chapters)
	enum class MangaResumeStep { Go, Wait, Fail, Nothing };
	MangaResumeStep mangaResumeStep(bool engineReady, bool engineFailed, bool listLoading, bool listFailed, bool haveChapters);

	// "now", "12 min ago", "3 h ago", "2 days ago", "5 weeks ago", "10 months ago", "2 years ago"; "" for no time
	std::string mangaAgo(int64_t at, int64_t now);

	// the item table a source's details()/chapters() get back when the series opened from the Library keeps no copy
	// of it: its key and title, escaped as JSON (the source still needs those fields to answer)
	std::string mangaSourceItemJson(const std::string& key, const std::string& title);

	// A series' own reading mode as the store keeps it (MangaSeries::readingMode): "rtl", "ltr" or "strip", and Book
	// after a slash ("rtl/book-left", "ltr/book-right"), so the places that copy the string carry Book along.
	// Parsing: false for "" or anything unknown (the setting's mode then; book off). The mode is the one set, whatever
	// the layout: "strip/book-left" is a Vertical scroll series held as a book for now (mangaModeIn says how it reads).
	bool parseMangaReadingMode(const std::string& s, int& mode, int& book);
	std::string mangaReadingModeId(int mode, int book);

	// Layout and Reading mode are two choices, neither changing the other. Book needs pages, so a Vertical scroll
	// reads a page at a time while Book is on, in bookDir (mangaBookDirection); every other pair reads as set. The
	// mode set is never rewritten by a layout, so leaving Book reads as it did before.
	int mangaModeIn(int set, int book, int bookDir);
	// the direction Book turns a Vertical scroll's pages in: the Reading mode setting's when that is a direction,
	// else right to left (no source says which way its pages run)
	int mangaBookDirection(int settingMode);
	// View's Reading mode value: the mode set, and what Book does with a Vertical scroll ("Vertical scroll (paged)":
	// as long as the row's value has room for); mangaModeNote is the sheet's line then ("" otherwise)
	std::string mangaModeLabel(int set, int book);
	std::string mangaModeNote(int set, int book);

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

	// Book's screens (both panels as one view, held sideways): two pages side by side, or a wide page (a double-page
	// spread) alone, cut down the middle across both. Pairing walks the pages in order: normal pages two to a screen; a
	// spread starts a fresh screen (a lone page before it sits alone) and takes one alone; pairing restarts after it.
	// Right to left changes only which panel the earlier page is on (mangaBookEarlierOnTop), never the pairing.
	struct MangaBookScreen
	{
		int first = 0, count = 1;	// its pages: first .. first + count - 1
		bool spread = false;		// one wide page across both panels
		bool operator==(const MangaBookScreen& o) const { return first == o.first && count == o.count && spread == o.spread; }
	};
	// a page drawn as a double-page spread: w/h at least 1.2, never the first page (a cover); size unknown (0): not
	bool mangaBookWide(int page, int w, int h);
	// the screen starting at `first` (wide: one flag per page): what the current screen shows, so a size learnt late
	// changes only what it holds, never where it starts
	MangaBookScreen mangaBookScreenFrom(const std::vector<char>& wide, int first);
	// every screen from page 0, in order
	std::vector<MangaBookScreen> mangaBookScreens(const std::vector<char>& wide);
	// the walk's screen holding `page` (reopening at a saved page, a Seek)
	MangaBookScreen mangaBookScreenOf(const std::vector<char>& wide, int page);
	// the screen before the one starting at `first`: the walk's screen holding first - 1, cut to end there (a screen a
	// one-page move realigned keeps its own start)
	MangaBookScreen mangaBookScreenBefore(const std::vector<char>& wide, int first);

	// ---- Sources › Available: the languages kept (Filter) and the words searched for (Search); both apply ----
	// "English · en" for "en"; a code it doesn't know as it is
	std::string mangaLangName(const std::string& code);
	// "en,ja" as its codes
	std::vector<std::string> splitLangs(const std::string& s);
	// a source's language against the ones chosen ("" chosen: every one; "multi" is the choice "all")
	bool mangaLangKept(const std::string& chosen, const std::string& lang);
	// every word of `query` (split at spaces) is in one of `fields`, ASCII capitals apart; no words: everything
	bool sourceSearchMatch(const std::string& query, const std::vector<std::string>& fields);
	// one row of Available against both: its language kept, and the words in its name, its language ("ja" or
	// "Japanese") or where it comes from (a repository's name)
	bool sourceListed(const std::string& query, const std::string& chosenLangs, const std::string& name, const std::string& lang,
		const std::string& from = "");

	// ---- the reader's page tag ----
	// the chapter as the page tag names it, before the page numbers: "Ch. 12" (its number, else one read from a title
	// such as "Chapter 12" / "Ch.12.5"), else the title cut to about 14 characters with an ellipsis; "" with no chapters
	// (total 0: a comic opened from Files)
	std::string mangaChapterTag(double number, const std::string& title, int total);
}
