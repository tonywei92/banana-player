#pragma once
// A book with no cover of its own gets one drawn from its title: the same title, the same cover, everywhere (the
// shelf, the top pane, the catalogs; the dashboard repeats the sums in web/js/books.js). Pure, so the bottom panel
// (Ui::Kit::bookPlaceholder), the top pane (previewpane.cpp) and the tests share them.
#include "artph.h"
#include <cstdint>
#include <string>

namespace bookcover
{
	struct Rgb { uint8_t r, g, b; };
	// Eight muted ground colours, dark enough for white type and apart from each other; not the theme's (a cover
	// is a thing on the shelf, as its own colour on any theme)
	inline const Rgb* palette()
	{
		static const Rgb P[8] = {{0x2F, 0x4B, 0x7C}, {0x2E, 0x6B, 0x5E}, {0x7A, 0x3B, 0x4E}, {0x5B, 0x45, 0x8A},
			{0x8A, 0x5A, 0x2B}, {0x38, 0x5F, 0x73}, {0x4F, 0x6B, 0x34}, {0x6E, 0x3F, 0x35}};
		return P;
	}
	// case and the ends' spaces don't make a different book
	inline std::string norm(const std::string& title)
	{
		size_t a = title.find_first_not_of(' '), b = title.find_last_not_of(' ');
		std::string t = a == std::string::npos ? "" : title.substr(a, b - a + 1);
		for (char& c : t) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
		return t;
	}
	inline int index(const std::string& title) { return (int)(artph::hash(norm(title)) % 8); }
	inline Rgb ground(const std::string& title) { return palette()[index(title)]; }
	inline Rgb ink() { return {0xF4, 0xF1, 0xEA}; }
	inline Rgb inkDim() { return {0xD2, 0xCC, 0xC0}; }

	enum Kind { OTHER, TXT, EPUB, PDF, COMIC };
	// the format's small mark, from the file's extension (any case, no dot needed)
	inline Kind kindOf(const std::string& pathOrExt)
	{
		size_t dot = pathOrExt.rfind('.');
		std::string e = dot == std::string::npos ? pathOrExt : pathOrExt.substr(dot + 1);
		for (char& c : e) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
		if (e == "txt") return TXT;
		if (e == "epub") return EPUB;
		if (e == "pdf") return PDF;
		if (e == "cbz" || e == "cbr") return COMIC;
		return OTHER;
	}
}
