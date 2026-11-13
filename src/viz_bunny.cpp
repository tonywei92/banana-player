// Bunny hop: pixel art on an 80x60 grid of 8-pixel pixels. A light-blue sky with a paler band, a sun, three white
// clouds drifting slowly right; a green hill with a wavy top and little pink flowers; five pixel bunnies on it
// (cream and white by turns, pink nose, dark eyes), each listening to a slice of the spectrum (bands 0-2, 3-5, 6-8,
// 9-11, 12-15): it hops up by its eased rise squared x 18 pixels (its level over its last half second, falling back
// over ~0.3 s), is squashed a row as it lands, and a faint shadow lies on the grass under one in the air.
// A frame costs: 16 bands measured and eased (as Pocket console's bars), five averages (a crop and an area scale
// each), stacked; a tmix of their last 30 frames, a lut2, a lagfun and a lut make each bunny's hop (+32 when
// squashed), stretched to the bunnies' columns: a row of 80. Stretched down the 60 rows, a lut2 against a still "row minus that column's bunny's ground" gives each
// pixel its sprite row (or the shadow's mark); a lut2 against a still of each column's place in its sprite and each
// pixel's background (sky, band, sun, hill top, grass, flower) gives its class (a lut2's table is computed once, so
// the sprite is an expression of row and column). The clouds: a still strip one period wider than the screen,
// cropped at an x that moves with the frame number and padded down to the sky, merged where the sky shows. Each
// plane is a lut of the class at 80x60, scaled up (nearest). YUV and gray only, no geq, nothing big converted.
// Over the art: the art's top as it is, and in the 640x200 strip under it the hill (25 rows) with the bunnies on it,
// hopping lower (x7); the art shows through where the sky would be.
#include "viz_more.h"
#include "settings.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	// a new name whenever the stills change: they're cached by size
	static const char* BUN_FILES[5] = {"bun1-d.pgm", "bun1-c.pgm", "bun1-sd.pgm", "bun1-sc.pgm", "bun1-cloud.pgm"};
	static const int W = 80, H = 60, SH = 25;	// the grid (8-pixel pixels); the strip's rows over the art
	static const int CP = 100, CY = 9, CH = 13;	// the clouds' period (columns), their first row and their rows
	// the sprite, 9x10: '#' fur, 'o' an eye, 'p' the nose
	static const char* BUN[10] = {"..##.##..", "..##.##..", "..##.##..", ".#######.", "##o###o##",
								  "#########", "###ppp###", ".#######.", "..#####..", ".##...##."};
	static int bunX(int k) { return 6 + k * 15; }
	// the hill's top row at a column: alone 44 +- 4, over the art (in the strip) 19 +- 2
	static int hillTop(int x, bool strip)
	{
		return strip ? 19 + (int)std::lround(2 * std::sin(x * 0.09 + 1)) : 44 + (int)std::lround(4 * std::sin(x * 0.09 + 1));
	}
	// the backgrounds' classes (the still's high bits), the clouds', the bunny's parts, a shadow (16 + background)
	enum { BG_SKY, BG_BAND, BG_SUN, BG_TOP, BG_GRASS, BG_DARK, BG_FLOWER, CL_CLOUD, CL_CREAM = 10, CL_WHITE, CL_EYE, CL_NOSE,
		   CL_SHADOW = 16 };

	struct BunYuv { int y, u, v; };
	static BunYuv yuvOf(double r, double g, double b)
	{
		// BT.601 limited range, as mpv reads a picture this size
		r /= 255; g /= 255; b /= 255;
		return {(int)std::lround(16 + 65.481 * r + 128.553 * g + 24.966 * b),
				(int)std::lround(128 - 37.797 * r - 74.203 * g + 112.0 * b),
				(int)std::lround(128 + 112.0 * r - 93.786 * g - 18.214 * b)};
	}
	static const int BG_RGB[7] = {0x8fd3ff, 0xc4ecff, 0xffe066, 0x6ed36a, 0x4cb34a, 0x43a341, 0xff8fb8};
	static BunYuv rgbYuv(int c) { return yuvOf((c >> 16) & 255, (c >> 8) & 255, c & 255); }
	// a shadow: rgba(40,80,40,0.35) over a background
	static BunYuv shadowOver(int c)
	{
		auto mix = [](int a, int s) { return 0.65 * a + 0.35 * s; };
		return yuvOf(mix((c >> 16) & 255, 40), mix((c >> 8) & 255, 80), mix(c & 255, 40));
	}

	static bool bunPgm(const std::string& path, int w, int h, const std::vector<unsigned char>& data)
	{
		std::string tmp = path + ".tmp";
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << "P5\n" << w << " " << h << "\n255\n";
		out.write((const char*)data.data(), (std::streamsize)data.size());
		if (!out.flush()) return false;
		out.close();
		return rename(tmp.c_str(), path.c_str()) == 0;
	}

	static size_t bunPgmSize(int w, int h)
	{
		return ("P5\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n").size() + (size_t)w * h;
	}

	// D: each pixel's row less its column's bunny's ground (its top row standing), +40; 0 off the bunnies' columns.
	// C: each pixel's background x32, plus its column's place in the sprite (0-8, +16 on a white bunny) or 31
	static void bunStills(bool strip, std::vector<unsigned char>& D, std::vector<unsigned char>& C)
	{
		const int rows = strip ? SH : H;
		D.assign((size_t)W * rows, 0);
		C.assign((size_t)W * rows, 0);
		for (int x = 0; x < W; x++)
		{
			int k = -1;
			for (int b = 0; b < 5; b++)
				if (x >= bunX(b) - 3 && x < bunX(b) + 12) k = b;
			const int top = hillTop(x, strip);
			for (int y = 0; y < rows; y++)
			{
				const size_t i = (size_t)y * W + x;
				if (k >= 0) D[i] = (unsigned char)(y - (hillTop(bunX(k) + 4, strip) - 10) + 40);
				int bg = BG_SKY;
				if (!strip && y >= 25 && y < 40) bg = BG_BAND;
				if (!strip && (x - 68) * (x - 68) + (y - 10) * (y - 10) <= 10) bg = BG_SUN;
				if (y == top) bg = BG_TOP;
				else if (y > top) bg = (x * 3 + y) % 7 ? BG_GRASS : BG_DARK;
				else if (y == top - 1 && x % 9 == 3) bg = BG_FLOWER;
				int code = 31;
				if (k >= 0 && x >= bunX(k) && x < bunX(k) + 9) code = x - bunX(k) + 16 * (k % 2);
				C[i] = (unsigned char)(bg * 32 + code);
			}
		}
	}

	bool writeBunnyMaps(const std::string& dir)
	{
		const int dims[5][2] = {{W, H}, {W, H}, {W, SH}, {W, SH}, {W + CP, CH}};
		std::string p[5];
		bool ready = true;
		for (int k = 0; k < 5; k++)
		{
			p[k] = dir + "/" + BUN_FILES[k];
			struct stat st;
			ready = ready && stat(p[k].c_str(), &st) == 0 && (size_t)st.st_size == bunPgmSize(dims[k][0], dims[k][1]);
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);
		std::vector<unsigned char> D, C, SD, SC;
		bunStills(false, D, C);
		bunStills(true, SD, SC);
		// the clouds (255), three of them in a period of 100 columns, repeated to fill 180
		std::vector<unsigned char> cloud((size_t)(W + CP) * CH, 0);
		const int parts[3][3] = {{0, 1, 9}, {2, 0, 5}, {1, 2, 8}};
		for (int c = 0; c < 3; c++)
			for (auto& pt : parts)
				for (int i = 0; i < pt[2]; i++)
				{
					const int w = ((c * 31 - 10 + pt[0] + i) % CP + CP) % CP, y = c * 5 + pt[1];
					for (int u = w; u < W + CP; u += CP) cloud[(size_t)y * (W + CP) + u] = 255;
				}
		return bunPgm(p[0], W, H, D) && bunPgm(p[1], W, H, C) && bunPgm(p[2], W, SH, SD) && bunPgm(p[3], W, SH, SC) &&
			   bunPgm(p[4], W + CP, CH, cloud);
	}

	std::string bunnyGraph(bool overCover, const std::string& dir)
	{
		auto n = [](int v) { return std::to_string(v); };
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + BUN_FILES[k], s); return s; };
		const int N = 16, TOP = 8;	// 16 bands; as Peaks the highest 8 measured and cut
		const int rows = overCover ? SH : H, HOP = overCover ? 7 : 18;
		const std::string R = n(rows);
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [bhart1] [bhart2]; [bhart1] crop=640:280:0:0 [top]; "
				 "[bhart2] crop=640:200:0:280,extractplanes=y+u+v [bhay] [bhau] [bhav]; ";
		// the bands measured, pushed apart and eased as Pocket console's bars (up at once, down over ~0.15 s), then a
		// bunny's slice averaged
		const int groups[5][2] = {{0, 3}, {3, 3}, {6, 3}, {9, 3}, {12, 4}};
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,bplut=c0='val*252/255'" + vizContrast("bh") + ","
			 "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split=" + n(overCover ? 5 : 6);
		for (int k = 0; k < 5; k++) g += " [bhs" + n(k) + "]";
		if (!overCover) g += " [bhs5]";
		g += "; ";
		for (int k = 0; k < 5; k++)
			g += "[bhs" + n(k) + "] crop=" + n(groups[k][1]) + ":1:" + n(groups[k][0]) + ":0,scale=1:1:flags=area [bhg" + n(k) + "]; ";
		// each bunny's hop: its level over its own last half second (tmix of 30 frames), so it hops on a rise and rests
		// on a steady sound (measured: the bass slice sits at 170-250 of 252 through a song, which as a plain level kept
		// the first bunny in the air; the others' median is near 0, a note ~110 for a few frames), then let down
		// slowly (lagfun 0.93 a frame: from the top to the grass in ~0.3 s, a hop rather than a flicker). In rows
		// v^2 x HOP; +32 while it's landing (under a row up, not yet still): squashed a row. Over its 15 columns
		g += "[bhg0] [bhg1] [bhg2] [bhg3] [bhg4] hstack=5,split [bhl1] [bhl2]; [bhl2] tmix=frames=30 [bhm]; "
			 "[bhl1] [bhm] bplut2=c0='255*clip((x-y)/110,0,1)',lagfun=decay=0.93,"
			 "bplut=c0='st(0,round(pow(val/255,2)*" + n(HOP) + "));ld(0)+32*lt(ld(0),1)*gt(val,12)',"
			 "scale=75:1:flags=neighbor,pad=" + n(W) + ":1:3:0,scale=" + n(W) + ":" + R + ":flags=neighbor,setsar=1 [bhh]; "
			 "movie=" + q(overCover ? 2 : 0) + " [bhd]; movie=" + q(overCover ? 3 : 1) + " [bhc]; ";
		// each pixel's sprite row + 1 (1-10; squashed: row 1 left out), 20 the shadow (the ground's row under a bunny
		// more than 2 rows up), 0 none
		g += "[bhh] [bhd] bplut2=c0='st(0,mod(x,32));st(1,gte(x,32));st(2,y-40);st(3,ld(2)+ld(0)-ld(1));"
			 "st(4,if(ld(1)*gte(ld(3),1),ld(3)+1,ld(3)));if(between(ld(4),0,9),ld(4)+1,20*gt(ld(0),2)*eq(ld(2),10))' [bhr]; ";
		// the sprite's rows, base 4 by column: 1 fur, 2 eye, 3 nose
		std::string rowsExpr = "0";
		for (int r = 9; r >= 0; r--)
		{
			long code = 0, m = 1;
			for (int c = 0; c < 9; c++, m *= 4)
				code += m * (BUN[r][c] == '#' ? 1 : BUN[r][c] == 'o' ? 2 : BUN[r][c] == 'p' ? 3 : 0);
			rowsExpr = "if(eq(x," + n(r + 1) + ")," + std::to_string(code) + "," + rowsExpr + ")";
		}
		// the class: a bunny's part, a shadow on the grass (not where the hill falls away under it), or the background
		g += "[bhr] [bhc] bplut2=c0='st(0,mod(y,32));st(1,floor(y/32));st(2,mod(ld(0),16));st(3,floor(ld(0)/16));"
			 "st(4,if(lt(ld(0),31)*between(x,1,10),mod(floor(" + rowsExpr + "/pow(4,ld(2))),4),0));"
			 "if(eq(ld(4),1)," + n(CL_CREAM) + "+ld(3),if(eq(ld(4),2)," + n(CL_EYE) + ",if(eq(ld(4),3)," + n(CL_NOSE) + ","
			 "if(eq(x,20)*lt(ld(0),31)*between(ld(2),2,6)*gte(ld(1)," + n(BG_TOP) + ")," + n(CL_SHADOW) + "+ld(1),ld(1)))))'";
		if (!overCover)
			// the clouds: right 4 columns a second, over the sky and its band only
			// (a still is one frame: it's carried by a per-frame row first, or crop's x would be read once)
			g += " [bhb]; [bhs5] crop=1:1:0:0,scale=" + n(W + CP) + ":" + n(CH) + ":flags=neighbor [bhtk]; movie=" + q(4) +
				 " [bhcs]; [bhtk] [bhcs] bplut2=c0='y',crop=" + n(W) + ":" + n(CH) + ":x='mod(" + n(CP) + "-mod(floor(n*4/60)," + n(CP) + ")," + n(CP) +
				 ")':y=0,pad=" + n(W) + ":" + n(H) + ":0:" + n(CY) + ",setsar=1 [bhcl]; [bhb] [bhcl] bplut2=c0='if(lte(x,1)*gt(y,0)," +
				 n(CL_CLOUD) + ",x)'";
		g += ",split=3 [bhk0] [bhk1] [bhk2]; ";

		// each plane's value by class; over the art the sky is the art (y) and a shadow on it darkens it
		const BunYuv cream = rgbYuv(0xf2d7c2), white = rgbYuv(0xfff6ee), eye = rgbYuv(0x2a1a20), nose = rgbYuv(0xff9db8),
					 cloudC = rgbYuv(0xffffff), shade = yuvOf(40, 80, 40);
		// K: the class (lut2's x, lut's val); the art's pixel is lut2's y
		const std::string K = overCover ? "x" : "val";
		auto paint = [&](int BunYuv::*c) {
			// the background (over the art the sky is the art) and a shadow on it
			std::string bg = overCover ? "y" : n(rgbYuv(BG_RGB[BG_SKY]).*c);
			std::string sh = overCover ? "(0.65*y+" + n((int)std::lround(0.35 * (shade.*c))) + ")" : n(shadowOver(BG_RGB[BG_SKY]).*c);
			for (int b = BG_SKY; b <= BG_FLOWER; b++)
			{
				if (overCover && b <= BG_SUN) continue;
				bg = "if(eq(" + K + "," + n(b) + ")," + n(rgbYuv(BG_RGB[b]).*c) + "," + bg + ")";
				sh = "if(eq(" + K + "," + n(CL_SHADOW + b) + ")," + n(shadowOver(BG_RGB[b]).*c) + "," + sh + ")";
			}
			return "'if(gte(" + K + "," + n(CL_SHADOW) + ")," + sh + ",if(eq(" + K + "," + n(CL_CREAM) + ")," + n(cream.*c) + ",if(eq(" + K + "," +
				   n(CL_WHITE) + ")," + n(white.*c) + ",if(eq(" + K + "," + n(CL_EYE) + ")," + n(eye.*c) + ",if(eq(" + K + "," + n(CL_NOSE) + ")," +
				   n(nose.*c) + ",if(eq(" + K + "," + n(CL_CLOUD) + ")," + n(cloudC.*c) + "," + bg + "))))))'";
		};
		if (overCover)
			return g + "[bhk0] scale=640:200:flags=neighbor [bhk0s]; [bhk0s] [bhay] bplut2=c0=" + paint(&BunYuv::y) + " [bhY]; "
					   "[bhk1] scale=320:100:flags=neighbor [bhk1s]; [bhk1s] [bhau] bplut2=c0=" + paint(&BunYuv::u) + " [bhU]; "
					   "[bhk2] scale=320:100:flags=neighbor [bhk2s]; [bhk2s] [bhav] bplut2=c0=" + paint(&BunYuv::v) + " [bhV]; "
					   "[bhY] [bhU] [bhV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "[bhk0] bplut=c0=" + paint(&BunYuv::y) + ",scale=640:480:flags=neighbor [bhY]; "
				   "[bhk1] bplut=c0=" + paint(&BunYuv::u) + ",scale=320:240:flags=neighbor [bhU]; "
				   "[bhk2] bplut=c0=" + paint(&BunYuv::v) + ",scale=320:240:flags=neighbor [bhV]; "
				   "[bhY] [bhU] [bhV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
