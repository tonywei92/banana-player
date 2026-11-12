// Bubble tea: pixel art on an 80x60 grid of 8-pixel dots. A pale pink background with faint dots; a tall clear cup
// (slightly tapered, a white outline, a lid) of milk tea with a cute face on it: a pale foam layer on top whose surface
// sloshes (a small wave rolling sideways, its height the mids), tan tea below, three ice cubes, and 16 tapioca pearls
// at the bottom in two staggered rows, each a band of the spectrum (the bass on the left): a pearl bounces up by its
// eased level squared x 10 dots. A pink straw goes in at a slant, and white fizz climbs it on the treble (the louder
// it is, the more of the six bubbles are out).
// A frame: 16 bands measured and eased (as Pocket console's bars). The surface: a still 1-row wave times the mids at a
// lut2, cropped at an x that moves with the frame number (it rolls left), giving each column its surface offset (0..6).
// The pearls: each one's lift (0..10), the front row's and the back row's stretched over their columns and put
// together (front x 11 + back) into one row. The two rows stacked and stretched over the rows (the surface's over the
// top half, the pearls' over the bottom) meet a still mask in one lut2: each dot of the mask is a fixed colour, a
// foam dot (its row), or a pearl dot (its row, which of the pearls over its column it may be, and what it is when
// no pearl is there), so the lut2 picks foam, surface or tea, a pearl's highlight, body or underside. The fizz: a
// still column of six bubbles at a lut2 against the treble (a bubble out or not), cropped upward by the frame number
// and spread across, lighting the straw's dots. All of that is 80x60 (4800 dots: next to nothing); every colour is its
// own luma, the luma scaled x8 (nearest), U and V looked up from it at 80x60 and scaled x4. YUV and gray only,
// nothing converted, no geq per frame.
// Over the art: the art's top as it is, and in the 640x200 strip under it (25 dots) a row of 16 round pearls (a milky rim
// round each) bouncing out of a thin layer of tea at the bottom; the art shows through everywhere else.
#include "viz_more.h"
#include "settings.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	// a new name whenever the stills change: they're cached by size
	static const char* BOBA_FILES[4] = {"boba1-alone.pgm", "boba2-strip.pgm", "boba1-wave.pgm", "boba1-fizz.pgm"};
	static const int W = 80, H = 60, SH = 25;		// dots across, down (alone), down (over the art)
	// the cup (as the preview): its left and right at the top, its top (the lid above) and bottom rows
	static const int CX0 = 26, CX1 = 54, TOP = 14, BOT = 54;
	static const int SURF = 20;						// the foam's surface at rest (it moves 3 up or down)
	static const int SLOSH_LAST = 29;				// the rows the surface's offset reaches (the top half)
	static const int PX0 = 29, PCOLS = 24;			// the pearls' first column and their columns (8 x 3)
	static const int PROW0 = 42, PROWS = 12;		// the rows a pearl can be on (front row rests at 52, back at 50)
	static const int WP = 18;						// the surface wave's period in dots
	static const int FIZZ_TOP = 6, FIZZ_ROWS = 45, FIZZ_GAP = 7, FIZZ_N = 6;	// the fizz: rows 6..50, 6 bubbles 7 apart
	// over the art: a pearl's width and height (and pitch), where one rests (its top row), the tea's surface row
	static const int SPP = 5, SREST = 19, STEA = 22;

	// the colours: the mask's fixed dots are these indices
	enum { C_BG, C_DOT, C_WHITE, C_FOAM, C_SURF, C_TEA, C_ICE, C_PTOP, C_PBOT, C_PHI, C_LIDP, C_STRAW1, C_STRAW2, C_CHEEK, C_COUNT };
	static const unsigned RGB[C_COUNT] = {0xffe3ec, 0xffd0de, 0xffffff, 0xfff5f8, 0xf3d7b5, 0xd9a878, 0xf2e2d0,
										  0x3a2418, 0x2a180e, 0x6a4430, 0xffd1e0, 0xff7aa8, 0xff9cc0, 0xff9fb5};
	// the mask's other values: a foam dot (M_FOAM + its row - TOP); a pearl dot (M_PEARL + (fallback x 3 + its kind) x
	// PROWS + its row - PROW0), its kind the pearls over its column (0 a front one's left, 1 a front one's right and a
	// back one's left, 2 a back one's right), its fallback what's there without a pearl (0 tea, 1 the cup's outline,
	// 2 the background)
	enum { M_FOAM = 100, M_PEARL = 128 };
	// over the art: 0 nothing (the art), a pearl dot 1 + its column in the pearl x 25 + its row, the tea's surface, the tea
	enum { S_ART = 0, S_PEARL = 1, S_SURF = 200, S_TEA = 201 };

	static int roundJs(double v) { return (int)std::floor(v + 0.5); }

	static std::vector<unsigned char> aloneMask()
	{
		std::vector<unsigned char> m((size_t)W * H, C_BG);
		auto at = [&](int x, int y) -> unsigned char& { return m[(size_t)y * W + x]; };
		// the background's faint dots
		uint32_t s = 7;
		auto rnd = [&]() { s = s * 1664525u + 1013904223u; return s >> 8; };
		for (int i = 0; i < 24; i++)
		{
			int x = (int)(rnd() % W), y = (int)(rnd() % H);
			at(x, y) = C_DOT;
		}
		// the cup: the outline, the foam (whose rows can slosh), the tea
		for (int y = TOP; y < BOT; y++)
		{
			int e = roundJs((y - TOP) * 3.0 / (BOT - TOP)), l = CX0 + e, r = CX1 - e;
			for (int x = l; x <= r; x++)
				at(x, y) = x == l || x == r ? C_WHITE : y <= SLOSH_LAST ? (unsigned char)(M_FOAM + y - TOP) : C_TEA;
		}
		// the ice
		const int ice[3][2] = {{32, 24}, {44, 22}, {38, 30}};
		for (auto& c : ice)
			for (int a = 0; a < 4; a++)
				for (int b = 0; b < 4; b++) at(c[0] + a, c[1] + b) = a == 0 || b == 0 ? C_WHITE : C_ICE;
		// the pearls' dots, over the tea, the outline or the background
		for (int y = PROW0; y < PROW0 + PROWS; y++)
			for (int x = PX0; x < PX0 + PCOLS; x++)
			{
				unsigned char& p = at(x, y);
				int fb = p == C_TEA ? 0 : p == C_WHITE ? 1 : 2;
				p = (unsigned char)(M_PEARL + (fb * 3 + (x - PX0) % 3) * PROWS + y - PROW0);
			}
		// the lid
		for (int x = CX0 - 1; x <= CX1 + 1; x++)
		{
			at(x, TOP - 1) = C_WHITE;
			at(x, TOP - 2) = C_LIDP;
		}
		// the straw (its left side is where the fizz climbs)
		for (int y = 2; y < BOT - 2; y++)
		{
			at(46 - y / 12, y) = C_STRAW1;
			at(47 - y / 12, y) = C_STRAW2;
		}
		// the face
		const int face[6][2] = {{36, 42}, {43, 42}, {38, 44}, {39, 45}, {40, 45}, {41, 44}};
		for (auto& f : face) at(f[0], f[1]) = C_PTOP;
		at(34, 44) = C_CHEEK;
		at(45, 44) = C_CHEEK;
		return m;
	}

	static std::vector<unsigned char> stripMask()
	{
		std::vector<unsigned char> m((size_t)W * SH, S_ART);
		for (int y = 0; y < SH; y++)
			for (int x = 0; x < W; x++)
			{
				int c = x % SPP;
				m[(size_t)y * W + x] = (unsigned char)(S_PEARL + c * SH + y);
			}
		return m;
	}

	static bool bobaPgm(const std::string& path, int w, int h, const std::vector<unsigned char>& data)
	{
		std::string tmp = path + ".tmp";
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << "P5\n" << w << " " << h << "\n255\n";
		out.write((const char*)data.data(), (std::streamsize)data.size());
		if (!out.flush()) return false;
		out.close();
		return rename(tmp.c_str(), path.c_str()) == 0;
	}

	bool writeBobaMaps(const std::string& dir)
	{
		const int dims[4][2] = {{W, H}, {W, SH}, {W + WP, 1}, {1, FIZZ_ROWS + FIZZ_GAP * FIZZ_N}};
		bool ready = true;
		for (int k = 0; k < 4; k++)
		{
			struct stat st;
			size_t want = ("P5\n" + std::to_string(dims[k][0]) + " " + std::to_string(dims[k][1]) + "\n255\n").size() + (size_t)dims[k][0] * dims[k][1];
			ready = ready && stat((dir + "/" + BOBA_FILES[k]).c_str(), &st) == 0 && (size_t)st.st_size == want;
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);
		// the surface wave: 128 +- 127 (a sine), one period wider than the screen
		std::vector<unsigned char> wave(W + WP), fizz((size_t)dims[3][1], 0);
		for (int x = 0; x < W + WP; x++) wave[x] = (unsigned char)std::lround(128 + 127 * std::sin(6.283185307179586 * x / WP));
		// the fizz: a bubble every 7 rows, each its number (1..6) over a period of 42; the crop's window slides over it
		for (int y = 0; y < (int)fizz.size(); y++)
			if (y % FIZZ_GAP == 0) fizz[y] = (unsigned char)(1 + y / FIZZ_GAP % FIZZ_N);
		return bobaPgm(dir + "/" + BOBA_FILES[0], W, H, aloneMask()) && bobaPgm(dir + "/" + BOBA_FILES[1], W, SH, stripMask()) &&
			   bobaPgm(dir + "/" + BOBA_FILES[2], W + WP, 1, wave) && bobaPgm(dir + "/" + BOBA_FILES[3], 1, dims[3][1], fizz);
	}

	struct BobaYuv { int y, u, v; };
	// BT.601 limited (mpv's reading of a picture this size)
	static BobaYuv bobaYuv(unsigned rgb)
	{
		double r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
		return {(int)std::lround(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255),
				(int)std::lround(128 + (-37.797 * r - 74.203 * g + 112 * b) / 255),
				(int)std::lround(128 + (112 * r - 93.786 * g - 18.214 * b) / 255)};
	}

	std::string bobaGraph(bool overCover, const std::string& mapsDir)
	{
		auto n = [](int v) { return std::to_string(v); };
		auto q = [&](int k) { std::string s; lavfiPath(mapsDir + "/" + BOBA_FILES[k], s); return s; };

		// every colour its own luma (the chroma is looked up from it), nudged apart where two would meet
		BobaYuv c[C_COUNT];
		for (int k = 0; k < C_COUNT; k++)
		{
			c[k] = bobaYuv(RGB[k]);
			for (bool again = true; again;)
			{
				again = false;
				for (int j = 0; j < k; j++)
					if (c[j].y == c[k].y) { c[k].y++; again = true; }
			}
		}
		auto Y = [&](int k) { return n(c[k].y); };

		// 16 bands measured and eased as Pocket console's bars (up at once, down over ~0.15 s); as Peaks the highest 8
		// measured and cut off
		const int N = 16, CUT = 8;
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease:out_range=limited,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [bbart1] [bbart2]; [bbart1] crop=640:280:0:0 [top]; "
				 "[bbart2] crop=640:200:0:280,extractplanes=y+u+v [bbay] [bbau] [bbav]; ";
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + CUT) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("bb") +
			 ",format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray";

		if (overCover)
		{
			// each pearl's lift (its level squared x 17 dots) over its 5 columns and down the strip's rows
			g += ",lut=c0='round(pow(val/252,2)*17)',scale=" + n(W) + ":1:flags=neighbor,scale=" + n(W) + ":" + n(SH) +
				 ":flags=neighbor,setsar=1 [bbL]; movie=" + q(1) + ",format=gray [bbM]; ";
			// a pearl dot: its row from the pearl's top (d) and its column (k): a round 5x5 (no corners), a milky rim
			// round a dark 3x3 (its underside darker, a white highlight up left: it reads on dark art too); else the
			// tea, its surface or the art (0)
			const std::string fb = "if(eq(ld(1)," + n(STEA) + ")," + Y(C_SURF) + ",if(gt(ld(1)," + n(STEA) + ")," + Y(C_TEA) + ",0))";
			const std::string pearls = "'if(eq(y,0),0,if(eq(y," + n(S_SURF) + ")," + Y(C_SURF) + ",if(eq(y," + n(S_TEA) + ")," + Y(C_TEA) +
									   ",st(0,floor((y-1)/" + n(SH) + "));st(1,mod(y-1," + n(SH) + "));st(2,ld(1)-" + n(SREST) +
									   "+x);if(between(ld(2),0,4)*not(eq(mod(ld(2),4),0)*eq(mod(ld(0),4),0)),"
									   "if(eq(mod(ld(2),4),0)+eq(mod(ld(0),4),0)," + Y(C_FOAM) + ",if(eq(ld(2),3)," + Y(C_PBOT) +
									   ",if(eq(ld(2),1)*eq(ld(0),1)," + Y(C_WHITE) + "," + Y(C_PTOP) + "))),"
									   + fb + "))))'";
			g += "[bbL] [bbM] lut2=c0=" + pearls + ",split=3 [bbc0] [bbc1] [bbc2]; ";
			// the art shows through where nothing (0); each plane the art's there, else the colour's
			auto paint = [&](int BobaYuv::*plane) {
				std::string e = "y";
				for (int k : {C_WHITE, C_FOAM, C_SURF, C_TEA, C_PTOP, C_PBOT}) e = "if(eq(x," + n(c[k].y) + ")," + n(c[k].*plane) + "," + e + ")";
				return "'" + e + "'";
			};
			return g + "[bbc0] scale=640:200:flags=neighbor [bbc0s]; [bbc0s] [bbay] lut2=c0=" + paint(&BobaYuv::y) + " [bbY]; "
					   "[bbc1] scale=320:100:flags=neighbor [bbc1s]; [bbc1s] [bbau] lut2=c0=" + paint(&BobaYuv::u) + " [bbU]; "
					   "[bbc2] scale=320:100:flags=neighbor [bbc2s]; [bbc2s] [bbav] lut2=c0=" + paint(&BobaYuv::v) + " [bbW]; "
					   "[bbY] [bbU] [bbW] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		}

		g += ",split=3 [bbl1] [bbl2] [bbl3]; ";
		// the surface: the mids (bands 4-9) times the still wave, rolled left ~14 dots a second; each column's offset
		// 0..6 (3 at rest), as the preview's round(sin x mid x 2.5)
		g += "[bbl1] crop=6:1:4:0,scale=1:1:flags=area,scale=" + n(W + WP) + ":1:flags=neighbor [bbmid]; movie=" + q(2) + ",format=gray [bbwv]; "
			 "[bbmid] [bbwv] lut2=c0='clip(round(3+2.5*(x/252)*(y-128)/127),0,6)',crop=" + n(W) + ":1:x='mod(floor(n*14/60)," + n(WP) +
			 ")':y=0 [bbS]; ";
		// the pearls: each one's lift (its level squared x 10), the front row's (bands 0-7) and the back row's (8-15)
		// over their columns (3 each), front x 11 + back
		g += "[bbl2] lut=c0='round(pow(val/252,2)*10)',split [bbp1] [bbp2]; "
			 "[bbp1] crop=8:1:0:0,scale=" + n(PCOLS) + ":1:flags=neighbor [bbpa]; "
			 "[bbp2] crop=8:1:8:0,scale=" + n(PCOLS) + ":1:flags=neighbor [bbpb]; "
			 "[bbpa] [bbpb] lut2=c0='x*11+y',pad=" + n(W) + ":1:" + n(PX0) + ":0 [bbP]; ";
		// the two rows over the top and bottom halves (the surface moves in rows 17-23, the pearls in 42-53)
		g += "[bbS] [bbP] vstack,scale=" + n(W) + ":" + n(H) + ":flags=neighbor,setsar=1 [bbst]; movie=" + q(0) + ",format=gray [bbM]; ";
		// the mask's dot (y) against its column's state (x)
		auto fbk = [&](int f) { return f == 0 ? Y(C_TEA) : f == 1 ? Y(C_WHITE) : Y(C_BG); };
		const std::string fallback = "if(eq(ld(2),0)," + fbk(0) + ",if(eq(ld(2),1)," + fbk(1) + "," + fbk(2) + "))";
		// ld(0) the row, ld(1) the kind, ld(2) the fallback, ld(3) the front pearl's lift, ld(4) the back one's; the
		// back row drawn over the front (as the preview)
		const std::string pearl =
			"st(5,y-" + n(M_PEARL) + ");st(0," + n(PROW0) + "+mod(ld(5)," + n(PROWS) + "));st(1,mod(floor(ld(5)/" + n(PROWS) + "),3));"
			"st(2,floor(ld(5)/" + n(3 * PROWS) + "));st(3,floor(x/11));st(4,mod(x,11));"
			"if(gte(ld(1),1)*eq(ld(0),50-ld(4)),if(eq(ld(1),1)," + Y(C_PHI) + "," + Y(C_PTOP) + "),"
			"if(gte(ld(1),1)*eq(ld(0),51-ld(4))," + Y(C_PBOT) + ","
			"if(lte(ld(1),1)*eq(ld(0),52-ld(3)),if(eq(ld(1),0)," + Y(C_PHI) + "," + Y(C_PTOP) + "),"
			"if(lte(ld(1),1)*eq(ld(0),53-ld(3))," + Y(C_PBOT) + "," + fallback + "))))";
		const std::string foam = "st(0,y-" + n(M_FOAM) + "+" + n(TOP) + ");st(1," + n(SURF - 3) + "+x);if(lt(ld(0),ld(1))," + Y(C_FOAM) +
								 ",if(eq(ld(0),ld(1))," + Y(C_SURF) + "," + Y(C_TEA) + "))";
		std::string fixed = Y(C_COUNT - 1);
		for (int k = C_COUNT - 2; k >= 0; k--) fixed = "if(eq(y," + n(k) + ")," + Y(k) + "," + fixed + ")";
		g += "[bbst] [bbM] lut2=c0='if(lt(y," + n(M_FOAM) + ")," + fixed + ",if(lt(y," + n(M_PEARL) + ")," + foam + "," + pearl + "))' [bbF]; ";
		// the fizz: the treble (bands 11-15) against the still column, a bubble out when it's past 20%, 30%, .. 70% for
		// the 1st .. 6th; slid up ~53 dots a second (over 42), placed on rows 6-50 and spread across; it whitens the
		// straw's left side
		const int FH = FIZZ_ROWS + FIZZ_GAP * FIZZ_N;
		g += "[bbl3] crop=5:1:11:0,scale=1:1:flags=area,scale=1:" + n(FH) + ":flags=neighbor [bbtr]; movie=" + q(3) + ",format=gray [bbfz]; "
			 "[bbtr] [bbfz] lut2=c0='255*gt(y,0)*gt(x,(0.1+0.1*y)*252)',crop=1:" + n(FIZZ_ROWS) + ":0:'mod(floor(n*53/60)," +
			 n(FIZZ_GAP * FIZZ_N) + ")',pad=1:" + n(H) + ":0:" + n(FIZZ_TOP) + ",scale=" + n(W) + ":" + n(H) + ":flags=neighbor,setsar=1 [bbZ]; "
			 "[bbF] [bbZ] lut2=c0='if(gt(y,0)*eq(x," + Y(C_STRAW1) + ")," + Y(C_WHITE) + ",x)',split=3 [bby] [bbu] [bbw]; ";
		// the luma scaled x8; U and V looked up from it at 80x60, scaled x4
		auto chroma = [&](int BobaYuv::*plane) {
			std::string e = n(c[C_BG].*plane);
			for (int k = C_COUNT - 1; k > 0; k--) e = "if(eq(val," + n(c[k].y) + ")," + n(c[k].*plane) + "," + e + ")";
			return "'" + e + "'";
		};
		g += "[bby] scale=640:480:flags=neighbor [bbY]; "
			 "[bbu] lut=c0=" + chroma(&BobaYuv::u) + ",scale=320:240:flags=neighbor [bbU]; "
			 "[bbw] lut=c0=" + chroma(&BobaYuv::v) + ",scale=320:240:flags=neighbor [bbW]; "
			 "[bbY] [bbU] [bbW] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
		return g;
	}
}
