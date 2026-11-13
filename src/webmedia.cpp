#include <cctype>
#include "webmedia.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iconv.h>
#include <mutex>
#ifdef BP_UCHARDET
#include <uchardet.h>
#endif
#include <vector>

namespace bp
{
	namespace
	{
		std::string lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
			return s;
		}

		std::string extOf(const std::string& name)
		{
			size_t dot = name.rfind('.');
			return dot == std::string::npos ? std::string() : lower(name.substr(dot + 1));
		}

		struct Mime { const char* ext; const char* mime; };
		const Mime MIMES[] = {
			{"mp3", "audio/mpeg"}, {"m4a", "audio/mp4"}, {"m4b", "audio/mp4"}, {"aac", "audio/aac"}, {"flac", "audio/flac"},
			{"ogg", "audio/ogg"}, {"oga", "audio/ogg"}, {"opus", "audio/ogg"}, {"wav", "audio/wav"},
			{"wma", "audio/x-ms-wma"}, {"ape", "audio/x-ape"}, {"mka", "audio/x-matroska"},
			{"mp4", "video/mp4"}, {"m4v", "video/mp4"}, {"webm", "video/webm"}, {"mkv", "video/x-matroska"},
			{"mov", "video/quicktime"}, {"avi", "video/x-msvideo"}, {"wmv", "video/x-ms-wmv"},
			{"ts", "video/mp2t"}, {"mpg", "video/mpeg"}, {"mpeg", "video/mpeg"},
			{"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"png", "image/png"}, {"gif", "image/gif"},
			{"webp", "image/webp"}, {"bmp", "image/bmp"}, {"avif", "image/avif"}, {"tif", "image/tiff"},
			{"tiff", "image/tiff"}, {"ico", "image/x-icon"}, {"svg", "image/svg+xml"},
			{"srt", "application/x-subrip"}, {"vtt", "text/vtt; charset=utf-8"}, {"ass", "text/plain; charset=utf-8"},
			{"ssa", "text/plain; charset=utf-8"}, {"lrc", "text/plain; charset=utf-8"},
			{"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"},
			{"js", "text/javascript; charset=utf-8"}, {"mjs", "text/javascript; charset=utf-8"},
			{"css", "text/css; charset=utf-8"}, {"txt", "text/plain; charset=utf-8"},
			{"json", "application/json; charset=utf-8"}, {"ttf", "font/ttf"}, {"woff2", "font/woff2"},
			{"woff", "font/woff"},
		};

		bool in(const std::string& s, std::initializer_list<const char*> l)
		{
			for (const char* x : l)
				if (s == x) return true;
			return false;
		}

		bool hasToken(const std::string& list, const char* t)
		{
			size_t pos = 0;
			while (pos <= list.size())
			{
				size_t c = list.find(',', pos);
				if (list.compare(pos, (c == std::string::npos ? list.size() : c) - pos, t) == 0 &&
					(c == std::string::npos ? list.size() : c) - pos == strlen(t))
					return true;
				if (c == std::string::npos) break;
				pos = c + 1;
			}
			return false;
		}
	}

	const char* mimeFor(const std::string& name)
	{
		std::string e = extOf(name);
		for (const Mime& m : MIMES)
			if (e == m.ext) return m.mime;
		return "application/octet-stream";
	}

	Playable playableByName(const std::string& name)
	{
		std::string e = extOf(name);
		if (in(e, {"mp3", "aac", "m4a", "m4b", "flac", "ogg", "oga", "opus", "wav", "jpg", "jpeg", "png", "gif", "webp"}))
			return Playable::Yes;
		if (in(e, {"mp4", "m4v", "webm", "mkv", "mov", "avif", "bmp", "mka"})) return Playable::Maybe;
		return Playable::No;
	}

