#include "viz_more.h"
#include "settings.h"

namespace bp
{
	// Heartbeat: a heart monitor. One red-pink line (#FF5A6E) scrolls right to left over a near-black screen (#05080A)
	// with a faint red grid (32 px squares), flat between beats and spiking up then down on each one, brightest at
	// the right (the newest) and fading toward the left.
	//
	// The beat is the bass's loudness against its own average, so it reads the same on a quiet or a loud song: the
	// sound brought down to 720 samples a second (the bass is all under 360 Hz, and every later step then costs next
	// to nothing), lowpassed at 150 Hz (the kick), squared (its energy; amultiply, no expression), smoothed at 24 Hz
	// three times (the envelope; fewer left the squared bass's ripple on the spikes) and at 0.4 Hz (its average over
	// a few seconds). Their ratio, less 1.1, to the 4th power and softly capped, is near 0 except on a hit, a short
	// pulse there; a highpass at 5 Hz (Q 0.5) turns each pulse into a spike up, a deeper one down and a small bump
	// back: a heartbeat's QRS and T. (aeval's output goes through pan before the biquad: a biquad right after aeval
	// left the sound untouched, its channel layout read wrong. The song's first second, before the average has
	// risen, draws a beat or two of full height.)
	//
	// The drawing: showwaves draws only 3 columns a frame (8 samples each: 720 / 30 / 3) at half size, in gray (it
	// draws gray itself, nothing converted), and tile keeps the last 107 of those pieces side by side, one frame a
	// frame (overlap 106; 321 columns, the oldest cut off): the line scrolls 3 columns (6 pixels) a frame, the screen
	// in 3.6 s. Alone: one lut2 at half size against a still (each column's fade and the grid) gives the luma, two
	// luts read the chroma from it, and only the luma is scaled 2x (nearest). Over the art, as Peaks: the line times
	// a still fade (lut2, half size), then each plane is the art's mixed toward the line's colour by that (lut2 each;
	// the luma at 640x200). YUV all the way, every still one frame. Per frame: the 720 Hz sound chain, showwaves (3
	// columns), tile (a 320 x H copy, 107 pieces), 1-2 half-size lut2s and two luts or lut2s, one 2x nearest scale of
	// a gray plane (and over the art one 640x200 lut2), mergeplanes
	namespace
	{
		std::string n(int v) { return std::to_string(v); }
	}

