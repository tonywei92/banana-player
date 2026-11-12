// Vibing cat: pixel art on an 80x60 grid of 8-pixel dots. A dark purple room; a window (a wooden frame with a cross
// bar) onto a navy night sky with a crescent moon and a few stars; a wooden sill with a little potted plant; on the sill
// an orange cat sitting (a darker outline, a white chest) in red headphones (a band over its head, a pad each side).
// Its head bobs down 0-2 rows with the bass (bands 0-2); its left ear twitches up when the treble (11-15) is high; its
// eyes are two dots, or happy closed arcs (^ ^) while the music is loud (the mean of all 16 past half); a pink nose and
// a small mouth. Its tail swishes beside it with the mids (4-9), faster and wider the louder; golden notes float up
// beside it on the treble, each from its own threshold, fading as they rise.
// A frame: the 16 bands measured, contrasted and eased (as Pocket console's bars), and from them four values: bass,
// mids, treble and the mean. Everything is drawn at the grid's size as a picture of classes (0 the scene below), then
// painted at the end:
//  - the head: the bass, the treble and the mean as one state (bob x 4 + ear x 2 + eyes); one lut2 against a still of
//    the dots' rows gives each dot its variant and its row in the head (255 outside it), another against a still of
//    the columns its class: each variant's row is one number (3 bits a column), drawn in C++ from the preview;
//  - the tail: an atlas of its frames (four widths, each swishing at its own speed, over a 4 s loop: 2 bits a width a
//    dot); the mids against it pick the width, a crop by the frame number the moment, padded to its place;
//  - the notes: an atlas of their 2 s rise (each dot: which note, how faded), gated by the treble, cropped by the
//    frame number;
//  - laid over the scene's still (each layer's 0 see-through), a lut per plane turns a class into its luma, U and V
//    (a note blended with the sky by its fade), the luma scaled x8 and the chroma x4 (nearest).
// Stills are one frame each (movie=), never looped. YUV and gray only, nothing converted, no geq per frame.
// Over the art: the art's top as it is, and in the 640x200 strip under it the same at 4-pixel dots (160x50): a sill
// across, the plant and the cat on it with its notes; the art shows through everywhere else (a note fades into it).
#include "viz_more.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	namespace
	{
		// a new name whenever the stills change: they're cached by size
		const char* const FILES[8] = {"cat1-scene-a.pgm", "cat1-row-a.pgm", "cat1-col-a.pgm", "cat1-scene-s.pgm",
									  "cat1-row-s.pgm",	  "cat1-col-s.pgm", "cat1-tail.pgm",  "cat1-notes.pgm"};

		// the drawing's grid: alone 80x60 dots of 8 pixels; over the art 160x50 of 4 (640x200), the scene moved by
		// (dx, dy) so the sill sits near the strip's bottom and the cat in its middle
		struct Layout
		{
			int W, H, P, dx, dy;
			bool room;	// the room and the window (alone), or the art (over it)
		};
		const Layout ALONE = {80, 60, 8, 0, 0, true};
		const Layout STRIP = {160, 50, 4, 50, 6, false};
		const double PI = 3.14159265358979323846;
		const int OFF = 64;	// the row and column stills: 64 + the dot's row (column) in the scene's own coordinates

		// the classes: 0 the scene (alone) or the art; the scene's parts; the cat's (the head's codes 1-6 follow
		// C_CAT in order); a note at four strengths
		enum
		{
			C_NONE,
			C_ROOM,
			C_FRAME,
			C_SKY,
			C_MOON,
			C_STAR,
			C_SILL,
			C_SILL2,
			C_LEAF,
			C_POT,
			C_WHITE,
			C_CAT,	// head code 1
			C_DARK,	// 2
			C_RED,	// 3
			C_RED2,	// 4
			C_EYE,	// 5
			C_NOSE,	// 6
			C_NOTE,	// + 0..3 (faint to full)
			C_COUNT = C_NOTE + 4
		};
		const unsigned RGB[C_COUNT] = {0x1b1430, 0x1b1430, 0x6a4a3a, 0x1f2a5a, 0xfff3c4, 0x8a96d0, 0x8a5a3a,
									   0x6a4228, 0x3fbf6a, 0xc0603a, 0xfff2e0, 0xf2a25a, 0xc87a38, 0xe8463c,
									   0xb02e26, 0x2a1a10, 0xff8fa8, 0xffd66e, 0xffd66e, 0xffd66e, 0xffd66e};
		const double NOTE_ALPHA[4] = {0.3, 0.55, 0.8, 1.0};

		// the levels' thresholds (0..252, after the contrast and easing; the preview's fractions x 252): the head down
		// a row from B1, two from B2; the ear up past EAR; happy eyes past LOUD; the tail's width steps; note i from
		// NOTE0 + NOTE1 x i
		const int B1 = 58, B2 = 172, EAR = 139, LOUD = 126, M1 = 63, M2 = 126, M3 = 189, NOTE0 = 50, NOTE1 = 30;

		// the head: rows from hy-3 (the band) to hy+8 (its chin), hy = HY0 + 3 + bob; columns from the cat's middle -7
		// to +7
		const int CX = 30, HY0 = 16, HR = 12, HC = 15;
		// the tail's box (the scene's dots), its loop (phases, a phase every TSTEP frames)
		const int TX = 37, TY = 29, TW = 9, TH = 12, TPH = 60, TSTEP = 4;
		// the notes' box, their loop (a phase every NSTEP frames: 2 s)
		const int NX = 39, NY = 6, NW = 19, NH = 22, NPH = 60, NSTEP = 2;

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

		// the preview's noise (its stars' places)
		double noise(int i)
		{
			int64_t seed = 7;
			double v = 0;
			for (int k = 0; k <= i; k++)
			{
				seed = seed * 16807 % 2147483647;
				v = (double)seed / 2147483647;
			}
			return v;
		}
		int jsRound(double v) { return (int)std::floor(v + 0.5); }

		// a still's size: k 0-2 alone, 3-5 over the art (scene, rows, columns), 6 the tail's atlas, 7 the notes'
		void dims(int k, int& w, int& h)
		{
			if (k == 6) { w = TW * TPH; h = TH; return; }
			if (k == 7) { w = NW * NPH; h = NH; return; }
			const Layout& L = k < 3 ? ALONE : STRIP;
			w = L.W;
			h = L.H;
		}

		std::vector<unsigned char> still(int k)
		{
			int w, h;
			dims(k, w, h);
			std::vector<unsigned char> m((size_t)w * h, 0);
			auto put = [&](int x, int y, int c) {
				if (x >= 0 && y >= 0 && x < w && y < h) m[(size_t)y * w + x] = (unsigned char)c;
			};
			if (k == 6)	// the tail: 2 bits a width (1 its fur, 2 its dark tip)
			{
				for (int p = 0; p < TPH; p++)
					for (int a = 0; a < 4; a++)
					{
						double mid = (a + 0.5) / 4, amp = 1 + 3 * mid;
						int cycles = 2 + a;	// over the 4 s loop: 0.5 to 1.25 swishes a second
						double sw = std::sin(2 * PI * cycles * p / TPH) * amp;
						for (int i = 0; i < 9; i++)
						{
							int x = TX + i, y = 38 - jsRound(i * 0.6 + std::sin(i * 0.5) * sw * i / 9);
							x -= TX;
							y -= TY;
							if (x < 0 || y < 0 || x >= TW || y >= TH) continue;
							unsigned char& d = m[(size_t)y * w + p * TW + x];
							d = (unsigned char)((d & ~(3 << 2 * a)) | (i > 6 ? 2 : 1) << 2 * a);
						}
					}
				return m;
			}
			if (k == 7)	// the notes: 1 + note x 4 + its strength (0 faint .. 3 full)
			{
				for (int p = 0; p < NPH; p++)
				{
					double T = 2.0 * p / NPH;
					for (int i = 0; i < 4; i++)
					{
						double life = std::fmod(T * 0.5 + i * 0.25, 1.0);
						int nx = 42 + i * 4 + jsRound(std::sin(T * PI + i) * 2) - NX, ny = jsRound(26 - life * 18) - NY;
						int f = std::min(3, (int)std::floor((1 - life) * 4));
						int c = 1 + i * 4 + f;
						const int d[5][2] = {{0, 0}, {0, -1}, {0, -2}, {1, -2}, {-1, 1}};
						for (auto& o : d)
						{
							int x = nx + o[0], y = ny + o[1];
							if (x >= 0 && x < NW && y >= 0 && y < NH) m[(size_t)y * w + p * NW + x] = (unsigned char)c;
						}
					}
				}
				return m;
			}
			const Layout& L = k < 3 ? ALONE : STRIP;
			if (k % 3 == 1)
			{
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) put(x, y, std::clamp(y - L.dy + OFF, 0, 254));
				return m;
			}
			if (k % 3 == 2)
			{
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) put(x, y, std::clamp(x - L.dx + OFF, 0, 254));
				return m;
			}
			// the scene, in its own coordinates (moved by dx, dy)
			auto px = [&](int x, int y, int c) { put(x + L.dx, y + L.dy, c); };
			if (L.room)
			{
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) put(x, y, C_ROOM);
				for (int y = 6; y < 40; y++)
					for (int x = 12; x < 68; x++) px(x, y, x == 12 || x == 67 || y == 6 || x == 39 || y == 22 ? C_FRAME : C_SKY);
				for (int y = -3; y <= 3; y++)
					for (int x = -3; x <= 3; x++)
						if (x * x + y * y <= 10 && (x - 1) * (x - 1) + y * y > 6) px(58 + x, 13 + y, C_MOON);
				for (int i = 0; i < 10; i++) px(14 + (int)std::floor(noise(i * 9) * 24), 8 + (int)std::floor(noise(i * 4) * 12), C_STAR);
				for (int x = 6; x < 74; x++) { px(x, 40, C_SILL); px(x, 41, C_SILL2); }
			}
			else	// over the art: the sill across the strip
				for (int x = 0; x < w; x++) { put(x, 40 + L.dy, C_SILL); put(x, 41 + L.dy, C_SILL2); }
			for (int y = 34; y < 40; y++)
				for (int x = 60; x < 65; x++) px(x, y, y < 36 ? C_LEAF : C_POT);
			// the body (the head is drawn over its top)
			for (int y = 28; y < 40; y++)
			{
				int bw = y < 31 ? 4 : 6;
				for (int x = -bw; x <= bw; x++) px(CX + x, y, x == -bw || x == bw ? C_DARK : (y > 35 && std::abs(x) < 3 ? C_WHITE : C_CAT));
			}
			return m;
		}

		// the head's drawing for variant v (ear up x 2 + happy eyes): each row's codes, 3 bits a column
		long long headRow(int v, int r)
		{
			int g[HR][HC] = {};
			auto px = [&](int x, int y, int c) {	// x from the middle, y from hy
				int cc = x + 7, rr = y + 3;
				if (cc >= 0 && cc < HC && rr >= 0 && rr < HR) g[rr][cc] = c;
			};
			const int C = 1, D = 2, RED = 3, RED2 = 4, EYE = 5, NOSE = 6;
			int tw = v >> 1, happy = v & 1;
			for (int y = 0; y < 9; y++)
			{
				int w = y < 1 ? 4 : y > 7 ? 4 : 5;
				for (int x = -w; x <= w; x++) px(x, y, std::abs(x) == w ? D : C);
			}
			const int ears[2][2] = {{-4, tw}, {4, 0}};
			for (auto& e : ears)
			{
				int ex = e[0], flip = e[1], s = ex < 0 ? -1 : 1;
				px(ex, -1 - flip, D);
				px(ex + s, -1, D);
				px(ex, -2 - flip, D);
			}
			for (int x = -6; x <= 6; x++) px(x, -2 - (std::abs(x) < 4 ? 1 : 0), RED);
			for (int y = 2; y < 6; y++) { px(-6, y, RED); px(6, y, RED); px(-7, y, RED2); px(7, y, RED2); }
			if (happy)
				for (int ex : {-3, 2}) { px(ex, 4, EYE); px(ex + 1, 3, EYE); px(ex + 2, 4, EYE); }
			else
				for (int ex : {-2, 2}) { px(ex, 3, EYE); px(ex, 4, EYE); }
			px(0, 5, NOSE);
			px(-1, 6, EYE);
			px(1, 6, EYE);
			long long mask = 0;
			for (int c = HC - 1; c >= 0; c--) mask = mask * 8 + g[r][c];
			return mask;
		}
	}

	bool writeCatMaps(const std::string& dir)
	{
		auto path = [&](int k) { return dir + "/" + FILES[k]; };
		auto ready = [&](int k) {
			int w, h;
			dims(k, w, h);
			struct stat st;
			return stat(path(k).c_str(), &st) == 0 && (size_t)st.st_size == header(w, h).size() + (size_t)w * h;
		};
		bool all = true;
		for (int k = 0; k < 8; k++) all = all && ready(k);
		if (all) return true;
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < 8; k++)
		{
			int w, h;
			dims(k, w, h);
			if (!pgm(path(k), w, h, still(k))) return false;
		}
		return true;
	}

	std::string catGraph(bool overCover, const std::string& dir)
	{
		const Layout& L = overCover ? STRIP : ALONE;
		const int base = overCover ? 3 : 0;
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + FILES[k < 3 ? base + k : k], s); return s; };
		const std::string W = n(L.W), H = n(L.H);

		Yuv col[C_COUNT];
		for (int k = 0; k < C_COUNT; k++) col[k] = yuv(RGB[k]);
		const Yuv sky = col[C_SKY];
		// a lut from the class (var) to a plane's value; over the art, 0 is the art's (y) and a note blends into it;
		// alone, a note blends into the sky
		auto paint = [&](int Yuv::*plane, const char* var, bool art) {
			std::string e = art ? "y" : n(col[0].*plane);
			for (int k = C_COUNT - 1; k >= 1; k--)
			{
				std::string v;
				if (k >= C_NOTE)
				{
					double a = NOTE_ALPHA[k - C_NOTE];
					if (art)
						v = "round(" + std::to_string(a) + "*" + n(col[k].*plane) + "+" + std::to_string(1 - a) + "*y)";
					else
						v = n(std::lround(a * (col[k].*plane) + (1 - a) * (sky.*plane)));
				}
				else
					v = n(col[k].*plane);
				e = std::string("if(eq(") + var + "," + n(k) + ")," + v + "," + e + ")";
			}
			return "'" + e + "'";
		};

		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [ctart1] [ctart2]; [ctart1] crop=640:280:0:0 [top]; "
				 "[ctart2] crop=640:200:0:280,extractplanes=y+u+v [ctay] [ctau] [ctav]; ";
		// the 16 bands measured (the top 8 measured and cut off, as Pocket console), contrasted, eased (up at once, down
		// over ~0.15 s: the exp/log lagfun), then the four values
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=24x256:mode=bar:cmode=combined:ascale=log:"
			 "fscale=log:win_size=1024:rate=60:colors=white,crop=16:256:0:0,scale=16:1:flags=area,format=gray,lut=c0='val*252/255'" +
			 vizContrast("ct") + ",format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split=4 [ctb] [ctm] [ctt] [ctl]; "
			 "[ctb] crop=3:1:0:0,scale=1:1:flags=area [ctb1]; [ctm] crop=6:1:4:0,scale=1:1:flags=area [ctm1]; "
			 "[ctt] crop=5:1:11:0,scale=1:1:flags=area,split [ctt1] [ctt2]; [ctl] scale=1:1:flags=area [ctl1]; ";

		// the head: the state (bob x 4 + ear x 2 + eyes), against the rows: the variant x 12 + the head's row (255
		// outside); against the columns: the class
		g += "[ctb1] [ctt1] lut2=c0='4*(gte(x," + n(B1) + ")+gte(x," + n(B2) + "))+2*gt(y," + n(EAR) + ")' [ctbt]; "
			 "[ctbt] [ctl1] lut2=c0='x+gt(y," + n(LOUD) + ")',scale=" + W + ":" + H + ":flags=neighbor [cts]; "
			 "movie=" + q(1) + ",format=gray [ctrow]; movie=" + q(2) + ",format=gray [ctcol]; "
			 "[cts] [ctrow] lut2=c0='st(0,y-" + n(OFF + HY0) + "-floor(x/4));if(between(ld(0),0," + n(HR - 1) + "),mod(x,4)*" + n(HR) +
			 "+ld(0),255)' [cth]; ";
		{
			std::string rows = "0";
			for (int k = 4 * HR - 1; k >= 0; k--)
			{
				long long mask = headRow(k / HR, k % HR);
				if (mask) rows = "if(eq(x," + n(k) + ")," + n(mask) + "," + rows + ")";
			}
			g += "[cth] [ctcol] lut2=c0='st(1,y-" + n(OFF + CX - 7) + ");st(0,if(eq(x,255)+lt(ld(1),0)+gt(ld(1)," + n(HC - 1) +
				 "),0,mod(floor((" + rows + ")/pow(8,ld(1))),8)));if(ld(0)," + n(C_CAT - 1) + "+ld(0),0)' [ctH]; ";
		}
		// the tail: the mids' width step against the atlas, the moment by the frame number
		g += "[ctm1] scale=" + n(TW * TPH) + ":" + n(TH) + ":flags=neighbor [ctms]; movie=" + q(6) + ",format=gray [cttm]; "
			 "[ctms] [cttm] lut2=c0='st(0,mod(floor(y/pow(4,gte(x," + n(M1) + ")+gte(x," + n(M2) + ")+gte(x," + n(M3) + "))),4));"
			 "if(eq(ld(0),1)," + n(C_CAT) + ",if(eq(ld(0),2)," + n(C_DARK) + ",0))',"
			 "crop=" + n(TW) + ":" + n(TH) + ":x='" + n(TW) + "*mod(floor(n/" + n(TSTEP) + ")," + n(TPH) + ")':y=0,"
			 "pad=" + W + ":" + H + ":" + n(TX + L.dx) + ":" + n(TY + L.dy) + ",setsar=1 [ctT]; ";
		// the notes: each past its own treble threshold
		g += "[ctt2] scale=" + n(NW * NPH) + ":" + n(NH) + ":flags=neighbor [ctts]; movie=" + q(7) + ",format=gray [ctnm]; "
			 "[ctts] [ctnm] lut2=c0='if(eq(y,0),0,if(gte(x," + n(NOTE0) + "+" + n(NOTE1) + "*floor((y-1)/4))," + n(C_NOTE) +
			 "+mod(y-1,4),0))',crop=" + n(NW) + ":" + n(NH) + ":x='" + n(NW) + "*mod(floor(n/" + n(NSTEP) + ")," + n(NPH) + ")':y=0,"
			 "pad=" + W + ":" + H + ":" + n(NX + L.dx) + ":" + n(NY + L.dy) + ",setsar=1 [ctN]; ";
		// merged over the scene: the tail, the head, the notes
		g += "movie=" + q(0) + ",format=gray [ctsc]; [ctT] [ctsc] lut2=c0='if(x,x,y)' [ctm1x]; [ctH] [ctm1x] lut2=c0='if(x,x,y)' [ctm2x]; "
			 "[ctN] [ctm2x] lut2=c0='if(x,x,y)',split=3 [ctc0] [ctc1] [ctc2]; ";

		if (overCover)
			return g + "[ctc0] scale=640:200:flags=neighbor [ctc0s]; [ctc0s] [ctay] lut2=c0=" + paint(&Yuv::y, "x", true) + " [ctY]; "
					   "[ctc1] scale=320:100:flags=neighbor [ctc1s]; [ctc1s] [ctau] lut2=c0=" + paint(&Yuv::u, "x", true) + " [ctU]; "
					   "[ctc2] scale=320:100:flags=neighbor [ctc2s]; [ctc2s] [ctav] lut2=c0=" + paint(&Yuv::v, "x", true) + " [ctV]; "
					   "[ctY] [ctU] [ctV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "[ctc0] lut=c0=" + paint(&Yuv::y, "val", false) + ",scale=640:480:flags=neighbor [ctY]; "
				   "[ctc1] lut=c0=" + paint(&Yuv::u, "val", false) + ",scale=320:240:flags=neighbor [ctU]; "
				   "[ctc2] lut=c0=" + paint(&Yuv::v, "val", false) + ",scale=320:240:flags=neighbor [ctV]; "
				   "[ctY] [ctU] [ctV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