	Playable playability(const std::string& containerIn, const std::string& vIn, const std::string& aIn)
	{
		std::string c = lower(containerIn), v = lower(vIn), a = lower(aIn);
		if (v == "none") v.clear();
		if (a == "none") a.clear();
		if (v.empty() && a.empty()) return Playable::No;

		if (hasToken(c, "avi") || hasToken(c, "asf") || hasToken(c, "ape") || hasToken(c, "mpegts") || hasToken(c, "mpeg") ||
			hasToken(c, "mpegps") || hasToken(c, "flv") || hasToken(c, "rm") || hasToken(c, "dts") || hasToken(c, "dtshd") ||
			hasToken(c, "truehd") || hasToken(c, "mlp"))
			return Playable::No;
		// ac3/eac3 as the default sound: Chrome and Firefox have no decoder, so the picture plays silent. MPEG audio
		// layer I/II (mp1, mp2), often inside a ".mp3" (game and broadcast rips): no browser decodes it, though it says
		// it plays audio/mpeg
		if (a == "mp1" || a == "mp2" || a == "ape" || a.compare(0, 3, "wma") == 0 || a == "dts" || a == "dca" || a == "truehd" || a == "mlp" ||
			a == "ac3" || a == "eac3" || a.compare(0, 2, "rv") == 0 || a == "cook")
			return Playable::No;
		if (!v.empty() && (v == "mpeg4" || v.compare(0, 8, "msmpeg4v") == 0 || v == "mpeg2video" || v == "mpeg1video" ||
			v.compare(0, 3, "wmv") == 0 || v == "vc1" || v.compare(0, 2, "rv") == 0 || v == "flv1" || v == "h263"))
			return Playable::No;

		bool mp4 = hasToken(c, "mp4") || hasToken(c, "mov") || hasToken(c, "m4a");
		bool mkv = hasToken(c, "matroska") || hasToken(c, "webm");
		bool pcm = a.compare(0, 4, "pcm_") == 0;
		bool plainA = a == "mp3" || a == "aac" || a == "flac" || a == "vorbis" || a == "opus" || pcm;

		if (v.empty())
		{
			if (!plainA) return Playable::Maybe;	// alac and the unknown: the browser decides
			if (hasToken(c, "mp3") || hasToken(c, "aac") || hasToken(c, "flac") || hasToken(c, "ogg") || hasToken(c, "wav") || mp4)
				return Playable::Yes;
			return Playable::Maybe;	// matroska audio, raw streams
		}

		bool sureV = v == "h264" || v == "vp8" || v == "vp9" || v == "av1";
		bool sureA = a.empty() || a == "aac" || a == "opus" || a == "vorbis" || a == "mp3";
		if (!sureV || !sureA) return Playable::Maybe;	// hevc...
		if (mp4) return Playable::Yes;
		// matroska,webm is one format name for both: the webm codecs are safe, an mkv's h264 or aac may not be
		if (mkv && v != "h264" && (a.empty() || a == "opus" || a == "vorbis")) return Playable::Yes;
		return Playable::Maybe;
	}
}

namespace bp
{
	namespace
	{
		bool validUtf8(const std::string& s)
		{
			size_t i = 0, n = s.size();
			while (i < n)
			{
				unsigned char c = (unsigned char)s[i];
				int len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
				if (!len || i + len > n) return false;
				for (int k = 1; k < len; k++)
					if (((unsigned char)s[i + k] >> 6) != 2) return false;
				i += len;
			}
			return true;
		}

		// text in `charset` as UTF-8; false when iconv can't open it or meets a byte that isn't in it
		bool convertFrom(const char* charset, const std::string& in, std::string& out)
		{
			iconv_t cd = iconv_open("UTF-8", charset);
			if (cd == (iconv_t)-1) return false;
			out.clear();
			std::string buf(in.size() * 4 + 16, '\0');
			char* src = const_cast<char*>(in.data());
			size_t left = in.size();
			bool ok = true;
			while (left > 0)
			{
				char* dst = &buf[0];
				size_t room = buf.size();
				size_t r = iconv(cd, &src, &left, &dst, &room);
				out.append(buf.data(), buf.size() - room);
				if (r == (size_t)-1 && errno != E2BIG)
				{
					ok = false;
					break;
				}
			}
			iconv_close(cd);
			return ok;
		}

		std::mutex charsetLock;
		std::string chosenCharset;	// Settings › Subtitles › Text encoding ("" Auto)

