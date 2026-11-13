#pragma once
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	// How plain subtitles look: SRT, VTT, mov_text... drawn by libass in mpv's sub-* style. Styled subtitles (ASS/SSA)
	// keep their own fonts, colours and borders (sub-ass-override stays at "scale"), and pictures (PGS, VobSub) are
	// pictures. And how a subtitle file's text is read (ENCODING: every text file, styled or not; a file inside a video
	// is UTF-8 already): Auto (UTF-8, else detected), or a code page for a file Auto reads wrong.

	struct StyleChoice
	{
		const char* id;		// in subtitles.conf
		const char* label;	// on the bottom panel
		const char* full = nullptr;	// on the web page, where there's room (else label)
	};

	struct StyleSetting
	{
		const char* key;		// in subtitles.conf
		const char* label;
		std::vector<StyleChoice> choices;
		int def;				// the default's index
	};

	enum StyleKey { FONT, SIZE, COLOR, BORDER, BORDER_COLOR, ENCODING, STYLE_KEYS };

	// in the order of StyleKey, as the style page lists them
	const std::vector<StyleSetting>& subStyleSettings();

	struct SubStyle
	{
		int pick[STYLE_KEYS];	// an index into each setting's choices
		SubStyle();
		bool operator==(const SubStyle& o) const;
	};

	// one step through a setting's choices, wrapping around
	void stepStyle(SubStyle& s, StyleKey k, int dir);
	std::string styleValueLabel(const SubStyle& s, StyleKey k);

	// the mpv properties that make the style, name/value; every style sets all of them
	std::vector<std::pair<std::string, std::string>> subStyleProperties(const SubStyle& s);

	// what the style page's preview needs: the font's file in assets/fonts, sizes in pixels of a 720-line picture,
	// colours as 0xRRGGBB
	struct SubStyleLook
	{
		std::string fontFile;
		double size, border;
		unsigned color, borderColor;
	};
	SubStyleLook subStyleLook(const SubStyle& s);

	// subtitles.conf: "key = id" lines; unknown keys, ids and other lines are left out, missing keys keep their default
	std::string serializeSubStyle(const SubStyle& s);
	SubStyle parseSubStyle(const std::string& text);

	// the ENCODING choice's iconv name ("CP1250"...), "" for Auto
	std::string subEncodingCharset(const SubStyle& s);
}
