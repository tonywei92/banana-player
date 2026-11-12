#include "viz_more.h"
#include "settings.h"

#include <cmath>

// Hi-fi LED meter: an old stereo's graphic equalizer display. 16 columns of 14 LED segments on near-black, lit from the
// bottom: green, then yellow, then red for the top two; the unlit ones faintly there (their colour, dimmed). Each
// column's level eases as an LED meter's (up at once, down over ~0.4 s), and one lit segment, the peak, holds at its
// highest point and falls back slowly, as Peaks' caps.
//
// Made as Pocket console's bars are, but classified at the segments' own size: showfreqs only measures (16 values a
// frame), lagfun eases them in an exp/log domain (the level and the peak), and the two rows of numbers stretched to a
// 16x14 picture (one pixel a segment) are told, by two lut2 against a still "segment index" picture, what each segment
// is: unlit, lit or the peak, and its colour band. That 16x14 picture is scaled (nearest) to the segments' grid at
// half size, and a still mask (one frame) puts the gaps and margins in; a lut each makes the luma (scaled x2, nearest)
// and the two chroma planes at their own size. YUV only, nothing converted, no per-frame geq.
// Over the art: the art's top as it is, and under it the meter in its 640x200 strip with no unlit segments, no
// background: the art's planes are each a lut2's second input, kept wherever nothing is lit (as Peaks).

namespace bp
{
	namespace
	{
		struct Yuv { int y, u, v; };

		// an sRGB colour in BT.601 limited YUV (mpv's reading of a picture this size)
		Yuv yuv(int rgb)
		{
			double r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
			return {(int)std::lround(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255),
					(int)std::lround(128 + (-37.797 * r - 74.203 * g + 112 * b) / 255),
					(int)std::lround(128 + (112 * r - 93.786 * g - 18.214 * b) / 255)};
		}

		std::string n(int v) { return std::to_string(v); }

		// a lut expression giving each class (0 the background, 1-3 unlit green/yellow/red, 4-6 lit) its value;
		// over the art (art: the plane's own value, y) the background and the unlit ones are the art's
		std::string classLut(const char* var, const int* v, bool art)
		{
			std::string e;
			const int last = art ? 4 : 1;
			for (int k = 6; k >= last; k--) e += std::string("if(eq(") + var + "," + n(k) + ")," + n(v[k]) + ",";
			e += art ? "y" : n(v[0]);
			return "'" + e + std::string(7 - last, ')') + "'";
		}
	}

