#pragma once
#include <string>

namespace bp
{
	// A video's picture look (Settings' PictureLook: Off, Sharpen, Soften, Scanlines, LCD grid, Retro) made into mpv's
	// `vf` (docs/redesign "video-picture"). Each is one cheap software pass (decoding is software only on the
	// handheld). Strength runs 10..100% in steps of 10, kept per look.
	constexpr int LOOK_STRENGTH_MIN = 10, LOOK_STRENGTH_MAX = 100, LOOK_STRENGTH_STEP = 10, LOOK_STRENGTH_DEFAULT = 60;
	int clampLookStrength(int v);	// into 10..100, on a step of 10
	int stepLookStrength(int v, int dir);	// one step up (dir > 0) or down, kept inside 10..100

	// the panel unit a line or a cell is made of, in the top panel's own pixels: 2 on the RG DS (480 lines), 3 on
	// the Plus (768), so the lines land on pixels and don't shimmer
	int lookLineUnit(int panelH);

	// the lavfi graph of a look at a strength for a top panel of panelW x panelH pixels ("" for Off). Scanlines and
	// the LCD grid first scale the film to the size Fit shows it at, so their lines are in the panel's pixels
	std::string lookGraph(int look, int strength, int panelW, int panelH);

	// the whole `vf` value: the graph under the label @look ("" for Off); bypass: the same entry disabled
	// ("@look:!lavfi=..."), what holding Y shows (mpv keeps the entry, only its flag changes)
	std::string lookVf(int look, int strength, int panelW, int panelH, bool bypass = false);
}
