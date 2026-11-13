// Tube man: pixel art on an 80x60 grid of 8-pixel dots. A light blue sky; a grey car-lot road along the bottom with
// white dashes; a red "SALE!" sign; an inflatable tube man standing on a dark blower: a red tube (pink in its top
// quarter) whose height follows the bass (10 rows when quiet, 36 on the beat), bending side to side along its length in
// a wave (the bend grows up the tube) whose speed and size follow the mids in four steps, two orange arms flailing from
// a point two thirds up, and a yellow face with dark eyes and an open mouth on its top.
// A frame: the 16 bands measured, contrasted and eased (as Pocket console's bars), and from them the bass (bands 0-2)
// and the mids (4-9). The tube is a stack of 5-dot rows, each row moved sideways by its bend, so it is drawn by
// lut2s against stills of the dots' rows and columns (each lut2's table computed once):
//  - the moment: the mids' step against a still clock (a row of 240 dots, 0..239) gives "step x 32 + the wave's phase"
//    (32 phases, a whole number of waves in the 4 s loop for each step), cropped where the frame number says;
//  - the bass against the rows: each dot's place up the tube (its height in 40ths, or which face row, or none);
//  - that against the moment: the row's role (red, pink, face rows) and its sideways offset;
//  - that against the columns: the dot's class.
//  The arms the same way: the bass against the rows gives each dot's row from the arms' root; the moment against the
//  columns its column from the tube's middle there; the arms' own moment (the mids' step x 16 phases) against that
//  column the arm's row in it; equal rows, an arm.
// Laid over the scene's still (the arms, then the tube), a lut per plane turns a class into its luma, U and V, the luma
// scaled x8 and the chroma x4 (nearest). Stills are one frame each (movie=), never looped. YUV and gray only, nothing
// converted, no geq per frame.
// Over the art: the art's top as it is, and in the 640x200 strip under it the same at 4-pixel dots (160x50): a strip of
// road along its bottom, the blower and the tube man (half size) on it; the art shows through everywhere else.
#include "viz_more.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	namespace
	{
		// a new name whenever the stills change: they're cached by size
		const char* const FILES[7] = {"tube2-scene-a.pgm", "tube2-row-a.pgm", "tube2-col-a.pgm", "tube2-scene-s.pgm",
									  "tube2-row-s.pgm",   "tube2-col-s.pgm", "tube2-clock.pgm"};

		// the drawing's grid: alone 80x60 dots of 8 pixels; over the art 160x50 of 4 (640x200), the scene moved by
		// (dx, dy)
		struct Layout
		{
			int W, H, dx, dy;
			bool full;	// the sky, road and sign (alone), or the art (over it)
		};
		const Layout ALONE = {80, 60, 0, 4, true};	// 4 rows down: the face under the player's title
		const Layout STRIP = {160, 50, 40, -1, false};
		const int OFF = 64;		 // the row and column stills: 64 + the dot's row (column from the tube's middle)
		const int CX = 40;		 // the tube's middle (scene columns)
		const int BASE = 46;	 // its bottom row
		const int CLOCK = 240;	 // the loop: 4 s at 60 frames
		const int PH = 32, APH = 16;	// the wave's phases, the arms'

		enum
		{
			C_NONE,
			C_SKY,
			C_ROAD,
			C_DASH,
			C_SIGN,
			C_BLOWER,
			C_RED,
			C_PINK,
			C_ARM,
			C_FACE,
			C_EYE,
			C_COUNT
		};
		const unsigned RGB[C_COUNT] = {0x9ad8ff, 0x9ad8ff, 0x7a7a80, 0xffffff, 0xe8322e, 0x3a3a40,
									   0xe8322e, 0xff4a8a, 0xff7a1a, 0xffd23f, 0x1a1a1a};

		// the mids' steps (0..252, after the contrast and easing): the wave's size and speed, the arms' reach
		const int M1 = 63, M2 = 126, M3 = 189;

		struct Yuv { int y, u, v; };
		// BT.601 limited (mpv's reading of a picture this size)
		Yuv yuv(unsigned rgb)
		{
			double r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
			return {(int)std::lround(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255),
					(int)std::lround(128 + (-37.797 * r - 74.203 * g + 112 * b) / 255),
					(int)std::lround(128 + (112 * r - 93.786 * g - 18.214 * b) / 255)};
		}

		std::string n(long long v) { return std::to_string(v); }

		std::string header(int w, int h) { return "P5\n" + n(w) + " " + n(h) + "\n255\n"; }

		bool pgm(const std::string& path, int w, int h, const std::vector<unsigned char>& data)
		{
			std::string tmp = path + ".tmp";
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			out << header(w, h);
			out.write((const char*)data.data(), (std::streamsize)data.size());
			if (!out.flush()) return false;
			out.close();
			return rename(tmp.c_str(), path.c_str()) == 0;
		}

		// a still's size: k 0-2 alone, 3-5 over the art (scene, rows, columns), 6 the clock
		void dims(int k, int& w, int& h)
		{
			if (k == 6) { w = CLOCK; h = 1; return; }
			const Layout& L = k < 3 ? ALONE : STRIP;
			w = L.W;
			h = L.H;
		}

		// "SALE!" in a 3x5 pixel font
		const char* const SALE[5] = {"### ### #.. ### #", "#.. #.# #.. #.. #", "### ### #.. ##. #", "..# #.# #.. #.. .",
									 "### #.# ### ### #"};

		std::vector<unsigned char> still(int k)
		{
			int w, h;
			dims(k, w, h);
			std::vector<unsigned char> m((size_t)w * h, 0);
			auto put = [&](int x, int y, int c) {
				if (x >= 0 && y >= 0 && x < w && y < h) m[(size_t)y * w + x] = (unsigned char)c;
			};
			if (k == 6)
			{
				for (int x = 0; x < w; x++) m[x] = (unsigned char)x;
				return m;
			}
			const Layout& L = k < 3 ? ALONE : STRIP;
			if (k % 3 == 1)
			{
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) put(x, y, std::clamp(y - L.dy + OFF, 0, 254));
				return m;
			}
			if (k % 3 == 2)
			{
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) put(x, y, std::clamp(x - L.dx - CX + OFF, 0, 254));
				return m;
			}
			auto px = [&](int x, int y, int c) { put(x + L.dx, y + L.dy, c); };
			if (L.full)
			{
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) put(x, y, y - L.dy >= 48 ? C_ROAD : C_SKY);
				for (int x = 0; x < w; x += 8)
					for (int i = 0; i < 4; i++) px(x + i, 52, C_DASH);
				for (int r = 0; r < 5; r++)
					for (int c = 0; SALE[r][c]; c++)
						if (SALE[r][c] == '#') px(57 + c, 11 + r, C_SIGN);
			}
			else	// over the art: a strip of road along the bottom
				for (int y = h - 3; y < h; y++)
					for (int x = 0; x < w; x++) put(x, y, y == h - 2 && x % 8 < 4 ? C_DASH : C_ROAD);
			for (int y = BASE; y < BASE + 3; y++)
				for (int x = CX - 4; x < CX + 4; x++) px(x, y, C_BLOWER);
			return m;
		}
	}

	bool writeTubemanMaps(const std::string& dir)
	{
		auto path = [&](int k) { return dir + "/" + FILES[k]; };
		auto ready = [&](int k) {
			int w, h;
			dims(k, w, h);
			struct stat st;
			return stat(path(k).c_str(), &st) == 0 && (size_t)st.st_size == header(w, h).size() + (size_t)w * h;
		};
		bool all = true;
		for (int k = 0; k < 7; k++) all = all && ready(k);
		if (all) return true;
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < 7; k++)
		{
			int w, h;
			dims(k, w, h);
			if (!pgm(path(k), w, h, still(k))) return false;
		}
		return true;
	}

	std::string tubemanGraph(bool overCover, const std::string& dir)
	{
		const Layout& L = overCover ? STRIP : ALONE;
		const int base = overCover ? 3 : 0;
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + FILES[k < 3 ? base + k : k], s); return s; };
		const std::string W = n(L.W), H = n(L.H);

		Yuv col[C_COUNT];
		for (int k = 0; k < C_COUNT; k++) col[k] = yuv(RGB[k]);
		// a lut from the class (var) to a plane's value; over the art, 0 is the art's (y)
		auto paint = [&](int Yuv::*plane, const char* var, bool art) {
			std::string e = art ? "y" : n(col[0].*plane);
			for (int k = C_COUNT - 1; k >= 1; k--) e = std::string("if(eq(") + var + "," + n(k) + ")," + n(col[k].*plane) + "," + e + ")";
			return "'" + e + "'";
		};
		// the mids' step from a level (in a lut2 expression)
		auto step = [&](const char* v) {
			return std::string("(gte(") + v + "," + n(M1) + ")+gte(" + v + "," + n(M2) + ")+gte(" + v + "," + n(M3) + "))";
		};
		// the wave from the moment (x: step x 32 + phase) at height k (an expression): its sideways offset, rounded
		auto bend = [&](const std::string& k) {
			return "round(sin(2*PI*mod(x," + n(PH) + ")/" + n(PH) + "+4*" + k + ")*(2+9*(floor(x/" + n(PH) + ")+0.5)/4)*" + k + ")";
		};

		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [tmart1] [tmart2]; [tmart1] crop=640:280:0:0 [top]; "
				 "[tmart2] crop=640:200:0:280,extractplanes=y+u+v [tmay] [tmau] [tmav]; ";
		// the 16 bands measured (the top 8 measured and cut off, as Pocket console), contrasted, eased (up at once, down
		// over ~0.15 s: the exp/log lagfun), then the bass and the mids
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=24x256:mode=bar:cmode=combined:ascale=log:"
			 "fscale=log:win_size=1024:rate=60:colors=white,crop=16:256:0:0,scale=16:1:flags=area,format=gray,lut=c0='val*252/255'" +
			 vizContrast("tm") + ",format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split [tmb] [tmm]; "
			 "[tmb] crop=3:1:0:0,scale=1:1:flags=area,scale=" + W + ":" + H + ":flags=neighbor,split [tmb1] [tmb2]; "
			 "[tmm] crop=6:1:4:0,scale=1:1:flags=area,scale=" + n(CLOCK) + ":1:flags=neighbor,split [tmm1] [tmm2]; "
			 "movie=" + q(6) + ",format=gray,split [tmk1] [tmk2]; "
			 "movie=" + q(1) + ",format=gray,split [tmrow1] [tmrow2]; movie=" + q(2) + ",format=gray,split [tmcol1] [tmcol2]; ";
		// the moments: the wave's (4, 5, 6 or 8 waves in the loop by the step; 32 phases) and the arms' (6 swings)
		g += "[tmm1] [tmk1] lut2=c0='st(0," + step("x") + ");ld(0)*" + n(PH) + "+mod(floor(y*if(eq(ld(0),3),8,4+ld(0))*" + n(PH) +
			 "/" + n(CLOCK) + ")," + n(PH) + ")',crop=1:1:x='mod(n," + n(CLOCK) + ")':y=0,scale=" + W + ":" + H +
			 ":flags=neighbor,split [tmw1] [tmw2]; "
			 "[tmm2] [tmk2] lut2=c0='" + step("x") + "*" + n(APH) + "+mod(floor(y*6*" + n(APH) + "/" + n(CLOCK) + ")," + n(APH) +
			 ")',crop=1:1:x='mod(n," + n(CLOCK) + ")':y=0,scale=" + W + ":" + H + ":flags=neighbor [tma]; ";
		// the tube: each dot's place up it (h the height, i the dot's row up from the bottom, t the top row, the arms'
		// row at 26 40ths): 0..40 (40ths of the height), 100 + 41 x face row (0 its mouth .. 2 its top) + the top's
		// 40ths, or 255
		g += "[tmb1] [tmrow1] lut2=c0='st(0,10+x*26/252);st(1," + n(BASE + OFF) + "-y);st(2,ceil(ld(0))-1);st(3,round(0.65*ld(0)));"
			 "if(between(ld(1),0,ld(2)),if(eq(ld(1),ld(3)),26,round(40*ld(1)/ld(0))),"
			 "if(between(ld(1)-ld(2),1,3),100+41*(ld(1)-ld(2)-1)+round(40*ld(2)/ld(0)),255))' [tmu]; ";
		// against the moment: role x 32 + offset + 12 (role 0 red, 1 pink, 2 the mouth's row, 3 the eyes', 4 the face's top)
		g += "[tmw1] [tmu] lut2=c0='if(eq(y,255),255,if(lt(y,41),32*gt(y,30)+12+" + bend("y/40") + ","
			 "32*(2+floor((y-100)/41))+12+" + bend("mod(y-100,41)/40") + "))' [tmv]; ";
		// against the columns: the class
		g += "[tmv] [tmcol1] lut2=c0='st(0,floor(x/32));st(1,y-" + n(OFF) + "-mod(x,32)+12);"
			 "if(eq(x,255)+gt(abs(ld(1)),2),0,if(eq(ld(0),0)," + n(C_RED) + ",if(eq(ld(0),1)," + n(C_PINK) + ","
			 "if(eq(ld(0),2)*eq(ld(1),0)+eq(ld(0),3)*eq(abs(ld(1)),1)," + n(C_EYE) + "," + n(C_FACE) + "))))' [tmT]; ";
		// the arms: each dot's row from their root (+8), its column from the tube's middle there (+16), the arm's row at
		// that column (+8: the left arm up as its sine, the right down), equal: an arm
		g += "[tmb2] [tmrow2] lut2=c0='st(0," + n(BASE + OFF) + "-y-round(0.65*(10+x*26/252)));if(between(ld(0),-8,8),ld(0)+8,255)' [tmdy]; "
			 "[tmw2] [tmcol2] lut2=c0='st(0,y-" + n(OFF) + "-" + bend("0.65") + ");if(between(ld(0),-12,12),ld(0)+16,255)' [tmdx]; "
			 "[tma] [tmdx] lut2=c0='st(0,y-16);st(1,abs(ld(0))-2);st(2,2*PI*mod(x," + n(APH) + ")/" + n(APH) + ");"
			 "st(3,0.6*ld(1)*(0.5+(floor(x/" + n(APH) + ")+0.5)/4));"
			 "if(eq(y,255)+not(between(ld(1),1,8)),255,if(lt(ld(0),0),8+round(sin(ld(2)+0.6*ld(1))*ld(3)),"
			 "8-round(sin(ld(2)+0.6*ld(1)+1)*ld(3))))' [tmay0]; "
			 "[tmay0] [tmdy] lut2=c0='if(eq(x,y)*lt(x,255)," + n(C_ARM) + ",0)' [tmA]; ";
		// merged over the scene: the arms, then the tube
		g += "movie=" + q(0) + ",format=gray [tmsc]; [tmA] [tmsc] lut2=c0='if(x,x,y)' [tmm0x]; "
			 "[tmT] [tmm0x] lut2=c0='if(x,x,y)',split=3 [tmc0] [tmc1] [tmc2]; ";

		if (overCover)
			return g + "[tmc0] scale=640:200:flags=neighbor [tmc0s]; [tmc0s] [tmay] lut2=c0=" + paint(&Yuv::y, "x", true) + " [tmY]; "
					   "[tmc1] scale=320:100:flags=neighbor [tmc1s]; [tmc1s] [tmau] lut2=c0=" + paint(&Yuv::u, "x", true) + " [tmU]; "
					   "[tmc2] scale=320:100:flags=neighbor [tmc2s]; [tmc2s] [tmav] lut2=c0=" + paint(&Yuv::v, "x", true) + " [tmV]; "
					   "[tmY] [tmU] [tmV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "[tmc0] lut=c0=" + paint(&Yuv::y, "val", false) + ",scale=640:480:flags=neighbor [tmY]; "
				   "[tmc1] lut=c0=" + paint(&Yuv::u, "val", false) + ",scale=320:240:flags=neighbor [tmU]; "
				   "[tmc2] lut=c0=" + paint(&Yuv::v, "val", false) + ",scale=320:240:flags=neighbor [tmV]; "
				   "[tmY] [tmU] [tmV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
