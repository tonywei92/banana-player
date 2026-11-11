#include "cbz.h"
#include "manga_names.h"
#include <cerrno>
#include <cstring>
#include <zlib.h>

namespace bp
{
	namespace
	{
		// Caps: a chapter is a few hundred pages, its directory a few tens of KB and a page tens of MB; past
		// these the archive is not a chapter but a hostile or broken one, so refuse rather than allocate on
		// numbers the file itself wrote
		const uint64_t kMaxEntries = 10000;
		const uint64_t kMaxCentral = 16ull << 20;
		const uint64_t kMaxSize = 64ull << 20;

		// The end record is 22 bytes plus a comment of at most 64 KB - 1
		const size_t kEocdWindow = 64 * 1024 + 22;

		uint16_t le16(const unsigned char* p)
		{
			return (uint16_t)(p[0] | (p[1] << 8));
		}

		uint32_t le32(const unsigned char* p)
		{
			return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
		}

		bool sig(const unsigned char* p, const char* s)
		{
			return std::memcmp(p, s, 4) == 0;
		}
	}

	Cbz::~Cbz()
	{
		close();
	}

	void Cbz::close()
	{
		if (f) std::fclose(f);
		f = nullptr;
		fileSize = 0;
		ent.clear();
		entFlags.clear();
	}

	bool Cbz::bad(const std::string& msg)
	{
		err = msg;
		return false;
	}

	bool Cbz::fail(const std::string& msg)
	{
		close();
		return bad(msg);
	}

	// Positioned read: every call seeks, so nothing depends on where the handle happens to sit
	bool Cbz::at(uint64_t off, void* dst, size_t n)
	{
		if (off + n > fileSize) return false;	// never past the file, however the archive points
		if (fseeko(f, (off_t)off, SEEK_SET) != 0) return false;
		return n == 0 || std::fread(dst, 1, n, f) == n;
	}

	bool Cbz::open(const std::string& path)
	{
		close();
		err.clear();
		f = std::fopen(path.c_str(), "rb");
		if (!f) return fail(path + ": " + std::strerror(errno));
		if (fseeko(f, 0, SEEK_END) != 0) return fail(path + ": " + std::strerror(errno));
		off_t end = ftello(f);
		if (end < 22) return fail(path + ": too short to be a ZIP archive");
		fileSize = (uint64_t)end;

		// The end record sits anywhere in the last 64 KB + 22 bytes behind a comment, so scan back from the end
		uint64_t win = fileSize < kEocdWindow ? fileSize : kEocdWindow;
		std::vector<unsigned char> tail(win);
		if (!at(fileSize - win, tail.data(), tail.size())) return fail(path + ": could not read the end");
		size_t eocd = 0;
		bool found = false;
		for (size_t i = tail.size(); i >= 22; i--)
		{
			if (!sig(&tail[i - 22], "PK\x05\x06")) continue;
			// what makes one believable is its comment: the record plus its length must end exactly at the file's end
			uint64_t at8 = fileSize - win + (i - 22);
			if (at8 + 22 + le16(&tail[i - 2]) == fileSize)
			{
				eocd = i - 22;
				found = true;
				break;
			}
		}
		if (!found) return fail(path + ": no end-of-central-directory, not a ZIP archive");
		const unsigned char* e = &tail[eocd];
		uint32_t cdSize = le32(e + 12);
		uint64_t cdOff = le32(e + 16);
		uint32_t total = le16(e + 10);
		// The 0xFFFF/0xFFFFFFFF fields and the ZIP64 end locator in front of the record all mean a ZIP64
		// archive, whose extra fields we don't read: chapters are small, refusing is cheaper than half-parsing
		bool zip64 = cdOff == 0xFFFFFFFFu || cdSize == 0xFFFFFFFFu || total >= 0xFFFF
			|| (eocd >= 20 && sig(&tail[eocd - 20], "PK\x06\x07"));
		if (zip64) return fail(path + ": ZIP64 archives are not supported");
		if (le16(e + 4) != 0 || le16(e + 6) != 0 || le16(e + 8) != total)
			return fail(path + ": archives that span several disks are not supported");
		if (total > kMaxEntries) return fail(path + ": too many entries (" + std::to_string(total) + ")");
		if (cdSize > kMaxCentral) return fail(path + ": the central directory is too large");
		if (cdOff + cdSize > fileSize) return fail(path + ": the central directory lies outside the file");
		std::vector<unsigned char> cd(cdSize);
		if (!at(cdOff, cd.data(), cd.size())) return fail(path + ": could not read the central directory");

		size_t pos = 0;
		for (uint32_t k = 0; k < total; k++)
		{
			if (pos + 46 > cd.size() || !sig(&cd[pos], "PK\x01\x02"))
				return fail(path + ": the central directory is damaged");
			uint16_t flags = le16(&cd[pos + 8]);
			uint32_t method = le16(&cd[pos + 10]);
			uint32_t crc = le32(&cd[pos + 16]), packed = le32(&cd[pos + 20]), size = le32(&cd[pos + 24]);
			uint32_t local = le32(&cd[pos + 42]);
			size_t n = le16(&cd[pos + 28]), extra = le16(&cd[pos + 30]), comment = le16(&cd[pos + 32]);
			if (pos + 46 + n + extra + comment > cd.size())
				return fail(path + ": the central directory is damaged");
			if (crc == 0xFFFFFFFFu || packed == 0xFFFFFFFFu || size == 0xFFFFFFFFu || local == 0xFFFFFFFFu)
				return fail(path + ": ZIP64 archives are not supported");
			std::string name((const char*)&cd[pos + 46], n);
			for (char& c : name)
				if (c == '\\') c = '/';	// archives built on Windows; names come out with '/' only
			pos += 46 + n + extra + comment;
			// A name ending in '/' is a folder: listings carry them, they hold nothing to read
			if (!name.empty() && name[name.size() - 1] == '/') continue;
			if (local + 30ull > fileSize) return fail(path + ": the central directory is damaged");
			ent.push_back(CbzEntry{std::move(name), method, packed, size, crc, local});
			entFlags.push_back(flags);
		}
		if (pos != cd.size()) return fail(path + ": the central directory is damaged");
		return true;
	}

