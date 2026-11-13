// Washing machine: pixel art on an 80x60 grid of 8-pixel dots. A pale blue-grey tiled laundry wall and a grey floor;
// on it a white washing machine (a control strip on top with a dial and a blue light, a round grey door with a blue
// window, little feet) slowly walking left and right (a sine of the frame number, ~10 s a round trip) and shaking and
// hopping with the bass (bands 0-2); in its window a pink sock, a yellow rubber duck and a white sock tumbling round,
// white foam specks spinning a little faster, all quicker with the mids (4-9); a grey "thunk" beside it on big hits.
// A frame: the 16 bands measured, contrasted and eased (as Pocket console's bars), and from them the bass and the mids.
// Everything is a picture of classes (0 see-through), painted at the end by a lut per plane:
//  - the window's contents: an atlas of one turn in 40 phases (the items in a row, the foam in a row under it); four
//    speeds, each a crop of it by the frame number, the mids' step picking one (the others gated to 0);
//  - the machine: its still with the contents laid in its window; three copies, one per step of the bass (still, a
//    little shake and hop, a big one), each gated by the bass and placed by a crop of a padded canvas whose x/y are
//    expressions of the frame number (the walk + that step's shake and hop): only one is drawn;
//  - the "thunk": a 3x5 pixel word at twice the grid's resolution (4-pixel dots), gated by the big step, placed the
//    same way beside the machine's top.
// Stills are one frame each (movie=), never looped. YUV and gray only, nothing converted, no geq per frame.
// Over the art: the art's top as it is, and in the 640x200 strip under it the same at 4-pixel dots (160x50): a floor
// along the bottom and a machine at that size walking across it; the art shows through everywhere else.
#include "viz_more.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
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
		const char* const FILES[5] = {"washer-scene-a.pgm", "washer-scene-s.pgm", "washer-machine.pgm", "washer-tumble.pgm",
									  "washer-thunk2.pgm"};

		// the drawing's grid (W x H dots of P pixels), the fine layer's factor (the thunk's grid: F times the dots), the
		// machine's place: its top-left at (XC + walk, Y0), the walk XAMP each way; the thunk's left at F x x0 - TXO,
		// its top at F x y0 + TYO
		struct Layout
		{
			int W, H, F, XC, XAMP, Y0, TXO, TYO;
			bool room;	// the wall and floor (alone), or a floor over the art
		};
		const Layout ALONE = {80, 60, 2, 25, 12, 16, 21, 3, true};
		const Layout STRIP = {160, 50, 1, 64, 40, 13, 20, 1, false};
		const double PI = 3.14159265358979323846;

		// the machine (32x36 with its feet), its window's middle; the contents' box (CW square, a turn in PH phases)
		const int BW = 32, BH = 36, WX = 16, WY = 20, CW = 19, PH = 40;
		// the word's still
		const int TW = 19, TH = 5;

		enum
		{
			C_NONE,
			C_WALL,
			C_TILE,
			C_FLOOR,
			C_FLOORDOT,
			C_BODY,
			C_STRIP,
			C_DIAL,
			C_LIGHT,
			C_DOOR,
			C_WINDOW,
			C_FEET,
			C_PINK,
			C_YELLOW,
			C_BEAK,
			C_EYE,
			C_WSOCK,
			C_FOAM,
			C_THUNK,
			C_COUNT
		};
		// the foam: white at 0.6 over the window's blue
		const unsigned RGB[C_COUNT] = {0xd8e4ec, 0xd8e4ec, 0xc8d6e0, 0x9aa8b4, 0xb8c4d0, 0xf4f6f8, 0xe0e4ea,
									   0x9aa8b4, 0x5ad1ff, 0x9aa8b4, 0x3a7ab8, 0x5a6a78, 0xff6a8a, 0xffd23f,
									   0xff8a1a, 0x1a1a1a, 0xffffff, 0xb0cae3, 0x5a6a78};

		// the bass's steps (0..252, after the contrast and easing: the preview's 0.28 and 0.6 x 252); each step's
		// shake (dots each way) and hop (dots up); the mids' steps for the four speeds
		const int B1 = 70, B2 = 151, M1 = 63, M2 = 126, M3 = 189;
		const double SHAKE[3] = {0, 0.8, 1.5}, HOP[3] = {0, 1.0, 2.0};
		// the contents' speeds in phases a frame (a turn: PH phases; the preview's 2 + 10 x mid rad/s at each step's
		// middle), the foam 1.3 times as fast
		const double SPEED[4] = {0.345, 0.61, 0.875, 1.14};

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
		std::string d(double v)
		{
			char b[32];
			std::snprintf(b, sizeof b, "%.4g", v);
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

		int jsRound(double v) { return (int)std::floor(v + 0.5); }

		void dims(int k, int& w, int& h)
		{
			if (k == 0) { w = ALONE.W; h = ALONE.H; return; }
			if (k == 1) { w = STRIP.W; h = STRIP.H; return; }
			if (k == 2) { w = BW; h = BH; return; }
			if (k == 3) { w = CW * PH; h = 2 * CW; return; }
			w = TW;
			h = TH;
		}

		std::vector<unsigned char> still(int k)
		{
			int w, h;
			dims(k, w, h);
			std::vector<unsigned char> m((size_t)w * h, 0);
			auto put = [&](int x, int y, int c) {
				if (x >= 0 && y >= 0 && x < w && y < h) m[(size_t)y * w + x] = (unsigned char)c;
			};
			auto rect = [&](int x0, int y0, int rw, int rh, int c) {
				for (int y = y0; y < y0 + rh; y++)
					for (int x = x0; x < x0 + rw; x++) put(x, y, c);
			};
			auto disc = [&](int cx, int cy, int r, int c) {
				for (int y = -r; y <= r; y++)
					for (int x = -r; x <= r; x++)
						if (x * x + y * y <= r * r + r) put(cx + x, cy + y, c);
			};
			if (k == 0)	// the laundry: tiled wall, floor with a lighter edge
			{
				rect(0, 0, w, h, C_WALL);
				for (int x = 0; x < 80; x += 6)
					for (int y = 0; y < 48; y += 6) put(x + (y / 6 % 2) * 3, y, C_TILE);
				rect(0, 50, 80, 10, C_FLOOR);
				for (int x = 0; x < 80; x += 5) put(x, 50, C_FLOORDOT);
				return m;
			}
			if (k == 1)	// over the art: a floor along the strip's bottom
			{
				rect(0, 47, w, 3, C_FLOOR);
				for (int x = 0; x < w; x += 5) put(x, 47, C_FLOORDOT);
				return m;
			}
			if (k == 2)	// the machine, its window empty
			{
				rect(0, 0, 32, 34, C_BODY);
				rect(0, 0, 32, 6, C_STRIP);
				disc(25, 3, 1, C_LIGHT);
				rect(3, 2, 10, 2, C_DIAL);
				disc(WX, WY, 11, C_DOOR);
				disc(WX, WY, 9, C_WINDOW);
				rect(2, 34, 4, 2, C_FEET);
				rect(26, 34, 4, 2, C_FEET);
				return m;
			}
			if (k == 3)	// a turn: the socks and the duck (top row), the foam (under it)
			{
				const int c = CW / 2;
				for (int p = 0; p < PH; p++)
				{
					double turn = 2 * PI * p / PH;
					auto at = [&](int x, int y, int col, int row) {
						if (x >= 0 && x < CW && y >= 0 && y < CW) m[(size_t)(y + row * CW) * w + p * CW + x] = (unsigned char)col;
					};
					const double phase[3] = {0, 2.1, 4.2};
					const int col[3] = {C_PINK, C_YELLOW, C_WSOCK};
					for (int i = 0; i < 3; i++)
					{
						double a = turn + phase[i];
						int ix = c + jsRound(std::cos(a) * 5), iy = c + jsRound(std::sin(a) * 5);
						if (i == 1)
						{
							for (int y = -2; y <= 2; y++)
								for (int x = -2; x <= 2; x++)
									if (x * x + y * y <= 6) at(ix + x, iy + y, C_YELLOW, 0);
							at(ix + 2, iy, C_BEAK, 0);
							at(ix - 1, iy - 1, C_EYE, 0);
						}
						else
						{
							for (int y = iy - 2; y < iy + 2; y++)
								for (int x = ix - 1; x < ix + 2; x++) at(x, y, col[i], 0);
							at(ix + 1, iy + 1, col[i], 0);
							at(ix + 2, iy + 1, col[i], 0);
						}
					}
					for (int i = 0; i < 6; i++)
						at(c + jsRound(std::cos(turn + i) * 7), c + jsRound(std::sin(turn + i) * 7), C_FOAM, 1);
				}
				return m;
			}
			// "thunk", 3x5 letters a column apart
			const char* const GLYPH[5][5] = {{".#.", "###", ".#.", ".#.", ".##"},
											 {"#..", "#..", "###", "#.#", "#.#"},
											 {"...", "#.#", "#.#", "#.#", "###"},
											 {"...", "##.", "#.#", "#.#", "#.#"},
											 {"#..", "#.#", "##.", "#.#", "#.#"}};
			for (int l = 0; l < 5; l++)
				for (int y = 0; y < 5; y++)
					for (int x = 0; x < 3; x++)
						if (GLYPH[l][y][x] == '#') put(l * 4 + x, y, C_THUNK);
			return m;
		}
	}

	bool writeWasherMaps(const std::string& dir)
	{
		auto path = [&](int k) { return dir + "/" + FILES[k]; };
		auto ready = [&](int k) {
			int w, h;
			dims(k, w, h);
			struct stat st;
			return stat(path(k).c_str(), &st) == 0 && (size_t)st.st_size == header(w, h).size() + (size_t)w * h;
		};
		bool all = true;
		for (int k = 0; k < 5; k++) all = all && ready(k);
		if (all) return true;
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < 5; k++)
		{
			int w, h;
			dims(k, w, h);
			if (!pgm(path(k), w, h, still(k))) return false;
		}
		return true;
	}

	std::string washerGraph(bool overCover, const std::string& dir)
	{
		const Layout& L = overCover ? STRIP : ALONE;
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + FILES[k], s); return s; };
		const std::string W = n(L.W), H = n(L.H), FW = n(L.W * L.F), FH = n(L.H * L.F);
		// the machine's top-left at the bass's step k: the walk (a slow sine), the shake (quick), the hop
		auto mx = [&](int k) {
			std::string e = n(L.XC) + "+round(" + n(L.XAMP) + "*sin(n*0.01))";
			if (SHAKE[k] > 0) e += "+round(" + d(SHAKE[k]) + "*sin(n*0.75))";
			return e;
		};
		auto my = [&](int k) {
			std::string e = n(L.Y0);
			if (HOP[k] > 0) e += "-round(" + d(HOP[k]) + "*abs(sin(n*0.3667)))";
			return e;
		};
		const std::string bassStep = "(gte(x," + n(B1) + ")+gte(x," + n(B2) + "))";
		const std::string midStep = "(gte(x," + n(M1) + ")+gte(x," + n(M2) + ")+gte(x," + n(M3) + "))";

		Yuv col[C_COUNT];
		for (int k = 0; k < C_COUNT; k++) col[k] = yuv(RGB[k]);
		// a lut from the class (var) to a plane's value; over the art, 0 is the art's (y) and the word is the wall's pale
		// grey (the dark grey is lost on most covers)
		const Yuv pale = yuv(RGB[C_WALL]);
		auto paint = [&](int Yuv::*plane, const char* var, bool art) {
			std::string e = art ? "y" : n(col[0].*plane);
			for (int k = C_COUNT - 1; k >= 1; k--)
				e = std::string("if(eq(") + var + "," + n(k) + ")," + n(art && k == C_THUNK ? pale.*plane : col[k].*plane) + "," + e + ")";
			return "'" + e + "'";
		};

		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [wmart1] [wmart2]; [wmart1] crop=640:280:0:0 [top]; "
				 "[wmart2] crop=640:200:0:280,extractplanes=y+u+v [wmay] [wmau] [wmav]; ";
		// the 16 bands measured (the top 8 measured and cut off, as Pocket console), contrasted, eased (up at once, down
		// over ~0.15 s: the exp/log lagfun), then the bass and the mids
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=24x256:mode=bar:cmode=combined:ascale=log:"
			 "fscale=log:win_size=1024:rate=60:colors=white,crop=16:256:0:0,scale=16:1:flags=area,format=gray,bplut=c0='val*252/255'" +
			 vizContrast("wm") + ",format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split [wmb] [wmm]; "
			 "[wmb] crop=3:1:0:0,scale=1:1:flags=area,split [wmb1] [wmb2]; "
			 "[wmb1] scale=" + n(BW) + ":" + n(BH) + ":flags=neighbor,split=3 [wmbb0] [wmbb1] [wmbb2]; "
			 "[wmb2] scale=" + n(TW) + ":" + n(TH) + ":flags=neighbor [wmbt]; "
			 "[wmm] crop=6:1:4:0,scale=1:1:flags=area,split [wmm1] [wmm2]; "
			 "[wmm2] scale=" + n(CW) + ":" + n(CW) + ":flags=neighbor,split=4 [wmmg0] [wmmg1] [wmmg2] [wmmg3]; ";

		// the contents: the atlas made a stream (by the mids' frames), a crop of a phase per speed (items and foam),
		// each speed gated by the mids' step, merged
		g += "[wmm1] scale=" + n(CW * PH) + ":" + n(2 * CW) + ":flags=neighbor [wmma]; movie=" + q(3) + ",format=gray [wmat]; "
			 "[wmma] [wmat] bplut2=c0='y',split=8 [wma0] [wma1] [wma2] [wma3] [wma4] [wma5] [wma6] [wma7]; ";
		for (int k = 0; k < 4; k++)
		{
			const std::string K = n(k);
			g += "[wma" + K + "] crop=" + n(CW) + ":" + n(CW) + ":x='" + n(CW) + "*mod(floor(n*" + d(SPEED[k]) + ")," + n(PH) + ")':y=0 [wmi" + K + "]; "
				 "[wma" + n(k + 4) + "] crop=" + n(CW) + ":" + n(CW) + ":x='" + n(CW) + "*mod(floor(n*" + d(SPEED[k] * 1.3) + ")," + n(PH) +
				 ")':y=" + n(CW) + " [wmf" + K + "]; "
				 "[wmf" + K + "] [wmi" + K + "] bplut2=c0='if(x,x,y)' [wms" + K + "]; "
				 "[wmmg" + K + "] [wms" + K + "] bplut2=c0='if(eq(" + midStep + "," + K + "),y,0)' [wmg" + K + "]; ";
		}
		g += "[wmg0] [wmg1] bplut2=c0='if(x,x,y)' [wmg01]; [wmg2] [wmg3] bplut2=c0='if(x,x,y)' [wmg23]; "
			 "[wmg01] [wmg23] bplut2=c0='if(x,x,y)',pad=" + n(BW) + ":" + n(BH) + ":" + n(WX - CW / 2) + ":" + n(WY - CW / 2) + " [wmc]; "
			 "movie=" + q(2) + ",format=gray [wmsp]; [wmc] [wmsp] bplut2=c0='if(x,x,y)',split=3 [wmk0] [wmk1] [wmk2]; ";

		// the machine: a copy per bass step, gated, placed by a crop of a canvas (the machine at (W, H) in it)
		const std::string canvas = "pad=" + n(2 * L.W + BW) + ":" + n(2 * L.H + BH) + ":" + W + ":" + H;
		for (int k = 0; k < 3; k++)
		{
			const std::string K = n(k);
			g += "[wmbb" + K + "] [wmk" + K + "] bplut2=c0='if(eq(" + bassStep + "," + K + "),y,0)'," + canvas + ",crop=" + W + ":" + H +
				 ":x='" + W + "-(" + mx(k) + ")':y='" + H + "-(" + my(k) + ")' [wmp" + K + "]; ";
		}
		g += "[wmp0] [wmp1] bplut2=c0='if(x,x,y)' [wmp01]; [wmp2] [wmp01] bplut2=c0='if(x,x,y)' [wmp]; "
			 "movie=" + q(overCover ? 1 : 0) + ",format=gray [wmsc]; [wmp] [wmsc] bplut2=c0='if(x,x,y)'";
		if (L.F > 1) g += ",scale=" + FW + ":" + FH + ":flags=neighbor";
		g += " [wmscene]; ";
		// the thunk on the big step, beside the machine's top at the fine grid
		g += "movie=" + q(4) + ",format=gray [wmtw]; [wmbt] [wmtw] bplut2=c0='if(gte(x," + n(B2) + "),y,0)',"
			 "pad=" + n(2 * L.W * L.F + TW) + ":" + n(2 * L.H * L.F + TH) + ":" + FW + ":" + FH + ",crop=" + FW + ":" + FH + ":x='" + FW +
			 "-(" + n(L.F) + "*(" + mx(2) + ")-" + n(L.TXO) + ")':y='" + FH + "-(" + n(L.F) + "*(" + my(2) + ")+" + n(L.TYO) + ")' [wmt]; "
			 "[wmt] [wmscene] bplut2=c0='if(x,x,y)',split=3 [wmc0] [wmc1] [wmc2]; ";

		if (overCover)
			return g + "[wmc0] scale=640:200:flags=neighbor [wmc0s]; [wmc0s] [wmay] bplut2=c0=" + paint(&Yuv::y, "x", true) + " [wmY]; "
					   "[wmc1] scale=320:100:flags=neighbor [wmc1s]; [wmc1s] [wmau] bplut2=c0=" + paint(&Yuv::u, "x", true) + " [wmU]; "
					   "[wmc2] scale=320:100:flags=neighbor [wmc2s]; [wmc2s] [wmav] bplut2=c0=" + paint(&Yuv::v, "x", true) + " [wmV]; "
					   "[wmY] [wmU] [wmV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "[wmc0] bplut=c0=" + paint(&Yuv::y, "val", false) + ",scale=640:480:flags=neighbor [wmY]; "
				   "[wmc1] bplut=c0=" + paint(&Yuv::u, "val", false) + ",scale=320:240:flags=neighbor [wmU]; "
				   "[wmc2] bplut=c0=" + paint(&Yuv::v, "val", false) + ",scale=320:240:flags=neighbor [wmV]; "
				   "[wmY] [wmU] [wmV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
