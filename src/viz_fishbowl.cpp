// Fishbowl: pixel art on an 80x60 grid (8-pixel pixels). A dark plum room, a wooden table along the bottom, and on it a
// round glass bowl (a pale rim, blue water under a lighter water line, sandy gravel, an open top); two strands of weed
// sway with the mids; three goldfish (orange, light orange, coral) drift slowly side to side, each rising in the water
// with its slice of the spectrum (bands 0-3, 5-9, 10-15) and wiggling its tail, faster when louder; white bubbles rise
// from the gravel on the treble.
// A frame costs: 16 bands measured and eased (as Pocket console's bars), averaged into the three fish, the mids and the
// treble (one value each). Each fish: its level against a still row of frame numbers gives its tail's frame (a lut2:
// the wiggle's speed is the level's), its drift a crop of a still column ramp at an x that moves with the frame number,
// its height a lut2 of its level against each pixel's row; a lut2 of those two is its sprite (a lut2's table is
// computed once: the shape is an expression of the row and column from the fish). The weed: the mids and a phase from
// the frame number make one frame index, and a lut2 against a still of each pixel's strand, height and offset draws
// the strand at that sway. The bubbles: a still column of them, gated by the treble at a lut2 and cropped at a y that
// moves with the frame number. Everything at 80x60 (or 80x25 over the art), merged into one class per pixel, then a
// lut2 a plane picking a colour or the still scene's (or the art's) pixel; the planes scaled up (nearest). YUV and gray
// only, no geq, nothing big converted.
// Over the art: the art's top as it is, and in the 640x200 strip under it the three goldfish swim and bubbles rise
// over the art, which shows through everywhere else (no bowl).
#include "viz_more.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	// a new name whenever the stills change: they're cached by size
	enum { FB_Y, FB_U, FB_V, FB_ROW, FB_SROW, FB_COL, FB_RAMP, FB_WEED, FB_BUB, FB_SBUB, FB_COUNT };
	static const char* FB_FILES[FB_COUNT] = {"fishbowl1-y.pgm", "fishbowl1-u.pgm", "fishbowl1-v.pgm", "fishbowl1-row.pgm",
											 "fishbowl1-srow.pgm", "fishbowl1-col.pgm", "fishbowl1-ramp.pgm", "fishbowl1-weed.pgm",
											 "fishbowl1-bub.pgm", "fishbowl1-sbub.pgm"};
	static const int W = 80, H = 60, SH = 25;	// the drawing's pixels (x8 on screen); the strip's height over the art
	static const int AMP = 9, CW = W + 2 * AMP;	// the fish's drift each side; the column ramp's width
	static const int RAMP = 240;				// the tail's frames: a table of 4 s
	// the bowl
	static const int CX = 40, CY = 32, R = 19;
	// the weed's still: its box, and its strands' columns and heights (their base the row above the gravel)
	static const int WX0 = 27, WY0 = 32, WWD = 28, WHT = 15, WBASE = CY + R - 5;
	static const int WEEDX[2] = {CX - 10, CX + 11}, WEEDH[2] = {14, 11};
	static const int WPH = 32, WAMP = 8;		// the weed's phases and amplitudes (one frame index of 256)
	// the bubbles: alone, a box over the gravel and a 26-row cycle; over the art, the whole strip and a 25-row cycle
	static const int BX0 = CX - 8, BW = 16, BY0 = CY - R + 6, BP = 26, BH = 27;
	static const int SBP = SH, NB = 10, SNB = 18;

	struct FbYuv { int y, u, v; };
	static FbYuv yuvOf(double r, double g, double b)
	{
		// BT.601 limited range, as mpv reads a picture this size
		r /= 255; g /= 255; b /= 255;
		return {(int)std::lround(16 + 65.481 * r + 128.553 * g + 24.966 * b),
				(int)std::lround(128 - 37.797 * r - 74.203 * g + 112.0 * b),
				(int)std::lround(128 + 112.0 * r - 93.786 * g - 18.214 * b)};
	}
	static FbYuv yuvOf(int rgb) { return yuvOf((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
	static const int ROOM = 0x2b1f3a, TABLETOP = 0xa0703c, TABLE = 0x7a5230, GLASS = 0xc9e6ff, RIM = 0xbfe3ff, SAND1 = 0xe0b070,
					 SAND2 = 0xc08850, WATERLINE = 0x9fd8ff, WATER = 0x3a8fd0, TOPRIM = 0xd8efff, WEED = 0x3fbf6a, EYE = 0x1a1020,
					 BUBBLE = 0xe8f6ff;
	static const int FISHC[3] = {0xff8a3d, 0xffb347, 0xff6b6b};

	// the preview's noise field (the same seed, so the bubbles are where they were)
	static std::vector<double> fbNoise()
	{
		std::vector<double> a(80 * 120);
		int64_t seed = 7;
		for (auto& v : a) { seed = (seed * 16807) % 2147483647; v = seed / 2147483647.0; }
		return a;
	}

	static bool fbPgm(const std::string& path, int w, int h, const std::vector<unsigned char>& data)
	{
		std::string tmp = path + ".tmp";
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << "P5\n" << w << " " << h << "\n255\n";
		out.write((const char*)data.data(), (std::streamsize)data.size());
		if (!out.flush()) return false;
		out.close();
		return rename(tmp.c_str(), path.c_str()) == 0;
	}

	static size_t fbPgmSize(int w, int h)
	{
		return ("P5\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n").size() + (size_t)w * h;
	}

	static const int FB_DIMS[FB_COUNT][2] = {{W, H}, {W, H}, {W, H}, {W, H}, {W, SH}, {CW, 1}, {RAMP, 1}, {WWD, WHT}, {BW, 2 * BP}, {W, 2 * SBP}};

	// a bubble still: each bubble's gate (1..255: drawn while the treble is past it) at the rows where it is, in two
	// cycles of p rows (cropped at a moving y, they rise)
	static std::vector<unsigned char> fbBubbles(int w, int p, int count, int x0, int span, int salt, const std::vector<double>& noise)
	{
		std::vector<unsigned char> b((size_t)w * 2 * p, 0);
		for (int i = 0; i < count; i++)
		{
			double gate = noise[(i * 19 + salt) % noise.size()], r = noise[(i * 7 + salt) % noise.size()];
			int x = x0 + (int)std::floor(noise[(i * 23 + salt) % noise.size()] * span);
			int s = (int)std::lround(p * (1 - r)) % p;
			unsigned char g = (unsigned char)std::clamp((int)std::lround(gate * 255), 1, 255);
			for (int row : {s, s + p})
			{
				unsigned char& c = b[(size_t)row * w + x];
				c = c ? std::min(c, g) : g;
			}
		}
		return b;
	}

	bool writeFishbowlMaps(const std::string& dir)
	{
		std::string p[FB_COUNT];
		bool ready = true;
		for (int k = 0; k < FB_COUNT; k++)
		{
			p[k] = dir + "/" + FB_FILES[k];
			struct stat st;
			ready = ready && stat(p[k].c_str(), &st) == 0 && (size_t)st.st_size == fbPgmSize(FB_DIMS[k][0], FB_DIMS[k][1]);
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);
		const std::vector<double> noise = fbNoise();

		// the scene: the room, the table, the bowl (its glass above the water a faint tint, its top rim)
		std::vector<unsigned char> Y((size_t)W * H), U((size_t)W * H), V((size_t)W * H);
		auto put = [&](int x, int y, FbYuv c) {
			if (x < 0 || x >= W || y < 0 || y >= H) return;
			size_t i = (size_t)y * W + x;
			Y[i] = (unsigned char)c.y; U[i] = (unsigned char)c.u; V[i] = (unsigned char)c.v;
		};
		const double ga = 0x55 / 255.0;
		auto mix = [&](int a, int b) {	// b over a at the glass's alpha
			auto ch = [&](int s) { return ((a >> s) & 255) * (1 - ga) + ((b >> s) & 255) * ga; };
			return yuvOf(ch(16), ch(8), ch(0));
		};
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) put(x, y, yuvOf(y < 52 ? ROOM : y == 52 ? TABLETOP : TABLE));
		for (int y = CY - R; y <= CY + R; y++)
			for (int x = CX - R; x <= CX + R; x++)
			{
				int dd = (x - CX) * (x - CX) + (y - CY) * (y - CY);
				if (dd > R * R) continue;
				if (y < CY - R + 6) { if (dd > (R - 1) * (R - 1)) put(x, y, mix(ROOM, GLASS)); continue; }
				put(x, y, yuvOf(dd > (R - 1) * (R - 1) ? RIM : y > CY + R - 5 ? ((x + y) % 3 ? SAND1 : SAND2) : y == CY - R + 6 ? WATERLINE : WATER));
			}
		for (int x = CX - 9; x <= CX + 9; x++) put(x, CY - R, yuvOf(TOPRIM));

		// each pixel's row (alone, and in the strip), and the drift's column ramp
		std::vector<unsigned char> row((size_t)W * H), srow((size_t)W * SH), col(CW), ramp(RAMP);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) row[(size_t)y * W + x] = (unsigned char)y;
		for (int y = 0; y < SH; y++)
			for (int x = 0; x < W; x++) srow[(size_t)y * W + x] = (unsigned char)y;
		for (int x = 0; x < CW; x++) col[x] = (unsigned char)x;
		for (int x = 0; x < RAMP; x++) ramp[x] = (unsigned char)x;

		// the weed's box: each pixel near a strand its strand, its height up it and its offset from the strand's
		// column (1 + s*112 + i*8 + dx+3; 0 elsewhere)
		std::vector<unsigned char> weed((size_t)WWD * WHT, 0);
		for (int s = 0; s < 2; s++)
			for (int i = 0; i < WEEDH[s]; i++)
				for (int dx = -3; dx <= 3; dx++)
				{
					int x = WEEDX[s] + dx - WX0, y = WBASE - i - WY0;
					if (x < 0 || x >= WWD || y < 0 || y >= WHT) continue;
					weed[(size_t)y * WWD + x] = (unsigned char)(1 + s * 112 + i * 8 + dx + 3);
				}

		const std::vector<unsigned char> bub = fbBubbles(BW, BP, NB, 0, BW, 0, noise);
		const std::vector<unsigned char> sbub = fbBubbles(W, SBP, SNB, 2, W - 4, 1000, noise);
		return fbPgm(p[FB_Y], W, H, Y) && fbPgm(p[FB_U], W, H, U) && fbPgm(p[FB_V], W, H, V) && fbPgm(p[FB_ROW], W, H, row) &&
			   fbPgm(p[FB_SROW], W, SH, srow) && fbPgm(p[FB_COL], CW, 1, col) && fbPgm(p[FB_RAMP], RAMP, 1, ramp) &&
			   fbPgm(p[FB_WEED], WWD, WHT, weed) && fbPgm(p[FB_BUB], BW, 2 * BP, bub) && fbPgm(p[FB_SBUB], W, 2 * SBP, sbub);
	}

	std::string fishbowlGraph(bool overCover, const std::string& dir)
	{
		auto n = [](int v) { return std::to_string(v); };
		auto f = [](double v) { char b[32]; snprintf(b, sizeof b, "%.5f", v); return std::string(b); };
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + FB_FILES[k], s); return s; };
		const int N = 16, TOP = 8;	// 16 bands; as Peaks the highest 8 measured and cut
		const int rows = overCover ? SH : H;
		const std::string RW = n(W), RH = n(rows);
		// the fish: their slices of the spectrum, their lowest rows (the sprite's top), how far they rise, their
		// drifts' centres, speeds and phases (as the preview: sin(t*(0.5+0.2k)+2k))
		const int band0[3] = {0, 5, 10}, bandN[3] = {4, 5, 6};
		const int rise = overCover ? 14 : 16;
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [fbart1] [fbart2]; [fbart1] crop=640:280:0:0 [top]; "
				 "[fbart2] crop=640:200:0:280,extractplanes=y+u+v [fbay] [fbau] [fbav]; ";
		// the bands measured and eased as Pocket console's bars (up at once, down over ~0.15 s), then averaged per fish,
		// the mids (4-9, alone: the weed) and the treble (11-15: the bubbles)
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("fb") + ","
			 "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split=" + n(overCover ? 4 : 5) +
			 " [fbl0] [fbl1] [fbl2] [fbtre]" + (overCover ? "" : " [fbmid]") + "; ";
		// the stills each used more than once
		g += "movie=" + q(FB_RAMP) + ",split=" + n(overCover ? 3 : 4) + " [fbrp0] [fbrp1] [fbrp2]" + (overCover ? "" : " [fbrp3]") + "; "
			 "movie=" + q(FB_COL) + ",split=3 [fbcs0] [fbcs1] [fbcs2]; "
			 "movie=" + q(overCover ? FB_SROW : FB_ROW) + ",split=3 [fbrs0] [fbrs1] [fbrs2]; ";
		for (int k = 0; k < 3; k++)
		{
			const std::string K = n(k), fbase = n((overCover ? 19 : 42) - 2 * k), centre = n(overCover ? 10 + 26 * k : 36);
			g += "[fbl" + K + "] crop=" + n(bandN[k]) + ":1:" + n(band0[k]) + ":0,scale=1:1:flags=area,split [fbla" + K + "] [fblb" + K + "]; "
				 // the tail's frame: as the preview's floor(t*(4+10v)) mod 2, read off the frame number's row at the level
				 "[fbla" + K + "] scale=" + n(RAMP) + ":1:flags=neighbor [fblw" + K + "]; "
				 "[fblw" + K + "] [fbrp" + K + "] lut2=c0='255*mod(floor(y/60*(4+10*x/252)),2)',crop=1:1:x='mod(n," + n(RAMP) +
				 ")':y=0,scale=" + n(CW) + ":1:flags=neighbor [fbwg" + K + "]; "
				 // the column from the fish's nose (64 on it; +128 when the tail is in its second frame), drifting
				 "[fbwg" + K + "] [fbcs" + K + "] lut2=c0='clip(y-" + n(AMP) + "-" + centre + "+64,0,127)+128*gt(x,127)',crop=" + RW +
				 ":1:x='" + n(AMP) + "-round(" + n(AMP) + "*sin(n/60*" + f(0.5 + 0.2 * k) + "+" + n(2 * k) + "))':y=0,scale=" + RW + ":" + RH +
				 ":flags=neighbor [fbc" + K + "]; "
				 // the row from the fish's top (64 on it), up with its level
				 "[fblb" + K + "] scale=" + RW + ":" + RH + ":flags=neighbor [fblr" + K + "]; "
				 "[fblr" + K + "] [fbrs" + K + "] lut2=c0='clip(y-" + fbase + "+round(x*" + n(rise) + "/252)+64,0,255)' [fbr" + K + "]; ";
			// the sprite (8x5: ..##.... / .####.## / ######## / .####.## / ..##....), the middle fish turned the other
			// way; its tail (sprite columns 6-7) forked or closed; 4 its eye, k+1 its body
			const std::string c = k == 1 ? "(7-(mod(y,128)-64))" : "(mod(y,128)-64)";
			g += "[fbr" + K + "] [fbc" + K + "] lut2=c0='st(0,x-64);st(1," + c + ");st(2,gte(y,128));"
				 "if(between(ld(0),0,4)*between(ld(1),0,7),if(eq(ld(0),1)*eq(ld(1),1),4,"
				 "if(eq(ld(0),2)+eq(abs(ld(0)-2),1)*(between(ld(1),1,4)+gte(ld(1),6)*(1-ld(2)))+eq(abs(ld(0)-2),2)*between(ld(1),2,3)," +
				 n(k + 1) + ",0)),0)' [fbf" + K + "]; ";
		}
		// the bubbles: the treble against each bubble's gate, cropped at a y rising a cycle each 2 s
		const int bw = overCover ? W : BW, bp = overCover ? SBP : BP, bh = overCover ? SH : BH;
		g += "[fbtre] crop=5:1:11:0,scale=1:1:flags=area,scale=" + n(bw) + ":" + n(2 * bp) + ":flags=neighbor [fbtm]; "
			 "movie=" + q(overCover ? FB_SBUB : FB_BUB) + " [fbbs]; "
			 "[fbtm] [fbbs] lut2=c0='6*gt(y,0)*lte(y/255,0.15+x/252)',crop=" + n(bw) + ":" + n(bh) + ":0:'mod(floor(n*" + n(bp) + "/120)," +
			 n(bp) + ")'" + (overCover ? "" : ",pad=" + RW + ":" + RH + ":" + n(BX0) + ":" + n(BY0)) + " [fbb]; ";
		std::string base;
		if (!overCover)
		{
			// the weed: the mids (8 steps) and a phase (32 a turn of sin(2t)) one frame index, drawn against the still
			// of the strands: a pixel is weed when its offset is the strand's sway at its height
			const double tau = 6.283185307179586;
			g += "[fbmid] crop=6:1:4:0,scale=1:1:flags=area,scale=" + n(WPH) + ":1:flags=neighbor [fbmw]; "
				 "[fbrp3] crop=" + n(WPH) + ":1:0:0 [fbrq]; "
				 "[fbmw] [fbrq] lut2=c0='y+" + n(WPH) + "*min(floor(x*" + n(WAMP) + "/253)," + n(WAMP - 1) + ")',crop=1:1:x='mod(floor(n*" +
				 f(2.0 * WPH / (60 * tau)) + ")," + n(WPH) + ")':y=0,scale=" + n(WWD) + ":" + n(WHT) + ":flags=neighbor [fbwi]; "
				 "movie=" + q(FB_WEED) + " [fbws]; "
				 "[fbwi] [fbws] lut2=c0='st(0,y-1);st(1,gte(ld(0),112));st(0,ld(0)-112*ld(1));st(2,floor(ld(0)/8));"
				 "5*gt(y,0)*eq(mod(ld(0),8)-3,round(sin(mod(x," + n(WPH) + ")*" + f(tau / WPH) + "+ld(2)*0.4)*(0.5+2*(floor(x/" + n(WPH) +
				 ")+0.5)/" + n(WAMP) + ")*ld(2)/(14-3*ld(1))))',pad=" + RW + ":" + RH + ":" + n(WX0) + ":" + n(WY0) + " [fbwd]; ";
			base = "[fbwd]";
		}
		// the classes merged, later over earlier: weed, the fish in turn, bubbles
		if (overCover)
			g += "[fbf0] [fbf1] lut2=c0='if(y,y,x)' [fbm1]; ";
		else
			g += base + " [fbf0] lut2=c0='if(y,y,x)' [fbm0]; [fbm0] [fbf1] lut2=c0='if(y,y,x)' [fbm1]; ";
		g += "[fbm1] [fbf2] lut2=c0='if(y,y,x)' [fbm2]; [fbm2] [fbb] lut2=c0='if(y,y,x)',split=3 [fbk0] [fbk1] [fbk2]; ";

		// each plane's colour by class: x the class, y the scene's (or the art's) pixel
		const FbYuv f0 = yuvOf(FISHC[0]), f1 = yuvOf(FISHC[1]), f2 = yuvOf(FISHC[2]), eye = yuvOf(EYE), weedC = yuvOf(WEED), bub = yuvOf(BUBBLE);
		auto paint = [&](int FbYuv::*c) {
			return "'if(eq(x,0),y,if(eq(x,1)," + n(f0.*c) + ",if(eq(x,2)," + n(f1.*c) + ",if(eq(x,3)," + n(f2.*c) + ",if(eq(x,4)," +
				   n(eye.*c) + ",if(eq(x,5)," + n(weedC.*c) + "," + n(bub.*c) + "))))))'";
		};
		if (overCover)
			return g + "[fbk0] scale=640:200:flags=neighbor [fbk0s]; [fbk0s] [fbay] lut2=c0=" + paint(&FbYuv::y) + " [fbY]; "
					   "[fbk1] scale=320:100:flags=neighbor [fbk1s]; [fbk1s] [fbau] lut2=c0=" + paint(&FbYuv::u) + " [fbU]; "
					   "[fbk2] scale=320:100:flags=neighbor [fbk2s]; [fbk2s] [fbav] lut2=c0=" + paint(&FbYuv::v) + " [fbV]; "
					   "[fbY] [fbU] [fbV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "movie=" + q(FB_Y) + " [fbsy]; movie=" + q(FB_U) + " [fbsu]; movie=" + q(FB_V) + " [fbsv]; "
				   "[fbk0] [fbsy] lut2=c0=" + paint(&FbYuv::y) + ",scale=640:480:flags=neighbor [fbY]; "
				   "[fbk1] [fbsu] lut2=c0=" + paint(&FbYuv::u) + ",scale=320:240:flags=neighbor [fbU]; "
				   "[fbk2] [fbsv] lut2=c0=" + paint(&FbYuv::v) + ",scale=320:240:flags=neighbor [fbV]; "
				   "[fbY] [fbU] [fbV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
