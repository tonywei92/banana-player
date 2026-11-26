#pragma once
// The manga reader's two small decisions, apart from the window: a generated cover's colours from a series' title,
// which chapter Resume opens, and how old a History row reads. The pages give it titles and the store's marks; it
// answers, so both can be tested on their own (tests/core_test.cpp).
#include <cstdint>
#include <functional>
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

	// The series page's bulk read marks: which chapters (by index) "Mark previous as read" (every one before the
	// focused) and "Mark all" reach, and whether "all" reads as Mark read or Mark unread (everything already read:
	// unread; an empty series: read, which marks nothing)
	enum class MangaMarkSet { Previous, All };
	std::vector<int> mangaMarkIndices(MangaMarkSet set, int focus, int n);
	bool mangaAllRead(const std::vector<MangaChapterMark>& chapters);

	// The reading mode a chapter opens in: `mode` (the series' own choice, else the setting's), except that when the
	// series has no direction chosen by the user (userChose false) a chapter whose ComicInfo says right-to-left
	// opens right-to-left. The user's own choice always wins.
	int mangaOpenMode(int mode, bool userChose, bool comicRtl);

	// "Chapter 12" as "Ch. 12", and a bare title gets its "Ch. " (the History row and the Resume key keep it short)
	std::string mangaChapterShort(const std::string& title);

	// The series page's main key: "Start \xC2\xB7 Ch. 01" (nothing read there yet) or "Resume \xC2\xB7 Ch. 02 \xC2\xB7 p. 5/8".
	// Fitted to `room` (units) by `width`/`elide` (the kit's): the chapter's name gives way first, the page part always
	// shows, and the verb alone is the last resort. pages <= 0: "p. 5" only.
	std::string mangaResumeLabel(bool started, const std::string& chapterShort, int page, int pages, int room,
		const std::function<int(const std::string&)>& width,
		const std::function<std::string(const std::string&, int)>& elide);

	// What a Resume or a picked chapter that waits does on this pass (looked at every pass while it waits): the series
	// check sent when it is wanted and not on its way (the press, a Retry; it also starts the engine), keep waiting
	// (the engine, the check, the chapter list), open now, say why it can't, give up on a check or list that hasn't
	// answered MANGA_ENGINE_USUAL_MS after the engine is up (busyMs: since the engine was seen up or the check last
	// sent, the later), or nothing to open (no chapters)
	static constexpr int64_t MANGA_ENGINE_USUAL_MS = 30000;	// (also the waiting state's usual engine start)
	struct MangaWaitIn
	{
		bool engineRunning = false, engineFailed = false;
		bool checkWanted = false, checkInFlight = false;
		bool listLoading = false, listFailed = false, haveChapters = false;
		int64_t busyMs = 0;
	};
	enum class MangaWaitAct { Wait, SendCheck, Go, Fail, TimedOut, Nothing };
	MangaWaitAct mangaWaitAct(const MangaWaitIn& in);

	// ---- waiting for the manga engine (its start takes about 30 s on the handheld) ----
	// How a chapter picked on an extension's series page opens: from its file on the card at once (no engine, no
	// network: a downloaded chapter never waits), from the source now, or after the wait (the waiting state)
	enum class MangaOpenWay { FromCard, FromSource, Wait };
	MangaOpenWay mangaOpenWay(bool engineReady, bool chapterOnCard);

	// What the wait is for, in the order it goes: the engine starting, the series checked against it, its chapter list
	enum class MangaWaitStage { Engine, Series, Chapters };
	MangaWaitStage mangaWaitStage(bool engineRunning, bool listLoading);
	// The waiting state's words and bar. Engine: "Starting the manga engine…", "12 s · usually about 30 s" (past the
	// usual time: "41 s · taking longer than usual") and a bar that fills over the usual time and holds short of
	// its end (it is an estimate, never "done" before the engine answers). The others: no bar (share -1), no line
	struct MangaWaitView
	{
		std::string title, line;
		double share = -1;
	};
	MangaWaitView mangaWaitView(MangaWaitStage stage, int64_t elapsedMs);

	// Starting the engine before Manga is opened, on Home, so the wait is over (or shorter) by the time a series is
	// resumed. Only when it is likely to be used and costs nothing heard or seen: the last thing read in Manga was an
	// extension's series, within MANGA_WARM_RECENT_S; nothing plays (a video can't share the memory, and a song's
	// visualizer would stutter under the start's 25 s of the cores); installed, not running, no runtime upgrade on;
	// once a run (a warm engine nobody used stops by itself after its idle time, and isn't started again)
	static constexpr int64_t MANGA_WARM_RECENT_S = 3 * 86400;
	struct MangaWarmIn
	{
		bool onHome = false, mangaOn = false;
		bool engineInstalled = false;	// installed and not started (JavaExtHost::State::Installed)
		bool upgrading = false, playing = false, warmedThisRun = false;
		bool lastReadOnline = false;	// the newest History row is an extension's series
		int64_t lastReadAgoS = -1;		// how long ago it was read; -1: nothing read
	};
	bool mangaEngineWarm(const MangaWarmIn& in);

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

	// ---- Book's zoom (both panels as one picture: the column of the top panel, the hinge, the bottom panel) ----
	// the column (viewW wide, colH = 2 panels + the hinge's gap for a page pair, 2 panels for a spread) drawn at z and
	// shifted: a column point p shows at p * z + o. Both panels draw from the one view, so they pan in lockstep
	struct MangaBookZoom { double z = 1, ox = 0, oy = 0; };
	struct MangaBookArea { double x = 0, y = 0, w = 0, h = 0; };	// the pages' bounds at the fit, column pixels
	// a lone page's area: the page and, as if it held a real page of the same size, the blank slot on the other panel
	// (shifted one panel and the gap), so zoom and pan treat the screen as two slots and the page keeps its own screen.
	// pageOnTop: the lone page is on the top panel
	MangaBookArea mangaBookLoneArea(const MangaBookArea& page, bool pageOnTop, double viewH, double gap);
	// the steps LEFT/RIGHT go through: the fit (1), 1.5, 2, 3; dir +1 in, -1 out (stops at either end)
	double mangaBookZoomStep(double z, int dir);
	// the view kept on the pages: on an axis where they are bigger than the column, no empty edge shows; where they
	// are smaller, they are centred (so the fit is z 1, o 0)
	MangaBookZoom mangaBookZoomClamp(MangaBookZoom v, const MangaBookArea& pages, double viewW, double colH);
	// to zoom z, the column point under (ax, ay) (view pixels of the column) staying where it is
	MangaBookZoom mangaBookZoomTo(MangaBookZoom v, double z, double ax, double ay, const MangaBookArea& pages, double viewW, double colH);
	// a drag or the stick: the picture moved by (dx, dy) view pixels, clamped
	MangaBookZoom mangaBookPan(MangaBookZoom v, double dx, double dy, const MangaBookArea& pages, double viewW, double colH);
	// the view is at the column's end that way (dirY +1: the bottom panel's foot; -1: the top panel's top), where a
	// turn key turns the page instead of panning
	bool mangaBookZoomAtEnd(const MangaBookZoom& v, int dirY, const MangaBookArea& pages, double viewW, double colH);
	// LEFT (-1) / RIGHT (+1) as held sideways: +1 zooms in (the key that is up as held), -1 out
	int mangaBookZoomKey(int book, int dpadDx);

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
