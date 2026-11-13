// Long cat: the camera climbing a cat that never ends. A tall tabby (the owner's pixels, drawn 8 screen pixels a pixel,
// 34 wide and centred, its top at y=40): its head fixed, its body's 18-row tile repeating down past the screen's bottom
// and sliding down under the head (6 rows a second), while white clouds in a blue sky (#5aa8f0 at the top to #bfe2ff at
// the bottom) slide down behind it half as fast again, so it reads as the camera rising up a cat that keeps growing.
// The climb is steady: a crop's offset is the frame number, it can't add up the music, so the music lives in the light.
// The music: a rainbow light hugging the cat's outline on both sides of every row (ears, cheeks, paws, body), each row
// a band (the bass at the bottom, the treble at the ears), 3..15 pixels wide and brighter as its band plays, its hue
// band x 24 degrees, drifting round the rainbow as it climbs.
// A frame: the 16 bands measured, contrasted and eased (as Pocket console's bars). Then, at 640 x the cat's rows (one
// row per cat pixel, every screen column, as a light's width is in screen pixels):
//  - the levels turned into a column (bass at the bottom) and stretched across: each row its band's level; a lut2
//    against a still of each pixel's distance from the row's outline (0 inside or far) lights the light's pixels, its
//    width and strength (16 steps) from the level; another against a still of the rows' hues (15 hues, one variant a
//    column, the column cropped by the frame number: the drift) gives each lit pixel its class (9 + step + 16 x hue);
//  - the cat: the tile's still cropped by the frame number (the climb) under the head's still, classes 1-8;
//  - per plane, a lut from the class to a packed value (the cat's colour index, or the light's colour in 5 bits and its
//    alpha in 3), and a lut2 against the background (alone: the sky's row or a cloud, from a tall still of clouds
//    cropped by the frame number; over the art: the art) paints it, the light blended over; scaled up (nearest).
// Stills are one frame each (movie=), never looped. YUV and gray only, nothing converted, no geq per frame.
// Over the art: the art's top as it is, and in the 640x200 strip under it the same cat at 4-pixel pixels (its head at
// the strip's top, its body climbing below), its light 2..8 pixels wide over the art, the art showing everywhere else.
#include "viz_more.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	namespace
	{
		// a new name whenever the stills change: they're cached by size
		const char* const FILES[10] = {"lcat1-head-a.pgm", "lcat1-tile-a.pgm", "lcat1-hue-a.pgm", "lcat1-dist-a.pgm",
									   "lcat1-head-s.pgm", "lcat1-tile-s.pgm", "lcat1-hue-s.pgm", "lcat1-dist-s.pgm",
									   "lcat1-cloud.pgm",  "lcat1-row.pgm"};

		// The cat: the owner's AI-made tall tabby, parsed with the owner's own settings (a 53x72 grid of 20.25x20 px
		// cells, offset 2.5,5, each sampled at 55%/44% of the cell, columns 8-41; palette as PAL below). The head, paws,
		// collar and the top of the body as drawn; then the body's tile (its stripes and spots), repeated down.
		const int COLS = 34, HEADR = 26, TILER = 18;
		const char* const HEAD[HEADR] = {
			"                                  ", "         aaaa       aaaa          ", "        aaaaa       aaaa          ",
			"       aallllaa   aallllaa        ", "       aallllaa   aallllaa        ", "       aalplllaaaaallpplaa        ",
			"       aapppplmmmmmmppppaaa       ", "       aapppplmllmmmpppplla       ", "  y   aaap....mllmmllppplla       ",
			" yyy  aa......mllmmllllllla       ", " ay   aa.......llllllllmmma    y  ", "    aa..............lllllllaa yyy ",
			"    aa....aa......aalllllllaa  y  ", "    aa....aa......aa.llllllaa     ", "    aamm..aa......aa...mmmmaa     ",
			"    aamm.....a..a.......lllaa     ", "    aa........aa..........laaa    ", "      aaa............aaaaallma    ",
			"    aa...aa.........a......lma    ", "   aaa...aa.........a......lma    ", "   aa.....karrrrrrra....kkalma    ",
			"   aa...kkkaaayyyaaa...kkkalma    ", "   aa..kkaaaaa.yyama..kkaaalla    ", "   aa..kkaalmayyyala..kkaallla    ",
			"     aaaaa....aaa...aaaallllla    ", "     aaa a..........llllllllla    "};
		const char* const TILE[TILER] = {
			"        aa..........llllllllla    ", "        aa............llmmmmma    ", "        aa............llmmmmma    ",
			"        aa...k........llllllla    ", "        aa...k........llllllla    ", "        aa............llllllla    ",
			"        aa........kk..llllllla    ", "        aa........kk..llmmmmma    ", "        aa............llmmmmma    ",
			"        aa............llllllla    ", "        aa............llllllla    ", "        aa....k........lllllla    ",
			"        aa.............lllmmma    ", "        aa.............lllmmma    ", "        aa.............lllmmma    ",
			"        aa........kk...lllllla    ", "        aa.............lllllla    ", "        aa.............lllllla    "};
		// the cat's classes 1-8, in this order
		const char PALCH[8] = {'a', '.', 'l', 'm', 'p', 'r', 'y', 'k'};
		const unsigned PAL[8] = {0x2a1e24, 0xfafafa, 0xcdcbd4, 0x888496, 0xf0828c, 0xd63a40, 0xecba28, 0xf6d6be};
		const unsigned SKY0 = 0x5aa8f0, SKY1 = 0xbfe2ff, CLOUD = 0xffffff, CLOUD2 = 0xe2f0ff;
		const int LIGHT = 9, HUES = 15, STEPS = 16;	// a light's class: LIGHT + step + STEPS x hue (9..248)

		// the drawing: W x H pixels of P screen pixels (a light's picture is 640 x H), the cat's top-left (ox, oy) in them;
		// the light's width in screen pixels from wmin (silent) to wmax (full)
		struct Layout
		{
			int W, H, P, ox, oy, wmin, wmax;
			bool sky;	// the sky and clouds (alone), or the art (over it)
		};
		const Layout ALONE = {80, 60, 8, 23, 5, 3, 15, true};
		const Layout STRIP = {160, 50, 4, 63, 0, 2, 8, false};
		int rowsOf(const Layout& L) { return L.H - L.oy; }	   // the cat's rows on screen
		int tileRows(const Layout& L) { return rowsOf(L) - HEADR; }

		// the climb: a tile row every CSTEP frames (6 a second); the clouds 1.5 times as fast; the hue a step (24
		// degrees) every HSTEP frames
		const int CSTEP = 10, HSTEP = 53;
		const int CLOUDP = 444;	// the clouds' still: 37 clouds 12 rows apart, then its first 60 rows again

		struct Yuv { int y, u, v; };
		struct Yuvd { double y, u, v; };
		// BT.601 limited (mpv's reading of a picture this size)
		Yuvd yuvd(double r, double g, double b)
		{
			return {16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255, 128 + (-37.797 * r - 74.203 * g + 112 * b) / 255,
					128 + (112 * r - 93.786 * g - 18.214 * b) / 255};
		}
		Yuv yuv(unsigned rgb)
		{
			Yuvd d = yuvd(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255);
			return {(int)std::lround(d.y), (int)std::lround(d.u), (int)std::lround(d.v)};
		}

		std::string n(long long v) { return std::to_string(v); }
		std::string f(double v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.5f", v);
			return b;
		}

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

		int cls(char c)
		{
			for (int i = 0; i < 8; i++)
				if (PALCH[i] == c) return i + 1;
			return 0;
		}
		// the cat's row r on screen at the start (its tile rows' outline never changes: they all span columns 8-29)
		const char* rowAt(int r) { return r < HEADR ? HEAD[r] : TILE[(r - HEADR) % TILER]; }
		// the band of the cat's row r of R (as the level column stretched by nearest: the bass at the bottom)
		int bandOf(int r, int R) { return 15 - std::min(15, (int)std::floor((r + 0.5) * 16 / R)); }

		// a still's size: k 0-3 alone (head, tile, hue, distance), 4-7 over the art, 8 the clouds, 9 the sky's rows
		void dims(int k, int& w, int& h)
		{
			if (k == 8) { w = ALONE.W; h = CLOUDP + ALONE.H; return; }
			if (k == 9) { w = ALONE.W; h = ALONE.H; return; }
			const Layout& L = k < 4 ? ALONE : STRIP;
			switch (k % 4)
			{
				case 0: w = COLS; h = rowsOf(L); return;
				case 1: w = COLS; h = TILER + tileRows(L); return;
				case 2: w = HUES; h = L.H; return;
				default: w = 640; h = L.H; return;
			}
		}

		// the preview's random clouds, made repeatable
		struct Rng
		{
			uint32_t s = 12345;
			double next() { s = s * 1103515245u + 12345u; return (double)((s >> 8) & 0xffffff) / 16777216.0; }
		};

		std::vector<unsigned char> still(int k)
		{
			int w, h;
			dims(k, w, h);
			std::vector<unsigned char> m((size_t)w * h, 0);
			if (k == 9)	// the sky's rows: the row's number
			{
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) m[(size_t)y * w + x] = (unsigned char)y;
				return m;
			}
			if (k == 8)	// the clouds: 1 white, 2 its pale underside; wrapped round CLOUDP, then its top again
			{
				Rng rng;
				for (int i = 0; i < CLOUDP / 12; i++)
				{
					int cx = (int)std::floor(rng.next() * 70) - 5, cw = 6 + (int)std::floor(rng.next() * 9);
					int ch = 2 + (int)std::floor(rng.next() * 2), cy = i * 12 + (int)std::floor(rng.next() * 5) - 2;
					for (int r = 0; r < ch; r++)
					{
						int inset = r == 0 ? 2 : 0, y = ((cy + r) % CLOUDP + CLOUDP) % CLOUDP;
						for (int x = cx + inset; x < cx + cw - inset; x++)
							if (x >= 0 && x < w) m[(size_t)y * w + x] = (unsigned char)(r == ch - 1 ? 2 : 1);
					}
				}
				for (int y = CLOUDP; y < h; y++)
					for (int x = 0; x < w; x++) m[(size_t)y * w + x] = m[(size_t)(y - CLOUDP) * w + x];
				return m;
			}
			const Layout& L = k < 4 ? ALONE : STRIP;
			const int R = rowsOf(L);
			switch (k % 4)
			{
				case 0:	// the head (0 under it)
					for (int y = 0; y < HEADR; y++)
						for (int x = 0; x < COLS; x++) m[(size_t)y * w + x] = (unsigned char)cls(HEAD[y][x]);
					break;
				case 1:	// the tile, repeated (cropped by the climb)
					for (int y = 0; y < h; y++)
						for (int x = 0; x < COLS; x++) m[(size_t)y * w + x] = (unsigned char)cls(TILE[y % TILER][x]);
					break;
				case 2:	// the hues: column d, the cat's row r: its band plus d, round the 15
					for (int y = L.oy; y < h; y++)
						for (int d = 0; d < HUES; d++) m[(size_t)y * w + d] = (unsigned char)((bandOf(y - L.oy, R) + d) % HUES);
					break;
				default:	// each pixel's distance outside its row's outline, 1..wmax (0 inside or past it)
					for (int r = 0; r < R; r++)
					{
						std::string row = rowAt(r);
						size_t l = row.find_first_not_of(' '), rr = row.find_last_not_of(' ');
						if (l == std::string::npos) continue;
						int eL = (L.ox + (int)l) * L.P, eR = (L.ox + (int)rr + 1) * L.P;
						unsigned char* line = &m[(size_t)(r + L.oy) * w];
						for (int d = 1; d <= L.wmax; d++)
						{
							if (eL - d >= 0) line[eL - d] = (unsigned char)d;
							if (eR + d - 1 < w) line[eR + d - 1] = (unsigned char)d;
						}
					}
			}
			return m;
		}

		// hsl -> rgb (0..255), h in degrees
		void hsl(double h, double s, double l, double& r, double& g, double& b)
		{
			double a = s * std::min(l, 1 - l);
			auto ch = [&](int k0) {
				double k = std::fmod(k0 + h / 30, 12.0);
				return 255 * (l - a * std::max(-1.0, std::min({k - 3, 9 - k, 1.0})));
			};
			r = ch(0);
			g = ch(8);
			b = ch(4);
		}
	}

	bool writeLongcatMaps(const std::string& dir)
	{
		auto path = [&](int k) { return dir + "/" + FILES[k]; };
		auto ready = [&](int k) {
			int w, h;
			dims(k, w, h);
			struct stat st;
			return stat(path(k).c_str(), &st) == 0 && (size_t)st.st_size == header(w, h).size() + (size_t)w * h;
		};
		bool all = true;
		for (int k = 0; k < 10; k++) all = all && ready(k);
		if (all) return true;
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < 10; k++)
		{
			int w, h;
			dims(k, w, h);
			if (!pgm(path(k), w, h, still(k))) return false;
		}
		return true;
	}

	std::string longcatGraph(bool overCover, const std::string& dir)
	{
		const Layout& L = overCover ? STRIP : ALONE;
		const int base = overCover ? 4 : 0, R = rowsOf(L), TR = tileRows(L);
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + FILES[k < 4 ? base + k : k], s); return s; };
		const std::string H = n(L.H);

		Yuv cat[8];
		for (int i = 0; i < 8; i++) cat[i] = yuv(PAL[i]);
		// a plane's lut from the class to its packed value: 0 nothing; 1-8 the cat's colour; a light's: its colour in
		// 5 bits (2..31 of 16..240) and its alpha in 3. The light: hsl(hue x 24, 95%, 45..70%), alpha 0.25..1
		auto pack = [&](double Yuvd::*plane) {
			// the plane's value is affine in the lightness and in a x the hue's own term: val = c0 + cl x l - a x g(hue)
			Yuvd o = yuvd(0, 0, 0), one = yuvd(255, 255, 255);
			std::string gh = "0";
			for (int h = HUES - 1; h >= 0; h--)
			{
				double r, g, b;
				hsl(h * 24.0, 1, 0.5, r, g, b);	// l=0.5, a=0.5: r = 255 x (0.5 - 0.5 x gc)
				Yuvd c = yuvd(r, g, b);
				double gv = ((one.*plane - o.*plane) * 0.5 - (c.*plane - o.*plane)) / 0.5;	// sum of coef x 255 x gc
				gh = "if(eq(ld(1)," + n(h) + ")," + f(gv) + "," + gh + ")";
			}
			std::string e = "st(0,mod(val-" + n(LIGHT) + "," + n(STEPS) + "));st(1,floor((val-" + n(LIGHT) + ")/" + n(STEPS) + "));"
							"st(2,(ld(0)+0.5)/" + n(STEPS) + ");st(3,0.45+0.25*ld(2));"
							"st(4," + f(o.*plane) + "+" + f(one.*plane - o.*plane) + "*ld(3)-0.95*min(ld(3),1-ld(3))*(" + gh + "));"
							"if(lt(val," + n(LIGHT) + "),val,8*clip(round((ld(4)-16)*31/224),2,31)+round(ld(2)*7))";
			return "'" + e + "'";
		};
		// a plane's lut2: the packed value (x) over the background (y: alone the sky's class, 0-59 its row, 60-61 a
		// cloud; over the art the art)
		auto paint = [&](double Yuvd::*plane, int Yuv::*iplane) {
			std::string bg = "y";
			if (L.sky)
			{
				Yuvd a = yuvd(SKY0 >> 16 & 255, SKY0 >> 8 & 255, SKY0 & 255), b = yuvd(SKY1 >> 16 & 255, SKY1 >> 8 & 255, SKY1 & 255);
				bg = "if(lt(y,60)," + f(a.*plane) + "+" + f(b.*plane - a.*plane) + "*(y*8+4)/480,if(eq(y,60)," +
					 n(yuv(CLOUD).*iplane) + "," + n(yuv(CLOUD2).*iplane) + "))";
			}
			std::string catc = "ld(0)";
			for (int i = 7; i >= 0; i--) catc = "if(eq(x," + n(i + 1) + ")," + n(cat[i].*iplane) + "," + catc + ")";
			return "'st(0," + bg + ");if(lt(x,9)," + catc + ",st(1,0.25+0.75*mod(x,8)/7);"
				   "round(ld(1)*(16+floor(x/8)*224/31)+(1-ld(1))*ld(0)))'";
		};

		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [lcart1] [lcart2]; [lcart1] crop=640:280:0:0 [top]; "
				 "[lcart2] crop=640:200:0:280,extractplanes=y+u+v [lcby] [lcbu] [lcbv]; ";
		// the 16 bands measured (the top 8 measured and cut off, as Pocket console), contrasted, eased (up at once, down
		// over ~0.15 s: the exp/log lagfun)
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=24x256:mode=bar:cmode=combined:ascale=log:"
			 "fscale=log:win_size=1024:rate=60:colors=white,crop=16:256:0:0,scale=16:1:flags=area,format=gray,lut=c0='val*252/255'" +
			 vizContrast("lc") + ",format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split=" + n(L.sky ? 4 : 3) +
			 " [lclv] [lcd1] [lcd2]" + (L.sky ? " [lcd3]" : "") + "; ";
		// the light: each row its band's level, against each pixel's distance from the outline: 0, or 1 + its step
		g += "[lclv] transpose=2,scale=640:" + n(R) + ":flags=neighbor,pad=640:" + H + ":0:" + n(L.oy) + " [lclc]; "
			 "movie=" + q(3) + ",format=gray [lcdist]; "
			 "[lclc] [lcdist] lut2=c0='if(y*lte(y,round(" + n(L.wmin) + "+" + n(L.wmax - L.wmin) + "*x/252)),1+min(" + n(STEPS - 1) +
			 ",floor(x*" + n(STEPS) + "/253)),0)' [lcmask]; ";
		// its hues, the column of the moment (the drift)
		g += "[lcd1] scale=" + n(HUES) + ":" + H + ":flags=neighbor [lcd1s]; movie=" + q(2) + ",format=gray [lchm]; "
			 "[lcd1s] [lchm] lut2=c0='y',crop=1:" + H + ":x='mod(floor(n/" + n(HSTEP) + ")," + n(HUES) + ")':y=0,"
			 "scale=640:" + H + ":flags=neighbor [lchue]; "
			 "[lcmask] [lchue] lut2=c0='if(x," + n(LIGHT - 1) + "+x+" + n(STEPS) + "*y,0)' [lclight]; ";
		// the cat: the tile cropped by the climb, under the head; put in its place, every screen column
		g += "[lcd2] scale=" + n(COLS) + ":" + n(TILER + TR) + ":flags=neighbor [lcd2s]; movie=" + q(1) + ",format=gray [lctm]; "
			 "movie=" + q(0) + ",format=gray [lchd]; "
			 "[lcd2s] [lctm] lut2=c0='y',crop=" + n(COLS) + ":" + n(TR) + ":x=0:y='mod(" + n(TILER) + "-mod(floor(n/" + n(CSTEP) + ")," +
			 n(TILER) + ")," + n(TILER) + ")',pad=" + n(COLS) + ":" + n(R) + ":0:" + n(HEADR) + " [lctl]; "
			 "[lctl] [lchd] lut2=c0='if(y,y,x)',scale=" + n(COLS * L.P) + ":" + n(R) + ":flags=neighbor,"
			 "pad=640:" + H + ":" + n(L.ox * L.P) + ":" + n(L.oy) + ",setsar=1 [lccat]; "
			 "[lccat] [lclight] lut2=c0='if(x,x,y)',split=3 [lck0] [lck1] [lck2]; ";

		if (L.sky)	// the sky's classes: its row, or a cloud (the clouds' still cropped by the climb, 1.5x the cat's)
			g += "[lcd3] scale=" + n(L.W) + ":" + n(CLOUDP + L.H) + ":flags=neighbor [lcd3s]; movie=" + q(8) + ",format=gray [lccm]; "
				 "movie=" + q(9) + ",format=gray [lcrow]; "
				 "[lcd3s] [lccm] lut2=c0='y',crop=" + n(L.W) + ":" + H + ":x=0:y='" + n(CLOUDP) + "-mod(floor(n*3/" + n(2 * CSTEP) + ")," +
				 n(CLOUDP) + ")' [lccl]; [lccl] [lcrow] lut2=c0='if(x,59+x,y)',split=3 [lcs0] [lcs1] [lcs2]; "
				 "[lcs0] scale=640:" + H + ":flags=neighbor [lcby]; [lcs1] scale=320:" + H + ":flags=neighbor [lcbu]; "
				 "[lcs2] scale=320:" + H + ":flags=neighbor [lcbv]; ";

		if (overCover)
			return g + "[lck0] lut=c0=" + pack(&Yuvd::y) + ",scale=640:200:flags=neighbor [lcq0]; "
					   "[lcq0] [lcby] lut2=c0=" + paint(&Yuvd::y, &Yuv::y) + " [lcY]; "
					   "[lck1] scale=320:" + H + ":flags=neighbor,lut=c0=" + pack(&Yuvd::u) + ",scale=320:100:flags=neighbor [lcq1]; "
					   "[lcq1] [lcbu] lut2=c0=" + paint(&Yuvd::u, &Yuv::u) + " [lcU]; "
					   "[lck2] scale=320:" + H + ":flags=neighbor,lut=c0=" + pack(&Yuvd::v) + ",scale=320:100:flags=neighbor [lcq2]; "
					   "[lcq2] [lcbv] lut2=c0=" + paint(&Yuvd::v, &Yuv::v) + " [lcV]; "
					   "[lcY] [lcU] [lcV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "[lck0] lut=c0=" + pack(&Yuvd::y) + " [lcq0]; "
				   "[lcq0] [lcby] lut2=c0=" + paint(&Yuvd::y, &Yuv::y) + ",scale=640:480:flags=neighbor [lcY]; "
				   "[lck1] scale=320:" + H + ":flags=neighbor,lut=c0=" + pack(&Yuvd::u) + " [lcq1]; "
				   "[lcq1] [lcbu] lut2=c0=" + paint(&Yuvd::u, &Yuv::u) + ",scale=320:240:flags=neighbor [lcU]; "
				   "[lck2] scale=320:" + H + ":flags=neighbor,lut=c0=" + pack(&Yuvd::v) + " [lcq2]; "
				   "[lcq2] [lcbv] lut2=c0=" + paint(&Yuvd::v, &Yuv::v) + ",scale=320:240:flags=neighbor [lcV]; "
				   "[lcY] [lcU] [lcV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
