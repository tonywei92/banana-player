#pragma once
// The manga reader's page decoder (docs/design/manga.md, section 7): an image's bytes into RGBA pixels at the width the
// panel needs, through libavcodec and libswscale. No window and no threads of its own, so the reader can run it on its
// decode threads and core-test can call it directly.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bp
{
	struct PageImage
	{
		int w = 0, h = 0;
		std::vector<uint8_t> rgba;	// w * h * 4, rows top to bottom, no padding, straight (not premultiplied) alpha
		int srcW = 0, srcH = 0;	// the image's own size, before scaling
		std::string error;	// a few words when ok() is false

		bool ok() const
		{
			return error.empty() && w > 0 && h > 0 && rgba.size() == (size_t)w * (size_t)h * 4;
		}
	};

	// Decodes JPEG, PNG, WebP, GIF (its first frame), BMP and AVIF when the build has a decoder for it, scaled to
	// targetW wide with the aspect kept (height rounded, at least 1). targetW <= 0 means the image's own size; the width
	// never grows past 2x the image's own. maxH > 0 caps the height, and the width then shrinks to keep the aspect.
	// Failure is never a crash: ok() is false and error says why.
	// maxPixels > 0 (the reader's pages): never wider than the image itself, and at most that many pixels (a longer
	// page decoded a little narrower: a 1:11 manhwa page at 1024 wide was 45 MB)
	PageImage decodePage(const uint8_t* data, size_t size, int targetW, int maxH = 0, long long maxPixels = 0);

	// The same for a file on the card; a file over 64 MB is refused instead of read.
	PageImage decodePageFile(const std::string& path, int targetW, int maxH = 0, long long maxPixels = 0);

	// A band of a long page (bands.h): the image's own rows y0 .. y0 + rows (cut at its end), scaled to targetW wide
	// (never wider than the image), without the whole picture ever in memory: a JPEG's rows before the band are
	// skipped (libjpeg-turbo), a PNG's are read and let go. srcW/srcH are the whole image's. Not ok() for any other
	// format, an interlaced PNG, a CMYK JPEG or a damaged file: the caller then decodes the page whole.
	PageImage decodeBand(const uint8_t* data, size_t size, int targetW, int y0, int rows);

	// a page's file whole (64 MB at most); false with a few words in error
	bool readPageFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error);
}
