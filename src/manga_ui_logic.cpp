#include "manga_ui_logic.h"
#include <algorithm>
#include "settings.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cmath>

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
		if (b == "book-left") book = MANGA_BOOK_LEFT;
		else if (b == "book-right") book = MANGA_BOOK_RIGHT;
		return true;
	}

	std::string mangaReadingModeId(int mode, int book)
	{
		static const char* IDS[MANGA_MODES] = {"rtl", "ltr", "strip"};
		std::string s = IDS[mode >= 0 && mode < MANGA_MODES ? mode : MANGA_RTL];
		if (book == MANGA_BOOK_LEFT) return s + "/book-left";
		if (book == MANGA_BOOK_RIGHT) return s + "/book-right";
		return s;
	}

	int mangaModeIn(int set, int book, int bookDir)
	{
		if (set < 0 || set >= MANGA_MODES) set = MANGA_STRIP;
		if (book == MANGA_BOOK_OFF || set != MANGA_STRIP) return set;
		return bookDir == MANGA_LTR ? MANGA_LTR : MANGA_RTL;
	}

	int mangaBookDirection(int settingMode)
	{
		return settingMode == MANGA_LTR ? MANGA_LTR : MANGA_RTL;
	}

	std::string mangaModeLabel(int set, int book)
	{
		static const char* NAMES[MANGA_MODES] = {"Right to left", "Left to right", "Vertical scroll"};
		if (set < 0 || set >= MANGA_MODES) set = MANGA_STRIP;
		if (set == MANGA_STRIP && book != MANGA_BOOK_OFF) return "Vertical scroll (paged)";
		return NAMES[set];
	}

	std::string mangaModeNote(int set, int book)
	{
		return set == MANGA_STRIP && book != MANGA_BOOK_OFF ? "Scrolls again outside Book" : "";
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

	bool mangaBookWide(int page, int w, int h)
	{
		return page > 0 && w > 0 && h > 0 && w * 5 >= h * 6;	// (w / h >= 1.2, in integers)
	}

	MangaBookScreen mangaBookScreenFrom(const std::vector<char>& wide, int first)
	{
		int n = (int)wide.size();
		MangaBookScreen s;
		s.first = std::clamp(first, 0, std::max(0, n - 1));
		if (s.first < n && wide[s.first]) { s.spread = true; return s; }
		// a page pairs with the next unless the next is a spread (which starts its own screen) or past the end
		if (s.first + 1 < n && !wide[s.first + 1]) s.count = 2;
		return s;
	}

	std::vector<MangaBookScreen> mangaBookScreens(const std::vector<char>& wide)
	{
		std::vector<MangaBookScreen> out;
		for (int i = 0; i < (int)wide.size();)
		{
			out.push_back(mangaBookScreenFrom(wide, i));
			i += out.back().count;
		}
		return out;
	}

	MangaBookScreen mangaBookScreenOf(const std::vector<char>& wide, int page)
	{
		MangaBookScreen last;
		for (const auto& s : mangaBookScreens(wide))
		{
			last = s;
			if (page < s.first + s.count) break;
		}
		return last;
	}

	MangaBookScreen mangaBookScreenBefore(const std::vector<char>& wide, int first)
	{
		if (first <= 0) return mangaBookScreenFrom(wide, 0);
		MangaBookScreen s = mangaBookScreenOf(wide, first - 1);
		s.count = std::min(s.count, first - s.first);
		return s;
	}

	double mangaBookZoomStep(double z, int dir)
	{
		static const double STEPS[] = {1.0, 1.5, 2.0, 3.0};
		const int n = 4;
		int at = 0;
		for (int i = 0; i < n; i++)
			if (z >= STEPS[i] - 0.01) at = i;
		return STEPS[std::clamp(at + (dir > 0 ? 1 : dir < 0 ? -1 : 0), 0, n - 1)];
	}

	// one axis: the pages from a to a + len (column pixels) in a window of size `view`
	static double bookZoomAxis(double o, double z, double a, double len, double view)
	{
		double drawn = len * z;
		if (drawn <= view) return (view - drawn) / 2 - a * z;
		return std::clamp(o, view - (a + len) * z, -a * z);
	}

	MangaBookZoom mangaBookZoomClamp(MangaBookZoom v, const MangaBookArea& pages, double viewW, double colH)
	{
		v.z = std::clamp(v.z, 1.0, 8.0);
		v.ox = bookZoomAxis(v.ox, v.z, pages.x, pages.w, viewW);
		v.oy = bookZoomAxis(v.oy, v.z, pages.y, pages.h, colH);
		return v;
	}

	MangaBookZoom mangaBookZoomTo(MangaBookZoom v, double z, double ax, double ay, const MangaBookArea& pages, double viewW, double colH)
	{
		z = std::clamp(z, 1.0, 8.0);
		double k = z / std::max(0.01, v.z);
		v.ox = ax - (ax - v.ox) * k;
		v.oy = ay - (ay - v.oy) * k;
		v.z = z;
		return mangaBookZoomClamp(v, pages, viewW, colH);
	}

	MangaBookZoom mangaBookPan(MangaBookZoom v, double dx, double dy, const MangaBookArea& pages, double viewW, double colH)
	{
		v.ox += dx;
		v.oy += dy;
		return mangaBookZoomClamp(v, pages, viewW, colH);
	}

	bool mangaBookZoomAtEnd(const MangaBookZoom& v, int dirY, const MangaBookArea& pages, double viewW, double colH)
	{
		MangaBookZoom to = mangaBookPan(v, 0, dirY > 0 ? -1e9 : 1e9, pages, viewW, colH);
		return std::abs(to.oy - mangaBookZoomClamp(v, pages, viewW, colH).oy) < 0.5;
	}

	int mangaBookZoomKey(int book, int dpadDx)
	{
		// held with the top panel on the left, the panel's right edge is up; held the other way, its left edge
		bool rightUp = book != MANGA_BOOK_RIGHT;
		return (dpadDx > 0) == rightUp ? 1 : -1;
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

	std::string mangaLangName(const std::string& code)
	{
		static const std::pair<const char*, const char*> NAMES[] = {{"all", "Multi-language"}, {"en", "English"},
			{"id", "Indonesian"}, {"ja", "Japanese"}, {"ko", "Korean"}, {"zh", "Chinese"}, {"zh-Hans", "Chinese (simplified)"},
			{"zh-Hant", "Chinese (traditional)"}, {"es", "Spanish"}, {"es-419", "Spanish (Latin America)"}, {"pt", "Portuguese"},
			{"pt-BR", "Portuguese (Brazil)"}, {"fr", "French"}, {"de", "German"}, {"it", "Italian"}, {"ru", "Russian"},
			{"ar", "Arabic"}, {"tr", "Turkish"}, {"vi", "Vietnamese"}, {"th", "Thai"}, {"pl", "Polish"}, {"uk", "Ukrainian"},
			{"fil", "Filipino"}, {"ms", "Malay"}, {"hi", "Hindi"}, {"bn", "Bengali"}, {"fa", "Persian"}, {"he", "Hebrew"},
			{"cs", "Czech"}, {"bg", "Bulgarian"}, {"ca", "Catalan"}, {"da", "Danish"}, {"fi", "Finnish"}, {"no", "Norwegian"},
			{"my", "Burmese"}, {"km", "Khmer"}, {"lo", "Lao"}, {"mn", "Mongolian"}, {"ta", "Tamil"}, {"te", "Telugu"}, {"ur", "Urdu"},
			{"sr", "Serbian"}, {"hr", "Croatian"}, {"sk", "Slovak"}, {"lt", "Lithuanian"}, {"et", "Estonian"}, {"ka", "Georgian"}, {"hu", "Hungarian"}, {"ro", "Romanian"}, {"nl", "Dutch"}, {"sv", "Swedish"}, {"el", "Greek"}};
		for (auto& n : NAMES)
			if (code == n.first) return std::string(n.second) + " \xC2\xB7 " + code;
		return code;
	}

	std::vector<std::string> splitLangs(const std::string& s)
	{
		std::vector<std::string> out;
		size_t a = 0;
		while (a <= s.size())
		{
			size_t b = s.find(',', a);
			if (b == std::string::npos) b = s.size();
			std::string one = s.substr(a, b - a);
			if (!one.empty()) out.push_back(one);
			a = b + 1;
		}
		return out;
	}

	bool mangaLangKept(const std::string& chosen, const std::string& lang)
	{
		// (with a choice made it is the whole rule: a multi-language source is "all", a choice of its own)
		if (chosen.empty() || lang.empty()) return true;
		std::string l0 = lang == "multi" ? "all" : lang;
		for (const auto& l : splitLangs(chosen))
			if (l == l0) return true;
		return false;
	}

	namespace
	{
		std::string asciiLower(std::string s)
		{
			for (char& c : s)
				if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
			return s;
		}
	}

	bool sourceSearchMatch(const std::string& query, const std::vector<std::string>& fields)
	{
		std::vector<std::string> low;
		for (const auto& f : fields) low.push_back(asciiLower(f));
		std::string q = asciiLower(query);
		size_t a = 0;
		while (a < q.size())
		{
			size_t b = q.find(' ', a);
			if (b == std::string::npos) b = q.size();
			std::string word = q.substr(a, b - a);
			a = b + 1;
			if (word.empty()) continue;
			bool found = false;
			for (const auto& f : low)
				if (f.find(word) != std::string::npos) { found = true; break; }
			if (!found) return false;
		}
		return true;
	}

	bool sourceListed(const std::string& query, const std::string& chosenLangs, const std::string& name, const std::string& lang,
		const std::string& from)
	{
		return mangaLangKept(chosenLangs, lang) && sourceSearchMatch(query, {name, mangaLangName(lang), from});
	}

	std::string mangaChapterTag(double number, const std::string& title, int total)
	{
		if (total <= 0) return "";
		auto numberText = [](double n) {
			char b[32];
			if (n == (double)(long long)n) snprintf(b, sizeof b, "%lld", (long long)n);
			else snprintf(b, sizeof b, "%g", n);
			return std::string(b);
		};
		if (number > 0) return "Ch. " + numberText(number);
		// "Chapter 12", "Ch. 12", "ch12.5", "Chap 3": the word, then the number
		std::string low;
		for (char c : title) low += (char)std::tolower((unsigned char)c);
		for (const char* word : {"chapter", "chap", "ch"})
		{
			size_t at = low.find(word);
			if (at == std::string::npos || (at > 0 && std::isalpha((unsigned char)low[at - 1]))) continue;
			size_t i = at + strlen(word);
			while (i < low.size() && (low[i] == '.' || low[i] == ' ')) i++;
			size_t j = i;
			while (j < low.size() && (std::isdigit((unsigned char)low[j]) || (low[j] == '.' && j > i))) j++;
			while (j > i && low[j - 1] == '.') j--;
			if (j > i) return "Ch. " + numberText(atof(low.substr(i, j - i).c_str()));
		}
		if (title.empty()) return "";
		// about 14 characters: cut at a character's start (UTF-8), never inside one
		const size_t keep = 14;
		size_t chars = 0, i = 0;
		while (i < title.size() && chars < keep)
		{
			i++;
			while (i < title.size() && ((unsigned char)title[i] & 0xC0) == 0x80) i++;
			chars++;
		}
		if (i >= title.size()) return title;
		return title.substr(0, i) + "\xE2\x80\xA6";
	}
}