	std::vector<std::string> Cbz::pages() const
	{
		std::vector<std::string> v;
		for (const CbzEntry& e : ent)
			if (isPageImage(e.name)) v.push_back(e.name);
		sortPages(v);
		return v;
	}

	bool Cbz::read(const std::string& name, std::vector<uint8_t>& out)
	{
		err.clear();
		if (!f) return bad("the archive is not open");
		size_t i = ent.size();
		for (size_t k = 0; k < ent.size(); k++)
			if (ent[k].name == name) { i = k; break; }
		if (i == ent.size()) return bad("\"" + name + "\": not in the archive");
		const CbzEntry& e = ent[i];
		if (entFlags[i] & 1) return bad("\"" + name + "\": encrypted entries cannot be read");
		if (e.method != 0 && e.method != 8)
			return bad("\"" + name + "\": compression method " + std::to_string(e.method) + " cannot be read");
		if (e.size > kMaxSize) return bad("\"" + name + "\": larger than 64 MB uncompressed");

		// The local header's own name and extra lengths decide where the data starts: writers may put different
		// ones there than the central record does
		unsigned char lh[30];
		if (!at(e.localOffset, lh, sizeof(lh)) || !sig(lh, "PK\x03\x04"))
			return bad("\"" + name + "\": the local header is damaged");
		uint64_t data = e.localOffset + 30 + le16(lh + 26) + le16(lh + 28);
		if (data > fileSize || e.packed > fileSize - data)
			return bad("\"" + name + "\": its data lies outside the file");

		if (e.method == 0)
		{
			if (e.packed != e.size) return bad("\"" + name + "\": stored entry of the wrong size");
			out.resize((size_t)e.size);
			if (e.size && !at(data, out.data(), (size_t)e.size)) return bad("\"" + name + "\": could not be read");
			if (crc32(0, out.data(), (uInt)out.size()) != e.crc) return bad("\"" + name + "\": CRC mismatch");
			return true;
		}

		// A raw deflate stream, without the gzip header zlib would expect by default
		z_stream s;
		std::memset(&s, 0, sizeof(s));
		if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return bad("\"" + name + "\": inflate could not start");
		std::vector<unsigned char> in(64u << 10);
		out.clear();
		uint64_t pos = data, end = data + e.packed;
		bool ok = false;
		for (;;)
		{
			if (s.avail_in == 0)
			{
				if (pos >= end) { bad("\"" + name + "\": the deflate stream ends early"); break; }
				size_t want = (size_t)(end - pos < in.size() ? end - pos : in.size());
				if (fseeko(f, (off_t)pos, SEEK_SET) != 0) { bad("\"" + name + "\": could not be read"); break; }
				size_t got = std::fread(in.data(), 1, want, f);
				if (got == 0) { bad("\"" + name + "\": could not be read"); break; }
				pos += got;
				s.next_in = in.data();
				s.avail_in = (uInt)got;
			}
			// Grow the output a chunk at a time, capped even if the header's size lied
			size_t room = kMaxSize - out.size();
			if (!room) { bad("\"" + name + "\": larger than 64 MB uncompressed"); break; }
			if (room > (64u << 10)) room = 64u << 10;
			size_t from = out.size();
			out.resize(from + room);
			s.next_out = out.data() + from;
			s.avail_out = (uInt)room;
			int r = inflate(&s, Z_NO_FLUSH);
			out.resize(out.size() - s.avail_out);
			if (r == Z_STREAM_END) { ok = true; break; }
			if (r != Z_OK) { bad("\"" + name + "\": the deflate stream is corrupt"); break; }
		}
		inflateEnd(&s);
		if (!ok) return false;
		if (out.size() != e.size)
			return bad("\"" + name + "\": wrong size (" + std::to_string(out.size()) + ", the archive said "
				+ std::to_string(e.size) + ")");
		if (crc32(0, out.data(), (uInt)out.size()) != e.crc) return bad("\"" + name + "\": CRC mismatch");
		return true;
	}
}
