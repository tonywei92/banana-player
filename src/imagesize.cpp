#include "imagesize.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace bp
{
	namespace
	{
		uint32_t be16(const uint8_t* p) { return (uint32_t)p[0] << 8 | p[1]; }
		uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
		uint32_t le16(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
		uint32_t le24(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; }
	}

	bool imageSize(const uint8_t* d, size_t n, int& w, int& h)
	{
		// PNG: the signature, then IHDR's width and height
		if (n >= 24 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0 && memcmp(d + 12, "IHDR", 4) == 0)
		{
			w = (int)be32(d + 16);
			h = (int)be32(d + 20);
			return w > 0 && h > 0;
		}
		// GIF: the logical screen's size
		if (n >= 10 && (memcmp(d, "GIF87a", 6) == 0 || memcmp(d, "GIF89a", 6) == 0))
		{
			w = (int)le16(d + 6);
			h = (int)le16(d + 8);
			return w > 0 && h > 0;
		}
		// WebP: RIFF....WEBP then VP8 (lossy), VP8L (lossless) or VP8X (extended)
		if (n >= 30 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0)
		{
			const uint8_t* c = d + 12;
			if (memcmp(c, "VP8X", 4) == 0)
			{
				w = (int)le24(c + 12) + 1;
				h = (int)le24(c + 15) + 1;
				return true;
			}
			if (memcmp(c, "VP8L", 4) == 0 && c[8] == 0x2f)
			{
				uint32_t b = (uint32_t)c[9] | (uint32_t)c[10] << 8 | (uint32_t)c[11] << 16 | (uint32_t)c[12] << 24;
				w = (int)(b & 0x3fff) + 1;
				h = (int)((b >> 14) & 0x3fff) + 1;
				return true;
			}
			if (memcmp(c, "VP8 ", 4) == 0 && c[11] == 0x9d && c[12] == 0x01 && c[13] == 0x2a)
			{
				w = (int)(le16(c + 14) & 0x3fff);
				h = (int)(le16(c + 16) & 0x3fff);
				return w > 0 && h > 0;
			}
			return false;
		}
		// JPEG: walk the markers to a start of frame (SOF0..SOF15 but DHT, JPG and DAC)
		if (n >= 4 && d[0] == 0xFF && d[1] == 0xD8)
		{
			size_t i = 2;
			while (i + 9 < n)
			{
				if (d[i] != 0xFF) { i++; continue; }
				uint8_t m = d[i + 1];
				if (m == 0xFF) { i++; continue; }
				if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }
				if (m == 0xD9 || m == 0xDA) return false;	// the end, or the image data: no frame header before it
				uint32_t len = be16(d + i + 2);
				if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)
				{
					h = (int)be16(d + i + 5);
					w = (int)be16(d + i + 7);
					return w > 0 && h > 0;
				}
				if (len < 2) return false;
				i += 2 + len;
			}
			return false;
		}
		return false;
	}

	bool imageSizeFile(const std::string& path, int& w, int& h)
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f) return false;
		std::vector<uint8_t> buf(64 * 1024);	// (a scan's header is near its start; a chapter's worth read at open)
		size_t n = fread(buf.data(), 1, buf.size(), f);
		fclose(f);
		return imageSize(buf.data(), n, w, h);
	}
}
