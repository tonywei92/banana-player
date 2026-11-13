// Banana dancer: pixel art on an 80x60 grid of 8-pixel dots. A dark purple room; along the bottom a disco floor of 16
// tiles in two staggered rows, each tile its band's colour (pink through blue and green to red, the bass on the left,
// the colours running round the rainbow in ~2 s), lit bright while its band is up; in the middle a pixel banana dances: a curved yellow body (a darker left edge, a pale
// right one), a brown stem, two eyes and a grin, dark arms with white gloves, red shoes and a shadow under it. Its
// pose follows the bass in four steps (squashed and wide, standing, tall, jumping five rows up with its legs out) and
// its grin opens on a strong bass; its left arm follows the low mids, its right the high mids (down, out, up); stars
// twinkle round it at fixed places, each one out once the treble passes its own threshold.
// A frame: the 16 bands measured, contrasted and eased (as Pocket console's bars), and from them five little values:
// bass, low mids, high mids, treble (averages of a few bands) and the floor's 16. Everything is drawn at the grid's
// size (80x60: 4800 dots) by lut2s against still pictures (one frame each, repeated): each still dot holds its row,
// its column (round the banana's middle), its tile or its star, and a lut2's table (computed once) turns "this level,
// that dot" into the dot's class. The banana in two passes of two lut2s: the bass against the row picks the pose and
// the dot's row in it (one byte: pose x 40 + row), then that against the column says what the dot is in that pose
// (body, edge, face, stem, legs, shoe, shadow: the preview's drawing as an expression of row and column). The arms
// the same, from one value (pose x 9 + left angle x 3 + right angle). The floor: the levels stretched over the tiles'
// columns (the front row moved half a tile) against the tile still. The stars: the treble against a still three
// grids wide (the twinkle's three phases), cropped where the time says. The layers merged (each over the one under),
// a lut each makes the luma, U and V from the class, the luma scaled x8 (nearest) with a still mask of the gaps
// between the dots laid over it, the chroma x4. YUV and gray only, nothing converted, no geq per frame.
// Over the art: the art's top as it is, and in the 640x200 strip under it the same at 4-pixel dots (160x50): the
// floor along its bottom and the banana at half size, the stars round it; the art shows through everywhere else and
// between the dots.
#include "viz_more.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	namespace
	{
		// a new name whenever the stills change: they're cached by size
		const char* const FILES[10] = {"dancer2-row-a.pgm", "dancer2-col-a.pgm", "dancer2-floor-a.pgm", "dancer2-stars-a.pgm",
									   "dancer2-gap-a.pgm", "dancer2-row-s.pgm", "dancer2-col-s.pgm", "dancer2-floor-s.pgm",
									   "dancer2-stars-s.pgm", "dancer2-gap-s.pgm"};
		const int STARS = 10;

		// the drawing's grid: alone 80x60 dots of 8 pixels; over the art 160x50 of 4 (640x200)
		struct Layout
		{
			int W, H, P;			// dots across and down, a dot's pixels
			int G;					// the row the feet stand on (at rest); the shadow two below
			int floorTop, tileW, tileH, x0, stagger;	// the floor: its first row, a tile's dots, the back row's left, the front's shift
			int cx;					// the banana's column
			int stars[STARS][2];	// the stars' places
		};
		const Layout ALONE = {80, 60, 8, 49, 52, 4, 3, 2, 2, 41,
							  {{18, 14}, {62, 12}, {14, 30}, {66, 28}, {24, 6}, {56, 22}, {10, 18}, {70, 40}, {30, 20}, {52, 6}}};
		const Layout STRIP = {160, 50, 4, 40, 43, 9, 3, 6, 4, 80,
							  {{40, 8}, {120, 6}, {30, 26}, {132, 24}, {52, 2}, {108, 16}, {18, 14}, {146, 36}, {60, 18}, {100, 2}}};
		const int ROWS = 40;	// the banana's rows: from G-37 (the stem, jumping) to G+2 (the shadow)
		const int COL0 = 64;	// the column still: 64 + the dot's column from the banana's

		// the classes (the merged picture's values): 0 nothing (the room, or the art), the banana's parts, the stars,
		// a tile: 16 + its band (dim), 32 + its band (lit)
		enum { C_NONE, C_SHADOW, C_EDGE, C_BODY, C_PALE, C_STEM, C_DARK, C_GLOVE, C_SHOE, C_STAR, C_STARX, C_TILE = 16, C_LIT = 32, C_COUNT = 48 };

		// the levels' thresholds (0..252, after the contrast and easing): the bass's poses (squashed below B1, standing,
		// tall from B2, jumping from B3) and its grin (open from BM); an arm out from A1, up from A2; the floor's back
		// row lit from F0, the front (nearer) one from F1; the stars from S0, each next one S1 later
		const int FLOORF = 8;	// the floor's colours a step on every 8 frames at 60 (round the rainbow in ~2 s)
		const int B1 = 178, B2 = 206, BM = 222, B3 = 236, A1 = 50, A2 = 100, F0 = 120, F1 = 84, S0 = 30, S1 = 13;

		struct Yuv { int y, u, v; };
		// BT.601 limited (mpv's reading of a picture this size)
		Yuv yuv(unsigned rgb)
		{
			double r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
			return {(int)std::lround(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255),
					(int)std::lround(128 + (-37.797 * r - 74.203 * g + 112 * b) / 255),
					(int)std::lround(128 + (112 * r - 93.786 * g - 18.214 * b) / 255)};
		}
		unsigned hsl(double h, double s, double l)
		{
			double a = s * std::min(l, 1 - l);
			auto f = [&](double n) {
				double k = std::fmod(n + h / 30, 12);
				return l - a * std::max(-1.0, std::min({k - 3, 9 - k, 1.0}));
			};
			auto c = [](double v) { return (unsigned)std::lround(std::max(0.0, std::min(1.0, v)) * 255); };
			return c(f(0)) << 16 | c(f(8)) << 8 | c(f(4));
		}
		// the tiles' hues, the bass's first
		const int HUES[16] = {330, 300, 270, 240, 210, 190, 170, 140, 110, 80, 60, 45, 30, 15, 0, 350};
		const unsigned ROOM = 0x120a22;

		std::string n(int v) { return std::to_string(v); }

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

		// a still's size: k 0-4 alone, 5-9 over the art (row, column, floor, stars, gaps)
		void dims(int k, int& w, int& h)
		{
			const Layout& L = k < 5 ? ALONE : STRIP;
			switch (k % 5)
			{
				case 3: w = 3 * L.W; h = L.H; break;
				case 4: w = 640; h = L.H * L.P; break;
				default: w = L.W; h = L.H;
			}
		}

		std::vector<unsigned char> still(int k)
		{
			const Layout& L = k < 5 ? ALONE : STRIP;
			int w, h;
			dims(k, w, h);
			std::vector<unsigned char> m((size_t)w * h, 0);
			auto at = [&](int x, int y) -> unsigned char& { return m[(size_t)y * w + x]; };
			// the tile a dot (or, x the dot's size, a pixel) is on: its band + 1 + 16 x its row (0 none)
			auto tile = [&](int x, int y) {
				int r = (y - L.floorTop) / L.tileH;
				if (y < L.floorTop || r > 1) return 0;
				int t = x - L.x0 - r * L.stagger;
				if (t < 0 || t >= 16 * L.tileW) return 0;
				return 1 + t / L.tileW + 16 * r;
			};
			switch (k % 5)
			{
				case 0:	// each dot's row
					for (int y = 0; y < h; y++)
						for (int x = 0; x < w; x++) at(x, y) = (unsigned char)y;
					break;
				case 1:	// its column round the banana's
					for (int y = 0; y < h; y++)
						for (int x = 0; x < w; x++) at(x, y) = (unsigned char)std::max(0, std::min(255, x - L.cx + COL0));
					break;
				case 2:
					for (int y = 0; y < h; y++)
						for (int x = 0; x < w; x++) at(x, y) = (unsigned char)tile(x, y);
					break;
				case 3:	// the stars in three phases side by side: each one's dot (1 + it), and in one phase of three its
						// cross round it (11 + it)
					for (int ph = 0; ph < 3; ph++)
						for (int i = 0; i < STARS; i++)
						{
							int x = ph * L.W + L.stars[i][0], y = L.stars[i][1];
							if ((ph + i) % 3 == 0)
							{
								const int d[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
								for (auto& o : d)
									if (y + o[1] >= 0 && y + o[1] < h) at(x + o[0], y + o[1]) = (unsigned char)(11 + i);
							}
							at(x, y) = (unsigned char)(1 + i);
						}
					break;
				case 4:	// the gaps between the dots (255): a dot's last line across and down; a tile is one block (as
						// the preview draws it: 1 pixel in from its left, 2 short of its right and bottom)
					for (int y = 0; y < h; y++)
						for (int x = 0; x < w; x++)
						{
							int dx = x / L.P, dy = y / L.P;
							int t = tile(dx, dy);
							bool gap;
							if (t)
							{
								int r = (t - 1) / 16;
								int lx = x - (L.x0 + r * L.stagger + ((t - 1) % 16) * L.tileW) * L.P;
								int ly = y - (L.floorTop + r * L.tileH) * L.P;
								gap = lx < 1 || lx > L.tileW * L.P - 3 || ly > L.tileH * L.P - 3;
							}
							else	// (at 4-pixel dots over the art, none: a 1-pixel line of art every 4 was noise)
								gap = L.P == 8 && (x % L.P == L.P - 1 || y % L.P == L.P - 1);
							at(x, y) = gap ? 255 : 0;
						}
					break;
			}
			return m;
		}

		// the pose's numbers by the bass's variant v (0 squashed, 1 standing, 2 tall, 3 tall with the grin open, 4
		// jumping): its body's height, its lift, its squash (wider at the middle), whether the grin is open
		const int BODYH[5] = {21, 25, 28, 28, 28}, LIFT[5] = {0, 0, 1, 1, 5}, SQUASH[5] = {4, 2, 0, 0, 0}, OPEN[5] = {0, 0, 0, 1, 1};
		int feetOf(int v, int G) { return G - LIFT[v] - (LIFT[v] ? 1 : 0); }

		// an expression's value by the variant in ld(0): a[0] .. a[k-1]
		std::string byVariant(const int* a, int k, const char* var = "ld(0)")
		{
			std::string e = n(a[k - 1]);
			for (int i = k - 2; i >= 0; i--) e = std::string("if(eq(") + var + "," + n(i) + ")," + n(a[i]) + "," + e + ")";
			return e;
		}
	}

	bool writeDancerMaps(const std::string& dir)
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

	std::string dancerGraph(bool overCover, const std::string& dir)
	{
		const Layout& L = overCover ? STRIP : ALONE;
		const int base = overCover ? 5 : 0;
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + FILES[base + k], s); return s; };
		const std::string W = n(L.W), H = n(L.H), R0 = n(L.G - ROWS + 3);

		// the colours by class
		Yuv col[C_COUNT];
		for (auto& c : col) c = yuv(ROOM);
		const unsigned part[C_TILE] = {ROOM, 0x0a0614, 0xc99a12, 0xffd23f, 0xfff08a, 0x5a3d12, 0x1b1206, 0xffffff, 0xe8463c, 0xfff6c8,
									   0xffd23f, ROOM, ROOM, ROOM, ROOM, ROOM};
		for (int k = 1; k < C_TILE; k++) col[k] = yuv(part[k]);
		for (int b = 0; b < 16; b++)
		{
			col[C_TILE + b] = yuv(hsl(HUES[b], 0.35, 0.18));
			col[C_LIT + b] = yuv(hsl(HUES[b], 0.85, 0.58));
		}
		// a lut from the class (var) to a plane's value; over the art, 0 is the art's (y)
		auto paint = [&](int Yuv::*plane, const char* var, bool art) {
			std::string e = art ? "y" : n(col[0].*plane);
			for (int k = C_COUNT - 1; k >= 1; k--)
				if (k <= C_STARX || k >= C_TILE) e = std::string("if(eq(") + var + "," + n(k) + ")," + n(col[k].*plane) + "," + e + ")";
			return "'" + e + "'";
		};

		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [dcart1] [dcart2]; [dcart1] crop=640:280:0:0 [top]; "
				 "[dcart2] crop=640:200:0:280,extractplanes=y+u+v [dcay] [dcau] [dcav]; ";
		// the 16 bands measured (the top 8 measured and cut off, as Pocket console), contrasted, eased (up at once, down
		// over ~0.15 s: the exp/log lagfun), then the floor's row and the four averages
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=24x256:mode=bar:cmode=combined:ascale=log:"
			 "fscale=log:win_size=1024:rate=60:colors=white,crop=16:256:0:0,scale=16:1:flags=area,format=gray,bplut=c0='val*252/255'" +
			 vizContrast("dc") + ",format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split=5 [dcf] [dcb] [dcl] [dcr] [dct]; "
			 "[dcb] crop=3:1:0:0,scale=1:1:flags=area,split [dcb1] [dcb2]; "
			 "[dcl] crop=2:1:5:0,scale=1:1:flags=area [dcml]; [dcr] crop=2:1:8:0,scale=1:1:flags=area [dcmr]; ";

		// the banana's body: the bass and the row make the variant and the dot's row from R0 (one byte, 255 outside its
		// rows); then that and the column, its class
		const std::string variant = "(gte(x," + n(B1) + ")+gte(x," + n(B2) + ")+gte(x," + n(BM) + ")+gte(x," + n(B3) + "))";
		g += "[dcb1] scale=" + W + ":" + H + ":flags=neighbor [dcbs]; movie=" + q(0) + ",format=gray,split [dcrow1] [dcrow2]; "
			 "movie=" + q(1) + ",format=gray,split [dccol1] [dccol2]; "
			 "[dcbs] [dcrow1] bplut2=c0='if(between(y-" + R0 + ",0," + n(ROWS - 1) + ")," + variant + "*" + n(ROWS) + "+y-" + R0 + ",255)' [dcbv]; ";
		{
			int top[5], feet[5], fy[5], legs[5], legR[5], shadow[5];
			for (int v = 0; v < 5; v++)
			{
				feet[v] = feetOf(v, L.G);
				top[v] = feet[v] - BODYH[v];
				fy[v] = top[v] + (int)std::floor(BODYH[v] * 0.35 + 0.5);
				legs[v] = LIFT[v] ? 3 : 2;
				legR[v] = 1 - (SQUASH[v] > 1 ? 1 : 0);
				shadow[v] = 6 - (LIFT[v] >> 1);
			}
			// ld(0) the variant, ld(1) the row, ld(2) the column (round the banana's), ld(3) the row from the top,
			// ld(4) the body's height, ld(5) the feet's row, ld(6) the face's row, ld(7) the legs' end, ld(8) k (0..1
			// down the body), ld(9) the body's half width
			const std::string sh = "(sin(ld(8)*PI))";
			const std::string body =
				"st(0,floor(x/" + n(ROWS) + "));st(1,mod(x," + n(ROWS) + ")+" + R0 + ");st(2,y-" + n(COL0) + ");"
				"st(4," + byVariant(BODYH, 5) + ");st(5," + byVariant(feet, 5) + ");st(3,ld(1)-ld(5)+ld(4));st(6," + byVariant(fy, 5) + ");"
				"st(7,ld(5)+" + byVariant(legs, 5) + ");st(8,ld(3)/ld(4));"
				"st(9,round(if(lt(ld(8),0.1),1,if(gt(ld(8),0.9),3,5))+" + byVariant(SQUASH, 5) + "*" + sh + "));"
				"if(eq(x,255),0,"
				// the shoes, the legs
				"if(eq(ld(1),ld(7)-1)*(between(ld(2),-6,-5)+between(ld(2),1,2))," + n(C_SHOE) + ","
				"if(between(ld(1),ld(5),ld(7)-1)*(eq(ld(2),-4)+eq(ld(2)," + byVariant(legR, 5) + "))," + n(C_DARK) + ","
				// the eyes, the grin's corners, its middle (a row lower when it's open)
				"if((eq(ld(2),-4)+eq(ld(2),0))*(between(ld(1)-ld(6),0,1)+eq(ld(1)-ld(6),3))+between(ld(2),-3,-1)*eq(ld(1)-ld(6),4+" +
				byVariant(OPEN, 5) + ")," + n(C_DARK) + ","
				// the stem
				"if(eq(ld(2),0)*eq(ld(3),-1)+eq(ld(2),1)*eq(ld(3),-2)," + n(C_STEM) + ","
				// the body: each row a run of 2 x half + 1 dots, bent left at the middle
				"if(between(ld(3),0,ld(4)-1)*lte(abs(ld(2)+round(3*" + sh + ")),ld(9)),"
				"if(eq(ld(2)+round(3*" + sh + "),-ld(9))," + n(C_EDGE) + ",if(eq(ld(2)+round(3*" + sh + "),ld(9))," + n(C_PALE) + "," + n(C_BODY) + ")),"
				// the shadow
				"if(eq(ld(1)," + n(L.G + 2) + ")*lte(abs(ld(2))," + byVariant(shadow, 5) + ")," + n(C_SHADOW) + ",0)))))))";
			g += "[dcbv] [dccol1] bplut2=c0='" + body + "' [dcB]; ";

			// the arms: one value, pose x 9 + left angle x 3 + right angle (0 down, 1 out, 2 up); with the row, the
			// angles and the dot's row from the shoulder (-9..4) as one byte; with the column, the class
			const std::string pose = "(gte(x," + n(B1) + ")+gte(x," + n(B2) + ")+gte(x," + n(B3) + "))";
			int sy[4];
			for (int p = 0; p < 4; p++)
			{
				int v = p < 3 ? p : 4;
				sy[p] = top[v] + (int)std::floor(BODYH[v] * 0.5 + 0.5);
			}
			const std::string ang = "(gte(x," + n(A1) + ")+gte(x," + n(A2) + "))";
			const std::string angY = "(gte(y," + n(A1) + ")+gte(y," + n(A2) + "))";
			g += "[dcml] [dcmr] bplut2=c0='3*" + ang + "+" + angY + "' [dcmm]; "
				 "[dcb2] [dcmm] bplut2=c0='9*" + pose + "+y',scale=" + W + ":" + H + ":flags=neighbor [dcas]; "
				 "[dcas] [dcrow2] bplut2=c0='st(0,floor(x/9));st(1,y-" + byVariant(sy, 4) + ");if(between(ld(1),-9,4),mod(x,9)*14+ld(1)+9,255)' [dcav0]; ";
			// ld(0) the angle (this side's), ld(1) the row from the shoulder, ld(2) the dots out from the shoulder
			// (1..8 the arm, 9 and 10 the glove), ld(3) the glove's row (up +)
			const std::string arm =
				"st(1,mod(x,14)-9);st(4,y-" + n(COL0) + ");"
				"st(0,if(lte(ld(4),-8),floor(floor(x/14)/3),mod(floor(x/14),3)));st(2,if(lte(ld(4),-8),-7-ld(4),ld(4)-3));"
				"st(3,if(eq(ld(0),2),8,if(eq(ld(0),1),4,-4)));"
				"if(eq(x,255)+between(ld(4),-7,3),0,"
				"if(between(ld(2),9,10)*between(-ld(1)-ld(3),0,1)," + n(C_GLOVE) + ","
				"if(between(ld(2),1,8)*eq(-ld(1),if(eq(ld(0),2),ld(2),if(eq(ld(0),1),floor(ld(2)/2),-floor(ld(2)/2))))," + n(C_DARK) + ",0)))";
			g += "[dcav0] [dccol2] bplut2=c0='" + arm + "' [dcA]; ";
		}

		// the floor: the levels over the tiles' columns, the back row's and the front's (half a tile on), down their rows
		const std::string tw = n(16 * L.tileW);
		g += "[dcf] split=3 [dcf1] [dcf2] [dcfc]; [dcf1] scale=" + tw + ":1:flags=neighbor,pad=" + W + ":1:" + n(L.x0) + ":0 [dcfb]; "
			 "[dcf2] scale=" + tw + ":1:flags=neighbor,pad=" + W + ":1:" + n(L.x0 + L.stagger) + ":0 [dcff]; "
			 "[dcfb] [dcff] vstack,scale=" + W + ":" + n(2 * L.tileH) + ":flags=neighbor,pad=" + W + ":" + H + ":0:" + n(L.floorTop) +
			 ",setsar=1 [dcfl]; movie=" + q(2) + ",format=gray [dcfm]; "
			 "[dcfl] [dcfm] bplut2=c0='if(eq(y,0),0," + n(C_TILE) + "+mod(y-1,16)+16*gte(x,if(gte(y,17)," + n(F1) + "," + n(F0) + ")))' [dcF0]; ";
		// the colours changing: each tile's colour a step further round the 16 every FLOORF frames (at 60), the step
		// read off the column still (64 + the column from the banana's) at the banana's column + the step, after a
		// lut2 (a still alone keeps n at 0)
		g += "[dcfc] scale=" + W + ":" + H + ":flags=neighbor,setsar=1 [dcfc2]; movie=" + q(1) + ",format=gray [dcfcol]; "
			 "[dcfc2] [dcfcol] bplut2=c0='y',crop=1:1:x='" + n(L.cx) + "+mod(floor(n/" + n(FLOORF) + "),16)':y=0,"
			 "scale=" + W + ":" + H + ":flags=neighbor [dcfs]; "
			 "[dcF0] [dcfs] bplut2=c0='if(lt(x," + n(C_TILE) + "),x,16*floor(x/16)+mod(x+y-" + n(COL0) + ",16))' [dcF]; ";
		// the stars: the treble against the three phases, one shown (a new one 8 times a second)
		g += "[dct] crop=4:1:12:0,scale=1:1:flags=area,scale=" + n(3 * L.W) + ":" + H + ":flags=neighbor [dcts]; "
			 "movie=" + q(3) + ",format=gray [dcsm]; "
			 "[dcts] [dcsm] bplut2=c0='if(eq(y,0),0,if(gte(x," + n(S0) + "+" + n(S1) + "*mod(y-1,10)),if(lte(y,10)," + n(C_STAR) + "," +
			 n(C_STARX) + "),0))',crop=" + W + ":" + H + ":x='" + W + "*mod(floor(n*8/60),3)':y=0 [dcS]; ";
		// merged: the body and the floor (apart), the arms over them, the stars over all
		g += "[dcB] [dcF] bplut2=c0='x+y' [dcm1]; [dcm1] [dcA] bplut2=c0='if(y,y,x)' [dcm2]; [dcm2] [dcS] bplut2=c0='if(y,y,x)',split=3 [dcc0] [dcc1] [dcc2]; ";

		if (overCover)
			return g + "movie=" + q(4) + ",format=gray [dcG]; "
					   "[dcc0] scale=640:200:flags=neighbor [dcc0s]; [dcc0s] [dcG] bplut2=c0='if(y,0,x)' [dcc0g]; "
					   "[dcc0g] [dcay] bplut2=c0=" + paint(&Yuv::y, "x", true) + " [dcY]; "
					   "[dcc1] scale=320:100:flags=neighbor [dcc1s]; [dcc1s] [dcau] bplut2=c0=" + paint(&Yuv::u, "x", true) + " [dcU]; "
					   "[dcc2] scale=320:100:flags=neighbor [dcc2s]; [dcc2s] [dcav] bplut2=c0=" + paint(&Yuv::v, "x", true) + " [dcV]; "
					   "[dcY] [dcU] [dcV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "movie=" + q(4) + ",format=gray [dcG]; "
				   "[dcc0] bplut=c0=" + paint(&Yuv::y, "val", false) + ",scale=640:480:flags=neighbor [dcY0]; "
				   "[dcY0] [dcG] bplut2=c0='if(y," + n(col[0].y) + ",x)' [dcY]; "
				   "[dcc1] bplut=c0=" + paint(&Yuv::u, "val", false) + ",scale=320:240:flags=neighbor [dcU]; "
				   "[dcc2] bplut=c0=" + paint(&Yuv::v, "val", false) + ",scale=320:240:flags=neighbor [dcV]; "
				   "[dcY] [dcU] [dcV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
