#include "viz_more.h"
#include "settings.h"
#include <algorithm>
#include <cmath>

// Phosphor oscilloscope: an old green lab scope. The sound's waveform as a bright green trace across a near-black
// screen with a faint graticule (10 divisions across, 8 down, the centre axes ticked), the last frames' traces
// fading behind it as a phosphor's afterglow.
//
// Everything that changes each frame is drawn at half size (320x240; 320x100 over the art), in gray, as an
// intensity: showwaves draws straight into gray (no RGBA, nothing converted), the line blurred (a quarter-size copy
// scaled back up, bilinear) and a lut2 of the two give it a bright core and a soft glow, lagfun keeps max(now, last x decay) per pixel (the afterglow), and a lut2 against
// a still graticule (one frame, repeated: lut2's second input) keeps whichever is brighter. Half size is the
// chroma's size at full size, so U and V are a lut each of that picture as it is; only the luma is scaled up (x2,
// bilinear: the soft edge of a real trace). No RGB anywhere: YUV straight to mpv.
// Over the art: no screen or graticule of its own. The intensity is the trace's opacity over the art's bottom slice
// (made once, one frame, split into its planes), each plane a lut2 of it and the art's, as Peaks does.
namespace bp
{
	namespace
	{
		struct Yuv { double y, u, v; };

		// BT.601 limited range (mpv's reading of a picture this size)
		Yuv yuv(int r, int g, int b)
		{
			return {16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255, 128 + (-37.797 * r - 74.203 * g + 112.0 * b) / 255,
					128 + (112.0 * r - 93.786 * g - 18.214 * b) / 255};
		}

		std::string num(double v)
		{
			char buf[32];
			snprintf(buf, sizeof buf, "%.2f", v);
			return buf;
		}

		// the intensity scale (0..255): FULL is the trace's own green, above it the core whitens towards CORE's
		const int FULL = 200;
		const Yuv SCREEN = yuv(0x02, 0x0c, 0x04), TRACE = yuv(0x7d, 0xff, 0x9e), CORE = yuv(0xd2, 0xff, 0xdc);

		// one plane's value for an intensity `x`: the screen's at 0, the trace's at FULL, the core's at 255
		std::string ramp(double screen, double trace, double core, const char* x)
		{
			return "'clip(" + num(screen) + "+" + num(trace - screen) + "*min(" + x + "/" + std::to_string(FULL) + ",1)+" +
				   num(core - trace) + "*max(" + x + "-" + std::to_string(FULL) + ",0)/" + std::to_string(255 - FULL) +
				   ",0,255)'";
		}

		// over the art: the art's value (y) where the intensity (x) is 0, the trace's (or core's) where it's FULL
		std::string over(double trace, double core)
		{
			const std::string f = std::to_string(FULL);
			return "'clip(y+(" + num(trace) + "+" + num(core - trace) + "*max(x-" + f + ",0)/" + std::to_string(255 - FULL) +
				   "-y)*min(x/" + f + ",1),0,255)'";
		}
	}

	std::string scopeGraph(bool overCover, const std::string&)
	{
		const int W = 320, H = overCover ? 100 : 240;
		const std::string w = std::to_string(W), h = std::to_string(H);
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [scart1] [scart2]; [scart1] crop=640:280:0:0 [top]; "
				 "[scart2] crop=640:200:0:280,extractplanes=y+u+v [scay] [scau] [scav]; ";
		// The trace: mono, a gentle lowpass (cymbals and hiss would turn the line to fuzz; the shape is the bass and
		// the voices), a little under full scale so peaks stay inside the screen. showwaves spreads one frame's
		// samples (a 30th of a second) over the width and draws a line between them, in gray: 255 on the line.
		// The glow: the line at half this size (area: a 1-pixel line becomes a faint 1-pixel one) scaled back (bilinear:
		// ~3 pixels wide, fading out), doubled and capped below the line's 255. (A 3x3 dilation, a crisper edge, cost
		// twice the rest of the graph together on a Mac: libavfilter's is generic.) Then the afterglow: x0.74 a frame,
		// a trace under half of what it was after ~0.1 s and gone into the screen by ~0.3 s
		const std::string half = std::to_string(W / 2) + ":" + std::to_string(H / 2);
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,lowpass=f=2500,volume=0.8,"
			 "showwaves=s=" + w + "x" + h + ":mode=p2p:rate=30:draw=full,format=gray,split [sct] [scd]; "
			 "[scd] scale=" + half + ":flags=area,scale=" + w + ":" + h + ":flags=bilinear [scD]; "
			 "[sct] [scD] lut2=c0='if(x,255,min(y*2,150))',lagfun=decay=0.74";
		if (overCover)
		{
			const std::string y = over(TRACE.y, CORE.y), u = over(TRACE.u, CORE.u), v = over(TRACE.v, CORE.v);
			return g + ",split=3 [sci0] [sci1] [sci2]; "
					   "[sci0] scale=640:200:flags=bilinear [sci0s]; "
					   "[sci0s] [scay] lut2=c0=" + y + " [scY]; "
					   "[sci1] [scau] lut2=c0=" + u + " [scU]; "
					   "[sci2] [scav] lut2=c0=" + v + " [scV]; "
					   "[scY] [scU] [scV] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		}
		// the graticule (a still, one frame): 10 divisions of 32 across and 8 of 30 down, the edges closed; the
		// centre axes ticked at fifths of a division. Faint: the trace's green at a sixth, the ticks a little more
		const std::string grid = "gt(eq(mod(X,32),0)+eq(X," + std::to_string(W - 1) + ")+eq(mod(Y,30),0)+eq(Y," + std::to_string(H - 1) + "),0)";
		const std::string ticks = "gt(lt(mod(X+0.5,6.4),1)*lte(abs(Y-" + std::to_string(H / 2) + "),2)+lt(mod(Y,6),1)*lte(abs(X-" +
								  std::to_string(W / 2) + "),2),0)";
		g += " [scI]; "
			 "color=black:s=" + w + "x" + h + ":r=1:d=1,format=gray,geq=lum='if(" + ticks + ",44,if(" + grid + ",32,0))' [scG]; "
			 "[scI] [scG] lut2=c0='max(x,y)',split=3 [sci0] [sci1] [sci2]; "
			 "[sci0] lut=c0=" + ramp(SCREEN.y, TRACE.y, CORE.y, "val") + ",scale=640:480:flags=bilinear [scY]; "
			 "[sci1] lut=c0=" + ramp(SCREEN.u, TRACE.u, CORE.u, "val") + " [scU]; "
			 "[sci2] lut=c0=" + ramp(SCREEN.v, TRACE.v, CORE.v, "val") + " [scV]; "
			 "[scY] [scU] [scV] mergeplanes=0x001020:yuv420p [vo]";
		return g;
	}

	bool writeScopeMaps(const std::string&)
	{
		return true;	// none: the graticule is made in the graph, once
	}
}