	std::string heartGraph(bool overCover, const std::string&)
	{
		const int W = 320, H = overCover ? 100 : 240;	// the drawing, at half size
		const int K = 3, TILES = W / K + 1;	// columns a frame; pieces on screen (321 columns, the oldest cut off)
		// BT.601 limited (mpv's reading of a picture this size): the line #FF5A6E
		const int LY = 138, LU = 112, LV = 199;
		const std::string mono = "aformat=channel_layouts=mono";
		std::string g = "[aid1] asplit [ao] [a]; ";
		// the beat (see above)
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,aresample=720,lowpass=f=150,"
			 "asplit [hbx1] [hbx2]; [hbx1] [hbx2] amultiply,lowpass=f=24,lowpass=f=24,lowpass=f=24,asplit [hbe0] [hbs0]; "
			 "[hbs0] lowpass=f=0.4," + mono + " [hbs]; [hbe0] " + mono + " [hbe]; "
			 "[hbe] [hbs] amerge=inputs=2,aeval='2.4*(1-exp(-1.5*pow(max(0,val(0)/(val(1)+0.002)-1.1),4)))':c=mono,pan=mono|c0=c0,"
			 "highpass=f=5:t=q:w=0.5,"
			 "showwaves=s=" + n(K) + "x" + n(H) + ":mode=p2p:rate=30:colors=white:draw=full,format=gray,"
			 "tile=layout=" + n(TILES) + "x1:overlap=" + n(TILES - 1) + ":init_padding=" + n(TILES - 1) + ",crop=" + n(W) + ":" + n(H) + ":" + n(TILES * K - W) + ":0 [hbl]; ";
		// the fade, as a fraction of the line's colour by column: all of it at the right edge, about a seventh at the
		// left (over the art a third: a faint line is lost in a busy cover)
		const std::string low = overCover ? "0.33" : "0.14";
		const std::string fade = "(" + low + "+(1-" + low + ")*pow(X/" + n(W - 1) + ",1.6))";
		// (A dark halo round the line, by dilation at half size, cost more than all the rest of the drawing: 0.6 ms a
		// frame on a Mac; there's none)
		if (!overCover)
		{
			// Alone, the luma is worked out at half size (one lut2) and the chroma read from it (a lut each): one
			// still says, for each pixel, its column's fade (0-127) and whether the grid runs through it (+128). The
			// grid every 16 pixels here is every 32 at full size, its lines 2 pixels wide. The luma: the line's, by
			// its column's fade, where the line is; else the screen #05080A (22) or the grid #2A0E12 (33). A faded
			// line is at least 38, so a luma of 22 or 33 is the screen or the grid, and above that the line's colour
			// mixed with the screen's by as much as its luma says
			const int SY = 22, GY = 33;
			const std::string bg = "if(gte(y,128)," + n(GY) + "," + n(SY) + ")";
			auto chroma = [&](int line, int screen, int grid) {
				return "'if(lte(val," + n(GY) + "),if(eq(val," + n(GY) + ")," + n(grid) + "," + n(screen) + ")," + n(screen) + "+(" +
					   n(line - screen) + ")*(val-" + n(SY) + ")/" + n(LY - SY) + ")'";
			};
			g += "color=black:s=" + n(W) + "x" + n(H) + ":r=1:d=1,format=gray,geq=lum='round(127*" + fade +
				 ")+128*gte(eq(mod(X,16),0)+eq(mod(Y,16),0),1)' [hbc]; "
				 "[hbl] [hbc] lut2=c0='" + bg + "+(" + n(LY) + "-" + bg + ")*x/255*mod(y,128)/127',split=3 [hby0] [hby1] [hby2]; "
				 "[hby0] scale=640:480:flags=neighbor [hbY]; "
				 "[hby1] lut=c0=" + chroma(LU, 129, 126) + " [hbU]; "
				 "[hby2] lut=c0=" + chroma(LV, 127, 138) + " [hbV]; "
				 "[hbY] [hbU] [hbV] mergeplanes=0x001020:yuv420p [vo]";
			return g;
		}
		// Over the art: the line times the fade (lut2, half size), then each plane the art's, mixed toward the line's
		// colour by that; the luma at full size against the art's own (as Peaks over the art)
		auto mix = [](int c) { return "'y+(" + n(c) + "-y)*x/255'"; };
		g += "color=black:s=" + n(W) + "x" + n(H) + ":r=1:d=1,format=gray,geq=lum='255*" + fade + "' [hbf]; "
			 "[hbl] [hbf] lut2=c0='x*y/255',split=3 [hbm0] [hbm1] [hbm2]; "
			 "[hbm0] scale=640:" + n(2 * H) + ":flags=neighbor [hbmy]; "
			 "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
			 "settb=AVTB,split [hbart1] [hbart2]; [hbart1] crop=640:280:0:0 [top]; "
			 "[hbart2] crop=640:200:0:280,extractplanes=y+u+v [hbby] [hbbu] [hbbv]; "
			 "[hbmy] [hbby] lut2=c0=" + mix(LY) + " [hbY]; "
			 "[hbm1] [hbbu] lut2=c0=" + mix(LU) + " [hbU]; "
			 "[hbm2] [hbbv] lut2=c0=" + mix(LV) + " [hbV]; "
			 "[hbY] [hbU] [hbV] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		return g;
	}

	bool writeHeartMaps(const std::string&)
	{
		return true;	// no maps: its stills are made in the graph, one frame each
	}
}
