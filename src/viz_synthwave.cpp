#include "viz_more.h"
#include "settings.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sys/stat.h>
#include <vector>

// Synthwave: a retro sunset (a purple to pink sky with stars, a big striped sun on the horizon, a dark floor with a
// magenta perspective grid) and on the horizon 16 neon bars from the music, pink at the bottom to cyan at the top, each
// with a 2-pixel glow either side and a white cap that holds its peak and falls back slowly.
// The scene is a still picture, written once (writeSynthwaveMaps) and read with movie= as one frame, converted to YUV
// once. Each frame only the 640x200 band the bars stand in is drawn, as Peaks' over-art strip is: the bars measured and
// eased (lagfun in an exp/log domain, as Pocket console), each pixel of the band classified at 320x100 by lut2 against
// a still "height" picture, then each plane is a lut2 of that class and the scene's (or the art's) plane: the scene
// where nothing is lit, the bar's colour by its row, a glow half way between, white for a cap. Then the scene's still
// top and bottom stacked around it. YUV only, nothing converted per frame.
namespace bp
{
	// a new name whenever the picture changes: it's cached by size
	static const char* SW_SCENE = "synthwave2.ppm";
	static const int SW_W = 640, SW_H = 480, SW_HORIZON = 300;
	// the bars' band: 200 rows standing on the horizon (alone) or on the screen's last row (over the art)
	static const int SW_BAND = 200, SW_BAND_Y = SW_HORIZON - SW_BAND;

