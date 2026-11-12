// Disco ball: a mirror ball hanging on a string in the middle of a near-black room, spinning like a globe, drawn in
// big 8-pixel cells (80x60). Its facets are a latitude/longitude grid on the sphere (16 round, 9 down), sliding
// sideways as it turns and squeezing toward its edge, with dark grout between them, lit from the upper left; each facet
// mirrors one of the 16 lights (its hue), brightening and saturating as that band plays, and some flash white on the
// treble. Round it three tilted rings (faint tracks) carry the 16 lights (6, 5, 5), each ring at its own speed and
// direction; each light a band, as big and bright as its eased level, with a soft glow; the lights behind the ball are
// hidden by it, those in front drawn over it.
//
// How a frame is made, all of it at 80x60 cells or on small textures (the stills are written once, writeDiscoMaps):
// - the 16 bands measured (showfreqs at 60/s), vizContrast, eased (the exp/log lagfun: up at once, down in ~0.15 s);
// - the ball: a facet texture 256x18 (two turns of 128 columns, 8 a facet, by 9 rows of facets, then the same 9 rows
//   again for the flash) is filled by a remap from the levels and a 17th value, the flash threshold (the treble's mean,
//   plus a little on a strong bass), and a lut2 against a still (the facet's band and grout, or its flash threshold):
//   each texel a code, its band and level (14 steps), grout or white. The two halves are max'ed, and the turn is a crop
//   of that texture moving with t. A still remap table gives each of the ball's cells its longitude and latitude row in
//   it: the facets slide and squeeze as on a globe, for the cost of a lookup a cell;
// - the lights' discs: 16 sprites (7x7 cells, a front and a back row) made each frame by a remap from the levels and a
//   lut2 (a cell is in the disc if dx^2 + dy^2 <= r^2 + 1, r 1..3 by the level, as the preview draws them), each laid
//   on the cells by an overlay whose place is worked out from t (its ring's ellipse, rounded to a cell); the sprite's
//   front or back row is cropped by t too, and those behind are taken out where the ball or the string is;
// - the lights' glows: each ring is unrolled into a texture, angle along the ring (192 columns) by distance from its
//   track (8 rows) for 2 classes of how fast the ring's angle moves on screen there. The three rings' textures
//   (stacked, 192x48) are filled the same way (remap from the levels, lut2 against a still of each texel's nearest
//   light and its distance): the light's band and the glow's strength (16 steps). Doubled sideways (hstack), each
//   ring's part is cropped at an offset moving with t, and remapped onto the cells by a still table (each cell near
//   the track: the angle of the nearest point and its distance; inside the ball only the ring's front half). The three
//   are max'ed;
// - the colours: a lut2 a plane, the ball's code against a still of each cell's class (its light, grout, or the room:
//   background, track, string), then a lut2 a plane of that against the discs' code, and a lut a plane for the glow.
//   The colours come from HSL worked out in the lut2s' expressions, once, at start;
// - the scene's planes and the glow's merged (4:4:4), the glow smoothed (bilinear x2) and added at 160x120, then scaled
//   to the picture (nearest) and to 4:2:0 in one go. YUV and gray only: no geq, nothing converted per frame.
// Over the art: its top 640x280 as it is, and in the 640x200 strip under it the scene at half the cell size (4
// pixels, its rows 6..55), with no room: the art shows through it (maskedmerge, a mask by the same lut2s), the glows
// added over it.
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
	static const int FILES = 14;
	static const char* DISCO_FILES[FILES] = {"disco3-bx.pgm", "disco3-by.pgm", "disco3-pix.pgm", "disco3-fx.pgm", "disco3-fy.pgm",
											 "disco3-fc.pgm", "disco3-dx.pgm", "disco3-dy.pgm", "disco3-dc.pgm", "disco3-rx.pgm",
											 "disco3-ry.pgm", "disco3-sx.pgm", "disco3-sy.pgm", "disco3-sc.pgm"};
	static const int W = 80, H = 60;						// the cells
	static const int N = 16;								// the bands, one a light
	static const int CX = 40, CY = 31, R = 13;				// the ball
	static const int FT = 128, FW = 2 * FT, FR = 9;			// the facet texture: a turn's columns, two turns, its rows
	static const int U = 192, CLS = 2, SR = 8, RROWS = CLS * SR;	// a ring's glow texture: columns, classes, distance rows
	static const int SP = 7;									// a light's sprite (cells a side)
	static const double REACH = 7.25;						// how far from a track a light reaches (a glow's radius 7)
	// the pixel classes: 0..100 a facet cell's light (x80), 101..201 a grout cell's, then the room
	static const int P_GROUT = 101, P_BG = 202, P_TRACK = 203, P_STRING = 204;
	static const double SPIN = 1.2;							// the ball's turn, radians a second
	struct Ring { double rx, ry, tilt, speed; int n, from; };
	static const Ring RINGS[3] = {{19, 6, -0.25, 0.9, 6, 0}, {27, 10, 0.18, -0.6, 5, 6}, {35, 14, -0.1, 0.4, 5, 11}};
	static const int HUES[N] = {330, 300, 270, 240, 210, 190, 170, 140, 110, 80, 60, 45, 30, 15, 0, 350};

	// the preview's fixed noise (a facet's band, its flash threshold), the same numbers
	static std::vector<double> noise()
	{
		std::vector<double> a(80 * 120);
		int64_t seed = 7;
		for (auto& v : a)
		{
			seed = (seed * 16807) % 2147483647;
			v = (double)seed / 2147483647;
		}
		return a;
	}

	static bool writePgm(const std::string& path, int w, int h, int maxv, const std::vector<unsigned char>& data)
	{
		std::string tmp = path + ".tmp";
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << "P5\n" << w << " " << h << "\n" << maxv << "\n";
		out.write((const char*)data.data(), (std::streamsize)data.size());
		if (!out.flush()) return false;
		out.close();
		return rename(tmp.c_str(), path.c_str()) == 0;
	}

	static size_t pgmSize(int w, int h, int bytes)
	{
		std::string header = "P5\n" + std::to_string(w) + " " + std::to_string(h) + "\n" + (bytes == 2 ? "65535" : "255") + "\n";
		return header.size() + (size_t)w * h * bytes;
	}

	static void put16(std::vector<unsigned char>& v, size_t i, int x)
	{
		v[i * 2] = (unsigned char)(x >> 8);
		v[i * 2 + 1] = (unsigned char)x;
	}

	// a point of ring j at angle a (cells)
	static void ringAt(const Ring& r, double a, double& x, double& y)
	{
		double ex = std::cos(a) * r.rx, ey = std::sin(a) * r.ry;
		x = CX + ex * std::cos(r.tilt) - ey * std::sin(r.tilt);
		y = CY + ex * std::sin(r.tilt) + ey * std::cos(r.tilt);
	}

	static bool inBall(int x, int y) { return (x - CX) * (x - CX) + (y - CY) * (y - CY) <= R * R; }

	bool writeDiscoMaps(const std::string& dir)
	{
		std::string p[FILES];
		for (int k = 0; k < FILES; k++) p[k] = dir + "/" + DISCO_FILES[k];
		const size_t sizes[FILES] = {pgmSize(W, H, 2),		  pgmSize(W, H, 2),			pgmSize(W, H, 1),
									 pgmSize(FW, 2 * FR, 2),  pgmSize(FW, 2 * FR, 2),	pgmSize(FW, 2 * FR, 1),
									 pgmSize(U, 3 * RROWS, 2), pgmSize(U, 3 * RROWS, 2), pgmSize(U, 3 * RROWS, 1),
									 pgmSize(W, 3 * H, 2),	  pgmSize(W, 3 * H, 2),		pgmSize(N * SP, 2 * SP, 2),
									 pgmSize(N * SP, 2 * SP, 2), pgmSize(N * SP, 2 * SP, 1)};
		bool ready = true;
		for (int k = 0; k < FILES && ready; k++)
		{
			struct stat st;
			ready = stat(p[k].c_str(), &st) == 0 && (size_t)st.st_size == sizes[k];
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);
		const std::vector<double> NZ = noise();

		// the ball's table (each ball cell: its longitude in the facet texture, its facet row) and the cells' classes
		std::vector<unsigned char> bx((size_t)W * H * 2, 0xff), by((size_t)W * H * 2, 0), pix((size_t)W * H, P_BG);
		for (int j = 0; j < 3; j++)
			for (int k = 0; k < 64; k++)
			{
				double x, y;
				ringAt(RINGS[j], k / 64.0 * 2 * M_PI, x, y);
				int X = (int)std::floor(x + 0.5), Y = (int)std::floor(y + 0.5);
				if (X >= 0 && X < W && Y >= 0 && Y < H) pix[(size_t)Y * W + X] = P_TRACK;
			}
		for (int y = 0; y < CY - R; y++) pix[(size_t)y * W + CX] = P_STRING;
		for (int y = -R; y <= R; y++)
			for (int x = -R; x <= R; x++)
			{
				int rr = x * x + y * y;
				if (rr > R * R) continue;
				double lat = std::asin((double)y / R), cl = std::cos(lat);
				if (cl < 1e-6) cl = 1e-6;
				double lon = std::asin(std::max(-1.0, std::min(1.0, x / (R * cl))));
				int fy = std::min(FR - 1, (int)std::floor((lat + M_PI / 2) / (M_PI / 9)));
				int u = (int)std::lround((lon + M_PI / 2) / (2 * M_PI) * FT);
				double nz = std::sqrt(std::max(0.0, 1 - (double)rr / (R * R)));
				double light = std::max(0.0, (-x * 0.5 - y * 0.6 + nz * R * 0.7) / R);
				double fl = (lat + M_PI / 2) / (M_PI / 9);
				bool grout = fl - std::floor(fl) < 0.12;
				size_t i = (size_t)(CY + y) * W + CX + x;
				put16(bx, i, u);
				put16(by, i, fy);
				pix[i] = (unsigned char)((grout ? P_GROUT : 0) + std::min(100, (int)std::lround(light * 80)));
			}

		// the facet texture's stills: rows 0..8 a texel's band (remap) and its band or 16 for grout (lut2), rows 9..17
		// the flash value (the 17th) and 17 + the facet's flash threshold (255: grout, never)
		std::vector<unsigned char> fx((size_t)FW * 2 * FR * 2), fy((size_t)FW * 2 * FR * 2, 0), fc((size_t)FW * 2 * FR);
		for (int r = 0; r < FR; r++)
			for (int u = 0; u < FW; u++)
			{
				int f = (((u % FT) / 8 - 4) % 16 + 16) % 16, id = f * 9 + r;
				int band = (int)std::floor(NZ[id + 400] * 16);
				bool grout = u % 8 == 0;
				size_t a = (size_t)r * FW + u, b = (size_t)(r + FR) * FW + u;
				put16(fx, a, band);
				put16(fx, b, N);
				fc[a] = (unsigned char)(grout ? 16 : band);
				fc[b] = (unsigned char)(grout ? 255 : 17 + std::min(238, (int)std::floor(NZ[id] * 238 / 0.4)));
			}

		// the rings' glow textures (stacked): each texel the nearest light's band (remap) and its distance (half cells)
		// x16 + the band (lut2); a class's rows 0..7 cells from the track
		std::vector<unsigned char> dx((size_t)U * 3 * RROWS * 2), dy((size_t)U * 3 * RROWS * 2, 0), dc((size_t)U * 3 * RROWS);
		for (int j = 0; j < 3; j++)
		{
			const Ring& rg = RINGS[j];
			for (int c = 0; c < CLS; c++)
				for (int si = 0; si < SR; si++)
					for (int u = 0; u < U; u++)
					{
						double st = rg.ry + (c + 0.5) * (rg.rx - rg.ry) / CLS, s = si, best = 1e9;
						int band = rg.from;
						for (int k = 0; k < rg.n; k++)
						{
							double da = std::remainder((u - (double)k * U / rg.n) * 2 * M_PI / U, 2 * M_PI);
							double d = std::hypot(st * da, s);
							if (d < best) { best = d; band = rg.from + k; }
						}
						size_t i = (size_t)(j * RROWS + c * SR + si) * U + u;
						put16(dx, i, band);
						dc[i] = (unsigned char)(std::min(15, (int)std::lround(best * 2)) * 16 + band);
					}
		}
		// each ring's table onto the cells (stacked 80x180): the nearest point of its track (its front half inside the
		// ball) as the angle's column, and its class and distance as the row; out of reach: none
		// (out of reach: a texel too far from any light, never remap's fill, which isn't 0)
		std::vector<unsigned char> rx((size_t)W * 3 * H * 2, 0), ry((size_t)W * 3 * H * 2, 0);
		for (size_t i = 0; i < (size_t)W * 3 * H; i++) put16(ry, i, SR - 1);
		const int SAMPLES = 4096;
		for (int j = 0; j < 3; j++)
		{
			const Ring& rg = RINGS[j];
			std::vector<double> ex(SAMPLES), ey(SAMPLES);
			for (int k = 0; k < SAMPLES; k++) ringAt(rg, k * 2 * M_PI / SAMPLES, ex[k], ey[k]);
			for (int Y = 0; Y < H; Y++)
				for (int X = 0; X < W; X++)
				{
					bool ball = inBall(X, Y);
					double best = 1e9;
					int bk = -1;
					for (int k = 0; k < SAMPLES; k++)
					{
						double a = k * 2 * M_PI / SAMPLES;
						if (ball && std::sin(a) <= 0) continue;
						double d = std::hypot(X - ex[k], Y - ey[k]);
						if (d < best) { best = d; bk = k; }
					}
					if (bk < 0 || best > REACH) continue;
					double a = bk * 2 * M_PI / SAMPLES, sp = std::hypot(rg.rx * std::sin(a), rg.ry * std::cos(a));
					int c = std::max(0, std::min(CLS - 1, (int)std::floor((sp - rg.ry) / (rg.rx - rg.ry) * CLS)));
					int si = std::min(SR - 1, (int)std::lround(best));
					size_t i = (size_t)(j * H + Y) * W + X;
					put16(rx, i, (int)std::lround(a / (2 * M_PI) * U) % U);
					put16(ry, i, c * SR + si);
				}
		}
		// the lights' sprites, one a band (7x7 cells), the front ones' row then the back ones': each cell the band
		// (remap) and band x8 + 4 if behind + how far out (0: the smallest disc's, 1: the middle one's, 2: the biggest's,
		// 3: none; a disc of radius r is the cells with dx^2 + dy^2 <= r^2 + 1, as the preview's)
		std::vector<unsigned char> sx((size_t)N * SP * 2 * SP * 2), sy((size_t)N * SP * 2 * SP * 2, 0), sc((size_t)N * SP * 2 * SP);
		for (int back = 0; back < 2; back++)
			for (int y = 0; y < SP; y++)
				for (int x = 0; x < N * SP; x++)
				{
					int b = x / SP, ddx = x % SP - SP / 2, ddy = y - SP / 2, d2 = ddx * ddx + ddy * ddy;
					int cls = d2 <= 2 ? 0 : d2 <= 5 ? 1 : d2 <= 10 ? 2 : 3;
					size_t i = (size_t)(back * SP + y) * N * SP + x;
					put16(sx, i, b);
					sc[i] = (unsigned char)(b * 8 + back * 4 + cls);
				}
		return writePgm(p[11], N * SP, 2 * SP, 65535, sx) && writePgm(p[12], N * SP, 2 * SP, 65535, sy) &&
			   writePgm(p[13], N * SP, 2 * SP, 255, sc) && writePgm(p[0], W, H, 65535, bx) && writePgm(p[1], W, H, 65535, by) && writePgm(p[2], W, H, 255, pix) &&
			   writePgm(p[3], FW, 2 * FR, 65535, fx) && writePgm(p[4], FW, 2 * FR, 65535, fy) && writePgm(p[5], FW, 2 * FR, 255, fc) &&
			   writePgm(p[6], U, 3 * RROWS, 65535, dx) && writePgm(p[7], U, 3 * RROWS, 65535, dy) &&
			   writePgm(p[8], U, 3 * RROWS, 255, dc) && writePgm(p[9], W, 3 * H, 65535, rx) && writePgm(p[10], W, 3 * H, 65535, ry);
	}

	static std::string num(double v)
	{
		char b[32];
		std::snprintf(b, sizeof b, "%.4g", v);
		return b;
	}

	// a plane (0 Y, 1 U, 2 V; BT.601 limited, as mpv reads a picture this size) of the colour hsl(H, S, L), H in degrees,
	// S and L 0..1, each an expression (the CSS formula: f(n) = L - S min(L, 1-L) max(-1, min(k-3, 9-k, 1)),
	// k = (n + H/30) mod 12, r g b = f(0) f(8) f(4)). Uses the expression's variables 0..7
	static std::string hslPlane(int plane, const std::string& h, const std::string& s, const std::string& l)
	{
		auto f = [](const char* n) {
			return std::string("(st(4,mod(") + n + "+ld(0)/30,12));ld(2)-ld(3)*max(-1,min(min(ld(4)-3,9-ld(4)),1)))";
		};
		std::string e = "(st(0," + h + ");st(1," + s + ");st(2," + l + ");st(3,ld(1)*min(ld(2),1-ld(2)));st(5," + f("0") +
						");st(6," + f("8") + ");st(7," + f("4") + ");";
		if (plane == 0) e += "16+65.481*ld(5)+128.553*ld(6)+24.966*ld(7))";
		else if (plane == 1) e += "128-37.797*ld(5)-74.203*ld(6)+112*ld(7))";
		else e += "128+112*ld(5)-93.786*ld(6)-18.214*ld(7))";
		return e;
	}

	// a plane of an RGB colour (0..255 each)
	static double rgbPlane(int plane, double r, double g, double b)
	{
		r /= 255; g /= 255; b /= 255;
		if (plane == 0) return 16 + 65.481 * r + 128.553 * g + 24.966 * b;
		if (plane == 1) return 128 - 37.797 * r - 74.203 * g + 112 * b;
		return 128 + 112 * r - 93.786 * g - 18.214 * b;
	}

	// a band's hue (b an expression 0..15)
	static std::string hueOf(const std::string& b)
	{
		std::string e = "(";
		for (int i = 0; i < N; i++) e += (i ? "+" : "") + std::to_string(HUES[i]) + "*eq(" + b + "," + std::to_string(i) + ")";
		return e + ")";
	}

	static std::string clip8(const std::string& e) { return "'clip(floor(" + e + "+0.5),0,255)'"; }

	// the ball and the room, a plane: x the ball's code (0 grout, 1..224 band x14 + level step + 1, 255 white), y the
	// cell's class. Over the art the room is 0 (background) or 1 (track), told apart later
	static std::string ballExpr(int plane, bool over)
	{
		const std::string grout = "(gte(y," + std::to_string(P_GROUT) + ")+eq(x,0))";
		const std::string light = "(if(gte(y," + std::to_string(P_GROUT) + "),y-" + std::to_string(P_GROUT) + ",y)/80)";
		const std::string v = "(mod(x-1,14)/13)";
		const std::string h = "if(" + grout + ",250," + hueOf("floor((x-1)/14)") + ")";
		const std::string s = "if(" + grout + ",0.3,if(eq(x,255),0,0.55+0.4*" + v + "))";
		const std::string l = "if(" + grout + ",0.08+0.1*" + light + ",if(eq(x,255),1,min(0.8,0.18+0.38*" + light + "+0.3*" + v + ")))";
		const double bg = rgbPlane(plane, 10, 6, 18), track = rgbPlane(plane, 23, 17.4, 36.2), str = rgbPlane(plane, 0x7a, 0x75, 0x90);
		return clip8("if(eq(y," + std::to_string(P_BG) + ")," + (over ? "0" : num(bg)) + ",if(eq(y," + std::to_string(P_TRACK) + ")," +
					 (over ? "1" : num(track)) + ",if(eq(y," + std::to_string(P_STRING) + ")," + num(str) + "," + hslPlane(plane, h, s, l) + ")))");
	}

	// the glow's strength from its code (16 x I + band, I 0..15)
	static std::string glowAlpha(const std::string& code) { return "floor(" + code + "/16)/15*0.55"; }
	static const char* NEUTRAL[3] = {"16", "128", "128"};

	// the discs over it, a plane: x what's under (the ball's plane), y the disc's code (0 none, else 1 + band x14 + 7 if
	// behind (those inside the ball already taken out) + its level 0..6). A disc's colour less the glow that's added over
	// it later (at its middle: so the disc ends up about its own colour). Over the art, under the room (0, 1): the
	// track's colour (the mask says how much), else anything (the art)
	static std::string discExpr(int plane, bool over)
	{
		const std::string band = "floor((y-1)/14)", hue = hueOf(band), dv = "(mod(y-1,7)/6)";
		const std::string disc = hslPlane(plane, hue, "0.6+0.4*" + dv, "0.25+0.55*" + dv) + "-(0.1+0.45*" + dv + ")*(" +
								 hslPlane(plane, hue, "1", "0.65") + "-" + NEUTRAL[plane] + ")";
		std::string under = "x";
		if (over) under = "if(lte(x,1)," + num(rgbPlane(plane, 140, 120, 200)) + ",x)";
		return clip8("if(eq(y,0)," + under + "," + disc + ")");
	}

	// the glow, a plane (a lut on the glow's code): its colour hsl(hue, 100%, 65%) times its strength, about the plane's
	// neutral value (16 or 128), added over the scene after it's smoothed
	static std::string glowExpr(int plane)
	{
		const std::string hue = hueOf("mod(val,16)");
		return clip8(std::string(NEUTRAL[plane]) + "+(" + glowAlpha("val") + ")*(" + hslPlane(plane, hue, "1", "0.65") + "-" +
					 NEUTRAL[plane] + ")");
	}

	// over the art, the mask (255 the art, 0 the scene): x the ball's luma (0 room, 1 track), y the disc's code
	static std::string maskExpr()
	{
		return clip8("if(gt(x,1),0,if(y,0,255*(1-0.18*eq(x,1))))");
	}

	std::string discoGraph(bool overCover, const std::string& dir)
	{
		auto n = [](int v) { return std::to_string(v); };
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + DISCO_FILES[k], s); return s; };
		const int TOP = 8;	// as Peaks: the highest bands (16-24 kHz) measured and cut off
		std::string g = "[aid1] asplit [ao] [a]; ";
		// the bands: measured as Peaks does, contrast, eased (up at once, down over ~0.15 s), 60 a second
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("dc") + ","
			 "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split=3 [dcl0] [dcl1] [dcl2]; "
			 // the flash value: 238 x (0.35 x the treble's mean + 0.05 on a bass over 0.7) / 0.4, after the 16 levels
			 "[dcl1] crop=5:1:11:0,scale=1:1:flags=area,setsar=1 [dctr]; [dcl2] crop=3:1:0:0,scale=1:1:flags=area,setsar=1 [dcbs]; "
			 "[dctr] [dcbs] lut2=c0='clip(floor((0.35*x/252+0.05*gt(y,176.4))/0.4*238+0.5),0,238)' [dcf]; "
			 "[dcl0] [dcf] hstack,split=3 [dcr0] [dcr1] [dcr2]; "
			 // the ball: the facet texture's codes, its two halves max'ed, turned by a crop, onto the ball's cells
			 "movie=" + q(3) + " [dcfx]; movie=" + q(4) + " [dcfy]; movie=" + q(5) + " [dcfc]; "
			 "[dcr0] [dcfx] [dcfy] remap=format=gray [dcft]; "
			 "[dcft] [dcfc] lut2=c0='if(lt(y,16),y*14+1+floor(13*x/252+0.5),if(eq(y,16),0,if(lt(y-17,x),255,0)))',split [dcf0] [dcf1]; "
			 "[dcf0] crop=" + n(FW) + ":" + n(FR) + ":0:0 [dcfa]; [dcf1] crop=" + n(FW) + ":" + n(FR) + ":0:" + n(FR) + " [dcfb]; "
			 "[dcfa] [dcfb] lut2=c0='max(x,y)',crop=w=80:h=" + n(FR) + ":x='mod(" + num(SPIN * FT / (2 * M_PI)) + "*t," + n(FT) + ")':y=0 [dcfs]; "
			 "movie=" + q(0) + " [dcbx]; movie=" + q(1) + " [dcby]; [dcfs] [dcbx] [dcby] remap=format=gray [dcball]; "
			 // the lights: the rings' textures' codes, doubled, each ring's part cropped by its turn, onto the cells
			 "movie=" + q(6) + " [dcdx]; movie=" + q(7) + " [dcdy]; movie=" + q(8) + " [dcdc]; "
			 "[dcr1] [dcdx] [dcdy] remap=format=gray [dcdt]; "
			 "[dcdt] [dcdc] lut2=c0='st(0,x/252);st(1,floor(y/16)/2);st(2,1+floor(2.2*ld(0)+0.5));"
			 "mod(y,16)+16*floor((0.1+0.45*ld(0))*max(0,1-ld(1)/(ld(2)+4))/0.55*15+0.5)',split [dcd0] [dcd1]; "
			 "[dcd0] [dcd1] hstack,split=3 [dcs0] [dcs1] [dcs2]; "
			 "movie=" + q(9) + ",split=3 [dcrx0] [dcrx1] [dcrx2]; movie=" + q(10) + ",split=3 [dcry0] [dcry1] [dcry2]; ";
		for (int j = 0; j < 3; j++)
		{
			const std::string J = n(j);
			g += "[dcs" + J + "] crop=w=" + n(U) + ":h=" + n(RROWS) + ":x='mod(" + num(-RINGS[j].speed * U / (2 * M_PI)) + "*t," + n(U) +
				 ")':y=" + n(j * RROWS) + " [dcc" + J + "]; "
				 "[dcrx" + J + "] crop=" + n(W) + ":" + n(H) + ":0:" + n(j * H) + " [dcmx" + J + "]; "
				 "[dcry" + J + "] crop=" + n(W) + ":" + n(H) + ":0:" + n(j * H) + " [dcmy" + J + "]; "
				 "[dcc" + J + "] [dcmx" + J + "] [dcmy" + J + "] remap=format=gray [dco" + J + "]; ";
		}
		// the discs: the 16 sprites made (remap from the levels, lut2: each cell the disc's code if the band's disc
		// reaches it), front and back, and each laid on the cells where its light is (overlay, its place from t; the
		// sprite's front or back row cropped by t too); those behind taken out where the ball or the string is
		g += "[dcb3] lut=c0=0,split=3 [dck0] [dck1] [dck2]; "
			 "[dck0] [dck1] [dck2] mergeplanes=0x001020:yuv444p [dcv0]; "
			 "movie=" + q(11) + " [dcsx]; movie=" + q(12) + " [dcsy]; movie=" + q(13) + " [dcsc]; "
			 "[dcr2] [dcsx] [dcsy] remap=format=gray [dcst]; "
			 "[dcst] [dcsc] lut2=c0='st(0,x/252);if(lte(mod(y,4),floor(2.2*ld(0)+0.5))*lt(mod(y,4),3),"
			 "1+floor(y/8)*14+7*mod(floor(y/4),2)+floor(6*ld(0)+0.5),0)',split [dcsa] [dcsb]; "
			 "[dcsb] lut=c0='255*gt(val,0)' [dcsal]; [dcsa] [dcsal] mergeplanes=0x00000010:yuva444p,split=" + n(N);
		for (int b = 0; b < N; b++) g += " [dcq" + n(b) + "]";
		g += "; ";
		for (int j = 0, b = 0; j < 3; j++)
			for (int k = 0; k < RINGS[j].n; k++, b++)
			{
				const Ring& rg = RINGS[j];
				const std::string A = "(" + num(rg.speed) + "*t+" + num(k * 2 * M_PI / rg.n) + ")";
				const std::string x = "floor(" + n(CX) + "+cos(" + A + ")*" + num(rg.rx * std::cos(rg.tilt)) + "-sin(" + A + ")*" +
									  num(rg.ry * std::sin(rg.tilt)) + "+0.5)-" + n(SP / 2);
				const std::string y = "floor(" + n(CY) + "+cos(" + A + ")*" + num(rg.rx * std::sin(rg.tilt)) + "+sin(" + A + ")*" +
									  num(rg.ry * std::cos(rg.tilt)) + "+0.5)-" + n(SP / 2);
				g += "[dcq" + n(b) + "] crop=w=" + n(SP) + ":h=" + n(SP) + ":x=" + n(b * SP) + ":y='" + n(SP) + "*lte(sin(" + A + "),0)' [dcq" +
					 n(b) + "c]; [dcv" + n(b) + "] [dcq" + n(b) + "c] overlay=x='" + x + "':y='" + y + "':format=yuv444 [dcv" + n(b + 1) + "]; ";
			}
		g += "movie=" + q(2) + ",split=4 [dcp0] [dcp1] [dcp2] [dcp3]; "
			 "[dcv" + n(N) + "] extractplanes=y [dcdisc0]; [dcdisc0] [dcp3] lut2=c0='if(gte(mod(x-1,14),7)*(lte(y,201)+eq(y," + n(P_STRING) + ")),0,x)',split=" +
			 std::string(overCover ? "4" : "3") + " [dcL0] [dcL1] [dcL2]" + (overCover ? " [dcL3]" : "") + "; "
			 "[dco0] [dco1] lut2=c0='max(x,y)' [dco01]; [dco01] [dco2] lut2=c0='max(x,y)' [dcLg]; "
			 // the colours: the ball and the room a plane, then the discs over them; the glows a plane apart
			 "[dcball] split=4 [dcb0] [dcb1] [dcb2] [dcb3]; "
			 "[dcLg] " + std::string(overCover ? "crop=80:50:0:6," : "") + "split=3 [dcg0] [dcg1] [dcg2]; ";
		for (int k = 0; k < 3; k++)
		{
			const std::string K = n(k);
			g += "[dcb" + K + "] [dcp" + K + "] lut2=c0=" + ballExpr(k, overCover) + (k == 0 && overCover ? ",split [dcB0] [dcBm]" : " [dcB" + K + "]") + "; "
				 "[dcB" + K + "] [dcL" + K + "] lut2=c0=" + discExpr(k, overCover) + " [dcP" + K + "]; "
				 "[dcg" + K + "] lut=c0=" + glowExpr(k) + " [dcG" + K + "]; ";
		}
		// the scene's planes and the glow's merged (4:4:4, the cells'), the glow smoothed (bilinear, x2) and added at 160
		// cells' width (each cell 2x2), then all of it scaled up to the picture (nearest: 4 pixels a step) in one go
		const std::string add = "c0='clip(x+y-16,0,255)':c1='clip(x+y-128,0,255)':c2='clip(x+y-128,0,255)'";
		if (!overCover)
			return g + "[dcP0] [dcP1] [dcP2] mergeplanes=0x001020:yuv444p,scale=160:120:flags=neighbor [dcS]; "
					   "[dcG0] [dcG1] [dcG2] mergeplanes=0x001020:yuv444p,scale=160:120:flags=bilinear [dcH]; "
					   "[dcS] [dcH] lut2=" + add + ",scale=640:480:flags=neighbor,format=yuv420p,setsar=1 [vo]";
		// over the art: the scene's rows 6..55 at 4 pixels a cell, the art kept where the mask says, the glows added over it
		const std::string rows = "crop=80:50:0:6,";
		g += "[dcBm] [dcL3] lut2=c0=" + maskExpr() + "," + rows + "split=3 [dcm0] [dcm1] [dcm2]; "
			 "[dcm0] [dcm1] [dcm2] mergeplanes=0x001020:yuv444p,scale=320:200:flags=neighbor,format=yuv420p [dcmask]; "
			 "[dcP0] " + rows + "null [dcY]; [dcP1] " + rows + "null [dcU]; [dcP2] " + rows + "null [dcV]; "
			 "[dcY] [dcU] [dcV] mergeplanes=0x001020:yuv444p,scale=320:200:flags=neighbor,format=yuv420p [dcscene]; "
			 "[dcG0] [dcG1] [dcG2] mergeplanes=0x001020:yuv444p,scale=160:100:flags=bilinear,scale=320:200:flags=neighbor,format=yuv420p [dcglow]; "
			 "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
			 "settb=AVTB,split=4 [dca0] [dca1] [dca2] [dca3]; [dca0] crop=640:280:0:0 [dctop]; [dca1] crop=160:200:0:280 [dcleft]; "
			 "[dca2] crop=320:200:160:280 [dcmid]; [dca3] crop=160:200:480:280 [dcright]; "
			 "[dcscene] [dcmid] [dcmask] maskedmerge [dcmerged]; "
			 "[dcmerged] [dcglow] lut2=" + add + " [dcstrip]; "
			 "[dcleft] [dcstrip] [dcright] hstack=3 [dcbottom]; [dctop] [dcbottom] vstack,setsar=1 [vo]";
		return g;
	}
}
