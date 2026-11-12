#include "viz_more.h"
#include "settings.h"
#include <cmath>
#include <fstream>
#include <sys/stat.h>
#include <vector>

// Banana stack: on a dark purple screen with faint lines, 12 columns, each a pile of pixel-art bananas filled from the
// bottom by its band's level, and a cap banana (paler) at the band's peak, falling back slowly (as Peaks' caps).
// Every pixel's part in the picture (which slot of which pile, and whether it's a banana's body, its shade, its
// outline, a line or the screen) is a still mask written once (writeBananaMaps), so a frame is 12 levels and their
// caps (Peaks' measuring and lagfun easing), each stretched over its column's pixels, and two lookups a pixel (lut2)
// against the mask. Drawn at half size (320x240; the sprite's art pixels are 2 there, 4 on the panel), as Peaks: the
// half-size picture is the chroma's planes as it is, and the luma scaled x2 (nearest). YUV only, nothing converted.
// Over the art: the strip under its top (320x100 here) has no screen of its own; the art shows through wherever no
// banana is lit, plane by plane (as Peaks)
namespace bp
{
	static const int N = 12, CW = 26, LEFT = 4;	// columns, their width and the left margin (12 x 26 + 8 = 320)
	static const int W = 320, H_ALONE = 240, H_STRIP = 100;
	static const int ART = 2;	// an art pixel at half size
	static const int PITCH = 14;	// a slot's height at half size: a banana (12) and a gap (2)
	// the sprite, 10 x 6 art pixels, lying in its pile: O the outline (and the stem, right), B the body, S its shade
	static const char* SPRITE[] = {
		"........OO",
		"OO.....OBO",
		"OBOOOOOBBO",
		".OBBBBBBO.",
		"..OSSSSO..",
		"...OOOO...",
	};
	static const int SW = 10, SH = 6;
	// what a mask pixel is (its low 3 bits; the slot above them): the cap's mark is 128, added per frame
	enum { T_BG = 0, T_LINE = 1, T_BODY = 2, T_SHADE = 3, T_OUTLINE = 4 };
	// a new name whenever the masks change: they're cached by size
	static const char* MASKS[2] = {"banana2-alone.pgm", "banana2-strip.pgm"};

	// slots in a pile and the margin under it (the bottom pile stands 4 or 2 half-size rows over the screen's last);
	// alone, the top 70 or so rows are left to the song's title
	static int slotsFor(int h) { return h == H_ALONE ? 13 : 7; }
	static int floorFor(int h) { return h == H_ALONE ? 4 : 2; }

	static std::vector<unsigned char> mask(int h)
	{
		std::vector<unsigned char> m((size_t)W * h, T_BG);
		// the faint lines (alone): one half-size row in 8
		if (h == H_ALONE)
			for (int y = 3; y < h; y += 8)
				for (int x = 0; x < W; x++) m[(size_t)y * W + x] = T_LINE;
		const int S = slotsFor(h), bottom = h - floorFor(h);
		for (int c = 0; c < N; c++)
			for (int s = 0; s < S; s++)
			{
				// every other banana turned round and moved over a pixel, so a pile reads as a pile, not a column
				bool flip = s & 1;
				int x0 = LEFT + c * CW + (CW - SW * ART) / 2 + (flip ? 1 : -1);
				int y0 = bottom - SH * ART - s * PITCH;
				for (int r = 0; r < SH * ART; r++)
					for (int q = 0; q < SW * ART; q++)
					{
						int ax = q / ART, ay = r / ART;
						char k = SPRITE[ay][flip ? SW - 1 - ax : ax];
						int t = k == 'O' ? T_OUTLINE : k == 'B' ? T_BODY : k == 'S' ? T_SHADE : -1;
						int x = x0 + q, y = y0 + r;
						if (t < 0 || x < 0 || x >= W || y < 0 || y >= h) continue;
						m[(size_t)y * W + x] = (unsigned char)(s * 8 + t);
					}
			}
		return m;
	}

