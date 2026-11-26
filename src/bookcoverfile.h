#pragma once
#include "pagedecode.h"
#include <cstdint>
#include <string>
#include <vector>

namespace bp
{
	// A book's own cover, found one way for the dashboard (/api/bookcover) and the handheld (Books' shelf, Reading
	// now, the top screen's preview): an EPUB's cover image (the manifest's cover-image, the EPUB 2 cover meta, the
	// guide's cover reference), else its first picture; a comic's first page; a PDF's first page drawn. No window
	// code here: the decode of `bytes` is the caller's (on its worker thread).
	struct BookCover
	{
		std::vector<uint8_t> bytes;	// the image as the book holds it (empty when `drawn` is set)
		PageImage drawn;	// a PDF's page, already pixels (drawW wide)
	};

	// false: the book has no cover (a TXT, a novel with no picture): the drawn sleeve is the answer
	bool findBookCover(const std::string& path, int drawW, BookCover& out);
}
