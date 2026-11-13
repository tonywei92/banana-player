// Jelly wobble: pixel art on an 80x60 grid of 8-pixel dots. A pale peach wall over a tan table; on it a white plate and
// a red strawberry jelly (a darker left edge, a lighter right one, a white highlight on its top, a strawberry with a
// green leaf on that) with a worried face: white eyes with dark pupils, a small mouth. It squashes (shorter and wider)
// with the bass (bands 0-2) and wobbles side to side, a sway that grows with the bass and leans more at the top than
// at the bottom; while the music is loud (the mean of all 16) its eyes open wide, and louder still a blue sweat drop
// shows by them.
// A frame: the 16 bands measured, contrasted and eased (as Pocket console's bars), and from them the bass and the mean.
// The jelly is drawn in C++ (the preview's drawing) into one still atlas: 16 wobble phases across, 8 squash steps
// down, each pose a 56x29 box whose dots hold the pose's step x 16 + the dot's code. The eyes' outer ring and the sweat
// drop are codes of their own ("big eye over body", "sweat over the light edge"...), so one pose serves every loudness.
//  - the phase: a one-row stream at the atlas' width (the bass, stretched) stacked over the atlas (vstack: the still's
//    one frame kept), and the phase's column cropped by the frame number (a new phase every 2 frames: a sway every
//    0.53 s, as the preview's 12 rad/s);
//  - the squash and the face: one value a frame (the step + 8 x the loudness 0-2) against the column: a lut2 keeps
//    the dots of the step's pose and turns their codes into classes (a ring white or the jelly under it, a drop blue
//    or not), the others 0; the eight poses folded into one (halves added, three times);
//  - padded to its place over the scene's still (its 0 see-through), a lut per plane turns a class into its luma, U
//    and V, the luma scaled x8 and the chroma x4 (nearest).
// Stills are one frame each (movie=), never looped. YUV and gray only, nothing converted, no geq per frame.
// Over the art: the art's top as it is, and in the 640x200 strip under it the same at 4-pixel dots (160x50): the plate
// along its bottom and the jelly on it at half the size; the art shows through everywhere else.
#include "viz_more.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	namespace
	{
		// a new name whenever the stills change: they're cached by size
		const char* const FILES[3] = {"wobble1-atlas.pgm", "wobble1-scene-a.pgm", "wobble1-scene-s.pgm"};

		// the drawing's grid: alone 80x60 dots of 8 pixels; over the art 160x50 of 4 (640x200), the scene moved by
		// (dx, dy) so the plate sits at the strip's bottom and the jelly in its middle
		struct Layout
		{
			int W, H, P, dx, dy;
			bool room;	// the wall and the table (alone), or the art (over it)
		};
		const Layout ALONE = {80, 60, 8, 0, 0, true};
		const Layout STRIP = {160, 50, 4, 40, 2, false};
		const double PI = 3.14159265358979323846;

		// the poses: a box of the scene's dots (from X0, Y0), PH phases across, ST squash steps down
		const int X0 = 12, Y0 = 18, BW = 56, BH = 29, PH = 16, ST = 8, PSTEP = 2;

		// the classes: 0 the art (over it) or nothing; the scene's; the jelly's
		enum
		{
			C_NONE,
			C_WALL,
			C_TABLE,
			C_PLATE,
			C_PLATE2,
			C_DARK,
			C_BODY,
			C_LIGHT,
			C_WHITE,
			C_LEAF,
			C_PUPIL,
			C_MOUTH,
			C_SWEAT,
			C_COUNT
		};
		const unsigned RGB[C_COUNT] = {0xfbe3d6, 0xfbe3d6, 0xe8b49a, 0xffffff, 0xdfe6ee, 0xc4182a, 0xe82a3c,
									   0xff6a7a, 0xffffff, 0x3fbf6a, 0x1a1a1a, 0x7a0a14, 0x7fd0ff};
		// a pose's codes (4 bits): 0 nothing; 1-7 the jelly's dark edge, body, light edge, white, leaf, pupil, mouth;
		// the eyes' outer ring (white when loud) over nothing, the dark edge, the body, the light one (8-11); the sweat
		// drop (when louder) over the same (12-15)
		enum { J_NONE, J_DARK, J_BODY, J_LIGHT, J_WHITE, J_LEAF, J_PUPIL, J_MOUTH, J_RING = 8, J_DROP = 12 };

		// the loudness' thresholds (0..252, after the contrast and easing; the preview's fractions x 252): wide eyes past
		// RING, the drop past DROP
		const int RING = 139, DROP = 151;

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

		int jsRound(double v) { return (int)std::floor(v + 0.5); }

		// a still's size: 0 the atlas, 1 the scene alone, 2 over the art
		void dims(int k, int& w, int& h)
		{
			if (k == 0) { w = BW * PH; h = BH * ST; return; }
			const Layout& L = k == 1 ? ALONE : STRIP;
			w = L.W;
			h = L.H;
		}

		// one pose (squash step s, phase p) in its box: the preview's drawing
		void pose(int s, int p, unsigned char* box, int stride)
		{
			auto get = [&](int x, int y) { x -= X0, y -= Y0; return x >= 0 && y >= 0 && x < BW && y < BH ? box[y * stride + x] : 0; };
			auto px = [&](int x, int y, int c) {
				x -= X0, y -= Y0;
				if (x >= 0 && y >= 0 && x < BW && y < BH) box[y * stride + x] = (unsigned char)c;
			};
			const double bass = (double)s / (ST - 1), ph = 2 * PI * p / PH;
			const double squash = bass * 6, wob = std::sin(ph) * bass * 3;
			for (int y = 0; y < 22; y++)
			{
				double k = y / 22.0, h = 22 - squash;
				int yy = 46 - jsRound(k * h);
				double half = 14 + squash * 0.8 - k * 4 + std::sin(k * 3 + ph) * wob * k;
				for (int x = -jsRound(half); x <= jsRound(half); x++)
					px(40 + x + jsRound(wob * k), yy, x < -half + 2 ? J_DARK : x > half - 3 ? J_LIGHT : J_BODY);
			}
			const int top = 46 - jsRound(22 - squash), w = jsRound(wob);
			for (int x = -6; x <= 6; x++) px(40 + x + w, top, J_WHITE);
			for (int y = -2; y <= 2; y++)
				for (int x = -2; x <= 2; x++)
					if (x * x + y * y <= 6) px(40 + w + x, top - 2 + y, J_DARK);
			px(41 + w, top - 5, J_LEAF);
			// the face: the eyes white (3x3), their ring (5x5 less the corners) white only when loud
			const int ey = top + 7, ex = 40 + jsRound(wob * 0.6);
			auto under = [&](int x, int y) { int c = get(x, y); return c <= J_LIGHT ? c : -1; };
			for (int e : {ex - 5, ex + 5})
			{
				for (int y = -2; y <= 2; y++)
					for (int x = -2; x <= 2; x++)
					{
						if (x * x + y * y > 6) continue;
						if (x * x + y * y <= 2)
							px(e + x, ey + y, J_WHITE);
						else if (under(e + x, ey + y) >= 0)
							px(e + x, ey + y, J_RING + under(e + x, ey + y));
					}
				px(e, ey, J_PUPIL);
			}
			for (int x = -3; x <= 3; x++) px(ex + x, ey + 6 + (std::abs(x) == 3 ? -1 : 0), J_MOUTH);
			for (int y : {ey - 3, ey - 2})
				if (under(ex + 9, y) >= 0) px(ex + 9, y, J_DROP + under(ex + 9, y));
		}

		std::vector<unsigned char> still(int k)
		{
			int w, h;
			dims(k, w, h);
			std::vector<unsigned char> m((size_t)w * h, 0);
			if (k == 0)
			{
				std::vector<unsigned char> box((size_t)BW * BH);
				for (int s = 0; s < ST; s++)
					for (int p = 0; p < PH; p++)
					{
						std::fill(box.begin(), box.end(), 0);
						pose(s, p, box.data(), BW);
						for (int y = 0; y < BH; y++)
							for (int x = 0; x < BW; x++)
								m[(size_t)(s * BH + y) * w + p * BW + x] = (unsigned char)(s * 16 + box[(size_t)y * BW + x]);
					}
				return m;
			}
			const Layout& L = k == 1 ? ALONE : STRIP;
			auto put = [&](int x, int y, int c) {
				x += L.dx, y += L.dy;
				if (x >= 0 && y >= 0 && x < w && y < h) m[(size_t)y * w + x] = (unsigned char)c;
			};
			if (L.room)
				for (int y = 0; y < h; y++)
					for (int x = 0; x < w; x++) put(x, y, y >= 46 ? C_TABLE : C_WALL);
			for (int x = 18; x < 62; x++) { put(x, 46, C_PLATE); put(x, 47, C_PLATE2); }
			return m;
		}
	}

	bool writeWobbleMaps(const std::string& dir)
	{
		auto path = [&](int k) { return dir + "/" + FILES[k]; };
		auto ready = [&](int k) {
			int w, h;
			dims(k, w, h);
			struct stat st;
			return stat(path(k).c_str(), &st) == 0 && (size_t)st.st_size == header(w, h).size() + (size_t)w * h;
		};
		if (ready(0) && ready(1) && ready(2)) return true;
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < 3; k++)
		{
			int w, h;
			dims(k, w, h);
			if (!pgm(path(k), w, h, still(k))) return false;
		}
		return true;
	}

	std::string wobbleGraph(bool overCover, const std::string& dir)
	{
		const Layout& L = overCover ? STRIP : ALONE;
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + FILES[k], s); return s; };
		const std::string W = n(L.W), H = n(L.H);

		Yuv col[C_COUNT];
		for (int k = 0; k < C_COUNT; k++) col[k] = yuv(RGB[k]);
		// a lut from the class (var) to a plane's value; over the art, 0 is the art's (y)
		auto paint = [&](int Yuv::*plane, const char* var, bool art) {
			std::string e = art ? "y" : n(col[0].*plane);
			for (int k = C_COUNT - 1; k >= 1; k--) e = std::string("if(eq(") + var + "," + n(k) + ")," + n(col[k].*plane) + "," + e + ")";
			return "'" + e + "'";
		};
		// a pose's code (ld(0)) at the loudness (ld(1): 0-2) as a class
		std::string cls = "0";
		for (int c = 15; c >= 1; c--)
		{
			std::string v;
			if (c >= J_DROP || c >= J_RING)
			{
				const int u = c >= J_DROP ? c - J_DROP : c - J_RING, base = u ? C_DARK + u - 1 : 0;
				v = "if(gte(ld(1)," + n(c >= J_DROP ? 2 : 1) + ")," + n(c >= J_DROP ? C_SWEAT : C_WHITE) + "," + n(base) + ")";
			}
			else
				v = n(C_DARK + c - 1);
			cls = "if(eq(ld(0)," + n(c) + ")," + v + "," + cls + ")";
		}

		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [jwart1] [jwart2]; [jwart1] crop=640:280:0:0 [top]; "
				 "[jwart2] crop=640:200:0:280,extractplanes=y+u+v [jway] [jwau] [jwav]; ";
		// the 16 bands measured (the top 8 measured and cut off, as Pocket console), contrasted, eased (up at once, down
		// over ~0.15 s: the exp/log lagfun), then the bass and the mean
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=24x256:mode=bar:cmode=combined:ascale=log:"
			 "fscale=log:win_size=1024:rate=60:colors=white,crop=16:256:0:0,scale=16:1:flags=area,format=gray,bplut=c0='val*252/255'" +
			 vizContrast("jw") + ",format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split [jwb] [jwl]; "
			 "[jwb] crop=3:1:0:0,scale=1:1:flags=area,split [jwb1] [jwb2]; [jwl] scale=1:1:flags=area [jwl1]; ";
		// the phase: a row at the atlas' width over the atlas (its one frame kept), the phase's column by the frame number
		g += "[jwb1] scale=" + n(BW * PH) + ":1:flags=neighbor [jwtick]; movie=" + q(0) + ",format=gray [jwatlas]; "
			 "[jwtick] [jwatlas] vstack,crop=" + n(BW) + ":" + n(BH * ST) + ":x='" + n(BW) + "*mod(floor(n/" + n(PSTEP) + ")," + n(PH) +
			 ")':y=1 [jwph]; ";
		// the step and the loudness as one value; against the column the step's pose as classes, the others 0
		g += "[jwb2] [jwl1] bplut2=c0='min(" + n(ST - 1) + ",floor(x*" + n(ST - 1) + "/252+0.5))+" + n(ST) + "*(gte(y," + n(RING) +
			 ")+gte(y," + n(DROP) + "))',scale=" + n(BW) + ":" + n(BH * ST) + ":flags=neighbor [jwst]; "
			 "[jwst] [jwph] bplut2=c0='st(0,mod(y,16));st(1,floor(x/" + n(ST) + "));if(eq(floor(y/16),mod(x," + n(ST) + "))," + cls + ",0)' [jwf0]; ";
		// the eight poses folded into one (only one isn't 0)
		for (int i = 0, rows = BH * ST; rows > BH; i++, rows /= 2)
		{
			const std::string a = "[jwf" + n(i) + "]", t = "jwf" + n(i), half = n(rows / 2);
			g += a + " split [" + t + "a] [" + t + "b]; [" + t + "a] crop=" + n(BW) + ":" + half + ":0:0 [" + t + "c]; [" + t +
				 "b] crop=" + n(BW) + ":" + half + ":0:" + half + " [" + t + "d]; [" + t + "c] [" + t + "d] bplut2=c0='x+y' [jwf" + n(i + 1) + "]; ";
		}
		// laid over the scene
		g += "[jwf3] pad=" + W + ":" + H + ":" + n(X0 + L.dx) + ":" + n(Y0 + L.dy) + ",setsar=1 [jwJ]; movie=" + q(overCover ? 2 : 1) +
			 ",format=gray [jwsc]; [jwJ] [jwsc] bplut2=c0='if(x,x,y)',split=3 [jwc0] [jwc1] [jwc2]; ";

		if (overCover)
			return g + "[jwc0] scale=640:200:flags=neighbor [jwc0s]; [jwc0s] [jway] bplut2=c0=" + paint(&Yuv::y, "x", true) + " [jwY]; "
					   "[jwc1] scale=320:100:flags=neighbor [jwc1s]; [jwc1s] [jwau] bplut2=c0=" + paint(&Yuv::u, "x", true) + " [jwU]; "
					   "[jwc2] scale=320:100:flags=neighbor [jwc2s]; [jwc2s] [jwav] bplut2=c0=" + paint(&Yuv::v, "x", true) + " [jwV]; "
					   "[jwY] [jwU] [jwV] mergeplanes=0x001020:yuv420p,setsar=1 [strip]; [top] [strip] vstack [vo]";
		return g + "[jwc0] bplut=c0=" + paint(&Yuv::y, "val", false) + ",scale=640:480:flags=neighbor [jwY]; "
				   "[jwc1] bplut=c0=" + paint(&Yuv::u, "val", false) + ",scale=320:240:flags=neighbor [jwU]; "
				   "[jwc2] bplut=c0=" + paint(&Yuv::v, "val", false) + ",scale=320:240:flags=neighbor [jwV]; "
				   "[jwY] [jwU] [jwV] mergeplanes=0x001020:yuv420p,setsar=1 [vo]";
	}
}
