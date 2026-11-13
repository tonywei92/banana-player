// Pixel sea: a pixel-art sea at night (4-pixel pixels: drawn at 160x120 and scaled x4). A dark sky, a horizon band and a
// still crescent moon; the sea's surface a long slow swell (its height the bass) plus a shorter chop (its height the
// mids), both rolling sideways; under it the water deepens from light to dark blue with a few moonlight glints; foam
// sparkles along the crest with the treble; a little sailboat rides the surface, tilting with it.
// A frame costs: 16 bands measured and eased (as Pocket console's bars), averaged into bass, mid and treble (one value
// each); each of those times a still 1-row wave (the swell, the chop, the foam's noise) at a lut2, the row cropped at
// an x that moves with the frame number (that's the rolling: crop's x is per frame), so the surface is one row of 160
// (each column its row, foam a bit on top). The boat's waterline: the surface under it averaged in two halves and
// stretched between them (a slant, damped by half). Then at 160 rows (or 50 over the art) a lut2 each: the class of
// each pixel (sky, crest, foam, water by its depth), the boat's row offset, its shape (a lut2's table is computed once:
// any shape is an expression of offset and column), the two merged, and a lut2 a plane picking a colour or the still
// scene's pixel; the planes scaled up (nearest). YUV and gray only, no geq, nothing big converted.
// Over the art: the art's top as it is, and in the 640x200 strip under it the sea, 50 pixels high, rolls over the
// art's bottom (the art above the waterline, the water a little translucent below, the boat on it).
#include "viz_more.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	// a new name whenever the stills change: they're cached by size
	static const char* SEA_FILES[9] = {"sea1-y.pgm", "sea1-u.pgm", "sea1-v.pgm", "sea1-swell.pgm", "sea1-chop.pgm",
									   "sea1-foam.pgm", "sea1-row.pgm", "sea1-col.pgm", "sea1-srow.pgm"};
	static const int W = 160, H = 120, SH = 50;	// the drawing's pixels (x4 on screen); the strip's height over the art
	// the waves' periods in pixels (their stills one period wider than the screen, cropped where the frame says)
	static const int P1 = 64, P2 = 18, PF = 64;
	// the boat: its centre column, and the columns its waterline is measured over (BX0 and the next BW)
	static const int BOATX = 72, BX0 = 63, BW = 18;
	// glints: in the scene's planes, rows always under water alone (the surface is never lower than 80)
	static const int GLINT = 255;

	struct SeaYuv { int y, u, v; };
	static SeaYuv yuvOf(double r, double g, double b)
	{
		// BT.601 limited range, as mpv reads a picture this size
		r /= 255; g /= 255; b /= 255;
		return {(int)std::lround(16 + 65.481 * r + 128.553 * g + 24.966 * b),
				(int)std::lround(128 - 37.797 * r - 74.203 * g + 112.0 * b),
				(int)std::lround(128 + 112.0 * r - 93.786 * g - 18.214 * b)};
	}
	static SeaYuv yuvOf(int rgb) { return yuvOf((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
	static const int SKY = 0x141a3a, HORIZON = 0x20285a, MOON = 0xfff3c4, CREST = 0x9fd8ff, FOAM = 0xffffff,
					 HULL = 0x7a3e1c, DECK = 0xa0582a, MAST = 0xd8d2c0, SAIL = 0xffd23f;

	static bool seaPgm(const std::string& path, int w, int h, const std::vector<unsigned char>& data)
	{
		std::string tmp = path + ".tmp";
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << "P5\n" << w << " " << h << "\n255\n";
		out.write((const char*)data.data(), (std::streamsize)data.size());
		if (!out.flush()) return false;
		out.close();
		return rename(tmp.c_str(), path.c_str()) == 0;
	}

	static size_t seaPgmSize(int w, int h)
	{
		return ("P5\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n").size() + (size_t)w * h;
	}

	bool writeSeaMaps(const std::string& dir)
	{
		const int dims[9][2] = {{W, H}, {W, H}, {W, H}, {W + P1, 1}, {W + P2, 1}, {W + PF, 1}, {W, H}, {W, H}, {W, SH}};
		std::string p[9];
		bool ready = true;
		for (int k = 0; k < 9; k++)
		{
			p[k] = dir + "/" + SEA_FILES[k];
			struct stat st;
			ready = ready && stat(p[k].c_str(), &st) == 0 && (size_t)st.st_size == seaPgmSize(dims[k][0], dims[k][1]);
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);

		// the scene (alone): the sky, the horizon band (rows 40-69), the moon; under row 80 (always water) only the
		// glints' marks
		std::vector<unsigned char> Y((size_t)W * H), U((size_t)W * H), V((size_t)W * H);
		auto put = [&](int x, int y, SeaYuv c) {
			if (x < 0 || x >= W || y < 0 || y >= H) return;
			size_t i = (size_t)y * W + x;
			Y[i] = (unsigned char)c.y; U[i] = (unsigned char)c.u; V[i] = (unsigned char)c.v;
		};
		const SeaYuv sky = yuvOf(SKY), band = yuvOf(HORIZON), moon = yuvOf(MOON);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) put(x, y, y >= 40 ? band : sky);
		// a few faint stars (not on the moon)
		uint32_t s = 0x5ea5ea;
		auto rnd = [&]() { s = s * 1664525u + 1013904223u; return s >> 8; };
		for (int i = 0; i < 18; i++)
		{
			int x = (int)(rnd() % W), y = (int)(rnd() % 36);
			if (std::hypot(x - 117.5, y - 30) < 13) continue;
			double t = 0.25 + (rnd() % 30) / 100.0;
			put(x, y, yuvOf(0x14 + (0xff - 0x14) * t, 0x1a + (0xf3 - 0x1a) * t, 0x3a + (0xc4 - 0x3a) * t));
		}
		// the crescent: a disc less one moved up and right
		for (int y = 0; y < 50; y++)
			for (int x = 100; x < 140; x++)
				if (std::hypot(x + 0.5 - 117.5, y + 0.5 - 30) < 8.5 && std::hypot(x + 0.5 - 121.25, y + 0.5 - 27.5) >= 7.5) put(x, y, moon);
		// the glints: short streaks of 3, every so often, in the water's upper half
		const SeaYuv mark{GLINT, GLINT, GLINT};
		for (int i = 0; i < 16; i++)
		{
			int x = (int)(rnd() % (W - 3)), y = 84 + (int)(rnd() % 26);
			for (int k = 0; k < 3; k++) put(x + k, y, mark);
		}

		// the waves, a still row each: the swell 0..255 (0.5+0.5 sin), the chop 128 +- 127 (sin), the foam's noise
		std::vector<unsigned char> swell(W + P1), chop(W + P2), foam(W + PF), row((size_t)W * H), col((size_t)W * H), srow((size_t)W * SH);
		const double TAU = 6.283185307179586;
		for (int x = 0; x < W + P1; x++) swell[x] = (unsigned char)std::lround(127.5 + 127.5 * std::sin(TAU * x / P1));
		for (int x = 0; x < W + P2; x++) chop[x] = (unsigned char)std::lround(128 + 127 * std::sin(TAU * x / P2));
		std::vector<unsigned char> noise(PF);
		for (int x = 0; x < PF; x++) noise[x] = (unsigned char)(rnd() % 256);
		for (int x = 0; x < W + PF; x++) foam[x] = noise[x % PF];
		// each pixel's row and column (the lut2s' other input)
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) { row[(size_t)y * W + x] = (unsigned char)y; col[(size_t)y * W + x] = (unsigned char)x; }
		for (int y = 0; y < SH; y++)
			for (int x = 0; x < W; x++) srow[(size_t)y * W + x] = (unsigned char)y;
		return seaPgm(p[0], W, H, Y) && seaPgm(p[1], W, H, U) && seaPgm(p[2], W, H, V) && seaPgm(p[3], W + P1, 1, swell) &&
			   seaPgm(p[4], W + P2, 1, chop) && seaPgm(p[5], W + PF, 1, foam) && seaPgm(p[6], W, H, row) && seaPgm(p[7], W, H, col) &&
			   seaPgm(p[8], W, SH, srow);
	}

	std::string seaGraph(bool overCover, const std::string& dir)
	{
		auto n = [](int v) { return std::to_string(v); };
		auto f = [](double v) { char b[32]; snprintf(b, sizeof b, "%.4f", v); return std::string(b); };
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + SEA_FILES[k], s); return s; };
		const int N = 16, TOP = 8;	// 16 bands (bass 0-2, mids 5-8, treble 12-15); as Peaks the highest 8 measured and cut
		const int rows = overCover ? SH : H;
		// the mean waterline's row and the waves' scale (over the art, in a strip of 50, a bit smaller)
		const int BASE = overCover ? 39 : 75;
		const double K = overCover ? 0.7 : 1.0;
		const std::string R = n(rows);
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [seart1] [seart2]; [seart1] crop=640:280:0:0 [top]; "
				 "[seart2] crop=640:200:0:280,extractplanes=y+u+v [seay] [seau] [seav]; ";
		// the bands measured and eased as Pocket console's bars (up at once, down over ~0.15 s), then averaged; the
		// contrast spread wider (x2.2: averaging three or four bands evens them out) and the wave heights curved
		// (level^1.5: a quiet passage near flat, a loud one high), so the sea moves with the music, not just on it
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,bplut=c0='val*252/255'" + vizContrast("se", 1, "6", "2.2", "150") + ","
			 "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split=3 [sel1] [sel2] [sel3]; "
			 "[sel1] crop=3:1:0:0,scale=1:1:flags=area,scale=" + n(W + P1) + ":1:flags=neighbor [sebass]; "
			 "[sel2] crop=4:1:5:0,scale=1:1:flags=area,scale=" + n(W + P2) + ":1:flags=neighbor [semid]; "
			 "[sel3] crop=4:1:12:0,scale=1:1:flags=area,scale=" + n(W + PF) + ":1:flags=neighbor [setre]; "
			 "movie=" + q(3) + " [sews]; movie=" + q(4) + " [sewc]; movie=" + q(5) + " [sewf]; "
			 // the swell's height (eighths of a row) and the chop's (around 128), each column; rolled sideways: the
			 // swell right ~16 pixels a second, the chop left ~9, the foam's sparkle left 10
			 "[sebass] [sews] bplut2=c0='clip(round(8*" + f(K) + "*(2+20*pow(x/252,1.5))*y/255),0,255)',crop=" + n(W) + ":1:x='" + n(P1 - 1) +
			 "-mod(floor(n*16/60)," + n(P1) + ")':y=0 [sea]; "
			 "[semid] [sewc] bplut2=c0='clip(round(128+8*" + f(K) + "*(0.5+5.5*pow(x/252,1.5))*(y-128)/127),0,255)',crop=" + n(W) +
			 ":1:x='mod(floor(n*9/60)," + n(P2) + ")':y=0 [seb]; "
			 // foam where the noise is under the treble's share (none when it's quiet)
			 "[setre] [sewf] bplut2=c0='128*lt(y,clip((x-130)*2.5,0,170))',crop=" + n(W) + ":1:x='mod(floor(n/6)," + n(PF) + ")':y=0 [sefo]; "
			 // the surface's row, each column
			 "[sea] [seb] bplut2=c0='clip(round(" + n(BASE) + "-x/8-(y-128)/8),0," + n(rows - 1) + ")',split [ses1] [ses2]; "
			 "[ses1] [sefo] bplut2=c0='x+y',scale=" + n(W) + ":" + R + ":flags=neighbor,setsar=1 [sesf]; "
			 // the boat's waterline: the surface under it in two halves, a line between them at a third of their slant,
			 // placed at its columns
			 "[ses2] crop=" + n(BW) + ":1:" + n(BX0) + ":0,split [seh1] [seh2]; "
			 "[seh1] crop=" + n(BW / 2) + ":1:0:0,scale=1:1:flags=area,scale=" + n(BW) + ":1:flags=neighbor [sehl]; "
			 "[seh2] crop=" + n(BW / 2) + ":1:" + n(BW / 2) + ":0,scale=1:1:flags=area,scale=" + n(BW) + ":1:flags=neighbor [sehr]; "
			 // (blend's expression per pixel per frame: 18 of them)
			 "[sehl] [sehr] blend=all_expr='(A+B)/2+(B-A)*(X-" + f((BW - 1) / 2.0) + ")/" + n(BW * 3 / 2) + "+0.5',pad=" + n(W) + ":1:" + n(BX0) +
			 ":0,scale=" + n(W) + ":" + R + ":flags=neighbor,setsar=1 [sebl]; "
			 "movie=" + q(overCover ? 8 : 6) + ",split [ser1] [ser2]; movie=" + q(7) + (overCover ? ",crop=" + n(W) + ":" + R + ":0:0" : "") + " [secol]; "
			 // each pixel: 0 above the surface, 1 the crest, 2 foam on it, 3.. under it (2 + its depth in rows)
			 "[sesf] [ser1] bplut2=c0='if(lt(y,x-128*gte(x,128)),0,if(eq(y,x-128*gte(x,128)),1+gte(x,128),min(2+y-x+128*gte(x,128),199)))' [sek]; "
			 // the boat: each pixel's row from its waterline (64 on it), then its part by that and the column: 1 the
			 // hull, 2 its deck, 3 the mast, 4 the sail, 5 under the hull (water where the wave falls away)
			 "[sebl] [ser2] bplut2=c0='clip(y-x+64,0,255)' [serel]; "
			 "[serel] [secol] bplut2=c0='st(0,x-64);st(1,y-" + n(BOATX) + ");"
			 "if(eq(ld(1),0)*between(ld(0),-15,-4),3,"
			 "if(between(ld(0),-14,-5)*between(ld(1),1,floor((ld(0)+15)*0.65)),4,"
			 "if(between(ld(0),-3,0)*lte(abs(ld(1)),5-ld(0)),if(eq(ld(0),-3),2,1),"
			 "if(between(ld(0),1,6)*lte(abs(ld(1)),5),5,0))))' [sebc]; "
			 "[sek] [sebc] bplut2=c0='if(eq(y,0),x,if(eq(y,5),if(eq(x,0),5,x),200+y))',split=3 [sec0] [sec1] [sec2]; ";

		// each plane's colour by class: x the class, y the scene's (or the art's) pixel
		const SeaYuv shallow = yuvOf(50, 140, 200), deep = yuvOf(20, 70, 140), crest = yuvOf(CREST), foamC = yuvOf(FOAM),
					 hull = yuvOf(HULL), deck = yuvOf(DECK), mast = yuvOf(MAST), sail = yuvOf(SAIL), glint = yuvOf(MOON);
		auto paint = [&](int SeaYuv::*c) {
			// the water: from shallow (d=0) towards deep (d=1, 45 rows down) and on past it, as the preview's
			std::string water = "(" + n(shallow.*c) + "+" + n(deep.*c - shallow.*c) + "*min((x-2)/45,1.4))";
			std::string under = overCover ? "(0.78*" + water + "+0.22*y)" : "if(eq(y," + n(GLINT) + "),(" + water + "+2*" + n(glint.*c) + ")/3," + water + ")";
			return "'if(eq(x,0),y,if(eq(x,1)," + n(crest.*c) + ",if(eq(x,2)," + n(foamC.*c) + ",if(lt(x,200)," + under +
				   ",if(eq(x,201)," + n(hull.*c) + ",if(eq(x,202)," + n(deck.*c) + ",if(eq(x,203)," + n(mast.*c) + "," + n(sail.*c) + ")))))))'";
		};
		if (overCover)
			return g + "[sec0] scale=640:200:flags=neighbor [sec0s]; [sec0s] [seay] bplut2=c0=" + paint(&SeaYuv::y) + " [seY]; "
					   "[sec1] scale=320:100:flags=neighbor [sec1s]; [sec1s] [seau] bplut2=c0=" + paint(&SeaYuv::u) + " [seU]; "
					   "[sec2] scale=320:100:flags=neighbor [sec2s]; [sec2s] [seav] bplut2=c0=" + paint(&SeaYuv::v) + " [seV]; "
					   "[seY] [seU] [seV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "movie=" + q(0) + " [sesy]; movie=" + q(1) + " [sesu]; movie=" + q(2) + " [sesv]; "
				   "[sec0] [sesy] bplut2=c0=" + paint(&SeaYuv::y) + ",scale=640:480:flags=neighbor [seY]; "
				   "[sec1] [sesu] bplut2=c0=" + paint(&SeaYuv::u) + ",scale=320:240:flags=neighbor [seU]; "
				   "[sec2] [sesv] bplut2=c0=" + paint(&SeaYuv::v) + ",scale=320:240:flags=neighbor [seV]; "
				   "[seY] [seU] [seV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
