// Campfire: a pixel-art campfire on a night sky, drawn on an 80x60 grid of 8-pixel "pixels" (a 1-pixel darker line
// between them). 16 flame columns of 3 pixels side by side, each as tall as its band's eased level (taller in the
// middle), their edges broken by a noise field that rises over time (the flicker), coloured by heat: a pale yellow
// core, yellow, orange, red, dark red at the tips. Logs under the fire and a few dim stars (still), and sparks rising
// over it with the treble.
// Everything is worked out at 80x60 (or 80x25 over the art) and scaled up (nearest) at the end, as Pocket console is:
//  - the level row (16 values, eased) stretched to 80 columns and down the rows;
//  - one lut2 with a still "shape" map (each fire pixel's height above the logs divided by its column's taper, and its
//    sky zone for sparks) gives each pixel's base heat, 1 - y/h, as Campfire's preview computes it;
//  - that picture stacked on the noise and spark stills (vstack repeats the stills' one frame), split, and cropped:
//    the base heat as it is, the noise and the sparks from a row that moves with the frame number (crop's y is a
//    per-frame scalar), so both scroll upward though they are stills, never loops;
//  - a lut2 adds the noise to the heat and picks the palette entry; another lights the sparks whose threshold the
//    treble (bands 12-15, averaged to one value) passes; a third lays them together;
//  - alone, a lut2 a plane picks the palette's colour or the scene's own pixel (sky, stars, logs: stills at 80x60),
//    the luma scaled x8 and the grid's lines darkened by one more lut2, the chroma scaled x4. Over the art, the same
//    classes scaled into the 640x200 strip, the grid's lines and every pixel without a flame or a spark the art's.
// YUV and gray only, no geq per frame (the grid's still is made once by geq, as Pocket console's).
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
	static const char* FIRE_FILES[7] = {"fire1-a.pgm", "fire1-as.pgm", "fire1-n.pgm", "fire1-s.pgm",
										"fire1-y.pgm", "fire1-u.pgm", "fire1-v.pgm"};
	static const int GW = 80, GH = 60, SR = 25;	// the grid alone, and the strip's rows over the art
	static const int NP = 120, SP = 36;			// the noise's and the sparks' periods, in rows
	static const int NH = GH + NP, SPH = GH + SP;	// their stills' heights (a period and a picture's height more)
	// the shape map: zone * 64 + code; code 0..62 the pixel's height over the logs divided by its column's taper (x KC),
	// 63 no fire there. Zone: 0 no sparks, 1 low sky (yellow sparks), 2 high sky (orange)
	static const int NOFIRE = 63;
	static const double KC_ALONE = 0.62, KC_STRIP = 1.15;
	// the fire alone: 48 columns from 16, its base on row 49 (the logs from 50), up to 46 rows high
	static const int X0 = 16, FW = 48, BASE = 49, FROWS = 46;

	struct Yuv { int y, u, v; };
	static Yuv yuv(int rgb)
	{
		// BT.601 limited range, as mpv reads a picture this size
		double r = ((rgb >> 16) & 255) / 255.0, g = ((rgb >> 8) & 255) / 255.0, b = (rgb & 255) / 255.0;
		return {(int)std::lround(16 + 65.481 * r + 128.553 * g + 24.966 * b),
				(int)std::lround(128 - 37.797 * r - 74.203 * g + 112.0 * b),
				(int)std::lround(128 + 112.0 * r - 93.786 * g - 18.214 * b)};
	}
	// the heat's palette, coolest (the tips) to hottest (the core); the sparks
	static const int FIRE[7] = {0x1a0505, 0x5c0d06, 0xa8200a, 0xe04a10, 0xf78a1d, 0xffc23a, 0xfff1a8};
	static const int SPARK_LOW = 0xffd76a, SPARK_HIGH = 0xe0661e;
	static const int SKY = 0x07060c, STAR = 0x3a3550, STAR_DIM = 0x26233a, LOG = 0x4a2a14, BARK = 0x2c170b, GROUND = 0x16100c;

	// the preview's fixed noise (Park-Miller, seed 7): 80x120 values in 0..1
	static std::vector<double> noiseField()
	{
		std::vector<double> a((size_t)GW * NP);
		uint64_t seed = 7;
		for (double& v : a) { seed = (seed * 16807) % 2147483647; v = (double)seed / 2147483647; }
		return a;
	}

	static bool writePgm(const std::string& path, int w, int h, const std::vector<unsigned char>& data)
	{
		std::string tmp = path + ".tmp";
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << "P5\n" << w << " " << h << "\n255\n";
		out.write((const char*)data.data(), (std::streamsize)data.size());
		if (!out.flush()) return false;
		out.close();
		return rename(tmp.c_str(), path.c_str()) == 0;
	}

	static size_t pgmSize(int w, int h)
	{
		std::string header = "P5\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
		return header.size() + (size_t)w * h;
	}

	// the shape map for rows pixels high, the fire's base on row base, its spark zones' rows (high from 0, low from
	// lowTop, to zoneEnd), the code scaled by kc
	static std::vector<unsigned char> shapeMap(int rows, int base, int frows, int lowTop, int zoneEnd, double kc)
	{
		std::vector<unsigned char> m((size_t)GW * rows);
		for (int r = 0; r < rows; r++)
			for (int c = 0; c < GW; c++)
			{
				int zone = (c >= 20 && c < 60 && r < zoneEnd) ? (r < lowTop ? 2 : 1) : 0;
				int code = NOFIRE, x = c - X0, y = base - r;
				if (x >= 0 && x < FW && y >= 0 && y < frows)
				{
					// the flames lean in towards the middle and taper at the sides (the preview's shape)
					double s = 1 - std::pow(std::fabs(x - FW / 2.0 + 0.5) / (FW / 2.0), 2) * 0.55;
					code = std::min(NOFIRE - 1, (int)std::lround(y / s * kc));
				}
				m[(size_t)r * GW + c] = (unsigned char)(zone * 64 + code);
			}
		return m;
	}

	bool writeFireMaps(const std::string& dir)
	{
		std::string p[7];
		for (int k = 0; k < 7; k++) p[k] = dir + "/" + FIRE_FILES[k];
		const size_t sizes[7] = {pgmSize(GW, GH), pgmSize(GW, SR), pgmSize(GW, NH), pgmSize(GW, SPH),
								 pgmSize(GW, GH), pgmSize(GW, GH), pgmSize(GW, GH)};
		bool ready = true;
		for (int k = 0; k < 7 && ready; k++)
		{
			struct stat st;
			ready = stat(p[k].c_str(), &st) == 0 && (size_t)st.st_size == sizes[k];
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);
		const std::vector<double> NOISE = noiseField();

		// the noise: a period of 120 rows and the first 60 again, so any 60 rows from 0..119 can be cropped
		std::vector<unsigned char> noise((size_t)GW * NH);
		for (int r = 0; r < NH; r++)
			for (int c = 0; c < GW; c++)
				noise[(size_t)r * GW + c] = (unsigned char)std::lround(NOISE[(size_t)(r % NP) * GW + c] * 255);
		// the sparks: 18 dots over the fire's middle in a 36-row period (repeated as the noise), each its threshold
		// in the treble's 0..252 (the preview's: a spark shows once the treble passes its share / 1.4)
		std::vector<unsigned char> sparks((size_t)GW * SPH, 0);
		for (int i = 0; i < 18; i++)
		{
			int x = X0 + 8 + (int)std::floor(NOISE[i * 17] * 32), r = (int)std::floor(NOISE[i * 5 + 3] * SP);
			int t = std::max(1, std::min(252, (int)std::lround(NOISE[i * 11] / 1.4 * 252)));
			for (int k = r; k < SPH; k += SP) sparks[(size_t)k * GW + x] = (unsigned char)t;
		}

		// the scene alone: the sky, the stars, the logs and the ground under them
		std::vector<int> rgb((size_t)GW * GH, SKY);
		for (int i = 0; i < 40; i++)
		{
			int x = (int)std::floor(NOISE[i * 7] * 80), y = (int)std::floor(NOISE[i * 13] * 22) + 7;
			rgb[(size_t)y * GW + x] = NOISE[i * 3] > 0.5 ? STAR : STAR_DIM;
		}
		for (int x = 12; x < 68; x++)
			for (int y = 50; y < 55; y++)
			{
				int c = ((x + y) % 23 == 0 || y == 54) ? BARK : (y == 50 || (x - 12) % 9 == 0) ? BARK : LOG;
				if (y < 52 || (x > 14 && x < 66)) rgb[(size_t)y * GW + x] = c;
			}
		for (int x = 10; x < 70; x++) rgb[(size_t)55 * GW + x] = GROUND;
		std::vector<unsigned char> Y((size_t)GW * GH), U((size_t)GW * GH), V((size_t)GW * GH);
		for (size_t i = 0; i < rgb.size(); i++)
		{
			Yuv c = yuv(rgb[i]);
			Y[i] = (unsigned char)c.y; U[i] = (unsigned char)c.u; V[i] = (unsigned char)c.v;
		}
		return writePgm(p[0], GW, GH, shapeMap(GH, BASE, FROWS, 15, 31, KC_ALONE)) &&
			   writePgm(p[1], GW, SR, shapeMap(SR, SR - 1, SR, 7, 15, KC_STRIP)) && writePgm(p[2], GW, NH, noise) &&
			   writePgm(p[3], GW, SPH, sparks) && writePgm(p[4], GW, GH, Y) && writePgm(p[5], GW, GH, U) && writePgm(p[6], GW, GH, V);
	}

	std::string fireGraph(bool overCover, const std::string& dir)
	{
		auto n = [](int v) { return std::to_string(v); };
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + FIRE_FILES[k], s); return s; };
		const int TOP = 8, N = 16;	// as Peaks: the highest bands (16-24 kHz) measured and cut off
		const int R = overCover ? SR : GH;
		// a flame's height in rows: H0 + HK * level, times its column's taper (the preview's 6 + 36 * level alone)
		const char* h0 = overCover ? "3" : "6";
		const char* hk = overCover ? "20" : "36";
		const std::string kc = overCover ? "1.15" : "0.62";
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [fiart1] [fiart2]; [fiart1] crop=640:280:0:0 [top]; "
				 "[fiart2] crop=640:200:0:280,extractplanes=y+u+v [fiay] [fiau] [fiav]; ";
		// the bands: measured as Peaks does, 60 a second, pushed apart (vizContrast), eased: up at once, down over
		// ~0.15 s (the exp/log lagfun, 16 values a frame)
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("fi") + ","
			 "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split [fil] [fit]; "
			 // the treble: bands 12-15 as one value, over the whole grid
			 "[fit] crop=4:1:12:0,scale=1:1:flags=area,scale=" + n(GW) + ":" + n(R) + ":flags=neighbor,setsar=1 [fiT]; "
			 // each fire column its band's level (3 columns a band, from column 16), down the grid
			 "[fil] scale=" + n(N * 3) + ":1:flags=neighbor,pad=" + n(GW) + ":1:" + n(X0) + ":0:black,scale=" + n(GW) + ":" + n(R) +
			 ":flags=neighbor,setsar=1 [fiL]; "
			 "movie=" + q(overCover ? 1 : 0) + " [fiA]; movie=" + q(2) + " [fiN]; movie=" + q(3) + " [fiS]; "
			 // the base heat, 1 - y / h, kept 0..63 (as (heat + 0.3) * 48) under the zone (x 64)
			 "[fiL] [fiA] lut2=c0='st(0,floor(y/64));st(1,mod(y,64));ld(0)*64+if(eq(ld(1)," + n(NOFIRE) + "),0,"
			 "clip(round((1.3-(ld(1)/" + kc + ")/(" + h0 + "+" + hk + "*x/252))*48),0,63))' [fiB]; "
			 // stacked on the stills, then cropped: the heat, and the noise and the sparks from rows that rise with
			 // the frames (24 rows a second at 60 frames)
			 "[fiB] [fiN] [fiS] vstack=inputs=3,split=3 [fis1] [fis2] [fis3]; "
			 "[fis1] crop=" + n(GW) + ":" + n(R) + ":0:0 [fiBc]; "
			 "[fis2] crop=w=" + n(GW) + ":h=" + n(R) + ":x=0:y='" + n(R) + "+mod(floor(n*0.4)," + n(NP) + ")' [fiNc]; "
			 "[fis3] crop=w=" + n(GW) + ":h=" + n(R) + ":x=0:y='" + n(R + NH) + "+mod(floor(n*0.4)," + n(SP) + ")' [fiSc]; "
			 // the heat plus the noise: 3..9 the palette's entry, else the zone (0..2)
			 "[fiBc] [fiNc] lut2=c0='st(0,mod(x,64)/48-0.3+0.55*(y/255-0.5));if(gt(ld(0),0),3+min(6,floor(ld(0)*7.5)),floor(x/64))' [fiF]; "
			 "[fiT] [fiSc] lut2=c0='gt(y,0)*gte(x,y)' [fiSp]; "
			 // the sparks where no flame is and in a zone (10 low, 11 high): every pixel's class, 0 the scene
			 "[fiF] [fiSp] lut2=c0='if(gte(x,3),x,if(gt(x,0)*y,9+x,0))',split=3 [fic0] [fic1] [fic2]; ";
		Yuv pal[12];
		for (int k = 0; k < 7; k++) pal[3 + k] = yuv(FIRE[k]);
		pal[10] = yuv(SPARK_LOW); pal[11] = yuv(SPARK_HIGH);
		// each class's colour, y (the scene's or the art's) for 0
		auto paint = [&](int Yuv::*c) {
			std::string s = "'if(lt(x,3),y,";
			for (int k = 3; k < 11; k++) s += "if(eq(x," + n(k) + ")," + n(pal[k].*c) + ",";
			s += n(pal[11].*c);
			for (int k = 3; k < 11; k++) s += ")";
			return s + ")'";
		};
		if (overCover)
		{
			// the strip: each class scaled into it, the grid's lines nothing (0), then each plane the art's where
			// nothing, else the class's colour
			return g + "color=black:s=640x200:r=1:d=1,format=gray,geq=lum='255*gte(eq(mod(X,8),7)+eq(mod(Y,8),7),1)' [fiG]; "
					   "color=black:s=320x100:r=1:d=1,format=gray,geq=lum='255*gte(eq(mod(X,4),3)+eq(mod(Y,4),3),1)',split [fiGu] [fiGv]; "
					   "[fic0] scale=640:200:flags=neighbor [fic0s]; [fic0s] [fiG] lut2=c0='if(y,0,x)' [fic0g]; "
					   "[fic0g] [fiay] lut2=c0=" + paint(&Yuv::y) + " [fiY]; "
					   "[fic1] scale=320:100:flags=neighbor [fic1s]; [fic1s] [fiGu] lut2=c0='if(y,0,x)' [fic1g]; "
					   "[fic1g] [fiau] lut2=c0=" + paint(&Yuv::u) + " [fiU]; "
					   "[fic2] scale=320:100:flags=neighbor [fic2s]; [fic2s] [fiGv] lut2=c0='if(y,0,x)' [fic2g]; "
					   "[fic2g] [fiav] lut2=c0=" + paint(&Yuv::v) + " [fiV]; "
					   "[fiY] [fiU] [fiV] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		}
		// alone: the colours at 80x60 over the scene's stills; the luma scaled x8 and the grid's lines darker (a
		// still, one frame), the chroma x4
		return g + "movie=" + q(4) + " [fisy]; movie=" + q(5) + " [fisu]; movie=" + q(6) + " [fisv]; "
				   "color=black:s=640x480:r=1:d=1,format=gray,geq=lum='255*gte(eq(mod(X,8),7)+eq(mod(Y,8),7),1)' [fiG]; "
				   "[fic0] [fisy] lut2=c0=" + paint(&Yuv::y) + ",scale=640:480:flags=neighbor [fiY0]; "
				   "[fiY0] [fiG] lut2=c0='if(y,16+(x-16)*0.4,x)' [fiY]; "
				   "[fic1] [fisu] lut2=c0=" + paint(&Yuv::u) + ",scale=320:240:flags=neighbor [fiU]; "
				   "[fic2] [fisv] lut2=c0=" + paint(&Yuv::v) + ",scale=320:240:flags=neighbor [fiV]; "
				   "[fiY] [fiU] [fiV] mergeplanes=0x001020:yuv420p [vo]";
	}
}
