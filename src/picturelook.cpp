#include "picturelook.h"
#include "settings.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace bp
{
	int clampLookStrength(int v)
	{
		v = (v + LOOK_STRENGTH_STEP / 2) / LOOK_STRENGTH_STEP * LOOK_STRENGTH_STEP;
		return std::clamp(v, LOOK_STRENGTH_MIN, LOOK_STRENGTH_MAX);
	}

	int stepLookStrength(int v, int dir)
	{
		return clampLookStrength(clampLookStrength(v) + (dir > 0 ? LOOK_STRENGTH_STEP : -LOOK_STRENGTH_STEP));
	}

	int lookLineUnit(int panelH)
	{
		return std::max(1, (int)std::lround(panelH / 240.0));
	}

	// ---- Scanlines: Timothy Lottes' public-domain CRT shader (crt-lottes), ported to one mpv OUTPUT hook

	// Lottes' parameters for a Strength share k (0.1..1): how hard a line's edge is (hardScan: -6 soft .. -16 hard, his
	// default -8), how soft a line is across (hardPix, his -3), the aperture grille's dark and light (his 0.5 and 1.5)
	struct Lottes { double hardScan, hardPix, maskDark, maskLight, boost; int pitch; double centre; };

	static Lottes lottesFor(double k, int unit)
	{
		Lottes l;
		l.pitch = std::max(2, unit);	// about 240 emulated lines: the panel's height / 240, whole pixels
		// each line's centre (in panel pixels from its first row's top) so that its last row is half a line away: that
		// row is the gap. (A centre on a row's middle left a 3-pixel line two rows a third of a line off and none in the
		// gap: on the Plus's 1024x768 the lines faded to a 25% ripple)
		l.centre = l.pitch / 2.0 - 0.5;
		l.hardScan = -(6 + 10 * k);
		l.hardPix = -3;
		l.maskDark = 1 - 0.5 * k;
		l.maskLight = 1 + 0.5 * k;
		// the gain: what a flat mid field keeps through the three lines' weights on each of the pitch's rows, and
		// through the grille (one channel light, two dark), given back (at most 2x; a highlight clips, as a CRT blooms)
		double rows = 0;
		for (int r = 0; r < l.pitch; r++)
		{
			double d = -((r + 0.5 - l.centre) / l.pitch);
			for (int off = -1; off <= 1; off++) rows += std::exp2(l.hardScan * (d + off) * (d + off));
		}
		rows /= l.pitch;
		l.boost = std::min(2.0, 1 / (rows * (l.maskLight + 2 * l.maskDark) / 3));
		return l;
	}

	// Functions before hook(): the emulated pixel `cell`'s colour (its centre on the panel, sampled from the picture as
	// drawn, in linear light), and one line's colour three emulated pixels across, Gaussian-weighted
	static std::string lottesPrelude(const Lottes& l)
	{
		char b[1600];
		snprintf(b, sizeof b,
			"// Scanlines after \"PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER\" by Timothy Lottes (crt-lottes):\n"
			"// his scan-line and pixel Gaussians and aperture-grille mask, warp off, three taps a line.\n\n"
			"vec3 bpFetch(vec2 cell)\n"
			"{\n"
			"\tvec2 px = vec2((cell.x + 0.5) * %d.0, cell.y * %d.0 + %.1f);\n"
			"\treturn pow(max(HOOKED_tex(px / HOOKED_size).rgb, 0.0), vec3(2.2));\n"
			"}\n\n"
			"vec3 bpLine(vec2 base, float off, vec3 w)\n"
			"{\n"
			"\treturn (bpFetch(base + vec2(-1.0, off)) * w.x + bpFetch(base + vec2(0.0, off)) * w.y\n"
			"\t\t+ bpFetch(base + vec2(1.0, off)) * w.z) / (w.x + w.y + w.z);\n"
			"}\n\n",
			l.pitch, l.pitch, l.centre);
		return b;
	}

	static std::string lottesBody(const Lottes& l)
	{
		char b[2000];
		snprintf(b, sizeof b,
			"\tvec2 px = HOOKED_pos * HOOKED_size;\n"
			"\tvec2 v = vec2(px.x / %d.0, (px.y - %.1f) / %d.0 + 0.5);\n"
			"\tvec2 base = floor(v);\n"
			"\tvec2 dst = -(v - base - 0.5);\n"
			"\tvec3 wx = exp2(%.3f * vec3((dst.x - 1.0) * (dst.x - 1.0), dst.x * dst.x, (dst.x + 1.0) * (dst.x + 1.0)));\n"
			"\tvec3 wy = exp2(%.3f * vec3((dst.y - 1.0) * (dst.y - 1.0), dst.y * dst.y, (dst.y + 1.0) * (dst.y + 1.0)));\n"
			"\tvec3 c = bpLine(base, -1.0, wx) * wy.x + bpLine(base, 0.0, wx) * wy.y + bpLine(base, 1.0, wx) * wy.z;\n"
			"\tfloat col = fract(floor(gl_FragCoord.x) / 3.0);\n"
			"\tvec3 mask = vec3(%.3f);\n"
			"\tif (col < 0.3) mask.r = %.3f; else if (col < 0.6) mask.g = %.3f; else mask.b = %.3f;\n"
			"\tc *= mask * %.3f;\n"
			"\treturn vec4(pow(min(c, 1.0), vec3(1.0 / 2.2)), 1.0);\n",
			l.pitch, l.centre, l.pitch, l.hardPix, l.hardScan, l.maskDark, l.maskLight, l.maskLight, l.maskLight, l.boost);
		return b;
	}

	// ---- LCD grid: a real LCD's cells (written for this app; the technique is in the note)

	// The panel is cut into square cells (3..6 pixels on the RG DS, wider with Strength; 1.5x on the Plus). Each cell
	// shows one colour, sampled at its centre (the picture snapped to the grid); across it three vertical stripes,
	// R, G and B, each passing mostly its own channel; a thin dark gap under each row of cells and, from 4 pixels up,
	// at each cell's right edge. In linear light, with a gain that gives back what the mask took from a mid grey.
	static std::string lcdBody(double k, int unit)
	{
		const int cell = std::max(3, (int)std::lround(std::max(1, unit) * (1.5 + 1.5 * k)));
		const int gapX = cell >= 4 ? 1 : 0;
		const double leak = 0.30 - 0.15 * k, gap = 0.25 - 0.15 * k;
		// a mid grey's share kept: the stripes (one channel whole, two leaking) over the lit part, the gaps the rest
		const double lit = (double)(cell - gapX) * (cell - 1) / (cell * cell);
		const double kept = lit * (1 + 2 * leak) / 3 + (1 - lit) * gap;
		const double gain = std::min(2.6, 1 / kept);
		char b[2000];
		snprintf(b, sizeof b,
			"\tvec2 fc = floor(gl_FragCoord.xy);\n"
			"\tvec2 cell = floor(fc / %d.0);\n"
			"\tvec2 at = fc - cell * %d.0;\n"
			"\tvec2 pos = HOOKED_pos + ((cell + 0.5) * %d.0 - fc - 0.5) * HOOKED_pt;\n"
			"\tvec3 c = pow(max(HOOKED_tex(pos).rgb, 0.0), vec3(2.2));\n"
			"\tfloat stripe = floor(at.x * 3.0 / %d.0);\n"
			"\tvec3 mask = vec3(%.3f);\n"
			"\tif (stripe < 1.0) mask.r = 1.0; else if (stripe < 2.0) mask.g = 1.0; else mask.b = 1.0;\n"
			"\tif (at.y >= %d.0 || at.x >= %d.0) mask = vec3(%.3f);\n"
			"\tc = c * mask * %.3f;\n"
			"\treturn vec4(pow(min(c, 1.0), vec3(1.0 / 2.2)), 1.0);\n",
			cell, cell, cell, cell - gapX, leak, cell - 1, cell - gapX, gap, gain);
		return b;
	}

	// The body of each look's hook(). GLES 3.0-safe: float maths only (mod, floor, mix), no textureGather nor compute,
	// no precision of its own (mpv's header asks highp, which GLES 3 fragment shaders have). HOOKED is the picture at
	// the size it's drawn on the panel, filtered linearly, so a tap between texels averages them. gl_FragCoord counts
	// the panel's own pixels: lines and cells land on them whatever the film's size.
	static std::string lookBody(int look, double k, int unit)
	{
		char b[1200];
		switch (look)
		{
		// unsharp on the luma: the 3x3 binomial blur from four taps between texels; what the centre has over it, added
		// back (60% about the old fixed look). On the luma only: no coloured fringes
		case LOOK_SHARPEN:
			snprintf(b, sizeof b,
				"\tvec4 c = HOOKED_tex(HOOKED_pos);\n"
				"\tvec3 blur = 0.25 * (HOOKED_texOff(vec2(-0.5, -0.5)).rgb + HOOKED_texOff(vec2(0.5, -0.5)).rgb\n"
				"\t\t+ HOOKED_texOff(vec2(-0.5, 0.5)).rgb + HOOKED_texOff(vec2(0.5, 0.5)).rgb);\n"
				"\tfloat d = dot(c.rgb - blur, vec3(0.299, 0.587, 0.114));\n"
				"\tc.rgb = clamp(c.rgb + %.3f * d, 0.0, 1.0);\n"
				"\treturn c;\n",
				1.5 * k);
			return b;
		// a blur about 5x5 wide: four taps, each the mean of a 2x2 block, in a pinwheel round the centre (16 texels),
		// with the centre itself; the picture moved toward it
		case LOOK_SOFTEN:
			snprintf(b, sizeof b,
				"\tvec4 c = HOOKED_tex(HOOKED_pos);\n"
				"\tvec3 blur = 0.2 * (c.rgb + HOOKED_texOff(vec2(1.5, 0.5)).rgb + HOOKED_texOff(vec2(-0.5, 1.5)).rgb\n"
				"\t\t+ HOOKED_texOff(vec2(-1.5, -0.5)).rgb + HOOKED_texOff(vec2(0.5, -1.5)).rgb);\n"
				"\tc.rgb = mix(c.rgb, blur, %.3f);\n"
				"\treturn c;\n",
				0.9 * k);
			return b;
		case LOOK_SCANLINES: return lottesBody(lottesFor(k, unit));
		case LOOK_LCD: return lcdBody(k, unit);
		// warm and faded, in YCbCr (BT.601): blacks lifted, contrast and colour lowered, the colour pushed to red and
		// away from blue (the old lutyuv's numbers)
		case LOOK_RETRO:
			snprintf(b, sizeof b,
				"\tvec4 c = HOOKED_tex(HOOKED_pos);\n"
				"\tfloat y = dot(c.rgb, vec3(0.299, 0.587, 0.114));\n"
				"\tfloat cb = (c.b - y) * 0.564, cr = (c.r - y) * 0.713;\n"
				"\ty = y * %.3f + %.3f;\n"
				"\tcb = cb * %.3f - %.3f;\n"
				"\tcr = cr * %.3f + %.3f;\n"
				"\tc.rgb = clamp(vec3(y + 1.403 * cr, y - 0.344 * cb - 0.714 * cr, y + 1.773 * cb), 0.0, 1.0);\n"
				"\treturn c;\n",
				1 - 0.2 * k, 0.07 * k, 1 - 0.4 * k, 0.03 * k, 1 - 0.4 * k, 0.04 * k);
			return b;
		}
		return "";
	}

	std::string lookShader(int look, int strength, int unit)
	{
		strength = clampLookStrength(strength);
		std::string body = lookBody(look, strength / 100.0, std::max(1, unit));
		if (body.empty()) return "";
		char head[160];
		snprintf(head, sizeof head, "//!HOOK OUTPUT\n//!BIND HOOKED\n//!DESC Banana Player %s %d%%\n\n", pictureLookId(look), strength);
		// (Scanlines' helper functions go before hook())
		std::string prelude = look == LOOK_SCANLINES ? lottesPrelude(lottesFor(strength / 100.0, std::max(1, unit))) : "";
		return head + prelude + "vec4 hook()\n{\n" + body + "}\n";
	}

	std::string lookShaderName(int look, int strength, int unit)
	{
		if (look <= LOOK_OFF || look >= PICTURE_LOOKS) return "";
		return std::string(pictureLookId(look)) + "-" + std::to_string(clampLookStrength(strength)) + "-u" +
			std::to_string(std::max(1, unit)) + ".glsl";
	}

	// a parse error (user_shaders.c: "Unrecognized command", "Error while parsing ...") or a compile log at error
	// level (ra_gl.c: "fragment shader compile log (status=0)"); warnings are other things
	bool isShaderRejection(const char* level, const char* text)
	{
		if (!level || !text || (strcmp(level, "error") && strcmp(level, "fatal"))) return false;
		return strstr(text, "shader") || strstr(text, "Unrecognized command") || strstr(text, "Error while parsing");
	}
}
