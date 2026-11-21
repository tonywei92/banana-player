#include "tagtext.h"
#include <cerrno>
#include <cstdint>
#include <iconv.h>
#include <vector>

namespace bp
{
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

		bool convert(iconv_t cd, const std::string& in, std::string& out)
		{
			iconv(cd, nullptr, nullptr, nullptr, nullptr);
			std::vector<char> buf(in.size() * 4 + 8);
			char* ip = const_cast<char*>(in.data());
			size_t il = in.size();
			char* op = buf.data();
			size_t ol = buf.size();
			if (iconv(cd, &ip, &il, &op, &ol) == (size_t)-1 || il != 0) return false;
			out.assign(buf.data(), op - buf.data());
			return true;
		}

		// the bytes read as `charset`, accepted only when they come back identical and read as CJK text
		bool tryCharset(const char* charset, const std::string& s, std::string& out)
		{
			iconv_t dec = iconv_open("UTF-8", charset);
			if (dec == (iconv_t)-1) return false;
			iconv_t enc = iconv_open(charset, "UTF-8");
			bool ok = false;
			std::string u, back;
			if (enc != (iconv_t)-1 && convert(dec, s, u) && validUtf8(u) && convert(enc, u, back) && back == s)
			{
				int wide = 0, hit = 0;
				for (size_t i = 0; i < u.size();)
				{
					long cp = next(u, i);
					if (cp < 0x80) continue;
					wide++;
					if (cjk(cp)) hit++;
				}
				ok = wide > 0 && hit * 10 >= wide * 8;
			}
			if (enc != (iconv_t)-1) iconv_close(enc);
			iconv_close(dec);
			if (ok) out = u;
			return ok;
		}
	}

	std::string cleanTagText(const std::string& s)
	{
		if (validUtf8(s)) return s;
		std::string out;
		for (const char* cs : {"CP932", "GBK", "BIG5"})
			if (tryCharset(cs, s, out)) return out;
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
