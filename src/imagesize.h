#pragma once
// An image's width and height from its first bytes, without decoding it (JPEG, PNG, WebP, GIF): the reader's strip
// sizes a page's place as soon as the page is on the card, so a long webtoon page is one tall place, not three short
// guesses that each want decoding at once. Pure, in bpcore.
#include <cstddef>
#include <cstdint>
#include <string>

namespace bp
{
	// false when the bytes aren't one of those formats, or its size isn't in them
	bool imageSize(const uint8_t* data, size_t n, int& w, int& h);
	// the same from a file's first 64 KB
	bool imageSizeFile(const std::string& path, int& w, int& h);
}
