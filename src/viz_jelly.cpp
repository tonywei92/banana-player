// Jellyfish: the deep sea at night, a dark blue fading to near-black at the bottom, faint specks of light drifting
// slowly up; four pixel jellyfish (8-pixel pixels) at fixed spots, each listening to a slice of the spectrum (bands 0-2
// magenta, 3-6 cyan, 7-10 violet, 11-15 teal): a dome bell with a brighter rim and six rippling tentacles under it.
// As its slice plays a jellyfish glows (a soft halo round it, its colours brighter) and its tentacles grow longer;
// they bob, sway and ripple briskly.
//
// How a frame is made, at 160x120 (4-pixel units; a jellyfish pixel is 2x2 of them) and then scaled up (nearest):
// - the 16 bands measured (showfreqs at 60/s), vizContrast, eased (the exp/log lagfun: up at once, down in ~0.15 s),
//   averaged per jellyfish (four crops, each to one value) into a row of 4;
// - the screen is cut into four column strips, one jellyfish each; each strip filled with its level (a 1x1 crop
//   scaled to the strip): the level map;
// - the motion: each jellyfish is a still "sheet" (writeJellyMaps) of class codes (0 nothing, the bell, its rim, a
//   tentacle pixel's distance from the bell), drawn 16 times stacked, one per phase of the tentacles' ripple. An
//   overlay of the sheet on its strip's level frame (a frame each 1/60 s: a still alone gives one frame) at an offset
//   from the frame number picks the phase and the bob and sway: a strip-sized copy, no more. The strips side by side
//   are the class map;
// - a lut2 of the class map and the level map: each pixel's code, a body pixel's lightness by its part and the level
//   (a tentacle further out than the level reaches is nothing); a lut2 of a still halo (an ellipse's falloff round each
//   jellyfish, with its index) and the level map: the halo's strength; a lut2 keeps the body over the halo;
// - the background: the specks (a still, two screens of them, overlaid at a rising offset on a level frame) and a still
//   "row" picture, one lut2: the row and a speck flag;
// - a lut2 a plane turns code and background into Y, U and V (the colours worked out per jellyfish: the body an HSL
//   colour of its hue by the lightness, the halo the sea blended towards a light tint of it), the luma scaled x4, the
//   chroma x2 (nearest), merged. YUV and gray only: no geq, no blur, nothing converted per frame but the small
//   strips (gray to yuv444p for overlay, a few thousand pixels).
// Over the art: its top 640x280 as it is, and in the 640x200 strip under it (160x50) the four jellyfish smaller (a
// pixel one unit), in a row, their halos blended over the art, the art itself everywhere else.
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
	namespace
	{
		// a new name whenever the stills change: they're cached by size
		const char* JELLY_FILES[12] = {"jelly2-s0.pgm", "jelly2-s1.pgm", "jelly2-s2.pgm", "jelly2-s3.pgm", "jelly2-o0.pgm",
									   "jelly2-o1.pgm", "jelly2-o2.pgm", "jelly2-o3.pgm", "jelly2-halo.pgm", "jelly2-ohalo.pgm",
									   "jelly2-speck.pgm", "jelly2-row.pgm"};
		const int W = 160, H = 120, SH = 50;	// the drawing alone, and the strip's height over the art (x4 on screen)
		const int NPH = 16;						// the tentacles' ripple: phases drawn
		const double RIPPLE = 6.0;				// its speed, radians a second (twice the preview's: the owner wanted more shake)
		const int SPEED = 2;					// the specks' rise, units a second

		// a layout: the strips' left edges (and the right edge), each jellyfish's bell centre (the unit left of the
		// middle when a pixel is 2), a pixel's size, the sway and bob (units), the halo's radii
		struct Layout
		{
			int x0[5];
			int cx[4], cy[4];
			int px, sway, bob;
			int rx[4], ry;
		};
		// alone: as the preview's grid (80x60): bells at 14,22 33,14 52,26 68,16; strips split between them (even widths)
		const Layout ALONE = {{0, 48, 86, 120, 160}, {28, 66, 104, 136}, {44, 28, 52, 32}, 2, 6, 7, {20, 18, 16, 16}, 22};
		// over the art: smaller and in a row
		const Layout OVER = {{0, 40, 80, 120, 160}, {20, 60, 100, 140}, {20, 17, 21, 18}, 1, 4, 4, {18, 18, 18, 18}, 18};
		// the sheets' phase hops between (the bob and sway's phases differ per jellyfish, as the preview's j.x and j.y)
		const double BOBPH[4] = {14, 33, 52, 68}, SWAYPH[4] = {22, 14, 26, 16};
		// the hues (HSL degrees)
		const double HUE[4] = {300, 190, 280, 170};
		// a class code's parts: 1 the bell, 2 its rim, k+1 a tentacle pixel k pixels below the bell's centre (k 2..15)
		const int BODY = 1, RIM = 2;

		std::string n(int v) { return std::to_string(v); }
		std::string f(double v) { char b[32]; std::snprintf(b, sizeof b, "%.4f", v); return b; }

		bool writePgm(const std::string& path, int w, int h, const std::vector<unsigned char>& data)
		{
			std::string tmp = path + ".tmp";
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			out << "P5\n" << w << " " << h << "\n255\n";
			out.write((const char*)data.data(), (std::streamsize)data.size());
			if (!out.flush()) return false;
			out.close();
			return rename(tmp.c_str(), path.c_str()) == 0;
		}

		size_t pgmSize(int w, int h) { return ("P5\n" + n(w) + " " + n(h) + "\n255\n").size() + (size_t)w * h; }

		int stripW(const Layout& L, int j) { return L.x0[j + 1] - L.x0[j]; }
		int sheetW(const Layout& L, int j) { return stripW(L, j) + 2 * L.sway; }
		int cellH(const Layout& L, int rows) { return rows + 2 * L.bob; }

		// a jellyfish's sheet: NPH cells stacked, each the strip and the sway and bob's margins round it
		std::vector<unsigned char> sheet(const Layout& L, int j, int rows)
		{
			const int sw = sheetW(L, j), ch = cellH(L, rows), p = L.px;
			std::vector<unsigned char> s((size_t)sw * ch * NPH, 0);
			const int cx = L.cx[j] - L.x0[j] + L.sway, cy = L.cy[j] + L.bob;
			for (int ph = 0; ph < NPH; ph++)
			{
				auto put = [&](int gx, int gy, int part, bool over) {
					for (int dy = 0; dy < p; dy++)
						for (int dx = 0; dx < p; dx++)
						{
							int x = cx + gx * p + dx, y = cy + gy * p + dy;
							if (x < 0 || x >= sw || y < 0 || y >= ch) continue;
							unsigned char& c = s[((size_t)ph * ch + y) * sw + x];
							int code = j * 64 + part;
							// a nearer tentacle pixel wins where two cross
							if (over || c == 0 || (c % 64) > part) c = (unsigned char)code;
						}
				};
				// the tentacles: six, from under the bell, each pixel swung by a sine travelling down it (as the preview)
				const double phi = 2 * M_PI * ph / NPH;
				for (int t = -5; t <= 5; t += 2)
					for (int k = 2; k <= 15; k++) put(t + (int)std::lround(std::sin(phi + k * 0.6 + t) * k / 4), k, k + 1, false);
				// the bell: a dome (x^2 + (1.4y)^2 <= 36, rows -4..1), its rim the pixels with the sea beside or above them
				auto in = [](int x, int y) { return y <= 1 && x * x + 1.96 * y * y <= 36; };
				for (int gy = -6; gy <= 1; gy++)
					for (int gx = -6; gx <= 6; gx++)
						if (in(gx, gy)) put(gx, gy, (!in(gx - 1, gy) || !in(gx + 1, gy) || !in(gx, gy - 1)) ? RIM : BODY, true);
			}
			return s;
		}

		// the halos: each strip its jellyfish's index (x64) and an ellipse's falloff (0..63) round it, still
		std::vector<unsigned char> halo(const Layout& L, int rows)
		{
			std::vector<unsigned char> h((size_t)W * rows, 0);
			for (int j = 0; j < 4; j++)
			{
				const double hx = L.cx[j] + L.px / 2.0, hy = L.cy[j] + 1.5 * L.px;
				for (int y = 0; y < rows; y++)
					for (int x = L.x0[j]; x < L.x0[j + 1]; x++)
					{
						double d = std::hypot((x + 0.5 - hx) / L.rx[j], (y + 0.5 - hy) / L.ry);
						int s = (int)std::lround(63 * std::max(0.0, 1 - d));
						h[(size_t)y * W + x] = (unsigned char)(s ? j * 64 + s : 0);
					}
			}
			return h;
		}

		struct Rgb { double r, g, b; };	// 0..1
		// a hue's fully saturated colour (HSL at S 1, L 0.5)
		Rgb pure(double h)
		{
			auto c = [&](double n0) {
				double k = std::fmod(n0 + h / 30, 12);
				return 0.5 - 0.5 * std::max(-1.0, std::min({k - 3, 9 - k, 1.0}));
			};
			return {c(0), c(8), c(4)};
		}
		// BT.601 limited range, as mpv reads a picture this size: a plane's offset and its weights for R, G and B (0..1)
		struct Plane { double off, kr, kg, kb; };
		const Plane PY = {16, 65.481, 128.553, 24.966}, PU = {128, -37.797, -74.203, 112}, PV = {128, 112, -93.786, -18.214};
		double planeOf(const Plane& p, const Rgb& c) { return p.off + p.kr * c.r + p.kg * c.g + p.kb * c.b; }
		// the sea's top and bottom, a speck's colour (sRGB)
		const Rgb SEA_TOP = {4 / 255.0, 18 / 255.0, 42 / 255.0}, SEA_LOW = {1 / 255.0, 4 / 255.0, 9 / 255.0},
				  SPECK = {140 / 255.0, 200 / 255.0, 255 / 255.0};

		// one of four values by the jellyfish's index (in ld(0))
		std::string sel(const double v[4])
		{
			return "if(eq(ld(0),0)," + f(v[0]) + ",if(eq(ld(0),1)," + f(v[1]) + ",if(eq(ld(0),2)," + f(v[2]) + "," + f(v[3]) + ")))";
		}

		// a plane's lut2 of the code (x) and the background (y: alone the row, +128 a speck; over the art its plane):
		// a body code (128 + 32 index + lightness 1..31) its HSL colour (S 0.9); else the background blended towards
		// the halo's tint (HSL L 0.7) by its strength (32 index + 0..31, at most 0.6)
		std::string paint(const Plane& p, bool overCover)
		{
			double P[4], T[4];
			const double K = p.kr + p.kg + p.kb;
			for (int j = 0; j < 4; j++)
			{
				Rgb c = pure(HUE[j]);
				P[j] = p.kr * c.r + p.kg * c.g + p.kb * c.b - 0.5 * K;	// the hue's chroma, in the plane
				T[j] = planeOf(p, {0.7 + 0.6 * (c.r - 0.5), 0.7 + 0.6 * (c.g - 0.5), 0.7 + 0.6 * (c.b - 0.5)});
			}
			std::string bg;
			if (overCover) bg = "st(3,y);";
			else
			{
				const double top = planeOf(p, SEA_TOP), low = planeOf(p, SEA_LOW), sp = planeOf(p, SPECK);
				bg = "st(3," + f(top) + "+" + f(low - top) + "*mod(y,128)/" + n(H - 1) + ");st(3,ld(3)+0.35*gte(y,128)*(" + f(sp) +
					 "-ld(3)));";
			}
			return "'st(0,mod(floor(x/32),4));st(1,mod(x,32));if(gte(x,128),st(2,ld(1)/32);" + f(p.off) + "+" + f(K) +
				   "*ld(2)+0.9*(1-abs(2*ld(2)-1))*" + sel(P) + "," + bg + "ld(3)+0.6*ld(1)/31*(" + sel(T) + "-ld(3)))'";
		}
	}

	bool writeJellyMaps(const std::string& dir)
	{
		std::string p[12];
		size_t sizes[12];
		for (int j = 0; j < 4; j++)
		{
			sizes[j] = pgmSize(sheetW(ALONE, j), cellH(ALONE, H) * NPH);
			sizes[4 + j] = pgmSize(sheetW(OVER, j), cellH(OVER, SH) * NPH);
		}
		sizes[8] = pgmSize(W, H); sizes[9] = pgmSize(W, SH); sizes[10] = pgmSize(W, 2 * H); sizes[11] = pgmSize(W, H);
		bool ready = true;
		for (int k = 0; k < 12; k++)
		{
			p[k] = dir + "/" + JELLY_FILES[k];
			struct stat st;
			ready = ready && stat(p[k].c_str(), &st) == 0 && (size_t)st.st_size == sizes[k];
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);

		bool ok = true;
		for (int j = 0; j < 4; j++)
		{
			ok = writePgm(p[j], sheetW(ALONE, j), cellH(ALONE, H) * NPH, sheet(ALONE, j, H)) && ok;
			ok = writePgm(p[4 + j], sheetW(OVER, j), cellH(OVER, SH) * NPH, sheet(OVER, j, SH)) && ok;
		}
		// the specks: 40 jellyfish-sized pixels on the 80x60 grid, the screen twice (one under the other) so a window at
		// any offset of one screen wraps
		std::vector<unsigned char> speck((size_t)W * 2 * H, 0), row((size_t)W * H);
		uint32_t s = 0x7e11f;
		auto rnd = [&]() { s = s * 1664525u + 1013904223u; return s >> 8; };
		for (int i = 0; i < 40; i++)
		{
			int gx = (int)(rnd() % 80), gy = (int)(rnd() % 60);
			for (int c = 0; c < 2; c++)
				for (int dy = 0; dy < 2; dy++)
					for (int dx = 0; dx < 2; dx++) speck[(size_t)(c * H + gy * 2 + dy) * W + gx * 2 + dx] = 255;
		}
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++) row[(size_t)y * W + x] = (unsigned char)y;
		return writePgm(p[8], W, H, halo(ALONE, H)) && writePgm(p[9], W, SH, halo(OVER, SH)) && writePgm(p[10], W, 2 * H, speck) &&
			   writePgm(p[11], W, H, row) && ok;
	}

	std::string jellyGraph(bool overCover, const std::string& dir)
	{
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + JELLY_FILES[k], s); return s; };
		const Layout& L = overCover ? OVER : ALONE;
		const int rows = overCover ? SH : H, N = 16, TOP = 8;	// as Peaks: the highest bands (16-24 kHz) measured and cut off
		const int ch = cellH(L, rows);
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [jlart1] [jlart2]; [jlart1] crop=640:280:0:0 [top]; "
				 "[jlart2] crop=640:200:0:280,extractplanes=y+u+v [jlay] [jlau] [jlav]; ";
		// the bands measured and eased (as Pocket console's bars), then each jellyfish its slice's mean
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("jl") + ","
			 "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split=4 [jlb0] [jlb1] [jlb2] [jlb3]; ";
		const int B0[4] = {0, 3, 7, 11}, B1[4] = {2, 6, 10, 15};
		for (int j = 0; j < 4; j++)
			g += "[jlb" + n(j) + "] crop=" + n(B1[j] - B0[j] + 1) + ":1:" + n(B0[j]) + ":0,scale=1:1:flags=area [jlv" + n(j) + "]; ";
		g += "[jlv0] [jlv1] [jlv2] [jlv3] hstack=4,split=4 [jll0] [jll1] [jll2] [jll3]; ";
		// each strip: its level, and its jellyfish (the sheet overlaid at the frame's phase, bob and sway)
		const std::string ph = "mod(floor(n*" + f(RIPPLE * NPH / (2 * M_PI * 60)) + ")," + n(NPH) + ")";
		for (int j = 0; j < 4; j++)
		{
			const std::string J = n(j);
			g += "[jll" + J + "] crop=1:1:" + J + ":0,scale=" + n(stripW(L, j)) + ":" + n(rows) + ":flags=neighbor,setsar=1,split [jlL" + J + "] [jlm" + J + "]; "
				 "movie=" + q((overCover ? 4 : 0) + j) + ",mergeplanes=0x000000:yuv444p [jlsh" + J + "]; "
				 "[jlm" + J + "] mergeplanes=0x000000:yuv444p [jlmm" + J + "]; "
				 "[jlmm" + J + "] [jlsh" + J + "] overlay=format=yuv444:eof_action=repeat:"
				 "x='round(" + n(L.sway) + "*sin(0.9*n/60+" + f(SWAYPH[j]) + "))-" + n(L.sway) + "':"
				 "y='round(" + n(L.bob) + "*sin(2.0*n/60+" + f(BOBPH[j]) + "))-" + n(L.bob) + "-" + n(ch) + "*" + ph + "',"
				 "extractplanes=y [jlcl" + J + "]; ";
		}
		// the codes: the body over the halo
		g += std::string("[jlL0] [jlL1] [jlL2] [jlL3] hstack=4,split=") + (overCover ? "2" : "3 [jlsm]") + " [jlLa] [jlLb]; [jlcl0] [jlcl1] [jlcl2] [jlcl3] hstack=4 [jlC]; "
			 "[jlC] [jlLa] lut2=c0='st(0,mod(x,64));st(1,floor(x/64));st(2,0.25+0.55*y/252);"
			 "if(eq(ld(0),0)+gte(ld(0),3)*gte(ld(0)-1,10+6*y/252),0,128+32*ld(1)+clip(round(32*if(eq(ld(0)," + n(BODY) +
			 "),0.85*ld(2)+0.03,if(eq(ld(0)," + n(RIM) + "),ld(2)+0.15,(1-(ld(0)-1)/18)*ld(2)+(ld(0)-1)/18*0.05))),1,31))' [jlK]; ";
		g += "movie=" + q(overCover ? 9 : 8) + " [jlhs]; "
			 "[jlLb] [jlhs] lut2=c0='if(eq(mod(y,64),0),0,32*floor(y/64)+clip(round(31*mod(y,64)/63*(0.25+0.75*x/252)),0,31))' [jlG]; "
			 "[jlK] [jlG] lut2=c0='if(gte(x,128),x,y)',split=3 [jlM0] [jlM1] [jlM2]; ";
		if (overCover)
			return g + "[jlM0] scale=640:200:flags=neighbor [jlM0s]; [jlM0s] [jlay] lut2=c0=" + paint(PY, true) + " [jlY]; "
					   "[jlM1] scale=320:100:flags=neighbor [jlM1s]; [jlM1s] [jlau] lut2=c0=" + paint(PU, true) + " [jlU]; "
					   "[jlM2] scale=320:100:flags=neighbor [jlM2s]; [jlM2s] [jlav] lut2=c0=" + paint(PV, true) + " [jlV]; "
					   "[jlY] [jlU] [jlV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		// the sea: the row and the specks (rising one screen in H/SPEED seconds, wrapped)
		return g + "movie=" + q(10) + ",mergeplanes=0x000000:yuv444p [jlsp]; movie=" + q(11) + " [jlrow]; "
				   "[jlsm] mergeplanes=0x000000:yuv444p [jlsmm]; "
				   "[jlsmm] [jlsp] overlay=format=yuv444:eof_action=repeat:x=0:y='-mod(floor(n*" + n(SPEED) + "/60)," + n(H) + ")',"
				   "extractplanes=y [jlspk]; "
				   "[jlspk] [jlrow] lut2=c0='y+128*gt(x,127)',split=3 [jlB0] [jlB1] [jlB2]; "
				   "[jlM0] [jlB0] lut2=c0=" + paint(PY, false) + ",scale=640:480:flags=neighbor [jlY]; "
				   "[jlM1] [jlB1] lut2=c0=" + paint(PU, false) + ",scale=320:240:flags=neighbor [jlU]; "
				   "[jlM2] [jlB2] lut2=c0=" + paint(PV, false) + ",scale=320:240:flags=neighbor [jlV]; "
				   "[jlY] [jlU] [jlV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
