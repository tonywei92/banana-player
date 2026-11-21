#pragma once
// Match album art's pure half (docs/redesign/components.md, "Match album art"): the art's backdrop and the cache file
// it is kept in. No window, no drawing: the art worker calls artBackdrop once per new art, main asks backdropFor for
// the theme's ground, stops and fallback, and the cache is plain bytes in `<state dir>/artbg/<fnv64>.bg`. Tests:
// tests/core_test.cpp (testArtBackdrop, testArtBackdropCache, testArtBackdropTheme).
//
// The pipeline is fixed: a 32 x 32 copy median-cut into 8 colours; near-black, near-white, extreme and rare colours
// dropped (and a grey has no hue to give); the largest one's hue at S 15% / L 13%, blended 45% into the theme's
// charcoal. The picture is the art cropped to 4:3, box-filtered to 160 x 120, Gaussian-blurred at sigma 10 (61 taps,
// edges clamped) and its saturation taken to 55%. The theme is not part of the art's key: backdropFor works the
// ground, the five stops and the accent clash out from the hue and the four luminances. A light-bodied theme (its
// ground lighter than its text: Light, Sakura, Gundam, the 16-bit ones) takes the light version — the same picture
// lifted onto the theme's paper, the veil whitened until its dark text holds — and its fallback is the plain ground.
#include "theme.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace artbg
{
	using Rgb = bp::theme::Rgb;
	constexpr int W = 160, H = 120;	// the backdrop picture's working size
	constexpr int BLUR_RADIUS = 27;	// 55 taps each way
	constexpr double BLUR_SIGMA = 12.0;	// at this size
	constexpr double SATURATION = 1.03;	// the blurred picture's saturation, times
	constexpr size_t PIXELS = (size_t)W * H;
	constexpr size_t CACHE_CAP = 16u * 1024u * 1024u;	// at most this much in artbg/ (about 270 files)
	constexpr uint8_t CACHE_VERSION = 4;	// (2: the 5th-percentile luminances joined the header; 3: the bands are the tall canvas's; 4: the tuned percentiles, columns and bands)
	// The two panels stacked are one tall canvas (640 x 960 units, 2:3): the picture's middle 80 columns (2:3 of its
	// 120 rows) run down both, the top panel showing its upper half and the bottom panel its lower half, as one
	// picture behind both. The luminance bands are the canvas's: above the bottom panel's y 168 (rows 0..80 of the
	// picture: the whole top panel and the bottom's head) and below it
	constexpr int CANVAS_X0 = 33, CANVAS_W = 96;
	constexpr int BAND_ROW = 112;	// tuned by eye (was (480 + 168) * H / 960 = 81)
	// the hue pick: a median-cut colour is dropped outside these (near-black / near-white, neon / grey) or when rare
	constexpr double HUE_L_MIN = 0.10, HUE_L_MAX = 0.90, HUE_S_MIN = 0.10, HUE_S_MAX = 0.85, HUE_SHARE_MIN = 0.05;
	// the dark ground: the charcoal (percent of SURFACE in BG), the art's hue tint (S, L, percent blended in) and the
	// fallback gradient's top (percent of SURFACE in the charcoal)
	constexpr int CHARCOAL_MIX = 50, DARK_TINT_MIX = 54, FALLBACK_TOP_MIX = 24;
	constexpr double DARK_TINT_S = 0.15, DARK_TINT_L = 0.0;
	// the light ground: the paper carries the art's hue at S / L, blended in at LIGHT_TINT_MIX percent
	constexpr double LIGHT_TINT_S = 0.41, LIGHT_TINT_L = 0.57;
	constexpr int LIGHT_TINT_MIX = 42;
	// the contrast protection: the veil's cap (percent), the ratios text, dim text and the accent must hold, the
	// percentiles of the picture's luminance protected against, and the accent clash's thresholds
	constexpr int VEIL_CAP = 91;
	constexpr double CONTRAST_TEXT = 4.1, CONTRAST_DIM = 3.4, CONTRAST_ACCENT = 3.7;
	constexpr double PERCENTILE_HI = 0.76, PERCENTILE_LO = 0.31;
	constexpr double CLASH_ACCENT_S_MIN = 0.30;
	constexpr int CLASH_HUE_GAP = 150;

	// What the art gives: one hue (or none), the picture's 95th- and 5th-percentile luminances over the two bands of
	// the panel (relative luminance x 255: the rows above y 168 and those below — the 95th says how bright a dark
	// theme's veil must be to keep its light text legible, the 5th how white a light theme's must be), and its
	// 160 x 120 RGB bytes.
	struct Art
	{
		bool hasHue = false;
		int hue = 0;	// 0..359
		int l95top = 0, l95low = 0;	// 0..255
		int l5top = 0, l5low = 0;	// 0..255, never above the matching l95
		std::vector<uint8_t> rgb;	// W * H * 3, top row first
	};

	// The theme's roles the backdrop may use. A caller fills this from theme.h (the current palette).
	struct ThemeColours
	{
		Rgb bg, surface, text, dimOn, accent;
	};

	// One stop of the veil over the picture: `veil` is the ground's opacity in percent (0..96). The renderer draws
	// the picture, then each stop's ground at its veil, or the fallback's top-to-bottom gradient instead (a light
	// theme's fallback is its plain BG: the gradient's two ends are the same colour, so it draws one fill).
	struct Backdrop
	{
		bool fallback = true;
		bool lift = false;	// a light theme's: the picture lifted to a pastel (liftedArt) before the veil
		Rgb ground;		// the tinted charcoal (or paper), the plain one for the fallback
		Rgb stop[5];		// the veil's colour at each stop (always `ground`)
		int veil[5] = {0, 0, 0, 0, 0};
		Rgb top, bottom;	// the fallback's gradient ends (the charcoal 35% towards SURFACE, then BG)
	};

	// the stops' places down the panel, in its 640 x 480 units (components.md "Match album art": 58% at y 0, 52% at
	// y 120, 74% at y 168, 86% at y 330, 92% at y 480); the renderer scales them to the Rect it draws
	constexpr int STOP_Y[5] = {0, 120, 168, 330, 480};
	// the veil's opacity at each stop before the contrast protection raises it (percent): a step under the
	// mock-up's 66 / 60 / 80 / 90 / 94 (2026-11-20, the owner: the art a little more visible), most at the top where
	// the art shows most; the protection still deepens a stop until the theme's text holds 4.5:1
	constexpr int VEIL_BASE[5] = {58, 52, 74, 86, 92};
	// the browser's pages (Home, Files, the sections, Settings…): their rows and cards have surfaces of their own, so
	// the veil starts lighter and even, raised only until TEXT holds 4.5:1 (DIM_ON 3:1) over the picture
	constexpr int LIST_VEIL_BASE[5] = {45, 44, 50, 54, 60};

	// The art's encoded bytes, RGBA (the alpha is ignored: an art is opaque), w x h. A tiny or empty image gives no
	// hue and an empty picture (the fallback).
	Art artBackdrop(const uint8_t* rgba, int w, int h);

	// The theme's part: `hue` -1 for none (the art gave nothing); the four luminances of the Art (the 5th-percentile
	// ones are what a light theme protects its text against). A light-bodied theme — its BG lighter than its TEXT
	// (Light, Sakura, Gundam, the 16-bit ones) — takes the light version: the paper (half way BG to SURFACE) with the
	// hue at S 22% / L 86% blended in at 45%, its veil scaled until the picture's 5th-percentile luminance under it
	// leaves the theme's TEXT, DIM_ON and ACCENT at 4.5:1 (a dark art is whitened, as a bright one is darkened in a
	// dark theme), and the fallback the theme's plain BG. The dark theme's fallback is the neutral gradient, when
	// there is no hue, when the veil would have to be over 96% at every stop, or when the art's hue is 150..210
	// degrees from a theme accent with a colour of its own.
	Backdrop backdropFor(const ThemeColours& theme, int hue, int l95top, int l95low, int l5top, int l5low);
	Backdrop backdropFor(const ThemeColours& theme, const Art& art);
	Backdrop backdropForList(const ThemeColours& theme, const Art& art);	// (the browser's: LIST_VEIL_BASE)
	// A light theme lifts the art instead of burying it under a white veil: each pixel's lightness (HSL) mapped into
	// LIFT_L0..LIFT_L1 and its saturation kept at LIFT_S, a pastel of the art, so a dark art needs a light veil, not a
	// 96% one. The Art overloads above work the light theme's luminances out on the lifted picture
	constexpr double LIFT_L0 = 0.73, LIFT_L1 = 0.90, LIFT_S = 0.96;
	// the lifted picture's veil before the check raises it: low, as the pastel is light already (VEIL_BASE's
	// 74..92 under the bar is what turned a light theme's page white)
	constexpr int LIFT_VEIL_BASE[5] = {39, 44, 68, 61, 68};
	std::vector<uint8_t> liftedArt(const Art& art);	// the picture, lifted (W * H * 3; empty with no picture)
	// The backdrop as one page of RGBA (w x h, straight alpha — opaque), a panel drawing it stretched: the
	// picture with the veil baked in per row, the theme's part applied to the art's own bytes never (the fallback
	// a gradient page, a light theme's the one colour of its plain ground). The bottom panel draws the veil live
	// instead (Kit::backdrop); this is the top panel's way, composed again only when the theme or the answer moves
	// Which part of the backdrop a page is: Whole (the 4:3 picture on one panel, the veil's stops down it: the
	// gallery's), or a half of the tall canvas — Upper (the top panel: the veil flat at the first stop, which the
	// protection set for the top band) and Lower (the bottom panel: the five stops down it, as the text needs)
	enum class Half { Whole, Upper, Lower };
	std::vector<uint8_t> composePage(const Art& art, const Backdrop& b, int w, int h, Half half = Half::Whole);

	// ---- the cache: <state dir>/artbg/<fnv64>.bg, a 7-byte header then W * H * 3 RGB bytes ----
	// The header: version (2), the hue as two little-endian bytes (0xFFFF: none), then the four luminances.
	uint64_t fnv64(const uint8_t* data, size_t n);
	std::string cacheName(uint64_t hash);	// "0123456789abcdef.bg"
	std::string cachePath(uint64_t hash);	// "artbg/0123456789abcdef.bg"
	std::vector<uint8_t> encode(const Art& art);	// empty when the art's picture is not W * H (never written)
	bool decode(const uint8_t* data, size_t n, Art& art);	// false: a wrong size, version, hue or luminances

	// The cache's oldest-first eviction: what to delete so the rest fits `cap`. `used` is the file's date (ms, the
	// time it was last read or written): the oldest go first.
	struct CacheFile
	{
		std::string name;
		size_t bytes = 0;
		uint64_t used = 0;
	};
	std::vector<size_t> evict(const std::vector<CacheFile>& files, size_t cap = CACHE_CAP);
}
