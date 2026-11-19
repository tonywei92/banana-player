// The themes' values (theme.h; docs/redesign/components.md, Themes). Each theme is one line of base roles below: the
// rest (RAIL, the TOP_* tokens) follows the spec's rules, as docs/redesign/themes.py works them out
#include "theme.h"
#include <algorithm>

namespace bp
{
	namespace theme
	{
		namespace
		{
			constexpr Rgb hex(unsigned v) { return {(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v}; }
			// c at percent over under, rounded (themes.py's mix: RAIL and TOP_CARD are SURFACE at 60% over BG)
			constexpr unsigned char roundTo(int a, int b, int percent)
			{
				return (unsigned char)(a + ((b - a) * percent + ((b - a) >= 0 ? 50 : -50)) / 100);
			}
			constexpr Rgb over(Rgb under, Rgb c, int percent)
			{
				return {roundTo(under.r, c.r, percent), roundTo(under.g, c.g, percent), roundTo(under.b, c.b, percent)};
			}

			// a theme's base roles (themes.py's T): rail 0 = SURFACE at 60% over BG; top: the theme whose values the
			// TOP_* tokens take (-1 itself); page: Match theme's page (0 Dark, 1 Light, 3 Black)
			struct Base
			{
				const char *id, *name;
				unsigned bg, sf, bt, pr, tr, ru, tx, dm, dOn, ac, oa, dg;
				int edgeMix;
				unsigned rail;
				int top, page;
				unsigned ok;
				// optional (0: unset): dark keys with light lettering, and a primary key in its own colour (theme.h)
				unsigned key = 0, onKey = 0, keyFocus = 0, hintBar = 0, onHint = 0, primary = 0, onPrimary = 0;
			};
			constexpr unsigned GREEN = 0x7BD88F;
			// the values: one line a theme, in the stepper's order (written from docs/redesign/themes.py, which checks their
			// contrast: `themes.py` there is the source; the ids are settings.conf's words)
			constexpr Base BASES[] = {
				{"yellow", "Yellow", 0x000000, 0x121116, 0x26242E, 0x3E3B4A, 0x1E1D24, 0x2A2833, 0xE8E2D3, 0x8F8A80, 0xABA597, 0xFFD23F, 0x000000, 0xF0645A, 30, 0, -1, 0, GREEN},
				{"dark", "Dark", 0x0B0B0D, 0x17171A, 0x2A2A2F, 0x424249, 0x222226, 0x2E2E34, 0xC2C4CA, 0x8A8C94, 0xA6A8B0, 0xA9C4E6, 0x0B0B0D, 0xF47067, 40, 0, -1, 0, GREEN},
				{"midnight-blue", "Midnight blue", 0x040816, 0x0C142E, 0x1A2649, 0x2C3C6C, 0x131C3A, 0x202C52, 0xE1E7F5, 0x8893B0, 0xA5AFCA, 0x8CC8FF, 0x000000, 0xFF7A70, 30, 0, -1, 0, GREEN},
				{"dark-purple", "Dark purple", 0x0A0612, 0x150E22, 0x2C1F45, 0x3D2C5E, 0x1D1430, 0x2E2147, 0xE6DFF2, 0x938AA8, 0xAEA5C3, 0xB79CFF, 0x0A0612, 0xFF7A70, 30, 0, -1, 0, GREEN},
				{"dark-red", "Dark red", 0x0A0405, 0x190C0E, 0x361D21, 0x4D2A2F, 0x241315, 0x382224, 0xEFE1DD, 0xA08B87, 0xBCA8A4, 0xFF6A6E, 0x000000, 0xFFB224, 30, 0, -1, 0, GREEN},
				{"pink", "Pink", 0x0C060A, 0x1B0F17, 0x301C29, 0x4B2E41, 0x25151F, 0x37222F, 0xF1E2E9, 0xA18B97, 0xBAA5B1, 0xFF8FC7, 0x000000, 0xFFB224, 30, 0, -1, 0, GREEN},
				{"coffee", "Coffee", 0x0D0907, 0x1A1410, 0x30261F, 0x4A3B30, 0x241C17, 0x392D25, 0xEADFCF, 0x9C8E7E, 0xB7A999, 0xE0B070, 0x0D0907, 0xFF7468, 30, 0, -1, 0, GREEN},
				{"mint", "Mint", 0x050D0C, 0x0D1917, 0x1A2E2B, 0x2A4642, 0x132321, 0x213734, 0xDDEBE7, 0x859A95, 0xA1B5B0, 0x6FE0C2, 0x050D0C, 0xFF7A70, 30, 0, -1, 0, GREEN},
				{"retro-green", "Retro green", 0x0A0F08, 0x131B10, 0x22301D, 0x35492D, 0x1A2516, 0x2A3A24, 0xD9E4C8, 0x8A9A7C, 0xA6B598, 0xA6D44A, 0x0A0F08, 0xFF7A6B, 30, 0, -1, 0, GREEN},
				{"sunset", "Sunset", 0x0E0806, 0x1C110D, 0x33211A, 0x4E3328, 0x261812, 0x3A261E, 0xF1E1D2, 0xA38D7E, 0xBDA797, 0xFF9A4D, 0x0E0806, 0xFF6B9D, 30, 0, -1, 0, GREEN},
				{"candy", "Candy", 0x100A14, 0x1C1323, 0x31223D, 0x4C365D, 0x26192F, 0x3A2947, 0xF3E4EE, 0xA08C9C, 0xBBA7B7, 0x7FE0E8, 0x100A14, 0xFF7A70, 30, 0, -1, 0, GREEN},
				{"16-bit", "16-bit", 0xE4E3EA, 0xF4F3F8, 0xCFCDDC, 0xB8B5CC, 0xD6D4E0, 0xC6C4D3, 0x26242E, 0x5E5B6B, 0x4F4C5C, 0x55429C, 0xF4F3F8, 0xA81E2A, 55, 0xDAD9E2, 3, 1, 0x1F7A4D},
				{"16-bit-lavender", "16-bit Lavender", 0xC2C2C2, 0xD4D4D6, 0xB5AFE1, 0x9E97D0, 0xB4B3BB, 0x96939F, 0x3B3B3B, 0x4C4A52, 0x434149, 0x4E3375, 0xE6E4F0, 0x8A1420, 80, 0xB8B8BA, 3, 1, 0x1F7A4D},
				{"32-bit", "32-bit", 0xDEDEE1, 0xEFEFF2, 0xC8C8CE, 0xB0B0B8, 0xD0D0D6, 0xC0C0C7, 0x1E1E22, 0x595960, 0x4B4B52, 0x1F4E9E, 0xEFEFF2, 0x961C24, 55, 0xD3D3D8, 1, 1, 0x1F7A4D},
				{"sakura", "Sakura", 0xFFE6E0, 0xFFF3EF, 0xE8B3CF, 0xC1839F, 0xFACFD8, 0xE8B3CF, 0x561F33, 0x8A5069, 0x6F3C55, 0x852A59, 0xFFE6E0, 0x7A3800, 55, 0xFACFD8, 5, 1, 0x1F7A4D},
				// (white armour SURFACE, the blue ACCENT, the yellow ON_ACCENT, the grey DIM as given; the red deepened: 3.8 : 1 on white as given)
				{"gundam", "Gundam", 0xE8ECF4, 0xFFFFFF, 0xF3F5FA, 0xD5DDEE, 0xCDD4E3, 0xC3CBDB, 0x141B33, 0x5A5B6D, 0x464755, 0x1A56C9, 0xFFFFFF, 0xB81F28, 55, 0xDFE4EE, 2, 1, 0x1F7A4D,
					0x1C3C8E, 0xFFFFFF, 0xFFF867, 0x172E6B, 0xFFFFFF, 0xD3222B, 0xFFFFFF},
				{"light", "Light", 0xF7F4EC, 0xFFFDF8, 0xE6E0D2, 0xD0C9B8, 0xDED8C9, 0xD5CEBE, 0x1B1A20, 0x625E56, 0x55514A, 0x7A5200, 0xFFFDF8, 0xA3261D, 55, 0xECE7DB, 0, 1, 0x1F7A4D},
				{"high-contrast", "High contrast", 0x000000, 0x0A0A0A, 0x1E1E1E, 0x3A3A3A, 0x2B2B2B, 0x5E5E5E, 0xFFFFFF, 0xC6C6C6, 0xD8D8D8, 0xFFE14D, 0x000000, 0xFF8A80, 50, 0, -1, 3, 0x8DF0A4},
			};

			constexpr int COUNT = (int)(sizeof BASES / sizeof *BASES);

			constexpr Palette build(int i)
			{
				const Base& b = BASES[i];
				const Base& t = BASES[b.top < 0 ? i : b.top];
				Palette p{};
				p.id = b.id;
				p.name = b.name;
				p.BG = hex(b.bg), p.SURFACE = hex(b.sf), p.BUTTON = hex(b.bt), p.PRESSED = hex(b.pr), p.TRACK = hex(b.tr), p.RULE = hex(b.ru);
				p.RAIL = b.rail ? hex(b.rail) : over(p.BG, p.SURFACE, 60);
				p.TEXT = hex(b.tx), p.DIM = hex(b.dm), p.DIM_ON = hex(b.dOn), p.ACCENT = hex(b.ac), p.ON_ACCENT = hex(b.oa), p.DANGER = hex(b.dg);
				p.EDGE_MIX = b.edgeMix;
				p.TOP_CARD = over(hex(t.bg), hex(t.sf), 60);
				p.TOP_EDGE = hex(t.ru), p.TOP_TITLE = hex(t.ac), p.TOP_TEXT = hex(t.tx), p.TOP_SUB = hex(t.dm), p.TOP_TRACK = hex(t.bt);
				p.TOP_SQUARE = hex(t.sf), p.TOP_DANGER = hex(t.dg);
				p.page = b.page;
				p.OK = hex(b.ok);
				p.strongKeys = b.key != 0;
				p.KEY = b.key ? hex(b.key) : p.BUTTON, p.ON_KEY = b.key ? hex(b.onKey) : p.TEXT, p.KEY_FOCUS = b.key ? hex(b.keyFocus) : p.ACCENT;
				p.HINT_BAR = b.key ? hex(b.hintBar) : p.BG, p.ON_HINT = b.key ? hex(b.onHint) : p.DIM_ON;
				p.PRIMARY = b.primary ? hex(b.primary) : p.ACCENT, p.ON_PRIMARY = b.primary ? hex(b.onPrimary) : p.ON_ACCENT;
				return p;
			}
			struct Table
			{
				Palette p[COUNT];
				constexpr Table() : p{}
				{
					for (int i = 0; i < COUNT; i++) p[i] = build(i);
				}
			};
			constexpr Table TABLE;
			int now = 0;
		}