	bool writeBananaMaps(const std::string& dir)
	{
		const int hs[2] = {H_ALONE, H_STRIP};
		auto header = [](int h) { return "P5\n" + std::to_string(W) + " " + std::to_string(h) + "\n255\n"; };
		auto ready = [&](int k) {
			struct stat st;
			return stat((dir + "/" + MASKS[k]).c_str(), &st) == 0 && (size_t)st.st_size == header(hs[k]).size() + (size_t)W * hs[k];
		};
		if (ready(0) && ready(1)) return true;
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < 2; k++)
		{
			std::vector<unsigned char> m = mask(hs[k]);
			std::string path = dir + "/" + MASKS[k], tmp = path + ".tmp";
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			out << header(hs[k]);
			out.write((const char*)m.data(), (std::streamsize)m.size());
			if (!out.flush()) return false;
			out.close();
			if (rename(tmp.c_str(), path.c_str()) != 0) return false;
		}
		return true;
	}

	struct Yuv { int y, u, v; };
	// BT.601 limited (mpv's reading of a picture this size)
	static Yuv yuv(unsigned rgb)
	{
		double r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
		return {(int)std::lround(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255),
				(int)std::lround(128 + (-37.797 * r - 74.203 * g + 112 * b) / 255),
				(int)std::lround(128 + (112 * r - 93.786 * g - 18.214 * b) / 255)};
	}

	std::string bananaGraph(bool overCover, const std::string& mapsDir)
	{
		auto n = [](int v) { return std::to_string(v); };
		const int H = overCover ? H_STRIP : H_ALONE, S = slotsFor(H);
		std::string mpath;
		lavfiPath(mapsDir + "/" + MASKS[overCover ? 1 : 0], mpath);
		// each column's value over its 26 pixels and down all the rows (as Peaks: one row, padded, smeared)
		const int mid = H / 2 - 1;
		const std::string stretch = "scale=" + n(N * CW) + ":1:flags=neighbor,pad=" + n(W) + ":" + n(H) + ":" + n(LEFT) + ":" + n(mid) +
									",fillborders=top=" + n(mid) + ":bottom=" + n(H - 1 - mid) + ":mode=smear,setsar=1";
		// the slots a level fills (0..S) and the cap's slot (0..S-1), from the 252 steps
		const std::string sl = n(S) + "/252+0.5";
		const std::string lit = "min(" + n(S) + ",floor(x*" + sl + "))", capAt = "min(" + n(S - 1) + ",floor(x*" + sl + "))";
		// measured in 6 bands a column (one a column left some columns empty, the log scale's bins falling between
		// them) and averaged; as Peaks, the top ninth (16-24 kHz, next to nothing in music) measured and cut off
		const int SUB = 6, TOP = 9;
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [bnart1] [bnart2]; [bnart1] crop=640:280:0:0 [top]; "
				 "[bnart2] crop=640:200:0:280,extractplanes=y+u+v [bnay] [bnau] [bnav]; ";
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N * SUB + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N * SUB) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("bn") + ",split [bnv] [bnp]; "
			 // the caps: held, and let fall at an even speed (Peaks' exp/log lagfun, ~2.4 s for the whole height)
			 "[bnp] format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.9316,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray," + stretch + " [bnP]; "
			 // the piles eased: up at once, down over ~0.15 s (as Pocket console's bars)
			 "[bnv] format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray," + stretch + " [bnV]; "
			 "movie=" + mpath + ",format=gray [bnM]; "
			 // +128: a banana's pixel in the cap's slot
			 "[bnP] [bnM] lut2=c0='if(gte(mod(y,8),2)*eq(floor(y/8)," + capAt + "),y+128,y)' [bnQ]; ";
		// each pixel's class by the level (x) and the marked mask (y): s its slot, t its part
		const std::string s = "floor(mod(y,128)/8)", t = "mod(y,8)";
		auto classify = [&](const std::string& body, const std::string& shade, const std::string& outline, const std::string& cap,
							const std::string& line, const std::string& ghost, const std::string& bg) {
			std::string part = "if(eq(" + t + ",2),%B,if(eq(" + t + ",3)," + shade + "," + outline + "))";
			auto with = [&](const std::string& b) {
				std::string p = part;
				p.replace(p.find("%B"), 2, b);
				return p;
			};
			return "'if(gte(" + t + ",2)*lt(" + s + "," + lit + ")," + with(body) + ",if(gte(y,128)*gte(" + s + "," + lit + ")," +
				   with(cap) + ",if(eq(" + t + ",1)," + line + ",if(eq(" + t + ",4)," + ghost + "," + bg + "))))'";
		};
		const Yuv body = yuv(0xffd23f), shade = yuv(0xe9a92b), outline = yuv(0x5a3a00), cap = yuv(0xfff4b0);
		if (overCover)
		{
			// 0 the art, 1 body, 2 shade, 3 outline, 4 the cap's body; then each plane the art's where 0, else a colour
			auto paint = [&](int b, int sh, int o, int c) {
				return "'if(eq(x,0),y,if(eq(x,1)," + n(b) + ",if(eq(x,2)," + n(sh) + ",if(eq(x,3)," + n(o) + "," + n(c) + "))))'";
			};
			return g + "[bnV] [bnQ] lut2=c0=" + classify("1", "2", "3", "4", "0", "0", "0") + ",split=3 [bnc0] [bnc1] [bnc2]; "
					   "[bnc0] scale=640:200:flags=neighbor [bncy]; "
					   "[bncy] [bnay] lut2=c0=" + paint(body.y, shade.y, outline.y, cap.y) + " [bnY]; "
					   "[bnc1] [bnau] lut2=c0=" + paint(body.u, shade.u, outline.u, cap.u) + " [bnU]; "
					   "[bnc2] [bnav] lut2=c0=" + paint(body.v, shade.v, outline.v, cap.v) + " [bnW]; "
					   "[bnY] [bnU] [bnW] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		}
		// alone: the luma at half size (every colour its own Y), U and V from it, the luma scaled up. An unlit slot's
		// outline a faint ghost, so the piles' places show on a quiet screen
		const Yuv bg = yuv(0x16121f), line = yuv(0x1f1a2c), ghost = yuv(0x262035);
		const Yuv all[] = {body, shade, outline, cap, line, ghost};
		auto chroma = [&](int Yuv::*plane) {
			std::string e = n(bg.*plane);
			for (const Yuv& c : all) e = "if(eq(val," + n(c.y) + ")," + n(c.*plane) + "," + e + ")";
			return "'" + e + "'";
		};
		g += "[bnV] [bnQ] lut2=c0=" + classify(n(body.y), n(shade.y), n(outline.y), n(cap.y), n(line.y), n(ghost.y), n(bg.y)) +
			 ",split=3 [bny] [bnu] [bnw]; "
			 "[bny] scale=640:480:flags=neighbor [bnY]; "
			 "[bnu] lut=c0=" + chroma(&Yuv::u) + " [bnU]; "
			 "[bnw] lut=c0=" + chroma(&Yuv::v) + " [bnW]; "
			 "[bnY] [bnU] [bnW] mergeplanes=0x001020:yuv420p [vo]";
		return g;
	}
}
