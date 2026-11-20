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
	constexpr int BLUR_RADIUS = 30;	// 61 taps each way: sigma 10 at this size (40 units on the panel)
	constexpr size_t PIXELS = (size_t)W * H;
	constexpr size_t CACHE_CAP = 16u * 1024u * 1024u;	// at most this much in artbg/ (about 270 files)
	constexpr uint8_t CACHE_VERSION = 2;	// (2: the 5th-percentile luminances joined the header)

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
	// The backdrop as one page of RGBA (w x h, straight alpha — opaque), a panel drawing it stretched: the
	// picture with the veil baked in per row, the theme's part applied to the art's own bytes never (the fallback
	// a gradient page, a light theme's the one colour of its plain ground). The bottom panel draws the veil live
	// instead (Kit::backdrop); this is the top panel's way, composed again only when the theme or the answer moves
	std::vector<uint8_t> composePage(const Art& art, const Backdrop& b, int w, int h);

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
