#include "opds.h"
#include <algorithm>
#include <set>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace bp
{
	namespace
	{
		// Kavita marks each entry's reading state at the start of its title (U+2B58 unread, U+25D1 started, U+2B24 read);
		// the pixel font has none of them and drew a box. A geometric-shapes mark (U+25A0-U+25FF, U+2B24, U+2B58) and the
		// spaces after it go when text follows; a title that is only marks stays as it is
		std::string withoutStateMark(const std::string& t)
		{
			size_t at = 0;
			bool any = false;
			while (at + 3 <= t.size())
			{
				unsigned char a = t[at], b = t[at + 1], c = t[at + 2];
				bool shape = a == 0xE2 && ((b == 0x96 && c >= 0xA0) || b == 0x97);	// U+25A0-U+25FF
				bool other = a == 0xE2 && ((b == 0xAC && c == 0xA4) || (b == 0xAD && c == 0x98));	// U+2B24, U+2B58
				if (!shape && !other) break;
				at += 3;
				any = true;
				while (at < t.size() && t[at] == ' ') at++;
			}
			return any && at < t.size() ? t.substr(at) : t;
		}

		// The same bounds the manga sources keep everywhere else: 8 MB is more than any real feed or list answer, and
		// the entry cap keeps a broken or hostile server from filling the handheld's memory on one response.
		const size_t kMaxInput = 8u << 20;
		const size_t kMaxEntries = 10000;
		// a feed nested deeper than this isn't one we read; the same limit bounds the JSON reader's recursion, so a
		// hostile "[[[[…" meets a false instead of a stack overflow
		const size_t kMaxDepth = 128;
		const size_t kMaxJsonDepth = 64;

		const char* const kAtomNs = "http://www.w3.org/2005/Atom";
		const char* const kThrNs = "http://purl.org/syndicate/threading";
		const char* const kPseNs = "http://vaemendis.net/opds-pse/ns";

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

		// comicinfo.cpp scans a text file the same tolerant way, but its helpers are private to that file, so the
		// entity and UTF-8 code is kept here too rather than editing comicinfo for a shared header

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

		// rels and types are URIs the specs write lower case; a server that doesn't is still the server the user
		// owns, so compare without case rather than lose their covers
		bool iequals(const std::string& s, const char* want)
		{
			size_t n = std::strlen(want);
			if (s.size() != n) return false;
			for (size_t i = 0; i < n; i++)
				if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)want[i])) return false;
			return true;
		}

		bool istarts(const std::string& s, const char* want)
		{
			size_t n = std::strlen(want);
			if (s.size() < n) return false;
			for (size_t i = 0; i < n; i++)
				if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)want[i])) return false;
			return true;
		}

		// feeds pretty-print and blurbs wrap: runs of whitespace become one space
		std::string oneLine(const std::string& s)
		{
			std::string o;
			bool space = false;
			for (char c : s)
			{
				if (isSpace(c))
				{
					space = !o.empty();
					continue;
				}
				if (space)
				{
					o += ' ';
					space = false;
				}
				o += c;
			}
			return o;
		}

		// an html summary is markup that survived its escaping: the tags carry no text we want. Only a '<' that
		// opens something tag-shaped is a tag, so "a < b" and "<3" stay words
		std::string stripTags(const std::string& s)
		{
			std::string o;
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] == '<' && i + 1 < s.size()
				    && (std::isalpha((unsigned char)s[i + 1]) || s[i + 1] == '/' || s[i + 1] == '!'))
				{
					size_t gt = s.find('>', i);
					if (gt == std::string::npos) return o;
					i = gt;
					continue;
				}
				o += s[i];
			}
			return o;
		}

		// a whole number; anything else ("1.5", "", a huge run) is not set
		int wholeCount(const std::string& s)
		{
			if (s.empty() || s.size() > 9) return -1;
			size_t a = (s[0] == '+') ? 1 : 0;
			if (a >= s.size()) return -1;
			int v = 0;
			for (size_t i = a; i < s.size(); i++)
			{
				if (s[i] < '0' || s[i] > '9') return -1;
				v = v * 10 + (s[i] - '0');
			}
			return v;
		}

		// ---- the Atom scanner

		struct Attr
		{
			std::string name;	// as written, prefix included
			std::string value;	// entity-decoded
		};

		// reads the start tag at xml[p] (on the '<'); p ends past its '>'. false when malformed or truncated
		bool readOpen(const std::string& xml, size_t n, size_t& p, std::string& name, std::vector<Attr>& attrs,
		              bool& empty)
		{
			name.clear();
			attrs.clear();
			size_t q = p + 1;
			while (q < n && !isSpace(xml[q]) && xml[q] != '/' && xml[q] != '>' && xml[q] != '<' && xml[q] != '=') q++;
			if (q == p + 1) return false;	// a lone "<", as in "a < b"
			name = xml.substr(p + 1, q - p - 1);
			empty = false;
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
				std::string key = xml.substr(a, q - a);
				while (q < n && isSpace(xml[q])) q++;
				if (q >= n || xml[q] != '=') return false;
				q++;
				while (q < n && isSpace(xml[q])) q++;
				if (q >= n || (xml[q] != '"' && xml[q] != '\'')) return false;
				size_t close = xml.find(xml[q], q + 1);
				if (close == std::string::npos) return false;
				Attr at;
				at.name = key;
				decodeInto(xml.substr(q + 1, close - q - 1), at.value);
				attrs.push_back(std::move(at));
				q = close + 1;
			}
			p = q;
			return true;
		}

		std::string localOf(const std::string& name, std::string& prefix)
		{
			size_t c = name.find(':');
			if (c == std::string::npos)
			{
				prefix.clear();
				return name;
			}
			prefix = name.substr(0, c);
			return name.substr(c + 1);
		}

		enum Role
		{
			R_NONE = 0, R_FEED, R_ENTRY, R_TEXT, R_AUTHOR, R_NAME, R_SUMMARY, R_CONTENT
		};

		struct Frame
		{
			std::string name;	// as written, so a close tag can match it
			std::string local;
			int role = R_NONE;
			bool collect = false;	// character data inside comes to me
			bool html = false;	// my text is markup once decoded: strip it
			size_t scope = 0;	// namespace stack to go back to when I close
			std::string text;
		};

		// the innermost open element that takes text; unknown children (xhtml inside a content) still route theirs
		// here, which is the text a reader wants once its tags are stripped
		Frame* sinkTo(std::vector<Frame>& st)
		{
			for (size_t i = st.size(); i-- > 0;)
				if (st[i].collect) return &st[i];
			return nullptr;
		}

		// ---- the JSON reader for Komga. json.h only writes, and Komga answers with far more than we read, so a
		// small reader lives here too: every member is decoded (unknown ones then ignored), and a malformed answer
		// is false. Depth is capped so deep nesting can't blow the stack.

		struct JVal
		{
			enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
			bool boolean = false;
			std::string s;	// the decoded text of a string, the raw token of a number
			double num = 0;
			std::vector<std::pair<std::string, JVal>> members;
			std::vector<JVal> elems;

			const JVal* find(const std::string& key) const
			{
				for (const auto& m : members)
					if (m.first == key) return &m.second;
				return nullptr;
			}
		};

		void jWs(const std::string& s, size_t& p)
		{
			while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++;
		}

		bool jHex4(const std::string& s, size_t p, unsigned& cp)
		{
			if (p + 4 > s.size()) return false;
			cp = 0;
			for (size_t i = 0; i < 4; i++)
			{
				char c = s[p + i];
				unsigned v;
				if (c >= '0' && c <= '9') v = c - '0';
				else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
				else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
				else return false;
				cp = cp * 16 + v;
			}
			return true;
		}

		bool jString(const std::string& s, size_t& p, std::string& out)
		{
			if (p >= s.size() || s[p] != '"') return false;
			p++;
			out.clear();
			while (p < s.size())
			{
				char c = s[p++];
				if (c == '"') return true;
				if (c != '\\')
				{
					out += c;
					continue;
				}
				if (p >= s.size()) return false;
				char e = s[p++];
				switch (e)
				{
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u':
				{
					unsigned cp;
					if (!jHex4(s, p, cp)) return false;
					p += 4;
					if (cp >= 0xD800 && cp <= 0xDBFF)
					{
						// a high surrogate is only a character once its low half follows
						if (p + 6 > s.size() || s[p] != '\\' || s[p + 1] != 'u') return false;
						unsigned lo;
						if (!jHex4(s, p + 2, lo)) return false;
						if (lo < 0xDC00 || lo > 0xDFFF) return false;
						p += 6;
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					}
					else if (cp >= 0xD800 && cp <= 0xDFFF) return false;
					appendUtf8(out, cp);
					break;
				}
				default: return false;
				}
			}
			return false;
		}

		bool jValue(const std::string& s, size_t& p, size_t depth, JVal& v)
		{
			if (depth > kMaxJsonDepth) return false;
			jWs(s, p);
			if (p >= s.size()) return false;
			char c = s[p];
			if (c == '{' || c == '[')
			{
				bool obj = c == '{';
				p++;
				for (;;)
				{
					jWs(s, p);
					if (p >= s.size()) return false;
					if (s[p] == (obj ? '}' : ']'))
					{
						p++;
						v.kind = obj ? JVal::OBJ : JVal::ARR;
						return true;
					}
					std::string key;
					if (obj)
					{
						if (!jString(s, p, key)) return false;
						jWs(s, p);
						if (p >= s.size() || s[p] != ':') return false;
						p++;
					}
					JVal m;
					if (!jValue(s, p, depth + 1, m)) return false;
					if (obj) v.members.emplace_back(std::move(key), std::move(m));
					else v.elems.push_back(std::move(m));
					jWs(s, p);
					if (p >= s.size()) return false;
					if (s[p] == ',')
					{
						p++;
						continue;
					}
					if (s[p] == (obj ? '}' : ']'))
					{
						p++;
						v.kind = obj ? JVal::OBJ : JVal::ARR;
						return true;
					}
					return false;
				}
			}
			if (c == '"')
			{
				v.kind = JVal::STR;
				return jString(s, p, v.s);
			}
			if (s.compare(p, 4, "true") == 0)
			{
				v.kind = JVal::BOOL;
				v.boolean = true;
				p += 4;
				return true;
			}
			if (s.compare(p, 5, "false") == 0)
			{
				v.kind = JVal::BOOL;
				p += 5;
				return true;
			}
			if (s.compare(p, 4, "null") == 0)
			{
				v.kind = JVal::NUL;
				p += 4;
				return true;
			}
			size_t a = p;
			while (p < s.size() && (std::isdigit((unsigned char)s[p]) || s[p] == '-' || s[p] == '+' || s[p] == '.'
			                        || s[p] == 'e' || s[p] == 'E')) p++;
			std::string tok = s.substr(a, p - a);
			if (tok.empty()) return false;
			char* end = nullptr;
			double d = std::strtod(tok.c_str(), &end);
			if (end != tok.c_str() + tok.size()) return false;
			v.kind = JVal::NUM;
			v.s = tok;
			v.num = d;
			return true;
		}
	}

	std::string OpdsEntry::cover() const
	{
		for (const OpdsLink& l : links)
			if (iequals(l.rel, "http://opds-spec.org/image")) return l.href;
		return "";
	}

	std::string OpdsEntry::thumbnail() const
	{
		for (const OpdsLink& l : links)
			if (iequals(l.rel, "http://opds-spec.org/image/thumbnail")) return l.href;
		return "";
	}

	std::string OpdsEntry::acquisition() const
	{
		// a chapter on a shelf is a zip: take the best comicbook type offered, not the first acquisition link, which
		// on some servers is a pdf the handheld can't open
		int best = -1;
		std::string href;
		for (const OpdsLink& l : links)
		{
			if (!istarts(l.rel, "http://opds-spec.org/acquisition")) continue;
			int rank = 3;
			if (istarts(l.type, "application/vnd.comicbook+zip")) rank = 0;
			else if (istarts(l.type, "application/zip")) rank = 1;
			else if (istarts(l.type, "application/x-cbz")) rank = 2;
			if (best < 0 || rank < best)
			{
				best = rank;
				href = l.href;
			}
		}
		return href;
	}

	std::string OpdsEntry::pse() const
	{
		for (const OpdsLink& l : links)
			if (iequals(l.rel, "http://vaemendis.net/opds-pse/stream")) return l.href;
		return "";
	}

	std::string OpdsEntry::nav() const
	{
		// the link that opens the next catalogue level, whatever rel the server hung on it: a catalogue is always
		// atom+xml, the acquisition links are the images and zips
		for (const OpdsLink& l : links)
			if (istarts(l.type, "application/atom+xml")) return l.href;
		return "";
	}

	bool parseOpds(const std::string& xml, OpdsFeed& out)
	{
		if (xml.size() > kMaxInput) return false;

		OpdsFeed feed;
		std::vector<OpdsLink> feedLinks;	// the feed's own links: its paging comes out of these at the end
		std::vector<Frame> st;
		std::vector<std::pair<std::string, std::string>> ns;	// prefix to URI, innermost scope last
		OpdsEntry* cur = nullptr;	// the entry being filled; null outside an entry (and past the cap)
		bool done = false;
		size_t p = 0, n = xml.size();
		while (!done && p < n)
		{
			size_t lt = xml.find('<', p);
			if (lt == std::string::npos) lt = n;
			Frame* sink = sinkTo(st);
			if (lt > p && sink) decodeInto(xml.substr(p, lt - p), sink->text);
			if (lt == n) break;	// the tail was plain text
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
				if (sink) sink->text.append(xml, p + 9, e - p - 9);
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
				if (st.empty() || st.back().name != name) return false;
				Frame f = std::move(st.back());
				st.pop_back();
				ns.resize(f.scope);
				int prole = st.empty() ? R_NONE : st.back().role;
				std::string v;
				switch (f.role)
				{
				case R_TEXT:
					v = trim(f.text);
					if (prole == R_FEED)
					{
						if (f.local == "id") feed.id = v;
						else if (f.local == "title") feed.title = v;
					}
					else if (prole == R_ENTRY && cur)
					{
						if (f.local == "id" && cur->id.empty()) cur->id = v;
						else if (f.local == "title" && cur->title.empty()) cur->title = withoutStateMark(v);
						else if (f.local == "updated" && cur->updated.empty()) cur->updated = v;
					}
					break;
				case R_NAME:
					// the first author name in the entry is the one shown under the title
					if (cur && cur->author.empty())
					{
						v = trim(f.text);
						if (!v.empty()) cur->author = v;
					}
					break;
				case R_SUMMARY:
					if (cur && cur->summary.empty())
					{
						v = oneLine(stripTags(f.text));
						if (!v.empty()) cur->summary = v;
					}
					break;
				case R_CONTENT:
					if (cur && cur->summary.empty())
					{
						v = f.html ? oneLine(stripTags(f.text)) : trim(f.text);
						if (!v.empty()) cur->summary = v;
					}
					break;
				case R_ENTRY:
					cur = nullptr;
					break;
				case R_FEED:
					done = true;	// nothing after the feed's closing tag is our business
					break;
				default:
					break;
				}
				p = e + 1;
			}
			else
			{
				std::string name;
				std::vector<Attr> attrs;
				bool emptyTag = false;
				if (!readOpen(xml, n, p, name, attrs, emptyTag)) return false;

				// an element's own xmlns declarations come before its name resolves: they name its namespace too
				size_t scope = ns.size();
				for (const Attr& a : attrs)
				{
					if (a.name == "xmlns") ns.emplace_back("", a.value);
					else if (a.name.compare(0, 6, "xmlns:") == 0) ns.emplace_back(a.name.substr(6), a.value);
				}

				std::string prefix;
				std::string local = localOf(name, prefix);
				std::string nsUri;
				for (size_t i = ns.size(); i-- > 0;)
					if (ns[i].first == prefix)
					{
						nsUri = ns[i].second;
						break;
					}
				// a child written without a prefix when the parent declared a default namespace carries that one,
				// which is how every real feed writes itself; children of a feed that didn't carry none
				bool atom = nsUri == kAtomNs || (prefix.empty() && nsUri.empty());

				int role = R_NONE;
				if (st.empty())
				{
					// the root decides whether this is an Atom feed at all; anything else isn't ours to read.
					// A feed written without any namespace declaration is sloppy but clearly meant as one; one in a
					// different namespace is somebody else's document.
					if (local == "feed" && (nsUri == kAtomNs || nsUri.empty())) role = R_FEED;
					else
					{
						ns.resize(scope);
						return false;
					}
				}
				else
				{
					int parRole = st.back().role;
					if (parRole == R_FEED && atom)
					{
						if (local == "id" || local == "title") role = R_TEXT;
						else if (local == "entry") role = R_ENTRY;
					}
					else if (parRole == R_ENTRY && atom)
					{
						if (local == "id" || local == "title" || local == "updated") role = R_TEXT;
						else if (local == "summary") role = R_SUMMARY;
						else if (local == "content") role = R_CONTENT;
						else if (local == "author") role = R_AUTHOR;
					}
					else if (parRole == R_AUTHOR && atom && local == "name") role = R_NAME;
				}

				if (role == R_NONE && atom && local == "link" && !st.empty())
				{
					// a link is complete on opening, self-closing or not: everything it carries is attributes
					int parRole = st.back().role;
					if (parRole == R_FEED || parRole == R_ENTRY)
					{
						auto attrValue = [&attrs](const char* key) -> std::string
						{
							for (const Attr& a : attrs)
								if (a.name == key) return a.value;
							return "";
						};
						OpdsLink l;
						l.rel = attrValue("rel");
						l.type = attrValue("type");
						l.href = attrValue("href");
						l.title = attrValue("title");
						for (const Attr& a : attrs)
						{
							std::string ap;
							if (localOf(a.name, ap) != "count") continue;
							std::string ac;
							for (size_t i = ns.size(); i-- > 0;)
								if (ns[i].first == ap)
								{
									ac = ns[i].second;
									break;
								}
							// thr:count and pse:count say the same thing; a bare count from a sloppy server too
							if (ap.empty() || ac == kThrNs || ac == kPseNs) l.count = wholeCount(a.value);
						}
						if (parRole == R_FEED) feedLinks.push_back(std::move(l));
						else if (cur && cur->links.size() < kMaxEntries) cur->links.push_back(std::move(l));
					}
				}
				else if (role == R_ENTRY)
					// the cap keeps a runaway server bounded; a feed past it is still a real feed, just shorter
					cur = feed.entries.size() < kMaxEntries ? &feed.entries.emplace_back() : nullptr;

				if (!emptyTag)
				{
					if (st.size() >= kMaxDepth)
					{
						ns.resize(scope);
						return false;
					}
					Frame f;
					f.name = name;
					f.local = local;
					f.role = role;
					f.collect = role == R_TEXT || role == R_NAME || role == R_SUMMARY || role == R_CONTENT;
					if (role == R_CONTENT)
					{
						for (const Attr& a : attrs)
							if (a.name == "type" && (iequals(a.value, "html") || iequals(a.value, "xhtml"))) f.html = true;
					}
					f.scope = scope;
					st.push_back(std::move(f));
				}
				else
				{
					ns.resize(scope);
					// <entry/> is a valid empty entry: it was added, and no more is collected for it
					if (role == R_ENTRY) cur = nullptr;
					else if (role == R_FEED) done = true;	// <feed/>: a valid empty feed
				}
			}
		}

		if (!done) return false;	// a feed that never closed was truncated, or there was never one
		for (const OpdsLink& l : feedLinks)
		{
			if (feed.next.empty() && iequals(l.rel, "next")) feed.next = l.href;
			if (feed.search.empty() && iequals(l.rel, "search")) feed.search = l.href;
			if (feed.start.empty() && iequals(l.rel, "start")) feed.start = l.href;
			if (feed.up.empty() && iequals(l.rel, "up")) feed.up = l.href;
		}
		// an entry id given twice is the same entry: Kavita puts a "Continue Reading from: …" copy of the chapter
		// being read before the chapters, under that chapter's own id. The last of each id stays, in its place
		{
			std::set<std::string> later;
			std::vector<OpdsEntry> kept;
			for (size_t i = feed.entries.size(); i-- > 0;)
			{
				const std::string& id = feed.entries[i].id;
				if (!id.empty() && !later.insert(id).second) continue;
				kept.push_back(std::move(feed.entries[i]));
			}
			std::reverse(kept.begin(), kept.end());
			feed.entries = std::move(kept);
		}
		out = std::move(feed);
		return true;
	}

	namespace
	{
		// the path part of a URL: from its first '/' after the authority, without the query or fragment; a URL that
		// has no path ("http://host" or "http://host?a=1") stands for "/"
		std::string urlPath(const std::string& url, size_t from)
		{
			size_t pa = url.find('/', from);
			size_t q = url.find_first_of("?#", from);
			if (pa == std::string::npos || (q != std::string::npos && q < pa)) return "/";
			std::string path = url.substr(pa);
			size_t cut = path.find_first_of("?#");
			if (cut != std::string::npos) path.resize(cut);
			return path;
		}

		// "a/../b" is "b" and "./x" is "x": catalogues are walked with relative links, and the dots must not stay in
		// the next request. ".." past the root is dropped, as a browser drops it.
		std::string cleanPath(const std::string& in)
		{
			std::vector<std::string> out;
			bool trailing = in.size() > 1 && in[in.size() - 1] == '/';
			size_t i = 0;
			while (i < in.size())
			{
				size_t j = in.find('/', i);
				if (j == std::string::npos) j = in.size();
				std::string seg = in.substr(i, j - i);
				if (seg == "..")
				{
					if (!out.empty()) out.pop_back();
				}
				else if (!seg.empty() && seg != ".")
					out.push_back(seg);
				i = j + 1;
			}
			std::string r;
			for (const std::string& s : out) r += "/" + s;
			if (r.empty()) r = "/";
			else if (trailing) r += "/";
			return r;
		}
	}

	std::string opdsResolve(const std::string& base, const std::string& href)
	{
		if (href.empty()) return base;

		// an href that already carries a scheme is the whole answer
		size_t colon = href.find(':'), sep = href.find_first_of("/?#");
		if (colon != std::string::npos && (sep == std::string::npos || colon < sep)) return href;

		size_t sc = base.find("://");
		if (sc == std::string::npos) return href;	// nothing to hang it on
		size_t pa = base.find_first_of("/?#", sc + 3);
		std::string origin = pa == std::string::npos ? base : base.substr(0, pa);
		std::string path = urlPath(base, sc + 3);

		// an href's tail from its first '?' or '#' joins untouched
		size_t hq = href.find_first_of("?#");
		std::string head = hq == std::string::npos ? href : href.substr(0, hq);
		std::string tail = hq == std::string::npos ? "" : href.substr(hq);

		// the href's own "//" is the separator the scheme already ended with, so rejoin at the colon
		if (head.compare(0, 2, "//") == 0) return base.substr(0, sc) + ":" + head + tail;
		if (!head.empty() && head[0] == '/') return origin + cleanPath(head) + tail;
		if (head.empty()) return origin + path + tail;	// "?a=1": the same page, another query
		std::string dir = path.substr(0, path.rfind('/') + 1);
		return origin + cleanPath(dir + head) + tail;
	}

	namespace
	{
		void subAll(std::string& s, const std::string& from, const std::string& to)
		{
			if (from.empty()) return;
			size_t p = 0;
			while ((p = s.find(from, p)) != std::string::npos)
			{
				s.replace(p, from.size(), to);
				p += to.size();
			}
		}
	}

	std::string opdsPseUrl(const std::string& templ, int page, int maxWidth)
	{
		// templates are written with literal braces or with them percent-encoded, and servers do both
		std::string o = templ;
		const std::string pg = std::to_string(page), mw = std::to_string(maxWidth);
		subAll(o, "{pageNumber}", pg);
		subAll(o, "%7BpageNumber%7D", pg);
		subAll(o, "%7bpageNumber%7d", pg);
		subAll(o, "{maxWidth}", mw);
		subAll(o, "%7BmaxWidth%7D", mw);
		subAll(o, "%7bmaxWidth%7d", mw);
		return o;
	}

	bool parseKomgaPage(const std::string& json, std::vector<KomgaItem>& items, int& totalPages)
	{
		if (json.size() > kMaxInput) return false;
		JVal root;
		size_t p = 0;
		if (!jValue(json, p, 0, root)) return false;
		jWs(json, p);
		if (p != json.size()) return false;	// trailing text: not one answer we got whole
		if (root.kind != JVal::OBJ) return false;
		const JVal* content = root.find("content");
		if (!content || content->kind != JVal::ARR) return false;	// not a paged list

		const JVal* tp = root.find("totalPages");
		totalPages = tp && tp->kind == JVal::NUM && tp->num >= 0 ? (int)tp->num : 0;

		for (const JVal& el : content->elems)
		{
			if (el.kind != JVal::OBJ) continue;	// an element we can't read is skipped, not fatal
			if (items.size() >= kMaxEntries) break;	// a runaway list stays bounded, the page stays usable
			KomgaItem it;
			const JVal* v = el.find("id");
			if (v && v->kind == JVal::STR) it.id = v->s;
			v = el.find("name");
			if (v && v->kind == JVal::STR) it.name = v->s;
			const JVal* md = el.find("metadata");
			if (md && md->kind == JVal::OBJ)
			{
				// a series answer names itself on the item, but a nameless one shows its metadata title
				if (it.name.empty())
				{
					const JVal* t = md->find("title");
					if (t && t->kind == JVal::STR) it.name = t->s;
				}
				const JVal* num = md->find("number");
				if (num && (num->kind == JVal::STR || num->kind == JVal::NUM)) it.number = num->s;
				const JVal* sort = md->find("numberSort");
				if (sort && sort->kind == JVal::NUM) it.numberSort = sort->num;
			}
			// series carry their number and order at the top of the item, books only inside metadata
			if (it.number.empty())
			{
				const JVal* num = el.find("number");
				if (num && num->kind == JVal::NUM) it.number = num->s;
			}
			if (it.numberSort == -1)
			{
				const JVal* sort = el.find("numberSort");
				if (sort && sort->kind == JVal::NUM) it.numberSort = sort->num;
			}
			// media is an object in older Komga and an array of files in newer ones; the first carries the pages
			const JVal* media = el.find("media");
			const JVal* m = nullptr;
			if (media && media->kind == JVal::OBJ) m = media;
			else if (media && media->kind == JVal::ARR && !media->elems.empty() && media->elems[0].kind == JVal::OBJ)
				m = &media->elems[0];
			if (m)
			{
				const JVal* pc = m->find("pagesCount");
				if (!pc) pc = m->find("pages");
				if (pc && pc->kind == JVal::NUM) it.pages = (int)pc->num;
			}
			items.push_back(std::move(it));
		}
		return true;
	}

	std::string komgaUrl(const std::string& base, const std::string& path)
	{
		// servers are saved with or without a trailing slash and paths with or without a leading one: join with one
		if (base.empty()) return path;
		if (path.compare(0, 7, "http://") == 0 || path.compare(0, 8, "https://") == 0) return path;
		std::string b = base;
		while (!b.empty() && b.back() == '/') b.pop_back();
		if (path.empty()) return b;
		if (path.front() == '/') return b + path;
		return b + "/" + path;
	}
}