		// a language's code pages, most likely first, and its family's name (to weigh a guess against); none: {}
		struct LangPages { std::vector<std::string> pages; const char* family; };
		LangPages pagesOf(const std::string& hint)
		{
			std::string h = lower(hint);
			auto is = [&](std::initializer_list<const char*> l) { for (const char* x : l) if (h == x) return true; return false; };
			if (is({"ja", "jpn"})) return {{"SHIFT_JIS", "EUC-JP"}, "ja"};
			if (is({"zh", "chi", "zho", "cn"})) return {{"GB18030", "BIG5"}, "zh"};
			if (is({"ko", "kor"})) return {{"CP949"}, "ko"};
			// Central European: Slovene, Croatian, Bosnian, Czech, Slovak, Polish, Hungarian, Romanian; Serbian's Latin
			if (is({"sl", "slv", "hr", "hrv", "scr", "bs", "bos", "cs", "ces", "cze", "sk", "slk", "slo", "pl", "pol", "hu", "hun", "ro", "ron", "rum"}))
				return {{"CP1250", "ISO-8859-2"}, "ce"};
			if (is({"ru", "rus", "uk", "ukr", "bg", "bul", "be", "bel", "mk", "mkd", "mac", "sr", "srp"})) return {{"CP1251", "KOI8-R"}, "cy"};
			if (is({"el", "ell", "gre"})) return {{"CP1253", "ISO-8859-7"}, "el"};
			if (is({"tr", "tur"})) return {{"CP1254", "ISO-8859-9"}, "tr"};
			if (is({"he", "heb", "iw"})) return {{"CP1255", "ISO-8859-8"}, "he"};
			if (is({"ar", "ara"})) return {{"CP1256"}, "ar"};
			if (is({"lt", "lit", "lv", "lav", "et", "est"})) return {{"CP1257", "ISO-8859-13"}, "bt"};
			return {{}, ""};
		}
		// a code page's family, as pagesOf names them ("" for Western or one not listed)
		std::string familyOf(const std::string& charset)
		{
			std::string c = lower(charset);
			if (c == "windows-1250" || c == "cp1250" || c == "iso-8859-2" || c == "ibm852" || c == "cp852" || c == "mac-centraleurope") return "ce";
			if (c == "windows-1251" || c == "cp1251" || c == "koi8-r" || c == "koi8-u" || c == "iso-8859-5" || c == "ibm855" || c == "ibm866" || c == "mac-cyrillic") return "cy";
			if (c == "windows-1253" || c == "cp1253" || c == "iso-8859-7") return "el";
			if (c == "windows-1254" || c == "cp1254" || c == "iso-8859-9") return "tr";
			if (c == "windows-1255" || c == "cp1255" || c == "iso-8859-8") return "he";
			if (c == "windows-1256" || c == "cp1256" || c == "iso-8859-6") return "ar";
			if (c == "windows-1257" || c == "cp1257" || c == "iso-8859-13" || c == "iso-8859-4") return "bt";
			if (c == "shift_jis" || c == "euc-jp" || c == "iso-2022-jp") return "ja";
			if (c == "gb18030" || c == "big5" || c == "euc-tw" || c == "hz-gb-2312" || c == "iso-2022-cn") return "zh";
			if (c == "euc-kr" || c == "uhc" || c == "cp949" || c == "iso-2022-kr") return "ko";
			return "";
		}

