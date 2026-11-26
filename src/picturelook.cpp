#include "picturelook.h"
#include "settings.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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

	// the film scaled to the box Fit draws it in on the panel (its display aspect kept: sar), so a line drawn after
	// is one panel pixel tall. Bilinear: the cheapest of swscale's that doesn't alias; a 720p film is made smaller,
	// which also leaves less to upload. (Fill and Stretch draw it larger: there the lines are close, not exact.)
	static std::string fitScale(int panelW, int panelH)
	{
		char f[64], b[320];
		snprintf(f, sizeof f, "min(%d/(iw*sar),%d/ih)", panelW, panelH);
		snprintf(b, sizeof b, "scale=w='2*trunc(iw*%s/2)':h='2*trunc(ih*%s/2)':flags=bilinear", f, f);
		return b;
	}

	// the look's own pass, on the film as Fit shows it (fitScale)
	static std::string lookPass(int look, double k, int unit)
	{
		char b[512];
		switch (look)
		{
		// unsharp on the luma only (chroma amount 0: its planes are copied, not filtered). Sharpen's 3x3 at the
		// panel's size is a pixel's edge; Soften's 5x5 a blur a little wider. 60% is about the old fixed look
		case LOOK_SHARPEN: snprintf(b, sizeof b, "unsharp=3:3:%.2f:3:3:0", 1.5 * k); return b;
		case LOOK_SOFTEN: snprintf(b, sizeof b, "unsharp=5:5:%.2f:5:5:0", -1.2 * k); return b;
		// lines `unit` tall, as far apart (w=0: across the whole width)
		case LOOK_SCANLINES: snprintf(b, sizeof b, "drawgrid=w=0:h=%d:t=%d:c=black@%.2f", 2 * unit, unit, 0.6 * k); return b;
		// a cell two units square, its edge one panel pixel: the handheld's own pixels, larger
		case LOOK_LCD: snprintf(b, sizeof b, "drawgrid=w=%d:h=%d:t=1:c=black@%.2f", 2 * unit, 2 * unit, 0.5 * k); return b;
		// warm and faded: blacks lifted, contrast and colour lowered, the colour pushed to red and away from blue. A
		// lookup table (built once, per value), in the file's own range (minval..maxval: 8 or 10 bits alike)
		case LOOK_RETRO:
			snprintf(b, sizeof b,
				"lutyuv=y='clip(minval+(val-minval)*%.3f+(maxval-minval)*%.3f,minval,maxval)'"
				":u='clip((maxval+minval)/2+(val-(maxval+minval)/2)*%.3f-(maxval-minval)*%.3f,minval,maxval)'"
				":v='clip((maxval+minval)/2+(val-(maxval+minval)/2)*%.3f+(maxval-minval)*%.3f,minval,maxval)'",
				1 - 0.2 * k, 0.07 * k, 1 - 0.4 * k, 0.03 * k, 1 - 0.4 * k, 0.04 * k);
			return b;
		}
		return "";
	}

	// Every look scales first: the lines then land on the panel's pixels, and a 720p film is filtered at a quarter of
	// its pixels (on a Mac, 720p: the scale 0.3 ms a frame, with a look 0.45..1.3 ms; Sharpen at 720p itself took 3.2)
	std::string lookGraph(int look, int strength, int panelW, int panelH)
	{
		if (panelW <= 0 || panelH <= 0) panelW = 640, panelH = 480;
		std::string pass = lookPass(look, clampLookStrength(strength) / 100.0, lookLineUnit(panelH));
		if (pass.empty()) return "";
		return fitScale(panelW, panelH) + "," + pass;
	}

	std::string lookVf(int look, int strength, int panelW, int panelH, bool bypass)
	{
		std::string g = lookGraph(look, strength, panelW, panelH);
		if (g.empty()) return "";
		return std::string(bypass ? "@look:!" : "@look:") + "lavfi=[" + g + "]";
	}
}
