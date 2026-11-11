#include "comicinfo.h"
#include <map>
#include <vector>

namespace bp
{
	namespace
	{
		// a ComicInfo is a short file: the cap keeps a huge download from being scanned, and the depth limit keeps the
		// open-element stack small (a file nested this deep isn't one we read)
		const size_t kMaxInput = 1 << 20;
		const size_t kMaxDepth = 256;

		// the direct children of ComicInfo we keep; any other element is skipped with its contents
		const char* const kFields[] = {"Series", "Title", "Number", "Volume", "Summary", "Writer", "Web", "PageCount",
		                               "Manga", "BPSource", "BPSeriesKey"};

		bool isSpace(char c)
		{
			return c == ' ' || c == '\t' || c == '\r' || c == '\n';
		}

		std::string trim(const std::string& s)
		{
			size_t a = 0, b = s.size();
			while (a < b && isSpace(s[a])) a++;
			while (b > a && isSpace(s[b - 1])) b--;
			return s.substr(a, b - a);
		}

		void appendUtf8(std::string& out, unsigned cp)
		{
			if (cp < 0x80)
				out += (char)cp;
			else if (cp < 0x800)
			{
				out += (char)(0xC0 | (cp >> 6));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000)
			{
				out += (char)(0xE0 | (cp >> 12));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else
			{
				out += (char)(0xF0 | (cp >> 18));
				out += (char)(0x80 | ((cp >> 12) & 0x3F));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
		}

		// the reference starting at s[i] ('&'): appends what it stands for and returns the characters it used. 0 when
		// it isn't one (a lone "AT&T"), so the '&' stays as written
		size_t entityAt(const std::string& s, size_t i, std::string& out)
		{
			size_t end = i + 1;
			while (end < s.size() && s[end] != ';' && end - i <= 12) end++;
			if (end >= s.size() || s[end] != ';') return 0;
			std::string name = s.substr(i + 1, end - i - 1);
			size_t used = end - i + 1;
			if (name == "amp") out += '&';
			else if (name == "lt") out += '<';
			else if (name == "gt") out += '>';
			else if (name == "quot") out += '"';
			else if (name == "apos") out += '\'';
			else if (!name.empty() && name[0] == '#')
			{
				bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
				unsigned long cp = 0;
				bool any = false;
				for (size_t k = hex ? 2 : 1; k < name.size(); k++)
				{
					char c = name[k];
					unsigned long v;
					if (c >= '0' && c <= '9') v = c - '0';
					else if (hex && c >= 'a' && c <= 'f') v = c - 'a' + 10;
					else if (hex && c >= 'A' && c <= 'F') v = c - 'A' + 10;
					else return 0;
					any = true;
					if (cp <= 0x10FFFF) cp = cp * (hex ? 16 : 10) + v;
				}
				if (!any) return 0;
				// not a character XML can carry: dropped, as the reference was never text
				if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return used;
				appendUtf8(out, (unsigned)cp);
			}
			else
				return 0;
			return used;
		}

		void decodeInto(const std::string& s, std::string& out)
		{
			for (size_t i = 0; i < s.size();)
			{
				if (s[i] == '&')
				{
					size_t used = entityAt(s, i, out);
					if (used)
					{
						i += used;
						continue;
					}
				}
				out += s[i++];
			}
		}

		// a whole number of nine digits or fewer; anything else ("two", "1.5", "") is not set
		int wholeNumber(const std::string& s)
		{
			if (s.empty() || s.size() > 9) return -1;
			int v = 0;
			for (char c : s)
			{
				if (c < '0' || c > '9') return -1;
				v = v * 10 + (c - '0');
			}
			return v;
		}

		// the value as text, escaped. Control characters other than tab and newline are dropped first, since XML 1.0
		// can't carry them; the trim comes last so it sees what is written
		std::string escaped(const std::string& v)
		{
			std::string t;
			for (char c : v)
			{
				if ((unsigned char)c < 0x20 && c != '\t' && c != '\n') continue;
				switch (c)
				{
				case '&': t += "&amp;"; break;
				case '<': t += "&lt;"; break;
				case '>': t += "&gt;"; break;
				case '"': t += "&quot;"; break;
				default: t += c;
				}
			}
			return trim(t);
		}
	}

	bool parseComicInfo(const std::string& xml, ComicInfo& out)
	{
		if (xml.size() > kMaxInput) return false;

		std::map<std::string, std::string> f;
		for (const char* name : kFields) f[name];

		std::vector<std::string> open;	// the open elements, outermost first
		std::string* cur = nullptr;	// the text of a direct child of ComicInfo that we keep, while it's open
		size_t infoDepth = 0;	// open.size() with ComicInfo open; 0 until it's found
		bool closed = false;
		size_t n = xml.size(), p = 0;
		while (!closed)
		{
			size_t lt = xml.find('<', p);
			if (lt == std::string::npos) return false;
			if (cur && open.size() == infoDepth + 1) decodeInto(xml.substr(p, lt - p), *cur);
			p = lt;

			if (xml.compare(p, 4, "<!--") == 0)
			{
				size_t e = xml.find("-->", p + 4);
				if (e == std::string::npos) return false;
				p = e + 3;
			}
			else if (xml.compare(p, 9, "<![CDATA[") == 0)
			{
				// CDATA is literal: its text is not entity-decoded
				size_t e = xml.find("]]>", p + 9);
				if (e == std::string::npos) return false;
				if (cur && open.size() == infoDepth + 1) cur->append(xml, p + 9, e - p - 9);
				p = e + 3;
			}
			else if (xml.compare(p, 2, "<?") == 0)
			{
				size_t e = xml.find("?>", p + 2);
				if (e == std::string::npos) return false;
				p = e + 2;
			}
			else if (xml.compare(p, 2, "<!") == 0)
			{
				size_t e = xml.find('>', p + 2);
				if (e == std::string::npos) return false;
				p = e + 1;
			}
			else if (xml.compare(p, 2, "</") == 0)
			{
				size_t e = xml.find('>', p + 2);
				if (e == std::string::npos) return false;
				std::string name = trim(xml.substr(p + 2, e - p - 2));
				if (open.empty() || open.back() != name) return false;
				open.pop_back();
				if (open.size() == infoDepth) cur = nullptr;
				if (open.size() + 1 == infoDepth) closed = true;
				p = e + 1;
			}
			else
			{
				size_t q = p + 1;
				while (q < n && !isSpace(xml[q]) && xml[q] != '/' && xml[q] != '>' && xml[q] != '<' && xml[q] != '=') q++;
				std::string name = xml.substr(p + 1, q - p - 1);
				if (name.empty()) return false;	// a lone "<", as in "a < b"

				bool empty = false;
				for (;;)
				{
					while (q < n && isSpace(xml[q])) q++;
					if (q >= n) return false;
					if (xml[q] == '>')
					{
						q++;
						break;
					}
					if (xml[q] == '/')
					{
						if (q + 1 >= n || xml[q + 1] != '>') return false;
						empty = true;
						q += 2;
						break;
					}
					size_t a = q;
					while (q < n && !isSpace(xml[q]) && xml[q] != '=' && xml[q] != '>' && xml[q] != '/' && xml[q] != '<') q++;
					if (q == a) return false;
					while (q < n && isSpace(xml[q])) q++;
					if (q >= n || xml[q] != '=') return false;
					q++;
					while (q < n && isSpace(xml[q])) q++;
					if (q >= n || (xml[q] != '"' && xml[q] != '\'')) return false;
					size_t close = xml.find(xml[q], q + 1);
					if (close == std::string::npos) return false;
					q = close + 1;
				}

				if (infoDepth && open.size() == infoDepth)
				{
					auto it = f.find(name);
					if (it != f.end())
					{
						it->second.clear();
						if (!empty) cur = &it->second;
					}
				}
				if (!empty)
				{
					if (open.size() >= kMaxDepth) return false;
					open.push_back(name);
				}
				if (!infoDepth && name == "ComicInfo")
				{
					infoDepth = open.size();
					if (empty) closed = true;
				}
				p = q;
			}
		}

		auto get = [&f](const char* key) { return trim(f[key]); };
		ComicInfo r;
		r.series = get("Series");
		r.title = get("Title");
		r.number = get("Number");
		r.volume = wholeNumber(get("Volume"));
		r.summary = get("Summary");
		r.writer = get("Writer");
		r.web = get("Web");
		r.pageCount = wholeNumber(get("PageCount"));
		r.rightToLeft = get("Manga") == "YesAndRightToLeft";
		r.sourceId = get("BPSource");
		r.seriesKey = get("BPSeriesKey");
		out = r;
		return true;
	}

	std::string writeComicInfo(const ComicInfo& in)
	{
		std::string o = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		                "<ComicInfo xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
		                "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\">\n";
		auto text = [&o](const char* tag, const std::string& v) {
			std::string t = escaped(v);
			if (!t.empty()) o += std::string("  <") + tag + ">" + t + "</" + tag + ">\n";
		};
		auto number = [&o](const char* tag, int v) {
			if (v >= 0) o += std::string("  <") + tag + ">" + std::to_string(v) + "</" + tag + ">\n";
		};
		text("Series", in.series);
		text("Title", in.title);
		text("Number", in.number);
		number("Volume", in.volume);
		text("Summary", in.summary);
		text("Writer", in.writer);
		text("Web", in.web);
		number("PageCount", in.pageCount);
		if (in.rightToLeft) o += "  <Manga>YesAndRightToLeft</Manga>\n";
		text("BPSource", in.sourceId);
		text("BPSeriesKey", in.seriesKey);
		o += "</ComicInfo>\n";
		return o;
	}
}
