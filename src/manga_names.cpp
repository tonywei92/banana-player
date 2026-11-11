#include "manga_names.h"
#include "playlist.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace bp
{
	namespace
	{
		bool digit(char c)
		{
			return c >= '0' && c <= '9';
		}

		char lower(char c)
		{
			return (char)std::tolower((unsigned char)c);
		}

		// a keyword counts only at the start of a word: "March 12" isn't "Ch.12" and "Step 3" isn't "Ep 3"
		bool wordStart(const std::string& s, size_t at)
		{
			return at == 0 || !std::isalnum((unsigned char)s[at - 1]);
		}

		bool wordAt(const std::string& s, size_t at, const char* word)
		{
			size_t n = std::strlen(word);
			if (at + n > s.size()) return false;
			for (size_t k = 0; k < n; k++)
				if (lower(s[at + k]) != word[k]) return false;
			return true;
		}

		bool wordBefore(const std::string& s, size_t at, const char* word)
		{
			size_t n = std::strlen(word);
			if (at < n) return false;
			for (size_t k = 0; k < n; k++)
				if (lower(s[at - n + k]) != word[k]) return false;
			return wordStart(s, at - n);
		}

		bool gap(char c)
		{
			return c == '.' || c == ':' || c == '-' || c == '_' || std::isspace((unsigned char)c);
		}

		// the '.' in "Ch.12" and the space in "ch 12.5"
		size_t skipGap(const std::string& s, size_t at)
		{
			while (at < s.size() && gap(s[at])) at++;
			return at;
		}

		// a number at at, with one fractional part: "12.5"; false when there are no digits there
		bool numberAt(const std::string& s, size_t at, double& out)
		{
			if (at >= s.size() || !digit(s[at])) return false;
			double v = 0;
			while (at < s.size() && digit(s[at]))
			{
				v = v * 10 + (s[at] - '0');
				at++;
			}
			if (at + 1 < s.size() && s[at] == '.' && digit(s[at + 1]))
			{
				double frac = 0, scale = 1;
				for (at++; at < s.size() && digit(s[at]); at++)
				{
					frac = frac * 10 + (s[at] - '0');
					scale *= 10;
				}
				v += frac / scale;
			}
			out = v;
			return true;
		}

		// true when the digit run at runStart is a volume's number: "Vol 3", "Volume 02", "v2"
		bool volumeBefore(const std::string& s, size_t runStart)
		{
			size_t at = runStart;
			while (at > 0 && gap(s[at - 1])) at--;
			if (wordBefore(s, at, "volume") || wordBefore(s, at, "vol")) return true;
			// a lone "v" only directly against the digits, as the rule says
			return runStart > 0 && lower(s[runStart - 1]) == 'v' && wordStart(s, runStart - 1);
		}
	}

	bool isPageImage(const std::string& name)
	{
		for (size_t at = 0; at <= name.size();)
		{
			size_t slash = name.find('/', at);
			std::string part = name.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
			// a folder entry, a hidden one or macOS's metadata isn't a page, even when its extension says otherwise
			if (part.empty() || part[0] == '.' || part == "__MACOSX") return false;
			if (slash == std::string::npos) break;
			at = slash + 1;
		}
		size_t dot = name.rfind('.');
		if (dot == std::string::npos) return false;
		std::string ext = name.substr(dot + 1);
		for (auto& c : ext) c = lower(c);
		for (const char* e : {"jpg", "jpeg", "png", "webp", "gif", "avif", "bmp"})
			if (ext == e) return true;
		return false;
	}

	void sortPages(std::vector<std::string>& names)
	{
		std::sort(names.begin(), names.end(), naturalLess);
	}

	double chapterNumber(const std::string& name)
	{
		for (size_t i = 0; i < name.size(); i++)
		{
			double v;
			if (wordStart(name, i))
			{
				if (wordAt(name, i, "chapter") && numberAt(name, skipGap(name, i + 7), v)) return v;
				if (wordAt(name, i, "ch") && numberAt(name, skipGap(name, i + 2), v)) return v;
				if (wordAt(name, i, "ep") && numberAt(name, skipGap(name, i + 2), v)) return v;
				// c012: no separator, so the letter alone says chapter
				if (lower(name[i]) == 'c' && numberAt(name, i + 1, v)) return v;
			}
			if (name[i] == '#' && numberAt(name, skipGap(name, i + 1), v)) return v;
		}
		// no marker: the last number is the chapter, passing over a volume's ("Series - 012 (v2)" is chapter 12, "Vol 3"
		// alone none)
		size_t at = name.size();
		while (true)
		{
			while (at > 0 && !digit(name[at - 1])) at--;
			if (at == 0) return -1;
			size_t start = at - 1;
			while (start > 0 && digit(name[start - 1])) start--;
			// a fraction belongs to its number: "Series 10.5" is chapter 10.5, not 5
			if (start >= 2 && name[start - 1] == '.' && digit(name[start - 2]))
			{
				start -= 2;
				while (start > 0 && digit(name[start - 1])) start--;
			}
			if (!volumeBefore(name, start))
			{
				double v;
				numberAt(name, start, v);
				return v;
			}
			at = start;
		}
	}

	double volumeNumber(const std::string& name)
	{
		for (size_t i = 0; i < name.size(); i++)
		{
			if (!wordStart(name, i)) continue;
			double v;
			if (wordAt(name, i, "volume") && numberAt(name, skipGap(name, i + 6), v)) return v;
			if (wordAt(name, i, "vol") && numberAt(name, skipGap(name, i + 3), v)) return v;
			// "v2" only directly, preceded by a space, the start or a separator
			if (lower(name[i]) == 'v' && numberAt(name, i + 1, v)) return v;
		}
		return -1;
	}
}
