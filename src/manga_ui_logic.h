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
}