		// the file's text as UTF-8 with \n line ends. UTF-8 (BOM off) as is; UTF-16 by its BOM; else, of the chosen
		// code page, the guess (when the language doesn't name another family), the language's code pages and
		// Windows-1252, the first that converts without an error (and Latin-1 for the bytes none takes)
		std::string cleanText(const std::string& in, const std::string& hint = "")
		{
			std::string s = in, conv;
			if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);
			else if (s.compare(0, 2, "\xFF\xFE") == 0 && convertFrom("UTF-16LE", s.substr(2), conv)) s = conv;
			else if (s.compare(0, 2, "\xFE\xFF") == 0 && convertFrom("UTF-16BE", s.substr(2), conv)) s = conv;
			else if (!validUtf8(s))
			{
				std::vector<std::string> tries;
				{
					std::lock_guard<std::mutex> l(charsetLock);
					if (!chosenCharset.empty()) tries.push_back(chosenCharset);
				}
				LangPages lang = pagesOf(hint);
				std::string guess = guessCharset(s);
				if (!guess.empty() && (!*lang.family || familyOf(guess) == lang.family)) tries.push_back(guess);
				for (auto& p : lang.pages) tries.push_back(p);
				tries.push_back("CP1252");
				bool done = false;
				for (const std::string& cs : tries)
					if (convertFrom(cs.c_str(), s, conv))
					{
						s = conv;
						done = true;
						break;
					}
				if (!done)
				{
					std::string o;
					for (unsigned char c : s)
					{
						if (c < 0x80) o += (char)c;
						else
						{
							o += (char)(0xC0 | (c >> 6));
							o += (char)(0x80 | (c & 0x3F));
						}
					}
					s = o;
				}
			}
			std::string o;
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] == '\r')
				{
					o += '\n';
					if (i + 1 < s.size() && s[i + 1] == '\n') i++;
				}
				else o += s[i];
			}
			return o;
		}

		struct Cue { double start, end; std::string text; };

		std::string cuesToVtt(std::vector<Cue>& cues)
		{
			std::stable_sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) { return a.start < b.start; });
			std::string out = "WEBVTT\n\n";
			for (auto& c : cues)
			{
				std::string t = vttCue(c.start, c.end, c.text);
				if (!t.empty()) out += t.substr(8);	// without the header
			}
			return out;
		}

		// "1:02:03,5" "00:01:02.345" "0:00:03.50" (ASS centiseconds are fractions too)
		bool parseClock(const std::string& in, double& sec)
		{
			std::string s;
			for (char c : in)
				if (c != ' ' && c != '\t') s += c;
			std::vector<std::string> parts;
			size_t pos = 0;
			while (true)
			{
				size_t c = s.find(':', pos);
				parts.push_back(s.substr(pos, c == std::string::npos ? c : c - pos));
				if (c == std::string::npos) break;
				pos = c + 1;
			}
			if (parts.size() < 2 || parts.size() > 3) return false;
			for (auto& p : parts)
				if (p.empty()) return false;
			for (auto& ch : parts.back())
				if (ch == ',') ch = '.';
			char* end = nullptr;
			double h = parts.size() == 3 ? strtod(parts[0].c_str(), &end) : 0;
			double m = strtod(parts[parts.size() - 2].c_str(), &end);
			double x = strtod(parts.back().c_str(), &end);
			if (*end) return false;
			sec = h * 3600 + m * 60 + x;
			return true;
		}

		std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\n"), b = s.find_last_not_of(" \t\n");
			return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
		}

		// SRT text lines: ASS tags like {\an8} dropped, HTML-ish <i> <b> <u> kept, the rest left to the browser's parser
		// (a bare "<" or "&" would start a tag or entity, so those are escaped when they aren't one of ours)
		std::string srtText(const std::string& in)
		{
			std::string o;
			for (size_t i = 0; i < in.size(); i++)
			{
				char c = in[i];
				if (c == '{' && i + 1 < in.size() && in[i + 1] == '\\')
				{
					size_t e = in.find('}', i);
					if (e != std::string::npos)
					{
						i = e;
						continue;
					}
				}
				if (c == '<')
				{
					static const char* ok[] = {"<i>", "</i>", "<b>", "</b>", "<u>", "</u>"};
					bool kept = false;
					for (auto t : ok)
						if (in.compare(i, strlen(t), t) == 0)
						{
							o += t;
							i += strlen(t) - 1;
							kept = true;
							break;
						}
					if (kept) continue;
					// other HTML (<font color>) goes: its tag only
					size_t e = in.find('>', i);
					if (e != std::string::npos && e - i < 80 && i + 1 < in.size() && (isalpha((unsigned char)in[i + 1]) || in[i + 1] == '/'))
					{
						i = e;
						continue;
					}
					o += "&lt;";
				}
				else if (c == '&')
					o += "&amp;";
				else o += c;
			}
			return o;
		}

		std::string srtToVtt(const std::string& text, const std::string& hint)
		{
			std::vector<std::string> lines;
			std::string s = cleanText(text, hint);
			size_t pos = 0;
			while (pos <= s.size())
			{
				size_t e = s.find('\n', pos);
				lines.push_back(s.substr(pos, e == std::string::npos ? e : e - pos));
				if (e == std::string::npos) break;
				pos = e + 1;
			}
			std::vector<Cue> cues;
			for (size_t i = 0; i < lines.size(); i++)
			{
				size_t arrow = lines[i].find("-->");
				if (arrow == std::string::npos) continue;
				std::string a = lines[i].substr(0, arrow), b = lines[i].substr(arrow + 3);
				size_t sp = b.find_first_not_of(" \t");
				size_t sp2 = sp == std::string::npos ? sp : b.find_first_of(" \t", sp);	// positions after the end time
				if (sp2 != std::string::npos) b = b.substr(0, sp2);
				Cue c;
				if (!parseClock(a, c.start) || !parseClock(b, c.end)) continue;
				std::string body;
				size_t j = i + 1;
				for (; j < lines.size() && !trim(lines[j]).empty(); j++)
					body += (body.empty() ? "" : "\n") + srtText(lines[j]);
				i = j - 1;
				c.text = body;
				cues.push_back(c);
			}
			return cuesToVtt(cues);
		}

		std::vector<std::string> splitCommas(const std::string& s, size_t max)
		{
			std::vector<std::string> v;
			size_t pos = 0;
			while (v.size() + 1 < max)
			{
				size_t c = s.find(',', pos);
				if (c == std::string::npos) break;
				v.push_back(s.substr(pos, c - pos));
				pos = c + 1;
			}
			v.push_back(s.substr(pos));
			return v;
		}

		std::string assToVtt(const std::string& text, const std::string& hint)
		{
			std::string s = cleanText(text, hint);
			std::vector<Cue> cues;
			bool events = false;
			size_t startCol = 1, endCol = 2, textCol = 9, cols = 10;
			size_t pos = 0;
			while (pos < s.size())
			{
				size_t e = s.find('\n', pos);
				std::string line = s.substr(pos, e == std::string::npos ? e : e - pos);
				pos = e == std::string::npos ? s.size() : e + 1;
				std::string t = trim(line);
				if (t.empty()) continue;
				if (t[0] == '[')
				{
					events = lower(t) == "[events]";
					continue;
				}
				if (!events) continue;
				if (t.compare(0, 7, "Format:") == 0)
				{
					auto names = splitCommas(t.substr(7), 64);
					cols = names.size();
					for (size_t k = 0; k < names.size(); k++)
					{
						std::string n = lower(trim(names[k]));
						if (n == "start") startCol = k;
						else if (n == "end") endCol = k;
						else if (n == "text") textCol = k;
					}
				}
				else if (t.compare(0, 9, "Dialogue:") == 0)
				{
					auto f = splitCommas(t.substr(9), cols);	// the text is last, and keeps its commas
					if (f.size() <= textCol || f.size() <= startCol || f.size() <= endCol) continue;
					Cue c;
					if (!parseClock(f[startCol], c.start) || !parseClock(f[endCol], c.end)) continue;
					c.text = assCueText(f[textCol]);
					cues.push_back(c);
				}
			}
			return cuesToVtt(cues);
		}
	}

	std::string vttTimestamp(double seconds)
	{
		if (!(seconds > 0)) seconds = 0;
		long long ms = (long long)(seconds * 1000 + 0.5);
		char b[32];
		snprintf(b, sizeof b, "%02lld:%02lld:%02lld.%03lld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, ms % 1000);
		return b;
	}

	std::string assCueText(const std::string& in)
	{
		std::string o;
		bool drawing = false;
		for (size_t i = 0; i < in.size(); i++)
		{
			char c = in[i];
			if (c == '{')
			{
				size_t e = in.find('}', i);
				if (e != std::string::npos)
				{
					std::string tags = in.substr(i + 1, e - i - 1);
					// \p1 starts a drawing, \p0 ends it
					size_t p = 0;
					while ((p = tags.find("\\p", p)) != std::string::npos)
					{
						p += 2;
						if (p < tags.size() && isdigit((unsigned char)tags[p])) drawing = atoi(tags.c_str() + p) != 0;
					}
					for (const char* t : {"i", "b", "u"})
					{
						std::string on = std::string("\\") + t + "1", off = std::string("\\") + t + "0";
						if (!drawing && tags.find(on) != std::string::npos) o += std::string("<") + t + ">";
						else if (!drawing && tags.find(off) != std::string::npos) o += std::string("</") + t + ">";
					}
					i = e;
					continue;
				}
			}
			if (drawing) continue;
			if (c == '\\' && i + 1 < in.size())
			{
				char n = in[i + 1];
				if (n == 'N' || n == 'n')
				{
					o += '\n';
					i++;
					continue;
				}
				if (n == 'h')
				{
					o += ' ';
					i++;
					continue;
				}
			}
			if (c == '&') o += "&amp;";
			else if (c == '<') o += "&lt;";
			else if (c == '>') o += "&gt;";
			else o += c;
		}
		return trim(o);
	}

	std::string vttCue(double start, double end, const std::string& text)
	{
		// a blank line ends a cue: no empty lines inside
		std::string body, line;
		for (size_t i = 0; i <= text.size(); i++)
		{
			if (i == text.size() || text[i] == '\n')
			{
				if (!trim(line).empty()) body += (body.empty() ? "" : "\n") + trim(line);
				line.clear();
			}
			else line += text[i];
		}
		if (body.empty() || !(end > start)) return "";
		// a "-->" in the text would end the timing line of nothing, but keep it out all the same
		for (size_t p; (p = body.find("-->")) != std::string::npos;) body.replace(p, 3, "->");
		return "WEBVTT\n\n" + vttTimestamp(start) + " --> " + vttTimestamp(end) + "\n" + body + "\n\n";
	}

	std::string subtitleTextToUtf8(const std::string& bytes, const std::string& langHint) { return cleanText(bytes, langHint); }

	void setSubtitleCharset(const std::string& charset)
	{
		std::lock_guard<std::mutex> l(charsetLock);
		chosenCharset = charset;
	}

	std::string guessCharset(const std::string& bytes)
	{
#ifdef BP_UCHARDET
		uchardet_t d = uchardet_new();
		std::string out;
		if (uchardet_handle_data(d, bytes.data(), bytes.size()) == 0)
		{
			uchardet_data_end(d);
			const char* cs = uchardet_get_charset(d);
			if (cs && *cs && lower(cs) != "ascii" && lower(cs) != "utf-8") out = cs;
		}
		uchardet_delete(d);
		return out;
#else
		(void)bytes;
		return "";
#endif
	}

	std::string subtitleFileToVtt(const std::string& ext, const std::string& text, const std::string& langHint)
	{
		std::string e = lower(ext);
		if (e == "srt") return srtToVtt(text, langHint);
		if (e == "ass" || e == "ssa") return assToVtt(text, langHint);
		if (e == "vtt")
		{
			std::string s = cleanText(text, langHint);
			return s.compare(0, 6, "WEBVTT") == 0 ? s : "WEBVTT\n\n" + s;
		}
		return "";
	}

	bool imageSubtitleCodec(const std::string& codec)
	{
		std::string c = lower(codec);
		return c == "hdmv_pgs_subtitle" || c == "dvd_subtitle" || c == "dvb_subtitle" || c == "xsub" || c == "dvb_teletext" ||
			c == "hdmv_text_subtitle" /* a text PGS, rare and unsupported by decoders */;
	}

	std::string yearFromDate(const std::string& d)
	{
		for (size_t i = 0; i + 4 <= d.size(); i++)
		{
			size_t j = i;
			while (j < d.size() && isdigit((unsigned char)d[j])) j++;
			if (j - i >= 4 && (i == 0 || !isdigit((unsigned char)d[i - 1])))
			{
				if (d[i] == '1' || d[i] == '2') return d.substr(i, 4);
			}
			if (j > i) i = j - 1;
		}
		return "";
	}

	std::string trackText(const std::string& raw)
	{
		size_t a = raw.find_first_not_of(" \t\r\n"), b = raw.find_last_not_of(" \t\r\n");
		if (a == std::string::npos) return "";
		std::string t = raw.substr(a, b - a + 1);
		for (char c : t)
			if (isdigit((unsigned char)c)) return t;
		return "";
	}
}
