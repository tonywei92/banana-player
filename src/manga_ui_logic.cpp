#include "manga_ui_logic.h"

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
}