	namespace
	{
		struct Rgb { double r, g, b; };
		Rgb hex(uint32_t c) { return {((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0}; }
		Rgb mix(Rgb a, Rgb b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
		Rgb add(Rgb a, Rgb b, double k) { return {a.r + b.r * k, a.g + b.g * k, a.b + b.b * k}; }
		double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
		double smooth(double e0, double e1, double v)
		{
			double t = clamp01((v - e0) / (e1 - e0));
			return t * t * (3 - 2 * t);
		}

		// the sun: its centre and radius, and its stripes (the rows cut out of its lower half, wider toward the bottom)
		const double SUN_X = 320, SUN_Y = 232, SUN_R = 118;
		bool sunStripe(double y)
		{
			double from = SUN_Y + 4, period = 16;
			for (int i = 0; from < SW_HORIZON + 4; i++)
			{
				double gap = 1.5 + i * 1.3;
				if (y >= from && y < from + gap) return true;
				from += period;
				period = std::max(9.0, period - 1.2);
			}
			return false;
		}

		// one sample of the scene (x, y: a point in the 640x480 picture)
		Rgb sceneAt(double x, double y)
		{
			const Rgb top = hex(0x1a0b2e), midSky = hex(0x5b1a6e), low = hex(0xc2307a), floorC = hex(0x12061f),
					  grid = hex(0xff4fd8), sunTop = hex(0xffe066), sunLow = hex(0xff4f8b), stripe = hex(0x4a1262);
			if (y < SW_HORIZON)
			{
				double t = y / SW_HORIZON;
				Rgb c = t < 0.55 ? mix(top, midSky, t / 0.55) : mix(midSky, low, (t - 0.55) / 0.45);
				double d = std::hypot(x - SUN_X, y - SUN_Y);
				if (d < SUN_R)
				{
					if (sunStripe(y)) return stripe;
					return mix(sunTop, sunLow, clamp01((y - (SUN_Y - SUN_R)) / (SW_HORIZON - (SUN_Y - SUN_R))));
				}
				// its halo, and the horizon's light
				c = add(c, sunLow, 0.42 * std::exp(-(d - SUN_R) / 26));
				c = add(c, grid, 0.55 * std::exp(-(SW_HORIZON - y) / 3.0));
				return c;
			}
			double h = y - SW_HORIZON, depth = SW_H - SW_HORIZON;
			Rgb c = add(floorC, grid, 0.30 * std::exp(-h / 14));
			// the lines: across, at even steps of depth (further apart toward the viewer); and down, from the vanishing
			// point at the horizon's centre, 64 pixels apart at the bottom. Faded near the horizon, where they crowd
			double z = depth / std::max(h, 0.01), z0 = 0.85, dz = 0.55;
			double k = std::max(0.0, std::round((z - z0) / dz));
			double dAcross = std::fabs(h - depth / (z0 + k * dz));
			const double G = 64;
			double u = (x - SUN_X) * depth / (G * std::max(h, 0.01)), j = std::round(u);
			double dDown = std::fabs(u - j) * G * h / depth / std::sqrt(1 + (j * G / depth) * (j * G / depth));
			double d = std::min(dAcross, dDown);
			double fade = smooth(0, 90, h) * 0.9 + 0.1;
			double core = clamp01(1.1 - d), glow = 0.45 * std::exp(-d / 2.2);
			c = add(c, grid, (core * 0.9 + glow) * fade);
			c = add(c, hex(0xffffff), core * 0.25 * fade);
			c = add(c, grid, 0.6 * std::exp(-h / 2.0));	// the horizon line
			return c;
		}

		struct Yuv { int y, u, v; };
		// BT.601 limited, as mpv reads a picture this size
		Yuv yuv(uint32_t c)
		{
			Rgb p = hex(c);
			return {(int)std::lround(16 + 65.481 * p.r + 128.553 * p.g + 24.966 * p.b),
					(int)std::lround(128 - 37.797 * p.r - 74.203 * p.g + 112.0 * p.b),
					(int)std::lround(128 + 112.0 * p.r - 93.786 * p.g - 18.214 * p.b)};
		}
		std::string n(int v) { return std::to_string(v); }
	}

	bool writeSynthwaveMaps(const std::string& dir)
	{
		std::string header = "P6\n" + n(SW_W) + " " + n(SW_H) + "\n255\n";
		size_t bytes = (size_t)SW_W * SW_H * 3;
		std::string path = dir + "/" + SW_SCENE;
		struct stat st;
		if (stat(path.c_str(), &st) == 0 && (size_t)st.st_size == header.size() + bytes) return true;
		std::vector<unsigned char> px(bytes);
		// 2x2 samples a pixel (the sun's edge, the grid's lines)
		for (int y = 0; y < SW_H; y++)
			for (int x = 0; x < SW_W; x++)
			{
				Rgb s{0, 0, 0};
				for (int sy = 0; sy < 2; sy++)
					for (int sx = 0; sx < 2; sx++) s = add(s, sceneAt(x + 0.25 + 0.5 * sx, y + 0.25 + 0.5 * sy), 0.25);
				size_t i = ((size_t)y * SW_W + x) * 3;
				px[i] = (unsigned char)std::lround(clamp01(s.r) * 255);
				px[i + 1] = (unsigned char)std::lround(clamp01(s.g) * 255);
				px[i + 2] = (unsigned char)std::lround(clamp01(s.b) * 255);
			}
		// the stars: small dots in the upper sky, fewer and dimmer toward the horizon
		uint32_t seed = 0x5eed1234;
		auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0; };
		for (int s = 0; s < 90; s++)
		{
			int x = (int)(rnd() * SW_W), y = (int)(rnd() * rnd() * 190);
			double b = (0.35 + 0.65 * rnd()) * (1 - y / 230.0);
			if (std::hypot(x - SUN_X, y - SUN_Y) < SUN_R + 20) continue;
			size_t i = ((size_t)y * SW_W + x) * 3;
			for (int k = 0; k < 3; k++) px[i + k] = (unsigned char)std::max<int>(px[i + k], (int)(255 * b));
		}
		mkdir(dir.c_str(), 0755);
		std::string tmp = path + ".tmp";
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << header;
		out.write((const char*)px.data(), (std::streamsize)px.size());
		if (!out.flush()) return false;
		out.close();
		return rename(tmp.c_str(), path.c_str()) == 0;
	}

	std::string synthwaveGraph(bool overCover, const std::string& mapsDir)
	{
		std::string scene;
		if (!overCover && !lavfiPath(mapsDir + "/" + SW_SCENE, scene)) return visualizerGraph(VIS_PEAKS, overCover);
		// the band at half size: 16 bars of 20 columns (3 empty, a glow, 12 lit, a glow, 3 empty), 100 rows of
		// heights 1..100 (the bars reach 96, the caps 2 more above)
		const int N = 16, BW = 20, W = N * BW, H = SW_BAND / 2, TOP = 2, MAX = 96, CAP = 2;
		const int mid = H / 2 - 1;
		const std::string stretch = "scale=" + n(W) + ":1:flags=neighbor,pad=" + n(W) + ":" + n(H) + ":0:" + n(mid) +
									",fillborders=top=" + n(mid) + ":bottom=" + n(H - 1 - mid) + ":mode=smear,setsar=1";
		// into the exp domain, and back to 0..MAX
		const std::string toExp = "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',";
		const std::string fromExp = ",lut=c0='clip(257*" + n(MAX) + "*(252+24*log(max(val,1)/65535))/252,0,257*" + n(MAX) +
									")',format=gray," + stretch;
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [swa1] [swa2]; [swa1] crop=640:280:0:0 [swtop]; "
				 "[swa2] crop=640:200:0:280,extractplanes=y+u+v [swby] [swbu] [swbv]; ";
		else
			g += "movie=" + scene + ",format=yuv420p,settb=AVTB,split=3 [sws1] [sws2] [sws3]; "
				 "[sws1] crop=640:" + n(SW_BAND_Y) + ":0:0 [swtop]; "
				 "[sws2] crop=640:" + n(SW_BAND) + ":0:" + n(SW_BAND_Y) + ",extractplanes=y+u+v [swby] [swbu] [swbv]; "
				 "[sws3] crop=640:" + n(SW_H - SW_HORIZON) + ":0:" + n(SW_HORIZON) + " [swbot]; ";
		// measured as Peaks (TOP more bands than drawn: the highest ninth, where music has next to nothing); the caps
		// fall over ~2.4 s, the bars ease down over ~0.4 s (up at once), as Pocket console's
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255',split [swv] [swp]; "
			 "[swp] " + toExp + "lagfun=decay=0.8679" + fromExp + " [swP]; "
			 "[swv] " + toExp + "lagfun=decay=0.417" + fromExp + " [swV]; "
			 // each pixel's height: 1..100 in a bar's columns, 101..200 (100 + height) in its glows, 253 empty
			 "color=black:s=" + n(W) + "x" + n(H) + ":r=1:d=1,format=gray,geq=lum='if(lt(mod(X," + n(BW) + "),3)+gte(mod(X," +
			 n(BW) + ")," + n(BW - 3) + "),253,if(eq(mod(X," + n(BW) + "),3)+eq(mod(X," + n(BW) + ")," + n(BW - 4) + ")," + n(2 * H) +
			 "-Y," + n(H) + "-Y))' [swR]; "
			 // a cap: within CAP heights over the cap's value (254 on the bar, 255 on its glow)
			 "[swP] [swR] lut2=c0='if(lte(y,100),if(gt(y,x)*lte(y,x+" + n(CAP) + "),254,y),if(lte(y,200),if(gt(y-100,x)*lte(y-100,x+" +
			 n(CAP) + "),255,y),y))' [swQ]; "
			 // what each pixel is: 0 nothing, 1..100 a bar at that height, 101..200 its glow, 254/255 a cap and its glow
			 "[swV] [swQ] lut2=c0='if(gte(y,254),y,if(lte(y,100),if(lte(y,x),y,0),if(lte(y,200),if(lte(y-100,x),y,0),0)))',"
			 "split=3 [swc0] [swc1] [swc2]; ";
		// each plane: the scene's (or the art's) where nothing, the bar's colour by its height (pink to cyan), its glow
		// a little over half way from the scene to it, the cap white
		const Yuv pink = yuv(0xff4fd8), cyan = yuv(0x5ef2ff);
		auto paint = [&](int lo, int hi, int white) {
			std::string col = n(lo) + "+(" + n(hi - lo) + ")*min(H," + n(MAX) + ")/" + n(MAX);
			auto at = [&](const char* h) {
				std::string s = col;
				for (size_t p; (p = s.find('H')) != std::string::npos;) s.replace(p, 1, h);
				return s;
			};
			return "'if(eq(x,0),y,if(lte(x,100)," + at("x") + ",if(lte(x,200),0.45*y+0.55*(" + at("(x-100)") +
				   "),if(eq(x,254)," + n(white) + ",if(eq(x,255),0.5*y+0.5*" + n(white) + ",y)))))'";
		};
		g += "[swc0] scale=640:" + n(SW_BAND) + ":flags=neighbor [swcy]; "
			 "[swcy] [swby] lut2=c0=" + paint(pink.y, cyan.y, 235) + " [swY]; "
			 "[swc1] [swbu] lut2=c0=" + paint(pink.u, cyan.u, 128) + " [swU]; "
			 "[swc2] [swbv] lut2=c0=" + paint(pink.v, cyan.v, 128) + " [swW]; "
			 "[swY] [swU] [swW] mergeplanes=0x001020:yuv420p [swstrip]; ";
		if (overCover) return g + "[swtop] [swstrip] vstack [vo]";
		return g + "[swtop] [swstrip] [swbot] vstack=inputs=3 [vo]";
	}
}
