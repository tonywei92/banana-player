#include "manga_ui_logic.h"
#include "settings.h"
#include <cstdio>

namespace bp
{
	namespace
	{
		// A cover never waits for a decoded one: eight dark-to-mid pairs, a vertical gradient each, so a grid of them
		// reads as different books without a single page of a card being read.
		struct Pair
		{
			unsigned char topR, topG, topB, bottomR, bottomG, bottomB;
		};

		constexpr Pair PAIRS[8] = {
			{0x1B, 0x2A, 0x3A, 0x2F, 0x5D, 0x7A},	// slate blue
			{0x2A, 0x1B, 0x24, 0x6B, 0x35, 0x52},	// plum
			{0x1F, 0x2A, 0x1C, 0x46, 0x6B, 0x39},	// moss
			{0x2E, 0x22, 0x14, 0x7A, 0x55, 0x2B},	// wood
			{0x24, 0x1F, 0x2E, 0x55, 0x45, 0x7A},	// violet
			{0x2B, 0x18, 0x18, 0x7A, 0x3B, 0x34},	// rust
			{0x1A, 0x26, 0x28, 0x3C, 0x63, 0x63},	// teal
			{0x23, 0x21, 0x1A, 0x5C, 0x56, 0x3C},	// olive
		};

		// FNV-1a over the title's bytes: a stable pick across builds and runs, unlike std::hash
		uint32_t hashOf(const std::string& s)
		{
			uint32_t h = 2166136261u;
			for (unsigned char c : s)
			{
				h ^= c;
				h *= 16777619u;
			}
			return h;
		}
	}

	MangaCoverColors mangaCoverColors(const std::string& title)
	{
		const Pair& p = PAIRS[hashOf(title) % 8];
		return {p.topR, p.topG, p.topB, p.bottomR, p.bottomG, p.bottomB};
	}

	MangaResumePoint mangaResume(const std::vector<MangaChapterMark>& chapters)
	{
		MangaResumePoint out;
		if (chapters.empty()) return out;
		for (size_t i = 0; i < chapters.size(); i++)
			if (chapters[i].started && !chapters[i].read)
			{
				out.chapter = (int)i;
				out.page = chapters[i].page;
				return out;
			}
		for (size_t i = 0; i < chapters.size(); i++)
			if (!chapters[i].read)
			{
				out.chapter = (int)i;
				return out;
			}
		out.chapter = 0;
		return out;
	}

	std::string mangaSourceItemJson(const std::string& key, const std::string& title)
	{
		auto esc = [](const std::string& s) {
			std::string out;
			for (char c : s)
			{
				if (c == '"' || c == '\\') { out += '\\'; out += c; }
				else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
				else out += c;
			}
			return out;
		};
		return "{\"key\":\"" + esc(key) + "\",\"title\":\"" + esc(title) + "\"}";
	}

	std::string mangaAgo(int64_t at, int64_t now)
	{
		// a time not known, or ahead of the clock (a card written with another clock): nothing
		if (at <= 0 || at > now) return "";
		int64_t d = now - at;
		auto n = [](int64_t v, const char* one, const char* many) {
			return std::to_string(v) + (v == 1 ? one : many);
		};
		if (d < 60) return "now";
		if (d < 3600) return n(d / 60, " min ago", " min ago");
		if (d < 86400) return n(d / 3600, " h ago", " h ago");
		if (d < 7 * 86400) return n(d / 86400, " day ago", " days ago");
		if (d < 30 * 86400) return n(d / (7 * 86400), " week ago", " weeks ago");
		if (d < 365 * 86400) return n(d / (30 * 86400), " month ago", " months ago");
		return n(d / (365 * 86400), " year ago", " years ago");
	}

	bool parseMangaReadingMode(const std::string& s, int& mode, int& book)
	{
		book = MANGA_BOOK_OFF;
		std::string m = s, b;
		size_t slash = s.find('/');
		if (slash != std::string::npos) { m = s.substr(0, slash); b = s.substr(slash + 1); }
		if (m == "rtl") mode = MANGA_RTL;
		else if (m == "ltr") mode = MANGA_LTR;
		else if (m == "strip") mode = MANGA_STRIP;
		else return false;
		// a strip can't be a book (its pages run down, one long column)
		if (mode != MANGA_STRIP)
		{
			if (b == "book-left") book = MANGA_BOOK_LEFT;
			else if (b == "book-right") book = MANGA_BOOK_RIGHT;
		}
		return true;
	}

	std::string mangaReadingModeId(int mode, int book)
	{
		static const char* IDS[MANGA_MODES] = {"rtl", "ltr", "strip"};
		std::string s = IDS[mode >= 0 && mode < MANGA_MODES ? mode : MANGA_RTL];
		if (mode == MANGA_STRIP) return s;
		if (book == MANGA_BOOK_LEFT) return s + "/book-left";
		if (book == MANGA_BOOK_RIGHT) return s + "/book-right";
		return s;
	}

	bool mangaBookEarlierOnTop(int book, bool rtl)
	{
		// the top panel is the reader's left when held top-left; the earlier page is on the left in left to right
		bool topIsLeft = book != MANGA_BOOK_RIGHT;
		return topIsLeft == !rtl;
	}

	int mangaBookAngle(int book)
	{
		// held with the top panel on the left, the panel's right edge is up: the page turned a quarter clockwise
		return book == MANGA_BOOK_RIGHT ? 270 : 90;
	}

	int mangaBookStep(int book, bool rtl, bool swap, int dpadDy)
	{
		if (dpadDy == 0) return 0;
		// top panel on the left: DOWN points to the reader's right; on the right, UP does
		bool pressRight = book == MANGA_BOOK_RIGHT ? dpadDy < 0 : dpadDy > 0;
		bool forwardRight = rtl == swap;	// left to right reads on rightward; Swap turns it round
		return pressRight == forwardRight ? 1 : -1;
	}

	int mangaBookSwipe(int book, bool rtl, bool swap, double dy)
	{
		if (dy == 0) return 0;
		// either way it is held, a finger moving up the panel goes the way UP points, and pulls in the page from
		// DOWN's side: the turn DOWN makes
		return mangaBookStep(book, rtl, swap, dy < 0 ? 1 : -1);
	}
}
