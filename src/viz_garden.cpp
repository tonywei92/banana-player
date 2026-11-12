// Sound garden: pixel art on an 80x60 grid of 8-pixel dots. A night-blue sky, a strip of grass at the bottom, and a
// row of 12 flowers, each a band of the spectrum (the bass on the left): a green stem (a leaf on its side once it's
// tall enough) grows up from the grass to its band's eased height, topped by a head in its own colour (pink, yellow,
// violet, orange, sky blue, over again): a small closed bud, opening into a full bloom (petals round a pale centre)
// once its band is past ~55%. Bees (yellow and black, a white wing) drift across on the treble: the louder it is, the
// more of the three are out.
// A frame: the 12 levels measured and eased (as Banana stack), each made a step count (0..30) with its flower's colour
// above it (+32 a colour), stretched over the flower's 6 columns and down the rows (80x60), and one lut2 against a still
// mask, each dot of which says where it is in its flower (its column round the stem, its row above the grass) or that
// it's grass or sky: the lut2 knows from the step count where the stem's top is, so it picks sky, stem, leaf, bud,
// petal or centre. The bees: a still strip of three, each in its own band of rows, made one frame a frame by a lut2
// against the treble (a bee is out or not), each band cropped sideways by the time (and bobbing), stacked and laid
// over the flowers by a lut2. Everything to there is 80x60 (4800 dots: next to nothing); every colour is its own
// luma, the luma scaled x8 (nearest) with the grid's 1-pixel lines laid over it (a still), and U and V looked up from
// the luma at half size, as Pocket console. YUV and gray only, nothing converted, no geq per frame.
// Over the art: the art's top as it is, and in the 640x200 strip under it (25 dots high) the flowers on a thin line of
// grass, with no sky: the art shows through everywhere but the plants, the bees and the grass line (and between the
// dots, as Pocket console's LCD)
#include "viz_more.h"
#include "settings.h"
#include <cmath>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	static const int W = 80;							// dots across
	static const int N = 12, CW = 6, LEFT = 4;			// flowers, a flower's columns, the first one's (12 x 6 + 8 = 80)
	static const int ROWS_ALONE = 60, ROWS_STRIP = 25;
	// the bee strip: three bees, each in 10 rows of 170 (two widths and a bit: the crop's window slides over 90)
	static const int BW = 170, BH = 10, BEES = 3;
	// a new name whenever the stills change: they're cached by size
	static const char* FILES[3] = {"garden1-alone.pgm", "garden1-strip.pgm", "garden1-bees.pgm"};

	// the mask's values: 0 sky (or nothing), 1 the grass's top line, 2 and 3 grass; from 8 a flower's dot: 8 +
	// (its column round the stem + 2) x 40 + (its row above the grass - 1)
	enum { M_SKY = 0, M_GRASS1 = 1, M_GRASS2 = 2, M_GRASS3 = 3, M_FLOWER = 8 };
	static const int RSPAN = 40;

	struct Shape { int rows, grass, h0, hs, openq, leaf, beeTop; };
	// the grass's first row, the stem's height at silence (h0, in dots) and its growth (hs steps), the steps at which a
	// bud opens (55%), the height from which it has a leaf, the bees' first row
	static Shape shape(bool strip)
	{
		if (strip) return {ROWS_STRIP, 24, 3, 18, 10, 6, 1};
		return {ROWS_ALONE, 52, 4, 30, 17, 8, 11};
	}

	static std::vector<unsigned char> mask(bool strip)
	{
		const Shape s = shape(strip);
		std::vector<unsigned char> m((size_t)W * s.rows, M_SKY);
		for (int y = 0; y < s.rows; y++)
			for (int x = 0; x < W; x++)
			{
				unsigned char& p = m[(size_t)y * W + x];
				if (y >= s.grass)
				{
					// over the art only the top line
					p = y == s.grass ? M_GRASS1 : (x + y) % 5 ? M_GRASS2 : M_GRASS3;
					continue;
				}
				int r = s.grass - y;
				if (r > RSPAN || x < LEFT || x >= LEFT + N * CW) continue;
				int dx = (x - LEFT) % CW - 2;	// -2..3 round the stem; 3 nothing (the gap)
				if (dx > 2) continue;
				p = (unsigned char)(M_FLOWER + (dx + 2) * RSPAN + (r - 1));
			}
		return m;
	}

	// the bees: 4 x the bee + its part (1 the wing, 2 yellow, 3 black); bee b's wing on its band's row 4, its body on
	// row 5, at columns 85-87 (the crop puts it at 85 - the window's left)
	static std::vector<unsigned char> bees()
	{
		std::vector<unsigned char> m((size_t)BW * BH * BEES, 0);
		for (int b = 0; b < BEES; b++)
		{
			auto at = [&](int x, int y, int part) { m[(size_t)(b * BH + y) * BW + x] = (unsigned char)(4 * b + part); };
			at(86, 4, 1);
			at(85, 5, 2);
			at(86, 5, 3);
			at(87, 5, 2);
		}
		return m;
	}

	bool writeGardenMaps(const std::string& dir)
	{
		const int ws[3] = {W, W, BW}, hs[3] = {ROWS_ALONE, ROWS_STRIP, BH * BEES};
		auto header = [&](int k) { return "P5\n" + std::to_string(ws[k]) + " " + std::to_string(hs[k]) + "\n255\n"; };
		auto ready = [&](int k) {
			struct stat st;
			return stat((dir + "/" + FILES[k]).c_str(), &st) == 0 && (size_t)st.st_size == header(k).size() + (size_t)ws[k] * hs[k];
		};
		if (ready(0) && ready(1) && ready(2)) return true;
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < 3; k++)
		{
			std::vector<unsigned char> m = k == 2 ? bees() : mask(k == 1);
			std::string path = dir + "/" + FILES[k], tmp = path + ".tmp";
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			out << header(k);
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

	std::string gardenGraph(bool overCover, const std::string& mapsDir)
	{
		auto n = [](int v) { return std::to_string(v); };
		const Shape s = shape(overCover);
		const int R = s.rows;
		std::string mpath, bpath;
		lavfiPath(mapsDir + "/" + FILES[overCover ? 1 : 0], mpath);
		lavfiPath(mapsDir + "/" + FILES[2], bpath);

		// the colours; every one its own luma (the chroma is looked up from it), nudged apart where two would meet.
		// The yellow petal and the bee's yellow are one colour
		enum { C_SKY, C_G1, C_G2, C_G3, C_STEM, C_CENTRE, C_P0, C_P1, C_P2, C_P3, C_P4, C_BLACK, C_WING, C_COUNT };
		const unsigned rgb[C_COUNT] = {0x0f1a2e, 0x3f8f3a, 0x2a5a26, 0x244e20, 0x4fb04a, 0xffe9a0,
									   0xff5a8a, 0xffd23f, 0x9b7bff, 0xff8a3d, 0x5ad1ff, 0x1b1206, 0xe8f4ff};
		Yuv c[C_COUNT];
		for (int k = 0; k < C_COUNT; k++)
		{
			c[k] = yuv(rgb[k]);
			for (bool again = true; again;)
			{
				again = false;
				for (int j = 0; j < k; j++)
					if (c[j].y == c[k].y) { c[k].y++; again = true; }
			}
		}
		const int PETALS = 5;
		const std::string sky = overCover ? "0" : n(c[C_SKY].y);

		// the flowers' state (x: steps + 32 x colour) against the mask (y)
		const std::string q = "mod(x,32)", h = "(" + n(s.h0) + "+mod(x,32))";
		const std::string dx = "(floor((y-" + n(M_FLOWER) + ")/" + n(RSPAN) + ")-2)";
		const std::string r = "(mod(y-" + n(M_FLOWER) + "," + n(RSPAN) + ")+1)";
		const std::string d = "(" + r + "-" + h + ")";	// the dot's row from the stem's top (up +)
		std::string petal = n(c[C_P0 + PETALS - 1].y);
		for (int k = PETALS - 2; k >= 0; k--) petal = "if(eq(floor(x/32)," + n(k) + ")," + n(c[C_P0 + k].y) + "," + petal + ")";
		const std::string stem = "(eq(" + dx + ",0)*lt(" + r + "," + h + ")+gt(" + h + "," + n(s.leaf) + ")*(eq(" + dx + ",-1)*eq(" + r +
								 ",floor(" + h + "/2))+eq(" + dx + ",-2)*eq(" + r + ",floor(" + h + "/2)+1)))";
		const std::string plant = "if(" + stem + "," + n(c[C_STEM].y) + "," + sky + ")";
		const std::string bloom = "if(eq(" + dx + ",0)*eq(" + d + ",0)," + n(c[C_CENTRE].y) + ",if(eq(abs(" + dx + "),1)*eq(abs(" + d +
								  "),1)+eq(" + dx + ",0)*eq(abs(" + d + "),2)+eq(abs(" + dx + "),2)*eq(" + d + ",0)," + petal + "," + plant + "))";
		const std::string bud = "if(eq(" + d + ",0)*lte(abs(" + dx + "),1)+eq(" + dx + ",0)*eq(" + d + ",1)," + petal + "," + plant + ")";
		const std::string grass = overCover ? "if(eq(y," + n(M_GRASS1) + ")," + n(c[C_G1].y) + ",0)"
											: "if(eq(y,0)," + sky + ",if(eq(y,1)," + n(c[C_G1].y) + ",if(eq(y,2)," + n(c[C_G2].y) + "," +
												  n(c[C_G3].y) + ")))";
		const std::string flowers = "'if(lt(y," + n(M_FLOWER) + ")," + grass + ",if(gte(" + q + "," + n(s.openq) + ")," + bloom + "," + bud + "))'";

		// the levels over the flowers' columns and down the rows (fillborders wants an even height: an odd one is
		// smeared a row taller and cut)
		const int EV = R + (R & 1), mid = EV / 2 - 1;
		const std::string stretch = "scale=" + n(N * CW) + ":1:flags=neighbor,pad=" + n(W) + ":" + n(EV) + ":" + n(LEFT) + ":" + n(mid) +
									",fillborders=top=" + n(mid) + ":bottom=" + n(EV - 1 - mid) + ":mode=smear" +
									(EV != R ? ",crop=" + n(W) + ":" + n(R) + ":0:0" : std::string()) + ",setsar=1";
		// as Banana stack: 6 bands a flower, averaged; the top ninth (16-24 kHz) measured and cut off
		const int SUB = 6, TOP = 9;
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [gdart1] [gdart2]; [gdart1] crop=640:280:0:0 [top]; "
				 "[gdart2] crop=640:200:0:280,extractplanes=y+u+v [gday] [gdau] [gdav]; ";
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N * SUB + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N * SUB) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("gd") +
			 // eased: up at once, down over ~0.15 s (Pocket console's exp/log lagfun)
			 ",format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split [gdl] [gdt]; "
			 // each flower's colour (0..4 over again): a still row, one frame
			 "color=black:s=" + n(N) + "x1:r=1:d=1,format=gray,geq=lum='mod(X," + n(PETALS) + ")' [gdc]; "
			 "[gdl] [gdc] lut2=c0='min(" + n(s.hs) + ",round(x*" + n(s.hs) + "/252))+32*y'," + stretch + " [gdS]; "
			 "movie=" + mpath + ",format=gray [gdM]; "
			 "[gdS] [gdM] lut2=c0=" + flowers + " [gdF]; "
			 // the treble (the top three flowers' levels) over the bee strip: a bee out (its part) when the treble is
			 // past 30%, 45% or 60% for the first, second and third, else 0
			 "[gdt] crop=3:1:" + n(N - 3) + ":0,scale=1:1:flags=area,scale=" + n(BW) + ":" + n(BH * BEES) + ":flags=neighbor [gdT]; "
			 "movie=" + bpath + ",format=gray [gdB]; "
			 "[gdT] [gdB] lut2=c0='if(eq(y,0),0,if(gt(x,76+38*floor(y/4)),mod(y,4),0))',split=3 [gdb0] [gdb1] [gdb2]; ";
		// each bee's band: sliding right 6 dots a second (each on its own place in the 90-dot round), bobbing 2 up and down
		for (int b = 0; b < BEES; b++)
			g += "[gdb" + n(b) + "] crop=w=" + n(W) + ":h=6:x='90-mod(t*6+" + n(27 * b) + ",90)':y='" + n(BH * b + 2) + "-round(2*sin(t*5+" +
				 n(b) + "))' [gdbc" + n(b) + "]; ";
		g += "[gdbc0] [gdbc1] [gdbc2] vstack=inputs=3,pad=" + n(W) + ":" + n(R) + ":0:" + n(s.beeTop) + " [gdBz]; "
			 "[gdF] [gdBz] lut2=c0='if(eq(y,0),x,if(eq(y,1)," + n(c[C_WING].y) + ",if(eq(y,2)," + n(c[C_P1].y) + "," + n(c[C_BLACK].y) + ")))'";
		if (overCover)
		{
			// the art shows through where nothing (0) and between the dots; each plane the art's there, else the colour's
			auto paint = [&](int Yuv::*plane) {
				std::string e = "y";
				for (int k = C_COUNT - 1; k >= 0; k--)
					if (k != C_SKY) e = "if(eq(x," + n(c[k].y) + ")," + n(c[k].*plane) + "," + e + ")";
				return "'" + e + "'";
			};
			return g + ",split=3 [gdc0] [gdc1] [gdc2]; "
					   "color=black:s=640x200:r=1:d=1,format=gray,geq=lum='255*gte(eq(mod(X,8),7)+eq(mod(Y,8),7),1)' [gdG]; "
					   "color=black:s=320x100:r=1:d=1,format=gray,geq=lum='255*gte(eq(mod(X,4),3)+eq(mod(Y,4),3),1)',split [gdGu] [gdGv]; "
					   "[gdc0] scale=640:200:flags=neighbor [gdc0s]; [gdc0s] [gdG] lut2=c0='if(y,0,x)' [gdc0g]; "
					   "[gdc0g] [gday] lut2=c0=" + paint(&Yuv::y) + " [gdY]; "
					   "[gdc1] scale=320:100:flags=neighbor [gdc1s]; [gdc1s] [gdGu] lut2=c0='if(y,0,x)' [gdc1g]; "
					   "[gdc1g] [gdau] lut2=c0=" + paint(&Yuv::u) + " [gdU]; "
					   "[gdc2] scale=320:100:flags=neighbor [gdc2s]; [gdc2s] [gdGv] lut2=c0='if(y,0,x)' [gdc2g]; "
					   "[gdc2g] [gdav] lut2=c0=" + paint(&Yuv::v) + " [gdW]; "
					   "[gdY] [gdU] [gdW] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		}
		// alone: the luma scaled x8 with the grid's lines (the sky's colour) over it; U and V from the luma at half size
		auto chroma = [&](int Yuv::*plane) {
			std::string e = n(c[C_SKY].*plane);
			for (int k = C_COUNT - 1; k > 0; k--) e = "if(eq(val," + n(c[k].y) + ")," + n(c[k].*plane) + "," + e + ")";
			return "'" + e + "'";
		};
		g += ",split=3 [gdy] [gdu] [gdw]; "
			 "color=black:s=640x480:r=1:d=1,format=gray,geq=lum='255*gte(eq(mod(X,8),7)+eq(mod(Y,8),7),1)' [gdG]; "
			 "[gdy] scale=640:480:flags=neighbor [gdY0]; [gdY0] [gdG] lut2=c0='if(y," + n(c[C_SKY].y) + ",x)' [gdY]; "
			 "[gdu] scale=320:240:flags=neighbor,lut=c0=" + chroma(&Yuv::u) + " [gdU]; "
			 "[gdw] scale=320:240:flags=neighbor,lut=c0=" + chroma(&Yuv::v) + " [gdW]; "
			 "[gdY] [gdU] [gdW] mergeplanes=0x001020:yuv420p [vo]";
		return g;
	}
}
