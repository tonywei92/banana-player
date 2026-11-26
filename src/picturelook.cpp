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

	// ---- Scanlines: a CRT's beam (written for this app; the technique is in docs/notes/2026-11-26-picture-looks-gpu.md)

	// the beam's height (its Gaussian's sigma, in lines) for a dark and for a bright pixel: a bright beam is wider and
	// fills the gap between lines, a dark one leaves a strong dark line
	constexpr double BEAM_DARK = 0.17, BEAM_BRIGHT = 0.30;

	// the beam's weight at `d` lines from its centre, `depth` of the way from flat (1) to the Gaussian
	static double beamWeight(double d, double sigma, double depth)
	{
		return 1 - depth + depth * std::exp(-0.5 * (d / sigma) * (d / sigma));
	}

	// The panel is cut into lines `unit` pixels tall (panel height / 240: about 240 lines on either panel). Each pixel
	// takes its line's colour (sampled at the line's centre row, softened across by two side taps), in linear light;
	// the beam's Gaussian across the line, wider where the colour is brighter; a light aperture grille (R, G, B
	// columns); a gain that gives back what the beam and the mask took from a mid grey; gamma again.
	static std::string crtBody(double k, int unit)
	{
		const int pitch = std::max(2, unit);
		const double depth = 0.45 + 0.55 * k, mask = 0.30 * k;
		// what a mid-grey line keeps on average (its rows' weights) and what the grille keeps: the gain is the inverse
		double rows = 0;
		const double centre = std::floor((pitch - 1) / 2.0) + 0.5;
		for (int r = 0; r < pitch; r++)
			rows += beamWeight((r + 0.5 - centre) / pitch, (BEAM_DARK + BEAM_BRIGHT) / 2, depth);
		rows /= pitch;
		const double gain = std::min(1.8, 1 / (rows * (1 - mask * 2 / 3)));
		char b[2000];
		snprintf(b, sizeof b,
			"\tvec2 fc = floor(gl_FragCoord.xy);\n"
			"\tfloat line = floor(fc.y / %d.0);\n"
			"\tfloat centre = line * %d.0 + %.1f;\n"
			"\tfloat d = (fc.y + 0.5 - centre) / %d.0;\n"
			"\tvec2 pos = HOOKED_pos + vec2(0.0, (centre - fc.y - 0.5) * HOOKED_pt.y);\n"
			"\tvec3 c = 0.5 * HOOKED_tex(pos).rgb + 0.25 * (HOOKED_tex(pos - vec2(0.7 * HOOKED_pt.x, 0.0)).rgb\n"
			"\t\t+ HOOKED_tex(pos + vec2(0.7 * HOOKED_pt.x, 0.0)).rgb);\n"
			"\tc = pow(max(c, 0.0), vec3(2.2));\n"
			"\tfloat bright = max(max(c.r, c.g), c.b);\n"
			"\tfloat sigma = mix(%.3f, %.3f, sqrt(bright));\n"
			"\tfloat beam = %.3f + %.3f * exp(-0.5 * (d / sigma) * (d / sigma));\n"
			"\tfloat col = mod(fc.x, 3.0);\n"
			"\tvec3 grille = vec3(%.3f);\n"
			"\tif (col < 1.0) grille.r = 1.0; else if (col < 2.0) grille.g = 1.0; else grille.b = 1.0;\n"
			"\tc = c * beam * grille * %.3f;\n"
			"\treturn vec4(pow(min(c, 1.0), vec3(1.0 / 2.2)), 1.0);\n",
			pitch, pitch, centre, pitch, BEAM_DARK, BEAM_BRIGHT, 1 - depth, depth, 1 - mask, gain);
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
		case LOOK_SCANLINES: return crtBody(k, unit);
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
		snprintf(head, sizeof head, "//!HOOK OUTPUT\n//!BIND HOOKED\n//!DESC Banana Player %s %d%%\n\nvec4 hook()\n{\n",
			pictureLookId(look), strength);
		return head + body + "}\n";
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
