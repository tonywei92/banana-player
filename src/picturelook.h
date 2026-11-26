#pragma once
#include <string>

namespace bp
{
	// A video's picture look (Settings' PictureLook: Off, Sharpen, Soften, Scanlines, LCD grid, Retro) made into an
	// mpv user shader (`glsl-shaders`, docs/notes/2026-11-26-picture-looks-gpu.md). Each is one pass on the GPU at
	// the panel's pixels (mpv's OUTPUT hook): the CPU, which decodes, does nothing per frame. Strength runs 10..100%
	// in steps of 10, kept per look.
	constexpr int LOOK_STRENGTH_MIN = 10, LOOK_STRENGTH_MAX = 100, LOOK_STRENGTH_STEP = 10, LOOK_STRENGTH_DEFAULT = 60;
	int clampLookStrength(int v);	// into 10..100, on a step of 10
	int stepLookStrength(int v, int dir);	// one step up (dir > 0) or down, kept inside 10..100

	// the panel unit a line or a cell is made of, in the top panel's own pixels: 2 on the RG DS (480 lines), 3 on
	// the Plus (768), so the lines land on pixels and don't shimmer
	int lookLineUnit(int panelH);

	// the shader's text for a look at a strength with lines `unit` pixels tall ("" for Off). The strength and the
	// unit are written into the text: the libmpv renderer (vo_gpu's) has no //!PARAM, so one file per
	// (look, strength, unit), named by lookShaderName
	std::string lookShader(int look, int strength, int unit);
	std::string lookShaderName(int look, int strength, int unit);	// "scanlines-60-u2.glsl" ("" for Off)

	// an mpv log line that says it couldn't take a user shader (a parse error, or the GPU's compiler refused it)
	bool isShaderRejection(const char* level, const char* text);
}
