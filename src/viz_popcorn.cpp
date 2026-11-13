// Popcorn machine: pixel art on an 80x60 grid of 8-pixel dots. A dark red-brown room; a red cinema popcorn cart (a red
// base on two dark feet, a glass case with pale edges, a red lid with a yellow "POP" sign on top); in the glass a pile
// of popcorn (cream, golden kernels here and there, a bumpy top) rising and settling with the overall level (the mean
// of the 16 bands); kernels popping up from the pile and falling back into it, more of them and higher as the bands
// get louder; the lid and its sign rattling a dot up and down while the bass is strong.
// A frame is worked out at the grid's size as a picture of classes (0 the room, see the table at PC_COLOURS), painted
// at the end:
//  - the 16 bands measured, contrasted and eased (as Pocket console's bars); from them the mean, the bass (bands 0-2),
//    and, held longer for the kernels (a slower lagfun, so a kernel in flight doesn't blink on every kick), the bass,
//    the mids (4-9) and the treble (10-15);
//  - the pile: a still gives each dot in the glass its height above the floor less its column's bump (and whether it
//    is a golden one); one lut2 against the mean (as a pile height, everywhere) keeps the dots under it;
//  - the kernels: a still atlas of a 2 s loop (120 frames: six bursts of three kernels on fixed arcs, each dot saying
//    which kernel, its group of bands, how loud that group must be, cream or golden), stacked under the three held
//    values packed into one (a digit 0-5 each), cropped by the frame number; one lut2 lights a kernel once its group
//    passes its step. The higher arcs need the louder steps;
//  - the lid: a still with its three places (a dot up, still, a dot down) packed 2 bits each; the bass past 60% and a
//    row of the rattle's phases cropped by the frame number pick one;
//  - laid over the scene's still (each layer's 0 see-through), a lut per plane turns a class into its luma, U and V,
//    the luma scaled x8 and the chroma x4 (nearest).
// Over the art: the art's top as it is, and in the 640x200 strip under it the cart's lower part (25 rows: the glass
// with its pile and smaller pops, the base and the feet' top), the art seen through the glass and everywhere else.
// Stills are one frame each (movie=), never looped. YUV and gray only, nothing converted, no geq per frame.
#include "viz_more.h"
#include "settings.h"

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
		const char* PC_FILES[8] = {"popcorn2-scene.pgm", "popcorn2-scenes.pgm", "popcorn2-pile.pgm", "popcorn2-piles.pgm",
								   "popcorn2-pops.pgm", "popcorn2-popss.pgm", "popcorn2-lid.pgm", "popcorn2-phase.pgm"};
		const int W = 80, H = 60, DY = 34, GY = 22;	// the grid; the strip (25 rows) is the scene from row 34
		const int GX0 = 21, GX1 = 58;		// the glass's inside (its pale edges at 20 and 59), from row GY: the
											// lid and the sign above it clear of the player's title
		const int F = 120, BURSTS = 6;		// the kernels' loop (2 s at 60 a second), its bursts of three
		const int LX = 18, LY = 11, LW = 44, LH = 12;	// the lid's window (it and the sign, a dot each way)
		const int PHASES = 19;				// the rattle's phases: two of its swings (sin(40 t) at 60 a second)
		const int FLOOR = 40;				// below it the spectrum's near-constant bed
		const double PI = 3.14159265358979323846;

		// the classes
		enum { C_ROOM, C_BASE, C_DARK, C_GLASS, C_EDGE, C_LID, C_SIGN, C_CREAM, C_GOLD, C_COUNT };
		const int PC_COLOURS[C_COUNT] = {0x2a0f14, 0xb8202a, 0x7a121a, 0xc8e6ff, 0xd8e8f8, 0xe8322e, 0xffd23f, 0xfff4d6, 0xffd58a};
		const double GLASS_ALPHA = 0.12;	// the glass's tint over the room (or the art)

		struct Lay
		{
			int rows, dy;			// rows shown, the scene's first
			int pileBase, pileGain;	// the pile's height: base + gain x the mean
			double y0, endY, g, v0, vGain;	// a kernel: launched at y0 up at v0 + vGain x its strength, gone below endY
		};
		Lay lay(bool strip)
		{
			if (strip) return {25, DY, 3, 6, 9 + DY, 13 + DY, 60, 14, 22};
			return {H, 0, 6, 8, 38, 44, 80, 18, 30};
		}
		// the kernels' window: the glass's inside, from its top (or the strip's) to just under the lowest kernel
		int kTop(const Lay& l) { return l.dy ? 0 : GY; }
		int kRows(const Lay& l) { return (int)l.endY + 2 - l.dy - kTop(l); }
		const int KW = GX1 - GX0 + 1;

		int bump(int x) { return (int)std::floor(std::sin(x * 0.7) * 1.5 + 0.5); }
		bool golden(int x, int y) { return (x * 7 + y * 3) % 5 == 0; }

		// the scene's classes: the room, the glass and its edges, the base and the feet (the lid is its own layer)
		std::vector<unsigned char> scene(const Lay& l)
		{
			std::vector<unsigned char> m((size_t)W * l.rows, C_ROOM);
			auto at = [&](int x, int y, int v) { y -= l.dy; if (y >= 0 && y < l.rows) m[(size_t)y * W + x] = (unsigned char)v; };
			for (int y = GY; y < 50; y++)
				for (int x = 20; x < 60; x++) at(x, y, x == 20 || x == 59 ? C_EDGE : C_GLASS);
			for (int y = 50; y < 58; y++)
				for (int x = 18; x < 62; x++) at(x, y, C_BASE);
			for (int y = 58; y < 60; y++)
				for (int x = 0; x < 4; x++) { at(22 + x, y, C_DARK); at(54 + x, y, C_DARK); }
			return m;
		}

		// the pile's still: each dot in the glass (h + 3) x 2 + golden, h its height above the floor less its column's bump
		std::vector<unsigned char> pile(const Lay& l)
		{
			std::vector<unsigned char> m((size_t)W * l.rows, 0);
			for (int y = GY; y < 50; y++)
				for (int x = GX0; x <= GX1; x++)
				{
					const int r = y - l.dy;
					if (r < 0 || r >= l.rows) continue;
					const int h = 49 - y - bump(x);
					m[(size_t)r * W + x] = (unsigned char)((h + 3) * 2 + (golden(x, y) ? 1 : 0));
				}
			return m;
		}

		// the kernels' atlas: F frames of the window, one under the other. A dot: 0 nothing, else 1 + golden + 2 x
		// (group x 5 + step - 1): group 0 bass, 1 mids, 2 treble; step 1-5 the held level's digit it needs
		std::vector<unsigned char> pops(const Lay& l)
		{
			const int KH = kRows(l), top = kTop(l) + l.dy;
			std::vector<unsigned char> m((size_t)KW * KH * F, 0);
			uint32_t seed = 7;
			auto rnd = [&]() { seed = (uint32_t)((uint64_t)seed * 16807 % 2147483647); return seed / 2147483647.0; };
			for (int b = 0; b < BURSTS; b++)
				for (int k = 0; k < 3; k++)
				{
					// every other burst a small one (steps 1-3); each kernel's step its arc's height
					const int step = 1 + (int)std::floor(rnd() * (b % 2 ? 2.99 : 4.99));
					const double s = (step - 1) / 4.0 + rnd() * 0.15;
					const double x0 = 30 + rnd() * 20, vx = (rnd() - 0.5) * 24, vy = -(l.v0 + l.vGain * s);
					const int f0 = b * F / BURSTS + (int)(rnd() * 4);
					const int code = 1 + 2 * (k * 5 + step - 1);
					for (int t = 0; t < F; t++)
					{
						const double tt = t / 60.0, x = x0 + vx * tt, y = l.y0 + vy * tt + l.g * tt * tt / 2;
						if (y > l.endY && vy + l.g * tt > 0) break;
						const int f = (f0 + t) % F, xi = (int)std::floor(x + 0.5), yi = (int)std::floor(y + 0.5);
						const int dots[3][3] = {{0, 0, 0}, {1, 0, 1}, {0, 1, 0}};
						for (auto& d : dots)
						{
							const int dx = xi + d[0], dyy = yi + d[1] - top;
							if (dx < GX0 || dx > GX1 || dyy < 0 || dyy >= KH) continue;
							m[((size_t)f * KH + dyy) * KW + (dx - GX0)] = (unsigned char)(code + d[2]);
						}
					}
				}
			return m;
		}

		// the lid and the sign at their three places (a dot up, still, a dot down), 2 bits each: 1 the lid, 2 the sign,
		// 3 its letters
		std::vector<unsigned char> lid()
		{
			std::vector<unsigned char> m((size_t)LW * LH, 0);
			static const char* P[4] = {"###", "#.#", "###", "#.."};
			static const char* O[4] = {"###", "#.#", "#.#", "###"};
			for (int s = -1; s <= 1; s++)
			{
				const int sh = 2 * (s + 1);
				auto at = [&](int x, int y, int v) {
					unsigned char& d = m[(size_t)(y + s - LY) * LW + x - LX];
					d = (unsigned char)((d & ~(3 << sh)) | v << sh);
				};
				for (int y = GY - 4; y < GY; y++)
					for (int x = 18; x < 62; x++) at(x, y, 1);
				for (int y = GY - 10; y < GY - 4; y++)
					for (int x = 30; x < 50; x++) at(x, y, 2);
				for (int y = 0; y < 4; y++)
					for (int x = 0; x < 3; x++)
					{
						if (P[y][x] == '#') { at(34 + x, GY - 9 + y, 3); at(42 + x, GY - 9 + y, 3); }
						if (O[y][x] == '#') at(38 + x, GY - 9 + y, 3);
					}
			}
			return m;
		}

		// the rattle's phases (the preview's round(sin(40 t))) + 1, a frame each
		std::vector<unsigned char> phase()
		{
			std::vector<unsigned char> m(PHASES);
			for (int k = 0; k < PHASES; k++) m[k] = (unsigned char)(1 + (int)std::floor(std::sin(k * 40 / 60.0) + 0.5));
			return m;
		}

		struct Yuv { int y, u, v; };
		// BT.601 limited (mpv's reading of a picture this size)
		Yuv yuv(int rgb)
		{
			double r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
			return {(int)std::lround(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255),
					(int)std::lround(128 + (-37.797 * r - 74.203 * g + 112 * b) / 255),
					(int)std::lround(128 + (112 * r - 93.786 * g - 18.214 * b) / 255)};
		}

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

		void sizes(int k, int& w, int& h)
		{
			const Lay a = lay(false), s = lay(true);
			const int ws[8] = {W, W, W, W, KW, KW, LW, PHASES};
			const int hs[8] = {H, s.rows, H, s.rows, kRows(a) * F, kRows(s) * F, LH, 1};
			w = ws[k]; h = hs[k];
		}
	}

	bool writePopcornMaps(const std::string& dir)
	{
		bool ready = true;
		for (int k = 0; k < 8 && ready; k++)
		{
			int w, h;
			sizes(k, w, h);
			struct stat st;
			const std::string header = "P5\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
			ready = stat((dir + "/" + PC_FILES[k]).c_str(), &st) == 0 && (size_t)st.st_size == header.size() + (size_t)w * h;
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);
		const Lay a = lay(false), s = lay(true);
		const std::vector<unsigned char> data[8] = {scene(a), scene(s), pile(a), pile(s), pops(a), pops(s), lid(), phase()};
		for (int k = 0; k < 8; k++)
		{
			int w, h;
			sizes(k, w, h);
			if (!writePgm(dir + "/" + PC_FILES[k], w, h, data[k])) return false;
		}
		return true;
	}

	std::string popcornGraph(bool overCover, const std::string& dir)
	{
		auto n = [](int v) { return std::to_string(v); };
		auto q = [&](int k) { std::string p; lavfiPath(dir + "/" + PC_FILES[k], p); return p; };
		const Lay l = lay(overCover);
		const int R = l.rows, KH = kRows(l), KY = kTop(l), S = overCover ? 1 : 0;
		const std::string range = n(252 - FLOOR);
		// a level's digit 0-5 (the kernels' steps)
		auto digit = [&](const char* v) { return std::string("clip(floor(6*(") + v + "-" + n(FLOOR) + ")/" + range + "),0,5)"; };

		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [pcart1] [pcart2]; [pcart1] crop=640:280:0:0 [top]; "
				 "[pcart2] crop=640:200:0:280,extractplanes=y+u+v [pcay] [pcau] [pcav]; ";
		// the 16 bands measured (the top 8 measured and cut off, as Pocket console), contrasted; eased (up at once, down
		// over ~0.15 s: the exp/log lagfun) for the pile and the lid, held longer (~0.9 s from the top) for the kernels
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=24x256:mode=bar:cmode=combined:ascale=log:"
			 "fscale=log:win_size=1024:rate=60:colors=white,crop=16:256:0:0,scale=16:1:flags=area,format=gray,bplut=c0='val*252/255'" +
			 vizContrast("pc") + ",format=gray16le,lut=c0='65535*exp((val/257-252)/24)',split [pce] [pch]; "
			 "[pce] lagfun=decay=0.311,lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray" +
			 (overCover ? " [pcl]; " : ",split [pcl] [pcb]; ") +
			 "[pch] lagfun=decay=0.85,lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split=3 [pch1] [pch2] [pch3]; ";

		// the pile: the mean as the pile's height (+3, as the still), against the still
		g += "[pcl] scale=1:1:flags=area,bplut=c0='3+" + n(l.pileBase) + "+round(" + n(l.pileGain) + "*clip((val-" + n(FLOOR) +
			 ")/" + range + ",0,1))',scale=" + n(W) + ":" + n(R) + ":flags=neighbor [pcpv]; movie=" + q(2 + S) + ",format=gray [pcpm]; "
			 "[pcpm] [pcpv] bplut2=c0='if(x*lte(floor(x/2),y)," + n(C_CREAM) + "+mod(x,2),0)' [pcP]; ";

		// the kernels: the three held values as one (bass + 6 x mids + 36 x treble digits), stacked over the atlas,
		// the frame's window cropped by the frame number; a kernel lit once its group's digit reaches its step
		g += "[pch1] crop=3:1:0:0,scale=1:1:flags=area [pchb]; [pch2] crop=6:1:4:0,scale=1:1:flags=area [pchm]; "
			 "[pch3] crop=5:1:11:0,scale=1:1:flags=area [pcht]; "
			 "[pchb] [pchm] bplut2=c0='" + digit("x") + "+6*" + digit("y") + "' [pchbm]; "
			 "[pchbm] [pcht] bplut2=c0='x+36*" + digit("y") + "',scale=" + n(KW) + ":" + n(KH) + ":flags=neighbor [pcks]; "
			 "movie=" + q(4 + S) + ",format=gray [pcka]; [pcks] [pcka] vstack,split [pck0] [pck1]; "
			 "[pck0] crop=" + n(KW) + ":" + n(KH) + ":0:0 [pckv]; "
			 "[pck1] crop=w=" + n(KW) + ":h=" + n(KH) + ":x=0:y='" + n(KH) + "*(1+mod(n," + n(F) + "))' [pckf]; "
			 "[pckf] [pckv] bplut2=c0='st(0,floor((x-1)/2));if(x*gte(mod(floor(y/pow(6,floor(ld(0)/5))),6),mod(ld(0),5)+1)," +
			 n(C_CREAM) + "+mod(x-1,2),0)',pad=" + n(W) + ":" + n(R) + ":" + n(GX0) + ":" + n(KY) + ",setsar=1 [pcK]; ";

		// merged over the scene: the pile, the kernels
		g += "movie=" + q(S) + ",format=gray [pcsc]; [pcP] [pcsc] bplut2=c0='if(x,x,y)' [pcm0]; [pcK] [pcm0] bplut2=c0='if(x,x,y)'";
		if (overCover)
			g += ",split=3 [pcc0] [pcc1] [pcc2]; ";	// no lid in the strip
		else
			// the lid: the bass past 60% picks the rattle's phase (a dot up, still or down), else still
			g += " [pcm1]; [pcb] crop=3:1:0:0,scale=1:1:flags=area,scale=" + n(PHASES) + ":1:flags=neighbor [pcbb]; "
				 "movie=" + q(7) + ",format=gray [pcph]; [pcbb] [pcph] vstack,split [pcr0] [pcr1]; "
				 "[pcr0] crop=1:1:0:0 [pcrb]; [pcr1] crop=w=1:h=1:x='mod(n," + n(PHASES) + ")':y=1 [pcrp]; "
				 "[pcrb] [pcrp] bplut2=c0='if(gt(x," + n(FLOOR + (252 - FLOOR) * 60 / 100) + "),y,1)',scale=" + n(LW) + ":" + n(LH) +
				 ":flags=neighbor [pcls]; movie=" + q(6) + ",format=gray [pclm]; "
				 "[pclm] [pcls] bplut2=c0='st(0,mod(floor(x/pow(4,y)),4));if(eq(ld(0),1)," + n(C_LID) + ",if(eq(ld(0),2)," + n(C_SIGN) +
				 ",if(eq(ld(0),3)," + n(C_DARK) + ",0)))',pad=" + n(W) + ":" + n(R) + ":" + n(LX) + ":" + n(LY) + ",setsar=1 [pcL]; "
				 "[pcL] [pcm1] bplut2=c0='if(x,x,y)',split=3 [pcc0] [pcc1] [pcc2]; ";

		// a class's colour; the glass a tint over the room (or the art)
		const Yuv room = yuv(PC_COLOURS[C_ROOM]);
		auto paint = [&](int Yuv::*plane, const char* var, bool art) {
			std::string e = art ? "y" : n(room.*plane);
			for (int k = C_COUNT - 1; k >= 1; k--)
			{
				const int c = yuv(PC_COLOURS[k]).*plane;
				std::string v;
				if (k == C_GLASS)
					v = art ? "round(y+" + std::to_string(GLASS_ALPHA) + "*(" + n(c) + "-y))"
							: n((int)std::lround(room.*plane + GLASS_ALPHA * (c - room.*plane)));
				else
					v = n(c);
				e = std::string("if(eq(") + var + "," + n(k) + ")," + v + "," + e + ")";
			}
			return "'" + e + "'";
		};
		if (overCover)
			return g + "[pcc0] scale=640:200:flags=neighbor [pcc0s]; [pcc0s] [pcay] bplut2=c0=" + paint(&Yuv::y, "x", true) + " [pcY]; "
					   "[pcc1] scale=320:100:flags=neighbor [pcc1s]; [pcc1s] [pcau] bplut2=c0=" + paint(&Yuv::u, "x", true) + " [pcU]; "
					   "[pcc2] scale=320:100:flags=neighbor [pcc2s]; [pcc2s] [pcav] bplut2=c0=" + paint(&Yuv::v, "x", true) + " [pcV]; "
					   "[pcY] [pcU] [pcV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "[pcc0] bplut=c0=" + paint(&Yuv::y, "val", false) + ",scale=640:480:flags=neighbor [pcY]; "
				   "[pcc1] bplut=c0=" + paint(&Yuv::u, "val", false) + ",scale=320:240:flags=neighbor [pcU]; "
				   "[pcc2] bplut=c0=" + paint(&Yuv::v, "val", false) + ",scale=320:240:flags=neighbor [pcV]; "
				   "[pcY] [pcU] [pcV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
