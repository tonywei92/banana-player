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
		// lines `unit` tall, as far apart
		case LOOK_SCANLINES:
			snprintf(b, sizeof b,
				"\tvec4 c = HOOKED_tex(HOOKED_pos);\n"
				"\tif (mod(floor(gl_FragCoord.y), %d.0) < %d.0) c.rgb *= %.3f;\n"
				"\treturn c;\n",
				2 * unit, unit, 1 - 0.6 * k);
			return b;
		// a cell two units square, its edge one panel pixel: the handheld's own pixels, larger
		case LOOK_LCD:
			snprintf(b, sizeof b,
				"\tvec4 c = HOOKED_tex(HOOKED_pos);\n"
				"\tvec2 p = mod(floor(gl_FragCoord.xy), %d.0);\n"
				"\tif (p.x < 1.0 || p.y < 1.0) c.rgb *= %.3f;\n"
				"\treturn c;\n",
				2 * unit, 1 - 0.5 * k);
			return b;
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
