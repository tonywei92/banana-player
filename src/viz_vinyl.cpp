// Turntable: a record player seen from above on a dark plum ground. The record's grooves are 16 rings, the bass at the
// inside next to the label and the treble at the rim, each glowing in its own hue (violet inside, then blue, teal,
// green at the rim) as bright as its band; a faint sheen sweeps round as the record turns (33 rpm), the yellow label
// swells a little on the bass and a mark on it turns; the tonearm rests on the record.
// Everything is drawn at half size (320x240, the chroma's size) and only the luma scaled up. The stills (written once,
// writeVinylMaps): a remap table naming each pixel's band, and a "class" picture a plane (Y, U, V) whose value says
// what the pixel is: 0..111 a groove pixel (its band, and how much of the pixel the groove covers, for smooth rings),
// 112..127 the label's edge (its distance from the centre), 128..255 the still scene's own value in that plane (the
// plinth, the record, the label, the arm, anti-aliased). A frame then costs: the 16 bands measured and eased, one
// remap (each pixel its band's level), one lut2 a plane (level x class: the groove's colour at that level, the label's
// edge in or out, or the scene as it is; lut2's table is built once, so its long expression costs nothing a frame),
// and the turning parts: a still 142x142 picture of the sheen and the mark, rotated by the clock (rotate on that small
// piece only), padded and laid over the luma by one more lut2. YUV and gray only, no geq, nothing converted.
// Over the art the same turntable, the art (one frame, made round once) as the label.
#include "viz_more.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	// a new name whenever the stills change: they're cached by size
	static const char* VINYL_FILES[7] = {"vinyl1-x.pgm", "vinyl1-y.pgm", "vinyl1-cy.pgm", "vinyl1-cu.pgm", "vinyl1-cv.pgm",
										 "vinyl1-m.pgm", "vinyl1-a.pgm"};
	static const int HW = 320, HH = 240;		 // half size
	static const int N = 16;					 // the bands, one a groove
	// the record (full-size units): its centre, radius, the grooves' radii (62 + 7 a band) and half width
	static const double CX = 290, CY = 276, R = 175, G0 = 62, GSTEP = 7, GHALF = 2;
	// the label: 46 at rest, 54 on a full bass; its edge's class codes cover 44..56
	static const double LAB = 46, SWELL = 8, EDGE_FROM = 44, EDGE_STEP = 0.8;
	static const int RING_CODES = 112, EDGE_CODES = 16, SCENE = 128, COVER = 7;
	// the turning piece (half size, centred on the record) and the art's label (full size)
	static const int MS = 142, MX = 145 - MS / 2, MY = 138 - MS / 2, ART = 88;
	// the turning piece's values: 16 nothing (pad's and rotate's black may read as 16), 16 + the sheen, 64 + the mark
	static const int M_NONE = 16, M_MARK = 64;

	static const int BG = 0x1a1420, PLINTH = 0x2a2230, RECORD = 0x0c0b0e, LABEL = 0xffd23f, MARK = 0xc99a12,
					 ARM = 0xc8c3d0, PIVOT = 0x8f8a99, HEAD = 0xe8e2d3;

	struct Yuv { double y, u, v; };
	static Yuv yuvOf(double r, double g, double b)
	{
		// BT.601 limited range, as mpv reads a picture this size (r, g, b in 0..1)
		return {16 + 65.481 * r + 128.553 * g + 24.966 * b, 128 - 37.797 * r - 74.203 * g + 112.0 * b,
				128 + 112.0 * r - 93.786 * g - 18.214 * b};
	}
	static Yuv yuv(int rgb) { return yuvOf(((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0); }

	// a groove's colour at half lightness: hsl(280 - 12 a band, 90%, 50%), violet inside to green at the rim
	static Yuv grooveHalf(int band)
	{
		double h = std::fmod(280.0 - 12 * band + 360, 360) / 60, s = 0.9, c = s, x = c * (1 - std::fabs(std::fmod(h, 2) - 1)),
			   m = 0.5 - c / 2, r = 0, g = 0, b = 0;
		if (h < 1) { r = c; g = x; }
		else if (h < 2) { r = x; g = c; }
		else if (h < 3) { g = c; b = x; }
		else if (h < 4) { g = x; b = c; }
		else if (h < 5) { r = x; b = c; }
		else { r = c; b = x; }
		return yuvOf(r + m, g + m, b + m);
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

	// the distance from a point to a segment
	static double segDist(double px, double py, double ax, double ay, double bx, double by)
	{
		double dx = bx - ax, dy = by - ay, t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
		t = std::max(0.0, std::min(1.0, t));
		return std::hypot(px - ax - t * dx, py - ay - t * dy);
	}

	// the tonearm's colour at a full-size point, or -1: the arm (two segments 8 wide, round ends), the pivot over it, the
	// headshell
	static int armAt(double x, double y)
	{
		if (x >= 420 && x < 442 && y >= 362 && y < 378) return HEAD;
		if (std::hypot(x - 520, y - 130) < 20) return PIVOT;
		if (segDist(x, y, 520, 130, 505, 300) < 4 || segDist(x, y, 505, 300, 430, 370) < 4) return ARM;
		return -1;
	}

	// the still scene at a full-size point, grooves aside
	static int sceneAt(double x, double y)
	{
		int a = armAt(x, y);
		if (a >= 0) return a;
		double r = std::hypot(x - CX, y - CY);
		if (r < 5) return BG;		  // the spindle's hole
		if (r < LAB) return LABEL;
		if (r < R) return RECORD;
		// the plinth: a rounded rectangle (70, 80) 500x380, corners 18
		double qx = std::max(std::max(88 - x, x - 552), 0.0), qy = std::max(std::max(98 - y, y - 442), 0.0);
		if (x >= 70 && x < 570 && y >= 80 && y < 460 && std::hypot(qx, qy) < 18) return PLINTH;
		return BG;
	}

	bool writeVinylMaps(const std::string& dir)
	{
		std::string p[7];
		for (int k = 0; k < 7; k++) p[k] = dir + "/" + VINYL_FILES[k];
		const size_t sizes[7] = {pgmSize(HW, HH, 2), pgmSize(HW, HH, 2), pgmSize(HW, HH, 1), pgmSize(HW, HH, 1),
								 pgmSize(HW, HH, 1), pgmSize(MS, MS, 1), pgmSize(ART, ART, 1)};
		bool ready = true;
		for (int k = 0; k < 7 && ready; k++)
		{
			struct stat st;
			ready = stat(p[k].c_str(), &st) == 0 && (size_t)st.st_size == sizes[k];
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);

		std::vector<unsigned char> xm((size_t)HW * HH * 2, 0xff), ym((size_t)HW * HH * 2, 0), cl[3];
		for (auto& c : cl) c.assign((size_t)HW * HH, 0);
		const int SS = 6;	// subsamples a side (each half-size pixel is 2x2 full-size ones)
		for (int Y = 0; Y < HH; Y++)
			for (int X = 0; X < HW; X++)
			{
				size_t i = (size_t)Y * HW + X;
				double sum[3] = {0, 0, 0};
				int arm = 0, hits[N] = {0};
				for (int sy = 0; sy < SS; sy++)
					for (int sx = 0; sx < SS; sx++)
					{
						double fx = 2 * X + (sx + 0.5) * 2 / SS, fy = 2 * Y + (sy + 0.5) * 2 / SS;
						Yuv c = yuv(sceneAt(fx, fy));
						sum[0] += c.y; sum[1] += c.u; sum[2] += c.v;
						if (armAt(fx, fy) >= 0) { arm++; continue; }
						double r = std::hypot(fx - CX, fy - CY);
						int b = (int)std::lround((r - G0) / GSTEP);
						if (b >= 0 && b < N && std::fabs(r - (G0 + GSTEP * b)) < GHALF) hits[b]++;
					}
				double rc = std::hypot(2 * X + 1 - CX, 2 * Y + 1 - CY);
				int band = (int)(std::max_element(hits, hits + N) - hits), code;
				if (!arm && rc >= EDGE_FROM && rc < EDGE_FROM + EDGE_CODES * EDGE_STEP)
				{
					code = RING_CODES + std::min(EDGE_CODES - 1, (int)std::lround((rc - EDGE_FROM) / EDGE_STEP));
					band = 0;	// the label swells on the bass
				}
				else if (!arm && hits[band] > 0)
				{
					int k = (int)std::lround((double)hits[band] / (SS * SS) * COVER) - 1;
					code = band * COVER + std::max(0, std::min(COVER - 1, k));
				}
				else code = -1;
				if (code >= 0)
				{
					xm[i * 2] = 0; xm[i * 2 + 1] = (unsigned char)band;
					for (int k = 0; k < 3; k++) cl[k][i] = (unsigned char)code;
				}
				else
					for (int k = 0; k < 3; k++)
					{
						double v = sum[k] / (SS * SS);
						cl[k][i] = (unsigned char)(SCENE + std::max(0, std::min(127, (int)std::lround((v - 16) * 127 / 224))));
					}
			}

		// the turning piece at half size, centred on the record: the sheen (two soft arcs, opposite, from angle 0 to 0.5
		// as the preview draws them at the start; 12% white at most) and the label's mark (a dot 7 across at 26 out)
		std::vector<unsigned char> m((size_t)MS * MS), al((size_t)ART * ART);
		for (int y = 0; y < MS; y++)
			for (int x = 0; x < MS; x++)
			{
				double sheen = 0, mark = 0;
				for (int sy = 0; sy < 4; sy++)
					for (int sx = 0; sx < 4; sx++)
					{
						double dx = (x + (sx + 0.5) / 4 - MS / 2.0) * 2, dy = (y + (sy + 0.5) / 4 - MS / 2.0) * 2, r = std::hypot(dx, dy);
						if (std::hypot(dx - 26, dy) < 7) mark += 1.0 / 16;
						double rad = std::max(0.0, std::min({1.0, (r - 78) / 14, (142 - r) / 14}));
						double a = std::atan2(dy, dx), d = std::fabs(std::remainder(a - 0.25, M_PI));	// either arc
						double ang = d < 0.45 ? 0.5 + 0.5 * std::cos(d / 0.45 * M_PI) : 0;
						sheen += rad * ang / 16;
					}
				int v = M_NONE + (int)std::lround(0.12 * 255 * sheen);
				if (mark > 0) v = M_MARK + (int)std::lround(191 * mark);
				m[(size_t)y * MS + x] = (unsigned char)v;
			}
		// the art's label: a disc, its edge smooth
		for (int y = 0; y < ART; y++)
			for (int x = 0; x < ART; x++)
			{
				double r = std::hypot(x + 0.5 - ART / 2.0, y + 0.5 - ART / 2.0);
				al[(size_t)y * ART + x] = (unsigned char)std::lround(255 * std::max(0.0, std::min(1.0, ART / 2.0 - r + 0.5)));
			}
		return writePgm(p[0], HW, HH, 65535, xm) && writePgm(p[1], HW, HH, 65535, ym) && writePgm(p[2], HW, HH, 255, cl[0]) &&
			   writePgm(p[3], HW, HH, 255, cl[1]) && writePgm(p[4], HW, HH, 255, cl[2]) && writePgm(p[5], MS, MS, 255, m) &&
			   writePgm(p[6], ART, ART, 255, al);
	}

	// one plane's lut2 expression: x the pixel's level (0..252), y its class
	static std::string planeExpr(double Yuv::*c, double black, double white)
	{
		auto f = [](double v) { char b[32]; std::snprintf(b, sizeof b, "%.2f", v); return std::string(b); };
		const double rec = yuv(RECORD).*c, lab = yuv(LABEL).*c;
		// the groove's lightness: 10% dark, 65% on a full band (hsl: up to 50% the colour scaled from black, then towards
		// white), and the share of the pixel it covers over the record's black
		const std::string L = "(0.1+0.55*x/252)";
		std::string ring;
		for (int b = N - 1; b >= 0; b--)
		{
			const double h = grooveHalf(b).*c;
			std::string col = "if(lte(" + L + ",0.5)," + f(black) + "+" + f(h - black) + "*2*" + L + "," + f(h) + "+" +
							  f(white - h) + "*(2*" + L + "-1))";
			std::string px = f(rec) + "+(" + col + "-" + f(rec) + ")*(y-" + std::to_string(b * COVER - 1) + ")/" + std::to_string(COVER);
			ring = b == N - 1 ? px : "if(lt(y," + std::to_string((b + 1) * COVER) + ")," + px + "," + ring + ")";
		}
		// the label's edge: in where the label (46 + 8 x the bass) reaches past the pixel, smooth over a pixel
		std::string edge = "(" + f(rec) + "+" + f(lab - rec) + "*clip((" + f(LAB) + "+" + f(SWELL) + "*x/252-" + f(EDGE_FROM) +
						   "-(y-" + std::to_string(RING_CODES) + ")*" + f(EDGE_STEP) + ")/2+0.5,0,1))";
		return "'if(gte(y," + std::to_string(SCENE) + "),16+(y-" + std::to_string(SCENE) + ")*224/127,if(gte(y," +
			   std::to_string(RING_CODES) + ")," + edge + "," + ring + "))'";
	}

	std::string vinylGraph(bool overCover, const std::string& dir)
	{
		auto n = [](int v) { return std::to_string(v); };
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + VINYL_FILES[k], s); return s; };
		const int TOP = 8;	// as Peaks: the highest bands (16-24 kHz) measured and cut off
		const double markY = yuv(MARK).y;
		char sheen[160];
		// the luma under the turning piece: the mark mixed in over 64.., else lifted towards white by the sheen
		std::snprintf(sheen, sizeof sheen, "'if(gte(y,%d),x+(%.2f-x)*(y-%d)/191,x+(235-x)*max(y-%d,0)/255)'", M_MARK, markY, M_MARK,
					  M_NONE);
		std::string g = "[aid1] asplit [ao] [a]; ";
		// the bands: measured as Peaks does (one value each, 0..252), then eased as Pocket console's bars: up at once,
		// down over ~0.15 s for the whole range (an exp/log lagfun, 16 values a frame), 60 a second
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("vn") + ","
			 "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split [vnl] [vnk]; "
			 // each pixel its band's level (the grooves and the label's edge; 0 elsewhere)
			 "movie=" + q(0) + " [vnx]; movie=" + q(1) + " [vny]; "
			 "[vnl] [vnx] [vny] remap=format=gray,setsar=1,split=3 [vnL0] [vnL1] [vnL2]; "
			 "movie=" + q(2) + " [vncy]; movie=" + q(3) + " [vncu]; movie=" + q(4) + " [vncv]; "
			 "[vnL0] [vncy] lut2=c0=" + planeExpr(&Yuv::y, 16, 235) + " [vnY0]; "
			 "[vnL1] [vncu] lut2=c0=" + planeExpr(&Yuv::u, 128, 128) + " [vnU]; "
			 "[vnL2] [vncv] lut2=c0=" + planeExpr(&Yuv::v, 128, 128) + " [vnV]; "
			 // the turning piece: the still taken once a frame (lut2 on a small frame of the bands' clock), rotated
			 // clockwise at 33 rpm by the frame's time, padded to its place
			 "movie=" + q(5) + " [vnm]; "
			 "[vnk] scale=" + n(MS) + ":" + n(MS) + ":flags=neighbor [vnt]; [vnt] [vnm] lut2=c0='y',rotate=a=3.5*t:c=black,"
			 "pad=" + n(HW) + ":" + n(HH) + ":" + n(MX) + ":" + n(MY) + ":black [vnM]; "
			 "[vnY0] [vnM] lut2=c0=" + sheen + ",scale=640:480:flags=bilinear [vnY]; "
			 "[vnY] [vnU] [vnV] mergeplanes=0x001020:yuv420p";
		if (!overCover) return g + " [vo]";
		// the art as the label: square, cut round (once: it is one frame), laid over the label's middle
		const int ax = (int)CX - ART / 2, ay = (int)CY - ART / 2;
		return g + " [vndeck]; "
				   "[vid1] scale=" + n(ART) + ":" + n(ART) + ":force_original_aspect_ratio=increase,crop=" + n(ART) + ":" + n(ART) +
			   ",setsar=1,format=yuva420p,settb=AVTB [vnart]; movie=" + q(6) + " [vnal]; [vnart] [vnal] alphamerge [vnlab]; "
			   "[vndeck] [vnlab] overlay=x=" + n(ax) + ":y=" + n(ay) + ":format=yuv420:eof_action=repeat [vo]";
	}
}
