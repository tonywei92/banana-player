// Night city: a pixel-art skyline at night whose windows the music lights. Each building listens to one band of the
// spectrum by where it stands (the bass on the left, the treble on the right) and each of its windows has its own
// fixed threshold, so the louder the band, the more of that building's windows are lit.
// All the drawing is done once, into stills (writeCityMaps): the scene's Y/U/V planes, a "window map" (each window's
// threshold, 0 elsewhere) at half size, and a 1-row table naming each column's band. A frame then costs: the bands
// measured and eased (one value a building), remap to one row of 320 (each column its building's level), that row
// stretched to the half-size picture, one lut2 deciding which window pixels are lit, and a lut2 a plane choosing a lit
// colour or the scene's own pixel. The windows sit on the half-size grid (4x5 there, 8x10 at full size), so all of it
// runs at half size: the chroma as it is, the luma scaled x2 (nearest) under a still top (the sky above the highest
// window, the moon and its 1-pixel stars, at full size). A luma lut2 at 640x480 was 40% of the graph's time. Below that
// line the scene is drawn in 2x2 blocks, so the half-size luma loses nothing. YUV and gray only, no geq per frame.
// Over the art the same skyline, shorter, stands in the 640x200 strip under the art's top, the art its sky.
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
	static const char* CITY_FILES[8] = {"city4-yt.pgm", "city4-u.pgm", "city4-v.pgm", "city4-win.pgm", "city4-swin.pgm",
										"city4-band.pgm", "city4-zero.pgm", "city4-yb.pgm"};
	static const int W = 640, H = 480, HW = 320, HH = 240;	// full and half size
	static const int SH = 100;								// the strip's height at half size (200 at full)
	// the still top's height at half size: above it no window (the tallest front building's start under its roof)
	static const int SPLIT = 75;
	// the window map's values: over the art 0 is the art, 1 a back building, 2 a front one; a window its threshold
	// (WIN_LO..WIN_HI in the eased level's 0..252), the highest from ORANGE up lit orange. Alone, the walls and unlit
	// windows are in the scene, so its map has only the thresholds (0 elsewhere)
	static const int BACK = 1, FRONT = 2, WIN_LO = 3, ORANGE = 196;
	// the buildings with windows, left to right: each its own band
	static const int N = 11;

	struct Yuv { int y, u, v; };
	static Yuv yuv(int rgb)
	{
		// BT.601 limited range, as mpv reads a picture this size
		double r = ((rgb >> 16) & 255) / 255.0, g = ((rgb >> 8) & 255) / 255.0, b = (rgb & 255) / 255.0;
		return {(int)std::lround(16 + 65.481 * r + 128.553 * g + 24.966 * b),
				(int)std::lround(128 - 37.797 * r - 74.203 * g + 112.0 * b),
				(int)std::lround(128 + 112.0 * r - 93.786 * g - 18.214 * b)};
	}
	static const int SKY_TOP = 0x060b24, SKY_LOW = 0x1b2a5c, WALL = 0x0b0f22, BACK_WALL = 0x131b3e, UNLIT = 0x1a2142,
					 LIT = 0xffd23f, LIT_HOT = 0xffb347, MOON = 0xf3eecb, ROOF_LIGHT = 0xd9434a;

	// a small, fixed random sequence: the same city every time
	struct Rng
	{
		uint32_t s;
		uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
		int range(int lo, int hi) { return lo + (int)(next() % (uint32_t)(hi - lo + 1)); }
	};

	struct Building { int x, w, h; bool antenna; int step; };	// half-size units; h from the ground; step: a crown's inset

	// the skyline at half size: N front buildings 25-45 wide (50-90 at full size) with gaps of 0-3, filling the width,
	// their heights by tall (240 or 100 rows): 70-170 alone (140-340 at full size)
	static std::vector<Building> skyline(int tall, uint32_t seed)
	{
		Rng r{seed};
		std::vector<int> ws(N), gaps(N);
		int sum = 0;
		for (int i = 0; i < N; i++) { ws[i] = r.range(25, 45); gaps[i] = r.range(0, 3); sum += ws[i] + gaps[i]; }
		// fitted to the width: the spare (or the excess) spread over the buildings, none under 25 or over 45
		int spare = HW + 2 - sum;
		for (int k = 0; spare != 0 && k < 400; k++)
		{
			int i = k % N;
			if (spare > 0 && ws[i] < 45) { ws[i]++; spare--; }
			else if (spare < 0 && ws[i] > 25) { ws[i]--; spare++; }
		}
		std::vector<Building> b;
		int x = -1;
		for (int i = 0; i < N; i++)
		{
			int lo = tall * 70 / 240, hi = tall * 170 / 240;
			if (tall < 200) { lo = 42; hi = 96; }
			// tall and short by turns, more or less, so no two neighbours hide each other
			int h = (i % 2) ? r.range(lo, (lo + hi) / 2 + 10) : r.range((lo + hi) / 2 - 10, hi);
			b.push_back({x, ws[i], h, r.range(0, 3) == 0, r.range(0, 2) == 0 ? r.range(3, 6) : 0});
			x += ws[i] + gaps[i];
		}
		return b;
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

	// the windows of a building, at half size: 4x5 on a 7x9 pitch, centred across, from under the roof to over the
	// street. f(x, y) for each window's top left
	template <class F> static void windows(const Building& b, int ground, F f)
	{
		int cols = (b.w - 6 + 3) / 7, left = b.x + (b.w - (cols * 7 - 3)) / 2;
		int top = ground - b.h + 5 + b.step;
		for (int y = top; y + 5 <= ground - 4; y += 9)
			for (int c = 0; c < cols; c++) f(left + c * 7, y);
	}

	// the back layer: lower, wider blocks between and behind the front ones, no windows
	static std::vector<Building> backLayer(int tall, uint32_t seed)
	{
		Rng r{seed};
		std::vector<Building> b;
		for (int x = -8; x < HW; )
		{
			int w = r.range(18, 34);
			int h = tall < 200 ? r.range(28, 70) : r.range(tall * 95 / 240, tall * 185 / 240);
			b.push_back({x, w, h, false, 0});
			x += w + r.range(-4, 6);
		}
		return b;
	}

	bool writeCityMaps(const std::string& dir)
	{
		std::string p[8];
		for (int k = 0; k < 8; k++) p[k] = dir + "/" + CITY_FILES[k];
		const size_t sizes[8] = {pgmSize(W, SPLIT * 2, 1), pgmSize(HW, HH, 1), pgmSize(HW, HH, 1), pgmSize(HW, HH, 1),
								 pgmSize(HW, SH, 1), pgmSize(HW, 1, 2), pgmSize(HW, 1, 2), pgmSize(HW, HH - SPLIT, 1)};
		bool ready = true;
		for (int k = 0; k < 8 && ready; k++)
		{
			struct stat st;
			ready = stat(p[k].c_str(), &st) == 0 && (size_t)st.st_size == sizes[k];
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);

		// the scene in RGB at full size (made once), then turned to Y and to U/V averaged over each 2x2
		std::vector<int> rgb((size_t)W * H);
		auto lerp = [](int a, int b, double t) {
			int r = 0;
			for (int s = 0; s < 24; s += 8)
				r |= (int)std::lround(((a >> s) & 255) + (((b >> s) & 255) - ((a >> s) & 255)) * t) << s;
			return r;
		};
		auto fill = [&](int x0, int y0, int w, int h, int c) {
			for (int y = std::max(0, y0); y < std::min(H, y0 + h); y++)
				for (int x = std::max(0, x0); x < std::min(W, x0 + w); x++) rgb[(size_t)y * W + x] = c;
		};
		// the sky: darkest at the top, lightest at 400 (a hint of the city's glow), in bands of 4 rows (pixel art)
		for (int y = 0; y < H; y++)
		{
			double t = std::min(1.0, (y / 4 * 4) / 400.0);
			fill(0, y, W, 1, lerp(SKY_TOP, SKY_LOW, t * t * 0.4 + t * 0.6));
		}
		// the stars: dots in the upper sky, a few brighter crosses
		Rng r{0x5eed1};
		for (int i = 0; i < 70; i++)
		{
			int x = r.range(0, W - 1), y = r.range(0, 250);
			if (std::hypot(x - 560, y - 72) < 44) continue;	// not on the moon
			int c = lerp(SKY_LOW, 0xffffff, 0.35 + r.range(0, 60) / 100.0);
			// under the still top the scene is in 2x2 blocks: a star there is one, dimmer (nearer the city's glow)
			if (y >= SPLIT * 2) { fill(x & ~1, y & ~1, 2, 2, lerp(SKY_LOW, c, 0.6)); continue; }
			fill(x, y, 1, 1, c);
			if (i % 9 == 0)
			{
				int dim = lerp(SKY_TOP, c, 0.5);
				fill(x - 1, y, 1, 1, dim); fill(x + 1, y, 1, 1, dim); fill(x, y - 1, 1, 1, dim); fill(x, y + 1, 1, 1, dim);
			}
		}
		// the moon: a crescent (a disc less one moved up and right), with a faint halo
		for (int y = 20; y < 130; y++)
			for (int x = 500; x < 620; x++)
			{
				double d = std::hypot(x - 560 + 0.5, y - 72 + 0.5), cut = std::hypot(x - 572 + 0.5, y - 64 + 0.5);
				size_t i = (size_t)y * W + x;
				if (d < 26 && cut >= 23) rgb[i] = MOON;
				else if (d < 40 && d >= 26) rgb[i] = lerp(rgb[i], MOON, 0.06 * (40 - d) / 14);
			}

		// the buildings: the back layer, then the front with their windows (alone: 240 half rows, ground at the bottom)
		std::vector<unsigned char> win((size_t)HW * HH, 0), swin((size_t)HW * SH, 0), band((size_t)HW * 2, 0),
			zero((size_t)HW * 2, 0);
		Rng th{0xc17e5};
		for (int pass = 0; pass < 2; pass++)
		{
			// pass 0: alone (the scene and its map); pass 1: the strip's map (the same skyline, shorter)
			int tall = pass ? SH : HH;
			std::vector<unsigned char>& map = pass ? swin : win;
			for (const Building& b : backLayer(tall, 0xba5e))
			{
				if (pass == 0) fill(b.x * 2, H - b.h * 2, b.w * 2, b.h * 2, BACK_WALL);
				else
					for (int y = std::max(0, tall - b.h); y < tall; y++)
						for (int x = std::max(0, b.x); x < std::min(HW, b.x + b.w); x++) map[(size_t)y * HW + x] = BACK;
			}
			std::vector<Building> front = skyline(tall, 0x0c17);
			for (int i = 0; i < N; i++)
			{
				const Building& b = front[i];
				int top = tall - b.h;
				auto wall = [&](int x0, int y0, int w, int h) {
					if (pass == 0) fill(x0 * 2, y0 * 2, w * 2, h * 2, WALL);
					else
						for (int y = std::max(0, y0); y < std::min(tall, y0 + h); y++)
							for (int x = std::max(0, x0); x < std::min(HW, x0 + w); x++) map[(size_t)y * HW + x] = FRONT;
				};
				// a crown: the body inset from the top, a narrower block above it
				wall(b.x, top + b.step, b.w, b.h - b.step);
				if (b.step) wall(b.x + b.step, top, b.w - 2 * b.step, b.step);
				if (b.antenna)
				{
					int ax = b.x + b.w / 2;
					wall(ax, top - 9, 1, 9);
					if (pass == 0) fill(ax * 2, (top - 10) * 2, 2, 2, ROOF_LIGHT);
				}
				// the band of each column the building covers (later buildings over earlier ones where they touch)
				if (pass == 0)
					for (int x = std::max(0, b.x); x < std::min(HW, b.x + b.w); x++) band[(size_t)x * 2 + 1] = (unsigned char)i;
				windows(b, tall, [&](int wx, int wy) {
					// thresholds spread over the level's range: skewed high for the bass (always loud: its building
					// was nearly all lit), low for the treble, so each building's share lit moves with its band
					double u = (th.next() % 10000) / 10000.0;
					int t = WIN_LO + (int)std::lround(std::pow(u, 0.7 + 0.05 * i) * (238 - WIN_LO));
					for (int y = wy; y < wy + 5; y++)
						for (int x = wx; x < wx + 4; x++)
						{
							if (x < 0 || x >= HW || y < 0 || y >= tall) continue;
							map[(size_t)y * HW + x] = (unsigned char)t;
							if (pass == 0) fill(x * 2, y * 2, 2, 2, UNLIT);
						}
				});
			}
		}

		// the luma: full size above SPLIT, half size (each 2x2 block's top left) below
		std::vector<unsigned char> Y((size_t)W * SPLIT * 2), Yb((size_t)HW * (HH - SPLIT)), U((size_t)HW * HH), V((size_t)HW * HH);
		for (int y = 0; y < SPLIT * 2; y++)
			for (int x = 0; x < W; x++) Y[(size_t)y * W + x] = (unsigned char)yuv(rgb[(size_t)y * W + x]).y;
		for (int y = 0; y < HH - SPLIT; y++)
			for (int x = 0; x < HW; x++) Yb[(size_t)y * HW + x] = (unsigned char)yuv(rgb[(size_t)(SPLIT + y) * 2 * W + x * 2]).y;
		for (int y = 0; y < HH; y++)
			for (int x = 0; x < HW; x++)
			{
				int su = 0, sv = 0;
				for (int k = 0; k < 4; k++)
				{
					Yuv c = yuv(rgb[(size_t)(y * 2 + k / 2) * W + x * 2 + k % 2]);
					su += c.u; sv += c.v;
				}
				U[(size_t)y * HW + x] = (unsigned char)((su + 2) / 4);
				V[(size_t)y * HW + x] = (unsigned char)((sv + 2) / 4);
			}
		return writePgm(p[0], W, SPLIT * 2, 255, Y) && writePgm(p[7], HW, HH - SPLIT, 255, Yb) && writePgm(p[1], HW, HH, 255, U) && writePgm(p[2], HW, HH, 255, V) &&
			   writePgm(p[3], HW, HH, 255, win) && writePgm(p[4], HW, SH, 255, swin) && writePgm(p[5], HW, 1, 65535, band) &&
			   writePgm(p[6], HW, 1, 65535, zero);
	}

	std::string cityGraph(bool overCover, const std::string& dir)
	{
		auto n = [](int v) { return std::to_string(v); };
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + CITY_FILES[k], s); return s; };
		const int TOP = 8;	// as Peaks: the highest bands (16-24 kHz) measured and cut off
		const int rows = overCover ? SH : HH, mid = rows / 2 - 1;
		const Yuv lit = yuv(LIT), hot = yuv(LIT_HOT), wall = yuv(WALL), back = yuv(BACK_WALL), unlit = yuv(UNLIT);
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [ctart1] [ctart2]; [ctart1] crop=640:280:0:0 [top]; "
				 "[ctart2] crop=640:200:0:280,extractplanes=y+u+v [ctay] [ctau] [ctav]; ";
		// the bands: measured as Peaks does (one value each, 0..252), then eased as Pocket console's bars: up at once,
		// down over ~0.15 s for the whole range (an exp/log lagfun, N values a frame)
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("ct") + ","
			 "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray [ctl]; "
			 // each column its building's level (one row: remap with the band table), stretched down the picture
			 "movie=" + q(5) + " [ctbx]; movie=" + q(6) + " [ctby]; movie=" + q(overCover ? 4 : 3) + " [ctm]; "
			 "[ctl] [ctbx] [ctby] remap=format=gray,pad=" + n(HW) + ":" + n(rows) + ":0:" + n(mid) + ",fillborders=top=" + n(mid) +
			 ":bottom=" + n(rows - 1 - mid) + ":mode=smear,setsar=1 [ctL]; ";
		if (overCover)
		{
			// each pixel: 0 the art, 1 a back wall, 2 a front wall, 3 an unlit window, 4 lit, 5 lit orange; then each
			// plane the art's where 0, else the colour's
			auto paint = [&](int Yuv::*c) {
				return "'if(eq(x,0),y,if(eq(x,1)," + n(back.*c) + ",if(eq(x,2)," + n(wall.*c) + ",if(eq(x,3)," + n(unlit.*c) +
					   ",if(eq(x,4)," + n(lit.*c) + "," + n(hot.*c) + ")))))'";
			};
			return g + "[ctL] [ctm] lut2=c0='if(lt(y," + n(WIN_LO) + "),y,if(gte(x,y),if(gte(y," + n(ORANGE) + "),5,4),3))',split=3 [ctc0] [ctc1] [ctc2]; "
					   "[ctc0] scale=640:200:flags=neighbor [ctcy]; "
					   "[ctcy] [ctay] lut2=c0=" + paint(&Yuv::y) + " [ctY]; "
					   "[ctc1] [ctau] lut2=c0=" + paint(&Yuv::u) + " [ctU]; "
					   "[ctc2] [ctav] lut2=c0=" + paint(&Yuv::v) + " [ctV]; "
					   "[ctY] [ctU] [ctV] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		}
		// alone: 0 the scene (walls, unlit windows and all), 1 lit, 2 lit orange
		auto paint = [&](int Yuv::*c) { return "'if(eq(x,0),y,if(eq(x,1)," + n(lit.*c) + "," + n(hot.*c) + "))'"; };
		return g + "[ctL] [ctm] lut2=c0='if(gte(x,y)*gt(y,0),if(gte(y," + n(ORANGE) + "),2,1),0)',split=3 [ctc0] [ctc1] [ctc2]; "
				   "movie=" + q(0) + " [ctst]; movie=" + q(7) + " [ctsy]; movie=" + q(1) + " [ctsu]; movie=" + q(2) + " [ctsv]; "
				   // the luma: under the still top, at half size and scaled up (vstack repeats the top's one frame)
				   "[ctc0] crop=" + n(HW) + ":" + n(HH - SPLIT) + ":0:" + n(SPLIT) + " [ctcb]; "
				   "[ctcb] [ctsy] lut2=c0=" + paint(&Yuv::y) + ",scale=640:" + n(H - SPLIT * 2) + ":flags=neighbor [ctyb]; "
				   "[ctst] [ctyb] vstack [ctY]; "
				   "[ctc1] [ctsu] lut2=c0=" + paint(&Yuv::u) + " [ctU]; "
				   "[ctc2] [ctsv] lut2=c0=" + paint(&Yuv::v) + " [ctV]; "
				   "[ctY] [ctU] [ctV] mergeplanes=0x001020:yuv420p [vo]";
	}
}
