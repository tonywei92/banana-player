// Aurora: northern lights over a dark, snowy ridge, drawn at 160x120 and scaled x4 (nearest: soft big pixels). The sky
// is cut into 16 vertical stretches, one band each (the bass on the left); in each a curtain rises from a slowly
// wavering foot line, as tall and as bright as its band's eased level, green at its foot fading to violet and pink at
// its top, in vertical rays. A few still stars; the ridge and its snow line along the bottom, still.
//
// How a frame is made, all of it at 160x120 or smaller (the stills are written once, writeAuroraMaps):
// - the 16 bands measured (showfreqs at 60/s), vizContrast, eased (the exp/log lagfun: up at once, down in ~0.15 s);
// - the motion: four still rows of sines (256x4: two for the foot, two for the rays) are copied once a frame (a lut2
//   against the level row, which has a frame each 1/60 s: a still alone gives one frame) and each row cropped at an
//   offset that moves with t, the two foot rows one way and the other, the two ray rows likewise. A lut2 sums the foot
//   rows into the foot's height per column, another the ray rows into each column's ray brightness (4 steps);
// - one lut2 packs each column's level (6 bits) and ray brightness (2 bits); that row and the foot row are stretched
//   down the picture (pad + smear);
// - one lut2 against a still "ramp" (each pixel's row, a star flag, or the ridge/snow) gives each pixel's height above
//   the foot (or ridge/snow), and one lut2 of that against the packed row the pixel's palette code: 0 the sky, a
//   curtain pixel's glow (30 steps) and its height in the curtain (8 steps), the star, the ridge, the snow;
// - a lut a plane turns the codes into Y, U and V (the colour worked out once per code, 256 of them, at start), the
//   luma scaled x4 and the chroma x2 (nearest), merged. YUV and gray only: no geq, no colour conversion per frame.
// Over the art: its top 640x280 as it is, and in the 640x200 strip under it (160x50) the same curtains with no sky,
// stars or ridge: each plane of the art's strip is blended toward the curtain's colour by its glow (a lut2 a plane).
#include "viz_more.h"
#include "settings.h"

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
		const char* AUR_FILES[3] = {"aurora1-rows.pgm", "aurora1-ramp.pgm", "aurora1-sramp.pgm"};
		const int W = 160, H = 120, SH = 50;	// the drawing's size alone, and the strip's height (x4 on screen)
		const int RW = 256;						// the still rows' width: 160 and a period of each sine
		// the sines' periods (pixels at 160 wide): the foot's two waves, the rays' two
		const int PA = 90, PB = 33, PS1 = 7, PS2 = 11;
		// the codes: 0 the sky; 8*glow+height for a curtain pixel (glow 1..29); then the star, the ridge, its snow
		const int GLOWS = 29, STAR = 240, RIDGE = 254, SNOW = 255;

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

		size_t pgmSize(int w, int h)
		{
			return ("P5\n" + n(w) + " " + n(h) + "\n255\n").size() + (size_t)w * h;
		}

		// the colours (sRGB 0..255): the night sky, a star, the ridge and its snow, as in the approved preview
		struct Rgb { double r, g, b; };
		const Rgb SKY = {4, 8, 22}, STARC = {180, 180, 180}, RIDGEC = {14, 20, 34}, SNOWC = {200, 215, 235};

		// a curtain pixel's colour: the sky plus its glow g (0..1) of a hue by its height k (0 the foot, green; 1 the
		// top, violet-pink)
		Rgb curtain(double g, double k)
		{
			return {std::min(255.0, SKY.r + g * (40 + 140 * k)), std::min(255.0, SKY.g + g * (230 - 150 * k)),
					std::min(255.0, SKY.b + g * (120 + 100 * k))};
		}

		// BT.601 limited range, as mpv reads a picture this size; c: 0 Y, 1 U, 2 V
		int comp(const Rgb& c, int which)
		{
			double v = which == 0 ? 16 + (65.481 * c.r + 128.553 * c.g + 24.966 * c.b) / 255
					 : which == 1 ? 128 + (-37.797 * c.r - 74.203 * c.g + 112 * c.b) / 255
								  : 128 + (112 * c.r - 93.786 * c.g - 18.214 * c.b) / 255;
			return (int)std::lround(std::max(0.0, std::min(255.0, v)));
		}

		// a code's glow and height in the curtain (for 1..239)
		double codeGlow(int code) { return (code / 8) / (double)GLOWS; }
		double codeK(int code) { return ((code % 8) + 0.5) / 8; }

		// a lut expression (of var) giving each code its value, a binary search over the codes: the values are worked
		// out here, 256 of them, so the expression is plain comparisons. v(code) the value; lo..hi the codes covered
		template <class V> std::string table(const std::string& var, V v, int lo, int hi)
		{
			// a run of the same value is one leaf
			bool same = true;
			for (int c = lo + 1; c <= hi && same; c++) same = v(c) == v(lo);
			if (same) return v(lo);
			int mid = (lo + hi) / 2;
			return "if(lte(" + var + "," + n(mid) + ")," + table(var, v, lo, mid) + "," + table(var, v, mid + 1, hi) + ")";
		}
	}

	bool writeAuroraMaps(const std::string& dir)
	{
		std::string p[3];
		for (int k = 0; k < 3; k++) p[k] = dir + "/" + AUR_FILES[k];
		const size_t sizes[3] = {pgmSize(RW, 4), pgmSize(W, H), pgmSize(W, SH)};
		bool ready = true;
		for (int k = 0; k < 3 && ready; k++)
		{
			struct stat st;
			ready = stat(p[k].c_str(), &st) == 0 && (size_t)st.st_size == sizes[k];
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);

		// the rows: a sine each (128 + 127 sin), periodic in its period so a crop at any offset wraps seamlessly
		std::vector<unsigned char> rows((size_t)RW * 4);
		const int per[4] = {PA, PB, PS1, PS2};
		for (int r = 0; r < 4; r++)
			for (int x = 0; x < RW; x++)
				rows[(size_t)r * RW + x] = (unsigned char)std::lround(128 + 127 * std::sin(2 * M_PI * x / per[r]));

		// the ramp alone: each pixel's row (0..119), +128 a star; the ridge (a dark hill line) and its 2-row snow cap
		std::vector<unsigned char> ramp((size_t)W * H), sramp((size_t)W * SH);
		uint32_t s = 0xa0207a;
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				s = s * 1664525u + 1013904223u;
				double ridge = 92 + 7 * std::sin(x * 0.045 + 1) + 4 * std::sin(x * 0.13);
				int v = y;
				if (y > ridge) v = y < ridge + 2 ? SNOW : RIDGE;
				else if (y < 90 && (s >> 8) % 1000 < 8) v = y + 128;
				ramp[(size_t)y * W + x] = (unsigned char)v;
			}
		// over the art: the rows alone (no stars, no ridge: the art is there)
		for (int y = 0; y < SH; y++)
			for (int x = 0; x < W; x++) sramp[(size_t)y * W + x] = (unsigned char)y;
		return writePgm(p[0], RW, 4, rows) && writePgm(p[1], W, H, ramp) && writePgm(p[2], W, SH, sramp);
	}

	std::string auroraGraph(bool overCover, const std::string& dir)
	{
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + AUR_FILES[k], s); return s; };
		const int N = 16, TOP = 8;	// as Peaks: the highest bands (16-24 kHz) measured and cut off
		const int rows = overCover ? SH : H, mid = rows / 2 - 1;
		// the foot line (rows from the top) and its two waves' sizes; the curtain's height: lo + span x the level
		const double base = overCover ? 45 : 62, ampA = overCover ? 2.5 : 6, ampB = overCover ? 1.5 : 3;
		const double hLo = overCover ? 6 : 10, hSpan = overCover ? 38 : 50;
		auto stretch = [&](const std::string& in, const std::string& out) {
			return "[" + in + "] pad=" + n(W) + ":" + n(rows) + ":0:" + n(mid) + ",fillborders=top=" + n(mid) + ":bottom=" +
				   n(rows - 1 - mid) + ":mode=smear,setsar=1 [" + out + "]; ";
		};

		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [auart1] [auart2]; [auart1] crop=640:280:0:0 [top]; "
				 "[auart2] crop=640:200:0:280,extractplanes=y+u+v [auay] [auau] [auav]; ";
		// the bands: measured (one value each, 0..252), contrast, eased as Night city's
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("au") + ","
			 "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split [aul1] [aul2]; "
			 // the still rows copied once a frame (the level row is the clock), each cropped where t has moved it:
			 // the foot's waves one way and the other, the rays shimmering back and forth as they drift
			 "movie=" + q(0) + " [aurw]; [aul1] scale=" + n(RW) + ":4:flags=neighbor [auck]; "
			 "[auck] [aurw] lut2=c0='y',split=4 [aur0] [aur1] [aur2] [aur3]; "
			 "[aur0] crop=" + n(W) + ":1:x='mod(8.6*t," + n(PA) + ")':y=0 [auca]; "
			 "[aur1] crop=" + n(W) + ":1:x='" + n(PB) + "-mod(5.8*t," + n(PB) + ")':y=1 [aucb]; "
			 "[aur2] crop=" + n(W) + ":1:x='mod(" + n(PS1 * 10) + "+1.2*t+3.3*sin(t)," + n(PS1) + ")':y=2 [aucc]; "
			 "[aur3] crop=" + n(W) + ":1:x='" + n(PS2) + "-mod(2.1*t+2*sin(0.7*t+1)+" + n(PS2 * 10) + "," + n(PS2) + ")':y=3 [aucd]; "
			 // the foot's row (each column's foot, in rows), and the rays' brightness (0..3)
			 "[auca] [aucb] lut2=c0='clip(round(" + f(base) + "+" + f(ampA / 127) + "*(x-128)+" + f(ampB / 127) + "*(y-128)),0,250)' [aufr]; "
			 "[aucc] [aucd] lut2=c0='clip(round(1.5+1.5*(0.6*(x-128)+0.4*(y-128))/127),0,3)' [austr]; "
			 // each column its band's level (6 bits) and its ray (2 bits)
			 "[aul2] scale=" + n(W) + ":1:flags=neighbor [aulv]; "
			 "[aulv] [austr] lut2=c0='4*round(x*63/252)+y' [aupk]; " +
			 stretch("aupk", "auP") + stretch("aufr", "auF") +
			 "movie=" + q(overCover ? 2 : 1) + " [aurp]; "
			 // each pixel's height above the foot (0: at or under it, 63 the most), +64 a star; the ridge and snow as
			 // they are
			 "[auF] [aurp] lut2=c0='if(gte(y," + n(RIDGE) + "),y,clip(x-y+128*gte(y,128),0,63)+64*gte(y,128))' [auup]; ";
		// the pixel's code: in its curtain (0 < up < h) its glow, (1-k) x the ray x (0.4 + 0.6 level), and k = up/h
		{
			const std::string lv = "(floor(x/4)/63)", ray = "(0.2+0.8*mod(x,4)/3)", up = "mod(y,64)";
			const std::string h = "(" + f(hLo) + "+" + f(hSpan) + "*" + lv + ")";
			const std::string k = "(" + up + "/" + h + ")";
			const std::string gq = "round(" + n(GLOWS) + "*(1-" + k + ")*" + ray + "*(0.4+0.6*" + lv + "))";
			const std::string lit = "gt(" + up + ",0)*lt(" + up + "," + h + ")";
			const std::string code = "if(" + lit + "*gt(" + gq + ",0)," + gq + "*8+min(7,floor(8*" + k + ")),0)";
			// a star shows through the faint glow
			const std::string withStar = "if(gte(y,64)*lt(" + lit + "*" + gq + ",6)," + n(STAR) + "," + code + ")";
			g += "[auP] [auup] lut2=c0='if(gte(y," + n(RIDGE) + "),y," + withStar + ")'";
		}

		if (overCover)
		{
			// each plane of the art blended toward the curtain's own colour (its full glow at its height) by its glow
			auto blend = [&](int which) {
				// a code's colour and its weight, then a lut2 of code (x) and the art (y)
				auto c = [&](int code) { return n(comp(curtain(1, codeK(code)), which)); };
				auto a = [&](int code) { return f(std::min(1.0, 1.6 * codeGlow(code))); };
				// codes 8..239 by their glow (code/8) and their height (code%8): the two looked up separately
				std::string colour = table("mod(x,8)", [&](int c8) { return c(c8); }, 0, 7);
				std::string weight = table("floor(x/8)", [&](int g8) { return a(g8 * 8); }, 0, GLOWS);
				return "'if(lt(x,8)+gte(x," + n(STAR) + "),y,y+" + weight + "*(" + colour + "-y))'";
			};
			return g + ",split=3 [auc0] [auc1] [auc2]; "
					   "[auc0] scale=640:200:flags=neighbor [aucy]; [aucy] [auay] lut2=c0=" + blend(0) + " [auY]; "
					   "[auc1] scale=320:100:flags=neighbor [aucu]; [aucu] [auau] lut2=c0=" + blend(1) + " [auU]; "
					   "[auc2] scale=320:100:flags=neighbor [aucv]; [aucv] [auav] lut2=c0=" + blend(2) + " [auV]; "
					   "[auY] [auU] [auV] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		}
		// alone: each code's colour, a lut a plane at 160x120, then scaled up (nearest)
		auto plane = [&](int which) {
			return "'" + table("val", [&](int code) {
				Rgb c = code == 0 ? SKY : code == STAR ? STARC : code == RIDGE ? RIDGEC : code == SNOW ? SNOWC
					  : code < 8 || code > 239 ? SKY : curtain(codeGlow(code), codeK(code));
				return n(comp(c, which));
			}, 0, 255) + "'";
		};
		return g + ",split=3 [auc0] [auc1] [auc2]; "
				   "[auc0] lut=c0=" + plane(0) + ",scale=640:480:flags=neighbor [auY]; "
				   "[auc1] lut=c0=" + plane(1) + ",scale=320:240:flags=neighbor [auU]; "
				   "[auc2] lut=c0=" + plane(2) + ",scale=320:240:flags=neighbor [auV]; "
				   "[auY] [auU] [auV] mergeplanes=0x001020:yuv420p [vo]";
	}
}
