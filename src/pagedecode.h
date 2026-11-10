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
	PageImage decodePage(const uint8_t* data, size_t size, int targetW, int maxH = 0);

	// The same for a file on the card; a file over 64 MB is refused instead of read.
	PageImage decodePageFile(const std::string& path, int targetW, int maxH = 0);
}