		// (constant-initialized: Yellow before anything is drawn, and before any static that reads it)
		Palette current = TABLE.p[0];

		int themeCount() { return COUNT; }
		const Palette& palette(int i) { return TABLE.p[i >= 0 && i < COUNT ? i : 0]; }
		const char* themeId(int i) { return palette(i).id; }
		const char* const* themeIds()
		{
			static const struct Ids { const char* v[COUNT]; Ids() { for (int i = 0; i < COUNT; i++) v[i] = TABLE.p[i].id; } } ids;
			return ids.v;
		}
		const char* const* themeNames()
		{
			static const struct Names { const char* v[COUNT]; Names() { for (int i = 0; i < COUNT; i++) v[i] = TABLE.p[i].name; } } names;
			return names.v;
		}
		int currentTheme() { return now; }
		bool setTheme(int i)
		{
			if (i < 0 || i >= COUNT) i = 0;
			if (i == now) return false;
			now = i;
			current = TABLE.p[i];
			return true;
		}
		int matchedPage(int i) { return palette(i).page; }

		std::string themeCss()
		{
			auto h = [](Rgb c) {
				char s[8];
				snprintf(s, sizeof s, "#%02x%02x%02x", c.r, c.g, c.b);
				return std::string(s);
			};
			auto a = [](Rgb c, int percent) {
				char s[40];
				snprintf(s, sizeof s, "rgba(%d,%d,%d,%.2f)", c.r, c.g, c.b, percent / 100.0);
				return std::string(s);
			};
			std::string css = "/* written by the handheld from src/theme.cpp's table (theme.h: themeCss); Yellow is style.css's :root */\n";
			for (int i = 1; i < COUNT; i++)
			{
				const Palette& p = TABLE.p[i];
				auto e = [&](Rgb c) { return h(edgeOf(c, p.TEXT, p.EDGE_MIX)); };
				auto l = [&](Rgb c) { return h(lipOf(c)); };
				Rgb picked = over(p.BG, p.ACCENT, 17);
				bool light = p.BG.r + p.BG.g + p.BG.b > 3 * 128;
				css += std::string(":root[data-theme=\"") + p.id + "\"]{color-scheme:" + (light ? "light" : "dark") + ";";
				css += "--bg:" + h(p.BG) + ";--surface:" + h(p.SURFACE) + ";--raised:" + h(over(p.SURFACE, p.BUTTON, 35)) + ";--btn:" + h(p.BUTTON) +
					";--pressed:" + h(p.PRESSED) + ";--track:" + h(p.TRACK) + ";--rule:" + h(p.RULE) + ";";
				css += "--cream:" + h(p.TEXT) + ";--dim:" + h(p.DIM) + ";--dim-btn:" + h(p.DIM_ON) + ";--yellow:" + h(p.ACCENT) + ";--red:" + h(p.DANGER) +
					";--green:" + h(p.OK) + ";--on-accent:" + h(p.ON_ACCENT) + ";";
				// (strong keys: Gundam's navy keys and red primary ones; unset, the button, accent and text roles they always were)
				css += "--key:" + h(p.KEY) + ";--on-key:" + h(p.ON_KEY) + ";--key-focus:" + h(p.KEY_FOCUS) +
					";--hint-bar:" + h(p.HINT_BAR) + ";--on-hint:" + h(p.ON_HINT) +
					";--primary:" + h(p.PRIMARY) + ";--on-primary:" + h(p.ON_PRIMARY) + ";";
				// (accent ink on a key: the accent, or on a strong theme's dark key its focus yellow)
				css += "--key-lit:" + h(p.strongKeys ? p.KEY_FOCUS : p.ACCENT) + ";";
				css += "--e-btn:" + e(p.BUTTON) + ";--e-pressed:" + e(p.PRESSED) + ";--e-surface:" + e(p.SURFACE) + ";--e-yellow:" + e(p.ACCENT) +
					";--e-red:" + e(p.DANGER) + ";--e-track:" + e(p.TRACK) + ";";
				css += "--l-btn:" + l(p.BUTTON) + ";--l-pressed:" + l(p.PRESSED) + ";--l-surface:" + l(p.SURFACE) + ";--l-yellow:" + l(p.ACCENT) +
					";--l-red:" + l(p.DANGER) + ";";
				css += "--e-key:" + e(p.KEY) + ";--l-key:" + l(p.KEY) + ";--e-primary:" + e(p.PRIMARY) + ";--l-primary:" + l(p.PRIMARY) + ";";
				// (a strong theme's hover is its key moved towards the text, kept readable; every other theme's is PRESSED, as before)
				Rgb keyHover = p.strongKeys ? over(p.KEY, p.TEXT, 40) : p.PRESSED;
				css += "--key-hover:" + h(keyHover) + ";--e-key-hover:" + e(keyHover) + ";--l-key-hover:" + l(keyHover) + ";";
				Rgb down{(unsigned char)(p.ACCENT.r * 88 / 100), (unsigned char)(p.ACCENT.g * 88 / 100), (unsigned char)(p.ACCENT.b * 88 / 100)};
				css += "--yellow-hover:" + h(over(p.ACCENT, p.TEXT, 40)) + ";--yellow-down:" + h(down) + ";--picked:" + h(picked) + ";--e-picked:" + e(picked) + ";";
				css += "--primary-hover:" + h(over(p.PRIMARY, p.TEXT, 40)) + ";";
				css += "--dot:" + a(p.TEXT, 7) + ";--scrim:" + a(p.BG, 72) + ";--veil-sheet:" + a(p.BG, VEIL_SHEET) + ";";
				// (style.css's own lines: a key cap's lip, a track's inner line, tiles' and placeholders' lines, a grip, a tip's edge)
				auto to = [&](Rgb c, int percent) { return h(edgeOf(c, p.TEXT, percent)); };
				css += "--l-cream:" + l(p.TEXT) + ";--in-track:" + to(p.TRACK, 8) + ";--line:" + to(p.BUTTON, 10) + ";--l-track:" + l(p.TRACK) +
					";--grip:" + to(p.BUTTON, 25) + ";--tip-edge:" + to(p.SURFACE, 16) + ";";
				auto trio = [](Rgb c) { return std::to_string(c.r) + ", " + std::to_string(c.g) + ", " + std::to_string(c.b); };
				css += "--accent-rgb:" + trio(p.ACCENT) + ";--text-rgb:" + trio(p.TEXT) + ";--surface-rgb:" + trio(p.SURFACE) + ";--bg-rgb:" + trio(p.BG) + ";";
				css += "--top-card:" + h(p.TOP_CARD) + ";--top-title:" + h(p.TOP_TITLE) + ";--top-text:" + h(p.TOP_TEXT) + ";--top-sub:" + h(p.TOP_SUB) + "}\n";
			}
			return css;
		}
	}
}