	std::string ledGraph(bool overCover, const std::string&)
	{
		// 16 columns, 14 segments: green 9, yellow 3, red the top 2
		const int N = 16, S = 14, TOP = 8;	// TOP: the highest bands measured and cut off, as Pocket console
		// the segments' grid at half size (320x240; 320x100 under the art): a cell 19 wide (the segment 16, a gap 3:
		// 32 and 6 pixels) and 16 high (13 and a gap 3 above it) or, under the art, 7 (5 and 2)
		const int CW = 19, SW = 16, W = 320;
		const int H = overCover ? 100 : 240, CH = overCover ? 7 : 16, GAP = overCover ? 2 : 3;
		const int X0 = (W - (N * CW - (CW - SW))) / 2, Y0 = (H - S * CH - GAP) / 2;	// the grid's place: centred
		// the colours: lit green #3CF05A, yellow #FFD21E, red #FF3224; unlit the same at about a sixth; the panel #08090B
		const Yuv bg = yuv(0x08090B), dg = yuv(0x10301A), dy = yuv(0x342B10), dr = yuv(0x381311),
				  lg = yuv(0x3CF05A), ly = yuv(0xFFD21E), lr = yuv(0xFF3224);
		const int Ys[7] = {bg.y, dg.y, dy.y, dr.y, lg.y, ly.y, lr.y}, Us[7] = {bg.u, dg.u, dy.u, dr.u, lg.u, ly.u, lr.u},
				  Vs[7] = {bg.v, dg.v, dy.v, dr.v, lg.v, ly.v, lr.v};
		// the meter's range: the level's 252 steps from FLOOR up, 14 a segment. Below FLOOR is the spectrum's
		// near-constant bed (ascale=log): counted from 0, the bottom half stood lit through any song
		const int FLOOR = 56, STEP = (252 - FLOOR) / S;
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [ldart1] [ldart2]; [ldart1] crop=640:280:0:0 [top]; "
				 "[ldart2] crop=640:200:0:280,extractplanes=y+u+v [lday] [ldau] [ldav]; ";
		// the 16 levels, 0..252 (as Peaks and Pocket console measure them), each eased in the exp(value/24) domain,
		// where lagfun's decay is an even fall: the level down over ~0.4 s for the whole height, the peak ~2.4 s
		const std::string toExp = "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=";
		const std::string toLin = ",lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,"
								  "scale=" + n(N) + ":" + n(S) + ":flags=neighbor,setsar=1";
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255',split [ldv] [ldp]; "
			 "[ldv] " + toExp + "0.417" + toLin + " [ldV]; "
			 "[ldp] " + toExp + "0.8679" + toLin + " [ldP]; "
			 // each segment's index, 0 at the bottom (a still: one frame, repeated by lut2)
			 "color=black:s=" + n(N) + "x" + n(S) + ":r=1:d=1,format=gray,geq=lum='" + n(S - 1) + "-Y' [ldR]; "
			 // the index, +16 when lit (the level reaches its middle)
			 "[ldV] [ldR] lut2=c0='y+16*gte(x," + n(STEP) + "*y+" + n(FLOOR + STEP / 2) + ")' [ldA]; "
			 // the class: 1 + the band (green, yellow, red) + 3 when lit or the peak's (the peak's value, rounded,
			 // has this segment on top)
			 "[ldP] [ldA] lut2=c0='1+gte(mod(y,16)," + n(S - 5) + ")+gte(mod(y,16)," + n(S - 2) + ")+3*gte(gte(y,16)+gte(x," +
			 n(STEP) + "*mod(y,16)+" + n(FLOOR + STEP / 2) + ")*lt(x," + n(STEP) + "*mod(y,16)+" + n(FLOOR + STEP + STEP / 2) + "),1)' [ldC]; "
			 // the segments' grid: each segment a cell, the gaps and margins 0 (by the still mask)
			 "[ldC] scale=" + n(N * CW) + ":" + n(S * CH) + ":flags=neighbor,pad=" + n(W) + ":" + n(H) + ":" + n(X0) + ":" +
			 n(Y0) + ",setsar=1 [ldG]; "
			 "color=black:s=" + n(W) + "x" + n(H) + ":r=1:d=1,format=gray,geq=lum='255*(1-between(X," + n(X0) + "," +
			 n(X0 + N * CW - 1) + ")*between(Y," + n(Y0) + "," + n(Y0 + S * CH - 1) + ")*lt(mod(X-" + n(X0) + "," + n(CW) +
			 ")," + n(SW) + ")*gte(mod(Y-" + n(Y0) + "," + n(CH) + ")," + n(GAP) + "))' [ldM]; "
			 "[ldG] [ldM] lut2=c0='if(y,0,x)',split=3 [ldc0] [ldc1] [ldc2]; ";
		if (overCover)
			return g + "[ldc0] scale=640:200:flags=neighbor [ldc0s]; "
					   "[ldc0s] [lday] lut2=c0=" + classLut("x", Ys, true) + " [ldY]; "
					   "[ldc1] [ldau] lut2=c0=" + classLut("x", Us, true) + " [ldU]; "
					   "[ldc2] [ldav] lut2=c0=" + classLut("x", Vs, true) + " [ldW]; "
					   "[ldY] [ldU] [ldW] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		return g + "[ldc0] lut=c0=" + classLut("val", Ys, false) + ",scale=640:480:flags=neighbor [ldY]; "
				   "[ldc1] lut=c0=" + classLut("val", Us, false) + " [ldU]; "
				   "[ldc2] lut=c0=" + classLut("val", Vs, false) + " [ldW]; "
				   "[ldY] [ldU] [ldW] mergeplanes=0x001020:yuv420p [vo]";
	}

	// no files: its stills (the segments' indices, the gaps) are one frame each, made in the graph
	bool writeLedMaps(const std::string&)
	{
		return true;
	}
}
