// Text cut into runs, each in a font that has its characters: SDL_ttf has no fallback of its own. Shared by the bottom
// panel (Ui::shapeRuns) and the top panel's cards and Preview pane (cardcanvas.h), which once drew CJK file names in
// Pixelify Sans alone: boxes.
//
// The CJK font is Fusion Pixel's Simplified Chinese build. Its Japanese build has the same characters, about 5000 of
// them drawn the Japanese way; those alone are bundled (fusion-pixel-12px-proportional-ja-forms.ttf, made by
// tools/make-ja-forms.py) and used for text with kana in it. (Its Traditional build has the same characters too: with
// no rule that tells Traditional text from Japanese kanji or Simplified, it isn't bundled.)
#pragma once
#include "tagtext.h"
#include <SDL_ttf.h>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	using FontRun = std::pair<TTF_Font*, std::string>;

	constexpr const char* FONT_CJK = "fusion-pixel-12px-proportional-zh_hans.ttf";
	constexpr const char* FONT_JA_FORMS = "fusion-pixel-12px-proportional-ja-forms.ttf";

	// s in runs: whole in `own` if it has every character, else whole in the first fallback that has them all, else
	// character by character (own, else the first fallback that has it). fallback(k) opens the k-th of `count` (null:
	// none). With kana in s, what `cjk` draws goes to `ja` wherever ja has the character (ja may be null)
	template <class Fallback>
	std::vector<FontRun> fontRuns(const std::string& s, TTF_Font* own, size_t count, Fallback fallback, TTF_Font* cjk, TTF_Font* ja)
	{
		std::vector<std::pair<Uint32, std::string>> chars;	// each character with its bytes
		for (size_t i = 0; i < s.size();)
		{
			unsigned char c = s[i];
			Uint32 cp = c;
			size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
			if (n > 1)
			{
				cp = c & (0x7F >> n);
				for (size_t k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
			}
			chars.push_back({cp, s.substr(i, n)});
			i += n;
		}
		auto has = [](TTF_Font* f, Uint32 cp) { return f && (cp < 0x20 || TTF_GlyphIsProvided32(f, cp)); };
		auto hasAll = [&](TTF_Font* f) {
			for (auto& c : chars)
				if (!has(f, c.first)) return false;
			return f != nullptr;
		};
		TTF_Font* whole = hasAll(own) ? own : nullptr;
		for (size_t k = 0; !whole && k < count; k++)
			if (hasAll(fallback(k))) whole = fallback(k);
		bool jaForms = ja && cjk && hasKana(s);
		std::vector<FontRun> out;
		for (auto& c : chars)
		{
			TTF_Font* f = whole;
			if (!f)
			{
				f = own;
				if (!has(own, c.first))
					for (size_t k = 0; k < count; k++)
						if (has(fallback(k), c.first)) { f = fallback(k); break; }
			}
			if (jaForms && f == cjk && has(ja, c.first)) f = ja;
			if (!f) f = own;
			if (!out.empty() && out.back().first == f) out.back().second += c.second;
			else out.push_back({f, c.second});
		}
		return out;
	}
}
