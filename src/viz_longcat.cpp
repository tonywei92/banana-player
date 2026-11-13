// Long cat: the camera climbing a cat that never ends. A tall tabby (the owner's pixels, drawn 8 screen pixels a pixel,
// 34 wide and centred, its top at y=40): its head fixed, its body's 18-row tile repeating down past the screen's bottom
// and sliding down under the head, while white clouds in a blue sky (#5aa8f0 at the top to #bfe2ff at the bottom) slide
// down behind it 1.5 times as fast, so it reads as the camera rising up a cat that keeps growing.
// The climb follows the music: 9 cloud rows a second when it's quiet, up to 2.5 times that when it's loud (capped there,
// so it never jumps), the speed eased over ~0.5 s. A crop's offset can't add up the music, so the adding up is done on
// the sound: its loudness (mono, |sample|, a one-pole lowpass at 0.35 Hz), mapped to the speed, integrated per sample
// (aintegral, in doubles) and wrapped to the clouds' 243 rows (the tile's 18 go into 162 = 243 / 1.5 evenly). That
// position reaches the picture through astats' metadata (the last value of each 160 samples) and adrawgraph, whose colour
// expression is the value itself: a 1x1 picture at 60 frames a second whose red is the position, exactly.
// The music also lights the cat: a stack of rainbow bars on both sides, one every two rows and touching, each its own
// hue and its level blended from the two nearest bands (the bass at the bottom, the treble at the ears), so the stack's
// outline flows as one spectrum; each reaching out from the cat's widest edge so equal levels end level (filled in to the
// cat where it's narrower), 8..120 pixels wide and brighter as its band
// plays (both on a curve, so a quiet row stays short and faint and a loud one shoots out), solid to its tip (no
// fade: the owner's call); its hue band x 24 degrees at full saturation, deep enough to stand out on the pale sky, drifting round.
// A frame: the 16 bands measured, contrasted and eased (as Pocket console's bars). Then, at 640 x the cat's rows (one
// row per cat pixel, every screen column, as a light's width is in screen pixels):
//  - the levels turned into a column (bass at the bottom) and stretched across: each row its band's level; a lut2
//    against a still of each pixel's distance from the row's outline (0 inside or far) lights the light's pixels, its
//    width and strength (16 steps, less further out) from the level; another against a still of the rows' hues (15
//    hues, one variant a column, the column cropped by the frame number: the drift) gives each lit pixel its class
//    (9 + step + 16 x hue);
//  - the cat: the head's still over its body, whose rows are the tile's rows shifted by the position (a lut2 of a
//    still of the rows against the position, then a lut2 of that against a still of the columns whose table is the
//    tile itself, written as an expression of row and column); classes 1-8;
//  - per plane, a lut from the class to a packed value (the cat's colour index, or the light's colour in 5 bits and its
//    alpha in 3), and a lut2 against the background paints it, the light blended over; scaled up (nearest). The
//    background alone: the sky's row, or a cloud (the clouds the same way as the tile: rows shifted by the position,
//    and a table of 20 random clouds over 243 rows); over the art: the art.
// Stills are one frame each (movie=), never looped. YUV and gray only, nothing converted, no geq per frame.
// Over the art: the art's top as it is, and in the 640x200 strip under it the same cat at 4-pixel pixels (its head at
// the strip's top, its body climbing below), its light 4..56 pixels wide over the art, the art showing everywhere else.
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
		enum { F_HEAD, F_TROW, F_TCOL, F_HUE, F_DIST, PER = 5, F_ROW = 2 * PER, F_COL, NFILES };
		const char* const FILES[NFILES] = {"lcat8-head-a.pgm", "lcat8-trow-a.pgm", "lcat8-tcol-a.pgm", "lcat8-hue-a.pgm",
										   "lcat8-dist-a.pgm", "lcat8-head-s.pgm", "lcat8-trow-s.pgm", "lcat8-tcol-s.pgm",
										   "lcat8-hue-s.pgm",  "lcat8-dist-s.pgm", "lcat8-row.pgm",	   "lcat8-col.pgm"};

		// The cat: the owner's AI-made tall tabby, parsed with the owner's newest settings (a 53x72 grid of 20.25x20 px
		// cells, offset 2.5,5, sampled at the point (84%, 43%... sx 84, sy 43), box 30, bg 70, columns 8-41, rows from 13,
		// the tile rows 39-56, point mode, snapped, with the owner's edits (8 cells cleared, the tile's stray sparkles
		// outside the body among them); palette as PAL below). The head, paws, collar and the
		// top of the body as drawn; then the body's tile (its stripes and spots), repeated down.
		const int COLS = 34, HEADR = 26, TILER = 18;
		const char* const HEAD[HEADR] = {
			"                                  ", "        aaaa        aaaa          ", "        aaaaa       aaaa          ",
			"       allllaa    aallllaa        ", "       allllaa    aallllaa        ", "       alpplllaaaalllpplaa        ",
			"       apppplmmmmmmmpppkaaa       ", "       apppplmmllmmmpppllaa       ", "  y   aapp...mmllmlllppllaa       ",
			" yyy  a......mmllmlllllllaa       ", "  y   a........lllllllmmmaa    y  ", "    aa..............lllllllaa yyy ",
			"    aa...aa.......aalllllllaa  y  ", "    aa...aa.......aa.llllllaa     ", "    aamm.aa.......aa..lmmmmaa     ",
			"    aamm....a...a.......lllaa     ", "    aa.......aaa.........llaaa    ", "      aaa............aaaalllaa    ",
			"    aa...a..........a......laa    ", "   aaa...a..........a......laa    ", "   al.....aarrrrrraa....kaalaa    ",
			"   al..kkkaaaayyaaaa...kkaalaa    ", "   al.kkkaaaakyyyaaa..kkaaalaa    ", "   am.kkkaalayyyyaaa..kkalllaa    ",
			"    aaaaaa....aaa...aaaallllaa    ", " y  aaaa a..........llllllllaa    "};
		const char* const TILE[TILER] = {
			"        aa..........llllllllaa    ", "        aa............llmmmmaa    ", "        aa............llmmmmaa    ",
			"        aa..kk........llllllaa    ", "        aa..kk........llllllaa    ", "        aa............llllllaa    ",
			"        aa........kk..llllllaa    ", "        aa........kk..llmmmmaa    ", "        aa............llmmmmaa    ",
			"        aa............llllllaa    ", "        aa............llllllaa    ", "        aa...kk........lllllaa    ",
			"        aa.............llmmmaa    ", "        aa.............llmmmaa    ", "        aa.............llmmmaa    ",
			"        aa........kk...lllllaa    ", "        aa.............lllllaa    ", "        aa.............lllllaa    "};
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
		const Layout ALONE = {80, 60, 8, 23, 5, 8, 120, true};
		const Layout STRIP = {160, 50, 4, 63, 0, 4, 56, false};
		int rowsOf(const Layout& L) { return L.H - L.oy; }	   // the cat's rows on screen
		int tileRows(const Layout& L) { return rowsOf(L) - HEADR; }

		// the climb, in cloud rows: the position wraps at CLOUDP (the tile's rows move 2/3 as far: 162 of them, 9 tiles);
		// its speed BASE a second, up to BASE + BOOST at a loudness (mean |sample|, eased) of LOUD and over
		const int CLOUDP = 243, NCLOUDS = 20;
		const char* const BASE = "9", * const BOOST = "13.5", * const LOUD = "0.18";
		const int HSTEP = 53;	// the hue a step (24 degrees) every HSTEP frames

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

		// a still's size: per layout (alone, then over the art) the head, the tile's rows and columns, the hues, the
		// distances; then the sky's rows and columns (alone)
		void dims(int k, int& w, int& h)
		{
			if (k >= F_ROW) { w = ALONE.W; h = ALONE.H; return; }
			const Layout& L = k < PER ? ALONE : STRIP;
			switch (k % PER)
			{
				case F_HEAD: w = COLS; h = rowsOf(L); return;
				case F_TROW:
				case F_TCOL: w = COLS; h = tileRows(L); return;
				case F_HUE: w = HUES; h = L.H; return;
				default: w = 640; h = L.H; return;
			}
		}

		std::vector<unsigned char> still(int k)
		{
			int w, h;
			dims(k, w, h);
			std::vector<unsigned char> m((size_t)w * h, 0);
			auto fill = [&](auto v) {
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) m[(size_t)y * w + x] = (unsigned char)v(x, y);
			};
			if (k == F_ROW) { fill([](int, int y) { return y; }); return m; }
			if (k == F_COL) { fill([](int x, int) { return x; }); return m; }
			const Layout& L = k < PER ? ALONE : STRIP;
			const int R = rowsOf(L);
			switch (k % PER)
			{
				case F_HEAD:	// the head (0 under it)
					fill([](int x, int y) { return y < HEADR ? cls(HEAD[y][x]) : 0; });
					break;
				case F_TROW: fill([](int, int y) { return y; }); break;
				case F_TCOL: fill([](int x, int) { return x; }); break;
				case F_HUE:	// the hues: column d, the cat's row r: its band plus d, round the 15
					fill([&](int d, int y) { return y < L.oy ? 0 : ((R - 1 - (y - L.oy)) / 2 + d) % HUES; });	// a hue a bar (2 rows), from the bottom
					break;
				default:	// the bars: every row one, touching, each pixel's distance out from the cat's widest
							// edge, 1..wmax, so equal levels end level whatever the row's outline; between the row's own
							// outline and that edge 1 (the bar still meets the cat); 0 elsewhere
				{
					int lmin = COLS, rmax = -1;
					for (int k = 0; k < HEADR + TILER; k++)
					{
						std::string row = k < HEADR ? HEAD[k] : TILE[k - HEADR];
						size_t l = row.find_first_not_of(' '), rr = row.find_last_not_of(' ');
						if (l == std::string::npos) continue;
						lmin = std::min(lmin, (int)l);
						rmax = std::max(rmax, (int)rr);
					}
					const int fL = (L.ox + lmin) * L.P, fR = (L.ox + rmax + 1) * L.P;
					for (int r = 0; r < R; r++)
					{
						std::string row = rowAt(r);
						size_t l = row.find_first_not_of(' '), rr = row.find_last_not_of(' ');
						if (l == std::string::npos) continue;	// (no cat on the row, above the ears: no bar, else it floats)
						int eL = (L.ox + (int)l) * L.P, eR = (L.ox + (int)rr + 1) * L.P;
						unsigned char* line = &m[(size_t)(r + L.oy) * w];
						for (int x = std::max(0, fL - L.wmax); x < eL; x++) line[x] = (unsigned char)std::max(1, fL - x);
						for (int x = eR; x < std::min(w, fR + L.wmax); x++) line[x] = (unsigned char)std::max(1, x - fR + 1);
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

		// the tile as a lut2's table: x its row (0-17), y its column: the class. Each row as three numbers (12 columns
		// of 4 bits each, the classes going to 8: under 2^53, exact in a double)
		std::string tileExpr()
		{
			std::string e = "st(0,0);";
			for (int r = 0; r < TILER; r++)
			{
				long long part[3] = {0, 0, 0};
				for (int c = COLS - 1; c >= 0; c--) part[c / 12] = part[c / 12] * 16 + cls(TILE[r][c]);
				e += "st(0,if(eq(x," + n(r) + "),if(lt(y,12)," + n(part[0]) + ",if(lt(y,24)," + n(part[1]) + "," + n(part[2]) +
					 ")),ld(0)));";
			}
			return "'" + e + "mod(floor(ld(0)/pow(16,mod(y,12))),16)'";
		}

		// the clouds as a lut2's table: x a row of their CLOUDP (wrapping), y a column: 0 none, 1 white, 2 its pale
		// underside (the preview's random clouds: 6-14 wide, 2-3 high, their top row 2 narrower each side)
		std::string cloudExpr()
		{
			uint32_t s = 12345;
			auto rnd = [&]() { s = s * 1103515245u + 12345u; return (double)((s >> 8) & 0xffffff) / 16777216.0; };
			std::string e = "st(1,0);";
			for (int i = 0; i < NCLOUDS; i++)
			{
				int cx = (int)std::floor(rnd() * 70) - 5, cw = 6 + (int)std::floor(rnd() * 9), ch = 2 + (int)std::floor(rnd() * 2);
				int cy = (int)std::lround(i * (double)CLOUDP / NCLOUDS) + (int)std::floor(rnd() * 5) - 2;
				cy = (cy % CLOUDP + CLOUDP) % CLOUDP;
				e += "st(0,mod(x-" + n(cy) + "+" + n(CLOUDP) + "," + n(CLOUDP) + "));st(1,if(lt(ld(0)," + n(ch) + ")*gte(y," + n(cx) +
					 "+2*eq(ld(0),0))*lte(y," + n(cx + cw - 1) + "-2*eq(ld(0),0)),1+eq(ld(0)," + n(ch - 1) + "),ld(1)));";
			}
			return "'" + e + "ld(1)'";
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
		for (int k = 0; k < NFILES; k++) all = all && ready(k);
		if (all) return true;
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < NFILES; k++)
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
		const int base = overCover ? PER : 0, R = rowsOf(L), TR = tileRows(L);
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + FILES[k < F_ROW ? base + k : k], s); return s; };
		const std::string H = n(L.H);

		Yuv cat[8];
		for (int i = 0; i < 8; i++) cat[i] = yuv(PAL[i]);
		// a plane's lut from the class to its packed value: 0 nothing; 1-8 the cat's colour; a light's: its colour in
		// 5 bits (2..31 of 16..240) and its alpha in 3. The light at strength v: hsl(hue x 24, 100%, 38..58%)
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
							"st(2,(ld(0)+0.5)/" + n(STEPS) + ");st(3,0.38+0.2*ld(2));"
							"st(4," + f(o.*plane) + "+" + f(one.*plane - o.*plane) + "*ld(3)-min(ld(3),1-ld(3))*(" + gh + "));"
							"if(lt(val," + n(LIGHT) + "),val,8*clip(round((ld(4)-16)*31/224),2,31)+round(ld(2)*7))";
			return "'" + e + "'";
		};
		// a plane's lut2: the packed value (x) over the background (y: alone the sky's class, 0-59 its row, 60-61 a
		// cloud; over the art the art); a light's alpha 0.06..1
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
			return "'st(0," + bg + ");if(lt(x,9)," + catc + ",st(1,0.06+0.94*mod(x,8)/7);"
				   "round(ld(1)*(16+floor(x/8)*224/31)+(1-ld(1))*ld(0)))'";
		};

		std::string g = "[aid1] asplit=3 [ao] [a] [ap]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [lcart1] [lcart2]; [lcart1] crop=640:280:0:0 [top]; "
				 "[lcart2] crop=640:200:0:280,extractplanes=y+u+v [lcby] [lcbu] [lcbv]; ";
		// the 16 bands measured (the top 8 measured and cut off, as Pocket console), contrasted, eased (up at once, down
		// over ~0.15 s: the exp/log lagfun)
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=24x256:mode=bar:cmode=combined:ascale=log:"
			 "fscale=log:win_size=1024:rate=60:colors=white,crop=16:256:0:0,scale=16:1:flags=area,format=gray,bplut=c0='val*252/255'" +
			 vizContrast("lc", 1, "8", "2.2") + ",format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split [lclv] [lcd1]; ";
		// the climb's position (cloud rows, wrapped): the loudness eased, the speed from it (capped), integrated per
		// sample; each 60th of a second its last value drawn as a 1x1 picture whose red is the value
		g += "[ap] aformat=sample_rates=44100|48000:channel_layouts=mono,aeval='abs(val(0))',lowpass=f=0.35:poles=1,"
			 "aeval='(" + std::string(BASE) + "+" + BOOST + "*min(val(0)/" + LOUD + ",1))/s',aintegral,"
			 "aeval='floor(mod(val(0)," + n(CLOUDP) + "))+0.5',asetnsamples=n=160,"
			 "astats=metadata=1:reset=1:measure_perchannel=Max_level:measure_overall=none,"
			 "adrawgraph=m1=lavfi.astats.1.Max_level:fg1='0xff000000+floor(VAL)':min=-1000:max=1000:mode=bar:slide=replace:"
			 "s=1x2:r=60:bg=black,crop=1:1:0:1,extractplanes=r,setparams=color_trc=unknown:color_primaries=unknown:colorspace=unknown" + (L.sky ? ",split [lcp1] [lcp2]" : " [lcp1]") + "; ";
		// the light: each row its band's level, against each pixel's distance from the outline: 0, or 1 + its step
		// (the level's strength, the same all along it)
		const std::string wexp = "(" + n(L.wmin) + "+" + n(L.wmax - L.wmin) + "*pow(x/252,1.4))";
		g += "[lclv] transpose=2,scale=1:" + n((R + 1) / 2) + ":flags=bilinear,scale=640:" + n((R + 1) / 2 * 2) + ":flags=neighbor,crop=640:" + n(R) + ":0:" + n((R + 1) / 2 * 2 - R) + ",pad=640:" + H + ":0:" + n(L.oy) + " [lclc]; "
			 "movie=" + q(F_DIST) + ",format=gray [lcdist]; "
			 "[lclc] [lcdist] bplut2=c0='if(y*lte(y," + wexp + "),1+min(" + n(STEPS - 1) + ",floor(" + n(STEPS) +
			 "*(0.12+0.88*pow(x/252,1.6)))),0)' [lcmask]; ";
		// its hues, the column of the moment (the drift)
		g += "[lcd1] scale=" + n(HUES) + ":" + H + ":flags=neighbor [lcd1s]; movie=" + q(F_HUE) + ",format=gray [lchm]; "
			 "[lcd1s] [lchm] bplut2=c0='y',crop=1:" + H + ":x='mod(floor(n/" + n(HSTEP) + ")," + n(HUES) + ")':y=0,"
			 "scale=640:" + H + ":flags=neighbor [lchue]; "
			 "[lcmask] [lchue] bplut2=c0='if(x," + n(LIGHT - 1) + "+x+" + n(STEPS) + "*y,0)' [lclight]; ";
		// the cat: its body's rows the tile's shifted by the position (2/3 of it), under the head; put in its place,
		// every screen column
		g += "[lcp1] scale=" + n(COLS) + ":" + n(TR) + ":flags=neighbor [lcps]; movie=" + q(F_TROW) + ",format=gray [lctr]; "
			 "movie=" + q(F_TCOL) + ",format=gray [lctc]; movie=" + q(F_HEAD) + ",format=gray [lchd]; "
			 "[lcps] [lctr] bplut2=c0='mod(y-floor(x*2/3)+" + n(TILER * 20) + "," + n(TILER) + ")' [lctr2]; "
			 "[lctr2] [lctc] bplut2=c0=" + tileExpr() + ",pad=" + n(COLS) + ":" + n(R) + ":0:" + n(HEADR) + " [lctl]; "
			 "[lctl] [lchd] bplut2=c0='if(y,y,x)',scale=" + n(COLS * L.P) + ":" + n(R) + ":flags=neighbor,"
			 "pad=640:" + H + ":" + n(L.ox * L.P) + ":" + n(L.oy) + ",setsar=1 [lccat]; "
			 "[lccat] [lclight] bplut2=c0='if(x,x,y)',split=3 [lck0] [lck1] [lck2]; ";

		if (L.sky)	// the sky's classes: its row, or a cloud (the clouds' rows shifted by the position)
			g += "[lcp2] scale=" + n(L.W) + ":" + H + ":flags=neighbor [lcp2s]; movie=" + q(F_ROW) + ",format=gray,split [lcrow1] [lcrow2]; "
				 "movie=" + q(F_COL) + ",format=gray [lccol]; "
				 "[lcp2s] [lcrow1] bplut2=c0='mod(y-x+" + n(CLOUDP) + "," + n(CLOUDP) + ")' [lccr]; "
				 "[lccr] [lccol] bplut2=c0=" + cloudExpr() + " [lccl]; "
				 "[lccl] [lcrow2] bplut2=c0='if(x,59+x,y)',split=3 [lcs0] [lcs1] [lcs2]; "
				 "[lcs0] scale=640:" + H + ":flags=neighbor [lcby]; [lcs1] scale=320:" + H + ":flags=neighbor [lcbu]; "
				 "[lcs2] scale=320:" + H + ":flags=neighbor [lcbv]; ";

		if (overCover)
			return g + "[lck0] bplut=c0=" + pack(&Yuvd::y) + ",scale=640:200:flags=neighbor [lcq0]; "
					   "[lcq0] [lcby] bplut2=c0=" + paint(&Yuvd::y, &Yuv::y) + " [lcY]; "
					   "[lck1] scale=320:" + H + ":flags=neighbor,bplut=c0=" + pack(&Yuvd::u) + ",scale=320:100:flags=neighbor [lcq1]; "
					   "[lcq1] [lcbu] bplut2=c0=" + paint(&Yuvd::u, &Yuv::u) + " [lcU]; "
					   "[lck2] scale=320:" + H + ":flags=neighbor,bplut=c0=" + pack(&Yuvd::v) + ",scale=320:100:flags=neighbor [lcq2]; "
					   "[lcq2] [lcbv] bplut2=c0=" + paint(&Yuvd::v, &Yuv::v) + " [lcV]; "
					   "[lcY] [lcU] [lcV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "[lck0] bplut=c0=" + pack(&Yuvd::y) + " [lcq0]; "
				   "[lcq0] [lcby] bplut2=c0=" + paint(&Yuvd::y, &Yuv::y) + ",scale=640:480:flags=neighbor [lcY]; "
				   "[lck1] scale=320:" + H + ":flags=neighbor,bplut=c0=" + pack(&Yuvd::u) + " [lcq1]; "
				   "[lcq1] [lcbu] bplut2=c0=" + paint(&Yuvd::u, &Yuv::u) + ",scale=320:240:flags=neighbor [lcU]; "
				   "[lck2] scale=320:" + H + ":flags=neighbor,bplut=c0=" + pack(&Yuvd::v) + " [lcq2]; "
				   "[lcq2] [lcbv] bplut2=c0=" + paint(&Yuvd::v, &Yuv::v) + ",scale=320:240:flags=neighbor [lcV]; "
				   "[lcY] [lcU] [lcV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
