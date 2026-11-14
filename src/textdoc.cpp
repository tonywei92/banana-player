#include "textdoc.h"
#include "webmedia.h"	// guessCharset (uchardet)
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iconv.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		bool validUtf8(const uint8_t* p, size_t n, bool cutOk)
		{
			size_t i = 0;
			while (i < n)
			{
				uint8_t c = p[i];
				if (c < 0x80) { i++; continue; }
				int k = c >= 0xF0 && c <= 0xF4 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 && c < 0xE0 ? 1 : -1;
				if (k < 0) return false;
				if (i + k >= n) return cutOk;	// (a character cut by the sample's end)
				for (int j = 1; j <= k; j++)
					if ((p[i + j] & 0xC0) != 0x80) return false;
				i += k + 1;
			}
			return true;
		}

		void putUtf8(std::string& o, uint32_t cp)
		{
			if (cp < 0x80) o += (char)cp;
			else if (cp < 0x800) { o += (char)(0xC0 | cp >> 6); o += (char)(0x80 | (cp & 63)); }
			else if (cp < 0x10000) { o += (char)(0xE0 | cp >> 12); o += (char)(0x80 | (cp >> 6 & 63)); o += (char)(0x80 | (cp & 63)); }
			else { o += (char)(0xF0 | cp >> 18); o += (char)(0x80 | (cp >> 12 & 63)); o += (char)(0x80 | (cp >> 6 & 63)); o += (char)(0x80 | (cp & 63)); }
		}

		// a line's last touches: tabs to four spaces, a CR (from CRLF) dropped, other controls left out
		void tidy(std::string& o, uint32_t cp)
		{
			if (cp == '\t') o += "    ";
			else if (cp == '\r' || (cp < 0x20) || cp == 0x7F || cp == 0xFEFF) return;
			else putUtf8(o, cp);
		}
	}

	uint32_t nextCodePoint(const std::string& s, size_t& i)
	{
		uint8_t c = (uint8_t)s[i];
		if (c < 0x80) { i++; return c; }
		int k = c >= 0xF0 && c <= 0xF4 ? 3 : c >= 0xE0 && c < 0xF0 ? 2 : c >= 0xC2 && c < 0xE0 ? 1 : -1;
		if (k < 0 || i + k >= s.size()) { i++; return 0xFFFD; }
		uint32_t cp = c & (0x3F >> k);
		for (int j = 1; j <= k; j++)
		{
			uint8_t d = (uint8_t)s[i + j];
			if ((d & 0xC0) != 0x80) { i++; return 0xFFFD; }
			cp = cp << 6 | (d & 63);
		}
		i += k + 1;
		return cp;
	}

	TextDoc::~TextDoc()
	{
		close();
	}

	void TextDoc::close()
	{
		if (ownsMap && data) munmap((void*)data, mapped);
		data = nullptr;
		ownsMap = false;
		copy.clear();
		mapped = start = end = 0;
		enc.clear();
		detected.clear();
	}

	bool TextDoc::open(const std::string& path)
	{
		close();
		int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
		if (fd < 0) return (err = "This file can't be opened"), false;
		struct stat st;
		if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
		{
			::close(fd);
			return (err = "This isn't a file"), false;
		}
		if (st.st_size == 0)
		{
			::close(fd);
			copy.clear();
			data = (const uint8_t*)"";
			detect();
			return true;
		}
		void* m = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
		::close(fd);	// (the mapping keeps the file)
		if (m == MAP_FAILED) return (err = "This file can't be read"), false;
		// read in order, mostly forward: the kernel reads ahead for it
		madvise(m, (size_t)st.st_size, MADV_SEQUENTIAL);
		data = (const uint8_t*)m;
		mapped = (size_t)st.st_size;
		ownsMap = true;
		end = mapped;
		detect();
		return true;
	}

	bool TextDoc::openBytes(const std::string& bytes)
	{
		close();
		copy = bytes;
		data = (const uint8_t*)copy.data();
		mapped = end = copy.size();
		detect();
		return true;
	}

	void TextDoc::detect()
	{
		start = 0;
		unit = 1;
		bigEndian = false;
		size_t n = std::min<size_t>(end, 64 * 1024);
		if (end >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) { start = 3; detected = "UTF-8"; }
		else if (end >= 2 && data[0] == 0xFF && data[1] == 0xFE) { start = 2; unit = 2; detected = "UTF-16LE"; }
		else if (end >= 2 && data[0] == 0xFE && data[1] == 0xFF) { start = 2; unit = 2; bigEndian = true; detected = "UTF-16BE"; }
		else if (validUtf8(data, n, n < end)) detected = "UTF-8";
		else
		{
			// UTF-16 without a BOM: the high bytes of plain text are mostly 0 (one side of each pair)
			size_t z0 = 0, z1 = 0;
			for (size_t i = 0; i + 1 < n; i += 2)
			{
				z0 += data[i] == 0;
				z1 += data[i + 1] == 0;
			}
			if (n >= 64 && z1 > n / 6 && z0 < n / 50) { unit = 2; detected = "UTF-16LE"; }
			else if (n >= 64 && z0 > n / 6 && z1 < n / 50) { unit = 2; bigEndian = true; detected = "UTF-16BE"; }
			else
			{
				std::string g = guessCharset(std::string((const char*)data, n));
				detected = g.empty() || g == "UTF-8" || g == "ASCII" ? "CP1252" : g;
			}
		}
		if (unit == 2) end = start + (end - start) / 2 * 2;	// (a stray last byte isn't a character)
		enc = detected;
	}

	void TextDoc::setEncoding(const std::string& charset)
	{
		// (only the code page changes: a UTF-16 file stays one, its line breaks are two bytes)
		if (unit == 2) return;
		enc = charset.empty() ? detected : charset;
	}

	size_t TextDoc::findNl(size_t from, size_t limit) const
	{
		if (unit == 1)
		{
			const void* p = memchr(data + from, '\n', limit - from);
			return p ? (size_t)((const uint8_t*)p - data) : limit;
		}
		// UTF-16: a '\n' unit on the units' grid (LE: 0A 00, BE: 00 0A); memchr finds the 0A candidates
		size_t i = from;
		while (i < limit)
		{
			const void* p = memchr(data + i, '\n', limit - i);
			if (!p) return limit;
			size_t at = (size_t)((const uint8_t*)p - data);
			if (!bigEndian && (at - start) % 2 == 0 && at + 1 < end && data[at + 1] == 0) return at;
			if (bigEndian && at > start && (at - 1 - start) % 2 == 0 && data[at - 1] == 0 && at - 1 >= from) return at - 1;
			i = at + 1;
		}
		return limit;
	}

	size_t TextDoc::lineStart(size_t off) const
	{
		// back to the byte after the line break before off (memrchr, by hand where libc has none)
		size_t i = off;
		if (unit == 1)
		{
#ifdef __GLIBC__
			const void* p = i > start ? memrchr(data + start, '\n', i - start) : nullptr;
			return p ? (size_t)((const uint8_t*)p - data) + 1 : start;
#else
			while (i > start && data[i - 1] != '\n') i--;
			return i;
#endif
		}
		i = start + (i - start) / 2 * 2;
		while (i >= start + 2)
		{
			size_t u = i - 2;
			bool nl = bigEndian ? (data[u] == 0 && data[u + 1] == '\n') : (data[u] == '\n' && data[u + 1] == 0);
			if (nl) return i;
			i = u;
		}
		return start;
	}

	size_t TextDoc::align(size_t off) const
	{
		if (unit == 2)
		{
			size_t a = start + (off - start + 1) / 2 * 2;
			// (not between a surrogate pair's halves)
			if (a + 1 < end)
			{
				uint16_t c = bigEndian ? (uint16_t)(data[a] << 8 | data[a + 1]) : (uint16_t)(data[a + 1] << 8 | data[a]);
				if (c >= 0xDC00 && c <= 0xDFFF) a += 2;
			}
			return std::min(a, end);
		}
		if (enc != "UTF-8") return off;	// (a code page's characters are one byte; the CJK ones split harmlessly)
		while (off < end && (data[off] & 0xC0) == 0x80) off++;
		return off;
	}

	TextDoc::Seg TextDoc::segFrom(size_t from) const
	{
		Seg s;
		s.from = from;
		size_t limit = std::min(end, from + SEG);
		size_t nl = findNl(from, limit);
		if (nl < limit)
		{
			s.to = nl;
			s.next = nl + unit;
		}
		else if (limit >= end) s.to = s.next = end;
		else s.to = s.next = align(limit);
		if (s.next <= from) s.to = s.next = std::min(end, from + unit);	// (always forward)
		return s;
	}

	TextDoc::Seg TextDoc::segAt(size_t off) const
	{
		off = std::clamp(off, start, end);
		size_t r = lineStart(off);
		Seg s = segFrom(r);
		while (s.next <= off && s.next < end && s.to == s.next) s = segFrom(s.next);	// (a long line's pieces)
		return s;
	}

	bool TextDoc::segBefore(size_t from, Seg& out) const
	{
		if (from <= start) return false;
		// the line holding the byte before from, then its pieces up to from
		Seg s = segFrom(lineStart(from - unit));
		while (s.next < from) s = segFrom(s.next);
		out = s;
		return true;
	}

	std::string TextDoc::text(const Seg& s) const
	{
		std::string o;
		if (s.to <= s.from) return o;
		size_t n = s.to - s.from;
		o.reserve(n + n / 4);
		const uint8_t* p = data + s.from;
		if (unit == 2)
		{
			for (size_t i = 0; i + 1 < n; i += 2)
			{
				uint32_t c = bigEndian ? (uint32_t)(p[i] << 8 | p[i + 1]) : (uint32_t)(p[i + 1] << 8 | p[i]);
				if (c >= 0xD800 && c <= 0xDBFF && i + 3 < n)
				{
					uint32_t d = bigEndian ? (uint32_t)(p[i + 2] << 8 | p[i + 3]) : (uint32_t)(p[i + 3] << 8 | p[i + 2]);
					if (d >= 0xDC00 && d <= 0xDFFF)
					{
						c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
						i += 2;
					}
				}
				tidy(o, c >= 0xD800 && c <= 0xDFFF ? 0xFFFD : c);
			}
			return o;
		}
		if (enc == "UTF-8")
		{
			std::string raw((const char*)p, n);
			for (size_t i = 0; i < raw.size();)
			{
				// plain ASCII runs straight through (most text)
				uint8_t c = (uint8_t)raw[i];
				if (c >= 0x20 && c < 0x7F) { o += (char)c; i++; continue; }
				tidy(o, nextCodePoint(raw, i));
			}
			return o;
		}
		// a code page: iconv for this piece alone
		std::string conv;
		iconv_t cd = iconv_open("UTF-8", enc.c_str());
		if (cd != (iconv_t)-1)
		{
			conv.resize(n * 4 + 8);
			char* in = (char*)p;
			size_t inLeft = n, outLeft = conv.size();
			char* out = &conv[0];
			while (inLeft > 0)
			{
				if (iconv(cd, &in, &inLeft, &out, &outLeft) != (size_t)-1) break;
				if (errno == E2BIG) break;
				// a byte this page doesn't have: a mark for it, and on
				in++;
				inLeft--;
				if (outLeft >= 3) { memcpy(out, "\xEF\xBF\xBD", 3); out += 3; outLeft -= 3; }
			}
			conv.resize(conv.size() - outLeft);
			iconv_close(cd);
		}
		else conv.assign((const char*)p, n);
		for (size_t i = 0; i < conv.size();) tidy(o, nextCodePoint(conv, i));
		return o;
	}

	std::string TextDoc::encodeQuery(const std::string& utf8) const
	{
		if (utf8.empty()) return "";
		if (unit == 2)
		{
			std::string o;
			for (size_t i = 0; i < utf8.size();)
			{
				uint32_t cp = nextCodePoint(utf8, i);
				auto put = [&](uint16_t u) {
					if (bigEndian) { o += (char)(u >> 8); o += (char)(u & 255); }
					else { o += (char)(u & 255); o += (char)(u >> 8); }
				};
				if (cp >= 0x10000) { cp -= 0x10000; put((uint16_t)(0xD800 + (cp >> 10))); put((uint16_t)(0xDC00 + (cp & 0x3FF))); }
				else put((uint16_t)cp);
			}
			return o;
		}
		if (enc == "UTF-8") return utf8;
		// a code page: through iconv, once
		iconv_t cd = iconv_open(enc.c_str(), "UTF-8");
		if (cd == (iconv_t)-1) return "";
		std::string out(utf8.size() * 4 + 8, '\0');
		char* in = (char*)utf8.data();
		size_t inLeft = utf8.size(), outLeft = out.size();
		char* o = &out[0];
		size_t r = iconv(cd, &in, &inLeft, &o, &outLeft);
		iconv_close(cd);
		if (r == (size_t)-1) return "";
		out.resize(out.size() - outLeft);
		return out;
	}

	bool TextDoc::find(const std::string& query, size_t from, size_t to, bool back, size_t& at,
		const std::function<bool()>& cancel, const std::function<void(double)>& progress) const
	{
		if (cancel && cancel()) return false;
		std::string needle = encodeQuery(query);
		size_t n = needle.size();
		from = std::clamp(from, start, end);
		to = std::clamp(to, start, end);
		if (!n || to <= from || to - from < n) return false;
		// ASCII letters match in either case (a byte at a time: any encoding's ASCII is itself, UTF-16's too)
		uint8_t fold[256];
		for (int c = 0; c < 256; c++) fold[c] = (uint8_t)(c >= 'A' && c <= 'Z' ? c + 32 : c);
		std::string low = needle;
		for (char& c : low) c = (char)fold[(uint8_t)c];
		const uint8_t* nd = (const uint8_t*)low.data();
		uint8_t first = nd[0], firstUp = (uint8_t)(first >= 'a' && first <= 'z' ? first - 32 : first);
		auto matchAt = [&](size_t i) {
			if (unit == 2 && (i - start) % 2) return false;	// (UTF-16: on the units' grid)
			for (size_t k = 1; k < n; k++)
				if (fold[data[i + k]] != nd[k]) return false;
			return true;
		};
		const size_t CHUNK = 4u << 20;	// (a look at cancel and a word of progress every 4 MB)
		size_t span = to - from, last = to - n;
		if (!back)
		{
			for (size_t i = from; i <= last;)
			{
				size_t chunkEnd = std::min(last + 1, i + CHUNK);
				while (i < chunkEnd)
				{
					// memchr for the first byte (both cases), then the rest
					const void* a = memchr(data + i, first, chunkEnd - i);
					const void* b = firstUp != first ? memchr(data + i, firstUp, chunkEnd - i) : nullptr;
					size_t pa = a ? (size_t)((const uint8_t*)a - data) : chunkEnd, pb = b ? (size_t)((const uint8_t*)b - data) : chunkEnd;
					size_t p = std::min(pa, pb);
					if (p >= chunkEnd) { i = chunkEnd; break; }
					if (matchAt(p)) { at = p; return true; }
					i = p + 1;
				}
				if (cancel && cancel()) return false;
				if (progress) progress((double)(i - from) / span);
			}
			return false;
		}
		for (size_t i = last + 1; i-- > from;)
		{
			if (fold[data[i]] == first && matchAt(i)) { at = i; return true; }
			if ((last - i) % CHUNK == 0 && i != last)
			{
				if (cancel && cancel()) return false;
				if (progress) progress((double)(last - i) / span);
			}
		}
		return false;
	}

	std::vector<uint32_t> wrapRows(const std::string& text, int width, const std::function<int(uint32_t, size_t)>& adv)
	{
		std::vector<uint32_t> rows{0};
		if (width <= 0) return rows;
		int x = 0;
		size_t rowStart = 0, breakAt = 0;	// breakAt: past the last space or hyphen in this row (0: none yet)
		int xAtBreak = 0;
		for (size_t i = 0; i < text.size();)
		{
			size_t at = i;
			uint32_t cp = nextCodePoint(text, i);
			int a = adv(cp, at);
			if (x + a > width && at > rowStart && cp != ' ')
			{
				// over: back to the last space that fit (the word goes down whole), else here
				size_t cut = breakAt > rowStart ? breakAt : at;
				rows.push_back((uint32_t)cut);
				x = cut == at ? 0 : x - xAtBreak;
				rowStart = cut;
				breakAt = 0;
			}
			x += a;
			if (cp == ' ' || cp == '-' || cp == 0x2014 || cp == 0x3000)
			{
				breakAt = i;
				xAtBreak = x;
			}
		}
		return rows;
	}
}
