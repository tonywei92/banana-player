#include "picturelook.h"
#include "settings.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

	// ---- Scan lines, Scanlines' and S-Video's (written for this app, after the technique of crt-easymode, the shader
	// Batocera's "scanlines" set uses; that shader is GPL, so none of its code is here: docs/notes/2026-11-26-picture-looks-gpu.md)

	// The lines as rows of the panel: every `pitch` rows one line, its last row the gap. Each row's weight is
	// crt-easymode's beam, 1 - depth * sin(pi d)^3 at d, the row's distance from its line's centre in lines (0 .. 0.5);
	// the light lost is given back by `gain`, a pixel at a time and never past its brightest channel, so a highlight
	// keeps its dark gap instead of clipping flat
	struct ScanRows { int pitch; std::vector<double> weight; double maskDark, gain; };

	// the gap row's weight at strength k: about 0.77 at 10%, 0.12 at 60%, 0.02 at 100% (in linear light; in the
	// picture's gamma the gap is 10%, 60% and 82% darker than the line)
	static double gapWeight(double k)
	{
		return 0.02 + 0.98 * std::pow(1 - k, 2.5);
	}

	static ScanRows scanRowsFor(int unit, double k, double maskDark)
	{
		ScanRows s;
		s.pitch = std::max(2, unit);	// about 240 lines: the panel's height / 240, whole pixels (2 on 480, 3 on 768)
		s.maskDark = maskDark;
		const double depth = 1 - gapWeight(k);
		double mean = 0;
		for (int r = 0; r < s.pitch; r++)
		{
			// a line's centre half a line before its last row: pitch 2 bright, dark; pitch 3 bright, bright, dark
			double v = (r + 1.0) / s.pitch, d = std::fabs(v - std::floor(v) - 0.5);
			double w = 1 - depth * std::pow(std::sin(M_PI * d), 3.0);
			s.weight.push_back(w);
			mean += w / s.pitch;
		}
		// the grille: on every third column one channel whole, the other two at maskDark
		s.gain = 1 / (mean * (1 + 2 * maskDark) / 3);
		return s;
	}

	// GLSL: `float scan`, this panel row's weight (a choice between the pitch's few rows: no maths per pixel)
	static std::string scanWeightGlsl(const ScanRows& s)
	{
		std::string g = "\tfloat row = mod(floor(gl_FragCoord.y), " + std::to_string(s.pitch) + ".0);\n\tfloat scan = ";
		char w[32];
		for (int r = 0; r + 1 < s.pitch; r++)
		{
			snprintf(w, sizeof w, "row < %d.5 ? %.4f : ", r, s.weight[r]);
			g += w;
		}
		snprintf(w, sizeof w, "%.4f;\n", s.weight.back());
		return g + w;
	}

	// GLSL: `c` (linear light) times the row's weight and the grille (when maskDark < 1), brightened by the gain as far
	// as its brightest channel allows: no min(c, 1) afterwards, so bright film keeps its lines and its colour
	static std::string scanApplyGlsl(const ScanRows& s)
	{
		std::string g = scanWeightGlsl(s);
		char b[400];
		snprintf(b, sizeof b, "\tc *= min(%.3f, 1.0 / max(max(c.r, max(c.g, c.b)), 0.001)) * scan;\n", s.gain);
		g += b;
		if (s.maskDark < 1)
		{
			snprintf(b, sizeof b,
				"\tfloat col = mod(floor(gl_FragCoord.x), 3.0);\n"
				"\tc *= col < 0.5 ? vec3(1.0, %.3f, %.3f) : col < 1.5 ? vec3(%.3f, 1.0, %.3f) : vec3(%.3f, %.3f, 1.0);\n",
				s.maskDark, s.maskDark, s.maskDark, s.maskDark, s.maskDark, s.maskDark);
			g += b;
		}
		return g;
	}

	// Scanlines: each pixel its own colour (no resampling across or down: film detail stays), in linear light, the
	// rows' weights and a light grille (crt-easymode's default mask is 0.3; here 0.25 at 100%)
	static std::string scanlinesBody(double k, int unit)
	{
		return "\tvec3 c = pow(max(HOOKED_tex(HOOKED_pos).rgb, 0.0), vec3(2.2));\n" +
			scanApplyGlsl(scanRowsFor(unit, k, 1 - 0.25 * k)) +
			"\treturn vec4(pow(c, vec3(1.0 / 2.2)), 1.0);\n";
	}

	// ---- S-Video: a TV fed by S-Video (written for this app; the technique is in docs/notes/2026-11-26-picture-looks-gpu.md)

	// Luma and chroma come down separate wires, so there's no dot crawl nor rainbow (that's composite), but the colour
	// has a narrow band: it runs sideways past edges the brightness keeps fairly crisp. One pass on the picture as
	// drawn, in its gamma (as the signal is): YIQ; the luma a little softened across (3 taps, 0.5 .. 1 px apart, mixed
	// by Strength); I and Q a 1-6-15-20-15-6-1 blur over 7 taps, its spread 0.25% .. 1.2% of the width (an edge's
	// colour runs about 0.6% .. 3% of the width); I and Q turned up to 3 degrees and 5% richer (a set's warm tint);
	// back to RGB; then, in linear light, the scan lines a little lighter than Scanlines' and with no grille.
	static std::string svideoBody(double k, int unit)
	{
		const double spread = 0.0025 + 0.0095 * k;	// the chroma blur's standard deviation, as a part of the width
		const double step = spread / std::sqrt(1.5);	// its taps' spacing (the 7-tap binomial's deviation is sqrt(1.5) taps)
		const double lumaGap = 0.5 + 0.5 * k, soften = 0.25 + 0.5 * k;
		const double turn = 3 * k * M_PI / 180, rich = 1 + 0.05 * k;
		char b[2400];
		snprintf(b, sizeof b,
			"\tvec2 dc = vec2(%.5f, 0.0), dl = vec2(%.2f * HOOKED_pt.x, 0.0);\n"
			"\tvec3 c0 = HOOKED_tex(HOOKED_pos).rgb;\n"
			"\tvec3 chroma = (20.0 * c0 + 15.0 * (HOOKED_tex(HOOKED_pos - dc).rgb + HOOKED_tex(HOOKED_pos + dc).rgb)\n"
			"\t\t+ 6.0 * (HOOKED_tex(HOOKED_pos - 2.0 * dc).rgb + HOOKED_tex(HOOKED_pos + 2.0 * dc).rgb)\n"
			"\t\t+ HOOKED_tex(HOOKED_pos - 3.0 * dc).rgb + HOOKED_tex(HOOKED_pos + 3.0 * dc).rgb) / 64.0;\n"
			"\tvec3 luma = 0.5 * c0 + 0.25 * (HOOKED_tex(HOOKED_pos - dl).rgb + HOOKED_tex(HOOKED_pos + dl).rgb);\n"
			"\tfloat y = dot(mix(c0, luma, %.3f), vec3(0.299, 0.587, 0.114));\n"
			"\tvec2 iq = vec2(dot(chroma, vec3(0.596, -0.274, -0.322)), dot(chroma, vec3(0.211, -0.523, 0.312)));\n"
			"\tiq = %.4f * vec2(%.4f * iq.x - %.4f * iq.y, %.4f * iq.x + %.4f * iq.y);\n"
			"\tvec3 c = vec3(y + 0.956 * iq.x + 0.621 * iq.y, y - 0.272 * iq.x - 0.647 * iq.y, y - 1.106 * iq.x + 1.703 * iq.y);\n"
			"\tc = pow(clamp(c, 0.0, 1.0), vec3(2.2));\n",
			step, lumaGap, soften, rich, std::cos(turn), std::sin(turn), std::sin(turn), std::cos(turn));
		return b + scanApplyGlsl(scanRowsFor(unit, 0.8 * k, 1)) + "\treturn vec4(pow(c, vec3(1.0 / 2.2)), 1.0);\n";
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
		case LOOK_SCANLINES: return scanlinesBody(k, unit);
		case LOOK_SVIDEO: return svideoBody(k, unit);
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
		return std::string(head) + "vec4 hook()\n{\n" + body + "}\n";
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
