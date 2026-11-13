#include "substyle.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace bp
{
	// The fonts are the ones in assets/fonts (mpv's sub-fonts-dir), named by their family. Fusion Pixel has the CJK
	// characters; libass falls back to it for those whatever the font.
	static const struct { const char* family; bool bold; const char* file; } FONTS[] = {
		{"Inter", false, "Inter-Regular.ttf"}, {"Inter", true, "Inter-Bold.ttf"},
		{"Pixelify Sans", false, "PixelifySans-Regular.ttf"},
		{"Fusion Pixel 12px Prop zh-Hans", false, "fusion-pixel-12px-proportional-zh_hans.ttf"},
	};
	// mpv's sizes are in pixels of a 720-line picture, whatever the panel
	static const char* SIZES[] = {"36", "46", "56", "68"};
	static const char* COLORS[] = {"#FFFFFF", "#FFE45C", "#6EE7F2", "#8EF08A", "#FF9BD2", "#C8C8C8"};
	static const char* BORDERS[] = {"0", "1.5", "3", "5"};
	static const char* BORDER_COLORS[] = {"#000000", "#404040", "#10204A"};

	const std::vector<StyleSetting>& subStyleSettings()
	{
		static const std::vector<StyleSetting> settings = {
			{"font", "Font", {{"inter", "Inter"}, {"inter-bold", "Inter Bold"}, {"pixelify", "Pixelify Sans"}, {"fusion-pixel", "Fusion Pixel"}}, 0},
			{"size", "Size", {{"small", "Small"}, {"medium", "Medium"}, {"large", "Large"}, {"extra-large", "Extra large"}}, 1},
			{"color", "Colour", {{"white", "White"}, {"yellow", "Yellow"}, {"cyan", "Cyan"}, {"green", "Green"}, {"pink", "Pink"}, {"grey", "Grey"}}, 0},
			{"border", "Border", {{"none", "None"}, {"thin", "Thin"}, {"normal", "Normal"}, {"thick", "Thick"}}, 2},
			{"border-color", "Border colour", {{"black", "Black"}, {"dark-grey", "Dark grey"}, {"dark-blue", "Dark blue"}}, 0},
			// (ids are their iconv names, lower case: ENCODINGS below in the same order)
			{"encoding", "Text encoding", {{"auto", "Auto", "Auto (UTF-8, or detected)"},
				{"cp1250", "C. Europe 1250", "Central European (Windows-1250)"},
				{"iso-8859-2", "C. Europe 8859-2", "Central European (ISO-8859-2)"},
				{"cp852", "C. Europe 852", "Central European (DOS 852)"},
				{"cp1251", "Cyrillic 1251", "Cyrillic (Windows-1251)"}, {"koi8-r", "Cyrillic KOI8-R", "Cyrillic (KOI8-R)"},
				{"cp1253", "Greek 1253", "Greek (Windows-1253)"}, {"cp1254", "Turkish 1254", "Turkish (Windows-1254)"},
				{"cp1257", "Baltic 1257", "Baltic (Windows-1257)"}, {"cp1255", "Hebrew 1255", "Hebrew (Windows-1255)"},
				{"cp1256", "Arabic 1256", "Arabic (Windows-1256)"}, {"cp1252", "Western 1252", "Western (Windows-1252)"}}, 0},
		};
		return settings;
	}

	SubStyle::SubStyle()
	{
		for (int k = 0; k < STYLE_KEYS; k++) pick[k] = subStyleSettings()[k].def;
	}

	bool SubStyle::operator==(const SubStyle& o) const
	{
		return !memcmp(pick, o.pick, sizeof pick);
	}

	void stepStyle(SubStyle& s, StyleKey k, int dir)
	{
		int n = (int)subStyleSettings()[k].choices.size();
		s.pick[k] = ((s.pick[k] + dir) % n + n) % n;
	}

	std::string styleValueLabel(const SubStyle& s, StyleKey k)
	{
		return subStyleSettings()[k].choices[s.pick[k]].label;
	}

	std::vector<std::pair<std::string, std::string>> subStyleProperties(const SubStyle& s)
	{
		// the sub-border-* names: aliases since mpv 0.38 (sub-outline-*), the names before it
		return {
			{"sub-font", FONTS[s.pick[FONT]].family},
			{"sub-bold", FONTS[s.pick[FONT]].bold ? "yes" : "no"},
			{"sub-font-size", SIZES[s.pick[SIZE]]},
			{"sub-color", COLORS[s.pick[COLOR]]},
			{"sub-border-size", BORDERS[s.pick[BORDER]]},
			{"sub-border-color", BORDER_COLORS[s.pick[BORDER_COLOR]]},
			// a code page still reads a file that's valid UTF-8 as UTF-8 (mpv tries UTF-8 first unless it's "+")
			{"sub-codepage", s.pick[ENCODING] ? subEncodingCharset(s) : "auto"},
		};
	}

	std::string subEncodingCharset(const SubStyle& s)
	{
		if (!s.pick[ENCODING]) return "";
		std::string id = subStyleSettings()[ENCODING].choices[s.pick[ENCODING]].id;
		for (char& c : id) c = (char)toupper((unsigned char)c);
		return id;
	}

	SubStyleLook subStyleLook(const SubStyle& s)
	{
		auto hex = [](const char* c) { return (unsigned)strtoul(c + 1, nullptr, 16); };
		return {FONTS[s.pick[FONT]].file, atof(SIZES[s.pick[SIZE]]), atof(BORDERS[s.pick[BORDER]]),
			hex(COLORS[s.pick[COLOR]]), hex(BORDER_COLORS[s.pick[BORDER_COLOR]])};
	}

	std::string serializeSubStyle(const SubStyle& s)
	{
		std::string out = "# banana-player's subtitle style (plain subtitles; ASS/SSA keep their own)\n";
		for (int k = 0; k < STYLE_KEYS; k++)
		{
			auto& st = subStyleSettings()[k];
			out += std::string(st.key) + " = " + st.choices[s.pick[k]].id + "\n";
		}
		return out;
	}

	static std::string trim(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
		return a == std::string::npos ? "" : s.substr(a, b - a + 1);
	}

	SubStyle parseSubStyle(const std::string& text)
	{
		SubStyle s;
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			size_t eq = line.find('=');
			if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
			std::string key = trim(line.substr(0, eq)), id = trim(line.substr(eq + 1));
			for (int k = 0; k < STYLE_KEYS; k++)
			{
				auto& st = subStyleSettings()[k];
				if (key != st.key) continue;
				for (size_t c = 0; c < st.choices.size(); c++)
					if (id == st.choices[c].id) s.pick[k] = (int)c;
			}
		}
		return s;
	}
}
