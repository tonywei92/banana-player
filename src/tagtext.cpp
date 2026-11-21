#include "tagtext.h"
#include <cerrno>
#include <cstdint>
#include <mutex>
#include <zlib.h>
#include <vector>

namespace bp
{
	extern const unsigned char CJK_cp932[], CJK_gbk[], CJK_big5[];
	extern const size_t CJK_cp932_LEN, CJK_gbk_LEN, CJK_big5_LEN;

	namespace
	{
		// next code point of valid UTF-8 at i (advances i), or -1 when it isn't well formed
		long next(const std::string& s, size_t& i)
		{
			unsigned char c = (unsigned char)s[i];
			int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
			if (n < 0 || i + n >= s.size() + (n == 0 ? 1 : 0)) return -1;
			long cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
			for (int k = 1; k <= n; k++)
			{
				unsigned char d = (unsigned char)s[i + k];
				if ((d & 0xC0) != 0x80) return -1;
				cp = (cp << 6) | (d & 0x3F);
			}
			static const long minimum[] = {0, 0x80, 0x800, 0x10000};
			if (cp < minimum[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return -1;
			i += n + 1;
			return cp;
		}

		bool validUtf8(const std::string& s)
		{
			for (size_t i = 0; i < s.size();)
				if (next(s, i) < 0) return false;
			return true;
		}

		bool cjk(long cp)
		{
			return (cp >= 0x3000 && cp <= 0x30FF) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
				(cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x2190 && cp <= 0x25FF);
		}

		void put(std::string& o, long cp)
		{
			if (cp < 0x80) o += (char)cp;
			else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
			else { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
		}

		// one table (rows: lead 0x81..0xFE, columns: trail 0x40..0xFE), inflated once on first use
		const std::vector<uint16_t>& table(int which)
		{
			static std::vector<uint16_t> t[3];
			static std::once_flag once[3];
			std::call_once(once[which], [&] {
				const unsigned char* z = which == 0 ? CJK_cp932 : which == 1 ? CJK_gbk : CJK_big5;
				size_t zl = which == 0 ? CJK_cp932_LEN : which == 1 ? CJK_gbk_LEN : CJK_big5_LEN;
				uLongf n = 126 * 191 * 2;
				std::vector<unsigned char> raw(n);
				if (uncompress(raw.data(), &n, z, (uLong)zl) != Z_OK || n != raw.size()) return;
				t[which].resize(n / 2);
				for (size_t i = 0; i < t[which].size(); i++) t[which][i] = (uint16_t)(raw[i * 2] | (raw[i * 2 + 1] << 8));
			});
			return t[which];
		}

		// the bytes read as table `which` (0 CP932, 1 GBK, 2 Big5), accepted only when every byte decodes and the
		// result is mostly CJK or kana
		bool tryTable(int which, const std::string& s, std::string& out)
		{
			const std::vector<uint16_t>& t = table(which);
			if (t.empty()) return false;
			std::string u;
			int wide = 0, hit = 0;
			for (size_t i = 0; i < s.size();)
			{
				unsigned char c = (unsigned char)s[i];
				long cp;
				if (c < 0x80) { cp = c; i++; }
				else if (which == 0 && c >= 0xA1 && c <= 0xDF) { cp = 0xFF61 + (c - 0xA1); i++; }
				else
				{
					if (c < 0x81 || c > 0xFE || i + 1 >= s.size()) return false;
					unsigned char d = (unsigned char)s[i + 1];
					if (d < 0x40) return false;
					cp = t[(size_t)(c - 0x81) * 191 + (d - 0x40)];
					if (!cp) return false;
					i += 2;
				}
				// half-width kana is neutral: it is how most Big5 and GBK text reads as CP932
				if (cp >= 0x80 && !(cp >= 0xFF61 && cp <= 0xFF9F))
				{
					wide++;
					if (cjk(cp)) hit++;
				}
				put(u, cp);
			}
			if (wide == 0 || hit * 10 < wide * 8) return false;
			out = u;
			return true;
		}
	}

	std::string cleanTagText(const std::string& s)
	{
		if (validUtf8(s)) return s;
		std::string out;
		for (int which = 0; which < 3; which++)
			if (tryTable(which, s, out)) return out;
		out.clear();
		for (unsigned char c : s)
		{
			if (c < 0x80) out += (char)c;
			else
			{
				out += (char)(0xC0 | (c >> 6));
				out += (char)(0x80 | (c & 0x3F));
			}
		}
		return out;
	}
}
