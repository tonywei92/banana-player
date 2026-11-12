// Music box: pixel art on an 80x60 grid of 8-pixel dots, on a dark plum. An open wooden music box: the lid standing up
// behind with a pale mirror and a tiny dancer (a pink tutu) on it, turning (four poses, four a second); the box's front
// open on a dark well with a gold rail, a brass comb of 16 teeth (each shorter than the last, the bass on the left),
// each tooth going from dull brass to bright gold as its band plays and its tip bending one dot when the band is loud;
// the drum below it turning, its pins sliding along; little golden notes floating up beside the box on the bass, fading
// as they rise.
// A frame is worked out at 80x60 (80x25 over the art) as a picture of classes (0 the scene, 1-8 the teeth's golds, 9 a
// pin, 10-13 the dancer, 14-17 a note at four strengths, 18 the rail, 19 the drum) and painted at the end, as Campfire:
//  - the 16 levels (measured and eased as Pocket console's) stretched over each tooth's three columns and down the
//    rows; one lut2 against the comb's still (each tooth dot its band, its tip twice: straight, and bent one dot right)
//    gives each tooth dot its gold (the eased level in 8 steps) and shows the bent tip or the straight one by a
//    threshold;
//  - that picture, padded, stacked over one still atlas (the dancer's four poses, a row of pins two turns long, a
//    column of notes a cycle and a picture's height long; vstack repeats its one frame) and cropped by the frame
//    number: the pose, the pins sliding, the notes rising and swaying. Each crop is padded to its place and laid over
//    by a lut2 (its 0 is see-through);
//  - the notes: each dot of the column says which of the 5 notes it is; a lut2 against a still of the rows' fade, and
//    one against the bass (bands 0-2 as one value) lights note i once the bass passes 25% + 12% x i;
//  - alone, a lut2 a plane picks the class's colour or the scene's own (a still at 80x60: plum, lid, mirror, box,
//    well), a note blended with what's under it by its strength; the luma scaled x8 and the chroma x4 (nearest).
//    Over the art: the rail, the comb, the drum and the notes in the 640x200 strip, the art everywhere else.
// YUV and gray only, nothing converted, no geq per frame (the fade's still is made once by geq).
#include "viz_more.h"
#include "settings.h"

#include <cmath>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	// a new name whenever the stills change: they're cached by size
	static const char* MB_FILES[6] = {"musicbox1-comb.pgm", "musicbox1-combs.pgm", "musicbox1-atlas.pgm",
									  "musicbox1-y.pgm", "musicbox1-u.pgm", "musicbox1-v.pgm"};
	static const int MW = 80, MH = 60, MS = 25;	// the grid alone, the strip's rows over the art
	// the atlas: 100 wide (the pins' two turns); rows 0-10 the four poses (8x11 each), row 11 the pins, from row 12
	// the notes' column (18 wide, 48 rows: a 22-row cycle and the tallest window, 26)
	static const int AW = 100, AH = 60, PW = 8, PH = 11, PIN_ROW = 11, NOTE_ROW = 12, NW = 18, NH = 48, CYCLE = 22;
	// the notes' window on screen: 14 columns from 66 (the still's 18 sway 2 each way), and its rows
	static const int NX = 66, NCW = 14;
	// the comb's still: 0 nothing, 1-16 a tooth's body (its band + 1), 17-32 its tip straight, 33-48 its tip bent, 49
	// the rail, 50 the drum
	enum { C_TIP = 17, C_BENT = 33, C_RAIL = 49, C_DRUM = 50 };

	struct MbLayout { int rows, rail, drum, notesRows, notesOff; };
	// the rail's row (the teeth from the next), the drum's first row (its pins on it), the notes' window rows and
	// where in the cycle it starts (over the art 4 rows higher: rising from the drum's end to the strip's top)
	static MbLayout mbLayout(bool strip)
	{
		if (strip) return {MS, 3, 20, 22, 4};
		return {MH, 30, 47, 26, 0};
	}

	static std::vector<unsigned char> combMap(bool strip)
	{
		const MbLayout l = mbLayout(strip);
		std::vector<unsigned char> m((size_t)MW * l.rows, 0);
		auto at = [&](int x, int y, int v) { if (y >= 0 && y < l.rows) m[(size_t)y * MW + x] = (unsigned char)v; };
		for (int x = 14; x < 66; x++) at(x, l.rail, C_RAIL);
		for (int y = l.drum; y < l.drum + 3; y++)
			for (int x = 15; x < 65; x++) at(x, y, C_DRUM);
		for (int i = 0; i < 16; i++)
		{
			const int x = 16 + i * 3, len = 15 - (int)std::floor(i * 0.5 + 0.5);
			for (int k = 0; k < len; k++)
			{
				if (k < len - 2) { at(x, l.rail + 1 + k, 1 + i); continue; }
				at(x, l.rail + 1 + k, C_TIP + i);
				at(x + 1, l.rail + 1 + k, C_BENT + i);
			}
		}
		return m;
	}

	// the notes' anchors (their middle dot's column on screen, swaying about it, and their row at the cycle's start)
	static const int NOTE_X[5] = {69, 72, 75, 71, 74};

	static std::vector<unsigned char> atlas()
	{
		std::vector<unsigned char> m((size_t)AW * AH, 0);
		auto at = [&](int x, int y, int v) { m[(size_t)y * AW + x] = (unsigned char)v; };
		// the dancer (on screen 36-43 by 11-21): the body, the hair, the arms by the pose, the tutu (light or deep)
		static const int ARMS[4][2] = {{-2, 1}, {-1, 2}, {2, 1}, {1, 2}};
		for (int p = 0; p < 4; p++)
		{
			auto d = [&](int x, int y, int v) { at(p * PW + x - 36, y - 11, v); };
			for (int y = 12; y < 22; y++) d(40, y, 10);
			d(40, 11, 11); d(39, 12, 11); d(41, 12, 11);
			d(40 + ARMS[p][0], 14 - ARMS[p][1], 10);
			d(40 - ARMS[p][0], 15 - ARMS[p][1], 10);
			for (int i = -3; i <= 3; i++) d(40 + i, (int)std::floor(17 + std::abs(i) / 3.0 + 0.5), p % 2 ? 13 : 12);
		}
		// the pins: 8 in the drum's 50 dots, twice over (the crop's window slides over 50)
		for (int i = 0; i < 8; i++) { at(i * 7 % 50, PIN_ROW, 9); at(i * 7 % 50 + 50, PIN_ROW, 9); }
		// the notes: note i (its value i + 1) at its anchor every cycle; at the cycle's start it's (0.2 x i) of its
		// way up (the preview's phases), 22 rows a way
		for (int i = 0; i < 5; i++)
		{
			const int cx = NOTE_X[i] - NX + 2, a0 = 26 - (int)std::floor(4.4 * i + 0.5);
			for (int a = a0 - CYCLE; a < NH + 2; a += CYCLE)
			{
				const int dots[5][2] = {{0, 0}, {0, -1}, {0, -2}, {1, -2}, {-1, 1}};
				for (auto& dt : dots)
				{
					const int y = a + dt[1];
					if (y >= 0 && y < NH) at(cx + dt[0], NOTE_ROW + y, i + 1);
				}
			}
		}
		return m;
	}

	struct MbYuv { int y, u, v; };
	// BT.601 limited (mpv's reading of a picture this size)
	static MbYuv mbYuv(int rgb)
	{
		double r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
		return {(int)std::lround(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255),
				(int)std::lround(128 + (-37.797 * r - 74.203 * g + 112 * b) / 255),
				(int)std::lround(128 + (112 * r - 93.786 * g - 18.214 * b) / 255)};
	}
	// hsl(h s% l%) as the preview's teeth are
	static int hsl(double h, double s, double l)
	{
		auto f = [&](double n) {
			double k = std::fmod(n + h / 30, 12), a = s * std::fmin(l, 1 - l);
			return (int)std::lround(255 * (l - a * std::fmax(-1, std::fmin(std::fmin(k - 3, 9 - k), 1))));
		};
		return f(0) << 16 | f(8) << 8 | f(4);
	}

	static const int MB_BG = 0x1e1430, MB_DARK = 0x5a2e1a, MB_WOOD = 0x8a4a2a, MB_GRAIN = 0x7a3f22, MB_MIRROR = 0xc8d6f0,
					 MB_WELL = 0x2a1a12, MB_RAIL = 0xc9a03a, MB_DRUM = 0x9a7a3a, MB_PIN = 0xe8d8a0, MB_SKIN = 0xf2c9b0,
					 MB_HAIR = 0x3a2010, MB_TUTU1 = 0xffc1dc, MB_TUTU2 = 0xff9ec7, MB_NOTE = 0xffd66e;

	static std::vector<int> scene()
	{
		std::vector<int> c((size_t)MW * MH, MB_BG);
		auto at = [&](int x, int y, int v) { c[(size_t)y * MW + x] = v; };
		for (int y = 8; y < 26; y++)
			for (int x = 14; x < 66; x++)
				at(x, y, y == 8 || x == 14 || x == 65 ? MB_DARK : y < 10 || x < 17 || x > 62 ? MB_WOOD : MB_MIRROR);
		for (int y = 26; y < 52; y++)
			for (int x = 10; x < 70; x++)
				at(x, y, y == 26 || y == 51 || x == 10 || x == 69 ? MB_DARK : (x + y) % 9 ? MB_WOOD : MB_GRAIN);
		for (int y = 29; y < 47; y++)
			for (int x = 14; x < 66; x++) at(x, y, MB_WELL);
		for (int x = 14; x < 66; x++) at(x, 30, MB_RAIL);
		for (int y = 47; y < 50; y++)
			for (int x = 15; x < 65; x++) at(x, y, MB_DRUM);
		return c;
	}

	static bool mbWritePgm(const std::string& path, int w, int h, const std::vector<unsigned char>& data)
	{
		std::string tmp = path + ".tmp";
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << "P5\n" << w << " " << h << "\n255\n";
		out.write((const char*)data.data(), (std::streamsize)data.size());
		if (!out.flush()) return false;
		out.close();
		return rename(tmp.c_str(), path.c_str()) == 0;
	}

	bool writeMusicboxMaps(const std::string& dir)
	{
		const int ws[6] = {MW, MW, AW, MW, MW, MW}, hs[6] = {MH, MS, AH, MH, MH, MH};
		bool ready = true;
		for (int k = 0; k < 6 && ready; k++)
		{
			struct stat st;
			const std::string header = "P5\n" + std::to_string(ws[k]) + " " + std::to_string(hs[k]) + "\n255\n";
			ready = stat((dir + "/" + MB_FILES[k]).c_str(), &st) == 0 && (size_t)st.st_size == header.size() + (size_t)ws[k] * hs[k];
		}
		if (ready) return true;
		mkdir(dir.c_str(), 0755);
		const std::vector<int> rgb = scene();
		std::vector<unsigned char> Y(rgb.size()), U(rgb.size()), V(rgb.size());
		for (size_t i = 0; i < rgb.size(); i++)
		{
			MbYuv c = mbYuv(rgb[i]);
			Y[i] = (unsigned char)c.y; U[i] = (unsigned char)c.u; V[i] = (unsigned char)c.v;
		}
		const std::vector<unsigned char> data[6] = {combMap(false), combMap(true), atlas(), Y, U, V};
		for (int k = 0; k < 6; k++)
			if (!mbWritePgm(dir + "/" + MB_FILES[k], ws[k], hs[k], data[k])) return false;
		return true;
	}

	std::string musicboxGraph(bool overCover, const std::string& dir)
	{
		auto n = [](int v) { return std::to_string(v); };
		auto q = [&](int k) { std::string s; lavfiPath(dir + "/" + MB_FILES[k], s); return s; };
		const MbLayout l = mbLayout(overCover);
		const int R = l.rows, RH = l.notesRows;
		const int TOP = 8, N = 16;	// as Peaks: the highest bands (16-24 kHz) measured and cut off
		// the teeth's range: the level's steps from FLOOR (below it the spectrum's near-constant bed) in 8 golds, and
		// the bend past 55% of it
		const int FLOOR = 40, BEND = FLOOR + (252 - FLOOR) * 55 / 100;
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [mbart1] [mbart2]; [mbart1] crop=640:280:0:0 [top]; "
				 "[mbart2] crop=640:200:0:280,extractplanes=y+u+v [mbay] [mbau] [mbav]; ";
		// the bands: measured as Peaks does, 60 a second, pushed apart (vizContrast), eased: up at once, down over
		// ~0.15 s (the exp/log lagfun, 16 values a frame)
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,lut=c0='val*252/255'" + vizContrast("mb", 1, "3", "1.3", "170") + ","
			 "format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,split [mbl] [mbb]; "
			 // each tooth's three columns its band's level, down the grid; padded to the atlas's width and stacked on it
			 "[mbl] scale=" + n(N * 3) + ":1:flags=neighbor,pad=" + n(MW) + ":1:16:0:black,scale=" + n(MW) + ":" + n(R) +
			 ":flags=neighbor,setsar=1,pad=" + n(AW) + ":" + n(R) + ":0:0:black [mbL]; "
			 "movie=" + q(2) + ",format=gray [mbA]; movie=" + q(overCover ? 1 : 0) + ",format=gray [mbC]; "
			 "[mbL] [mbA] vstack,split=" + n(overCover ? 3 : 4) + " [mbs0] [mbs1] [mbs2]" + (overCover ? "" : " [mbs3]") + "; "
			 // the comb: each tooth's gold by its level, its tip straight or (loud) bent; the rail and the drum
			 "[mbs0] crop=" + n(MW) + ":" + n(R) + ":0:0 [mbLv]; "
			 "[mbLv] [mbC] lut2=c0='st(0,1+clip(round(7*(x-" + n(FLOOR) + ")/" + n(252 - FLOOR) + "),0,7));"
			 "if(eq(y,0),0,if(lt(y," + n(C_TIP) + "),ld(0),if(lt(y," + n(C_BENT) + "),if(lte(x," + n(BEND) + "),ld(0),0),"
			 "if(lt(y," + n(C_RAIL) + "),if(gt(x," + n(BEND) + "),ld(0),0),if(eq(y," + n(C_RAIL) + "),18,19)))))' [mbK0]; "
			 // the pins, 6 dots a second along the drum
			 "[mbs1] crop=w=50:h=1:x='50-mod(floor(n/10),50)':y=" + n(R + PIN_ROW) + ",pad=" + n(MW) + ":" + n(R) + ":15:" + n(l.drum) +
			 ":black [mbP]; "
			 "[mbK0] [mbP] lut2=c0='if(y,y,x)' [mbK1]; "
			 // the notes: rising 22 rows in 2.5 s, swaying 2 dots each way; each dot's note (x 8) and its row's strength
			 "[mbs2] crop=w=" + n(NCW) + ":h=" + n(RH) + ":x='2+round(2*sin(n/30))':y='" + n(R + NOTE_ROW + l.notesOff) +
			 "+mod(floor(n*" + n(CYCLE) + "/150)," + n(CYCLE) + ")' [mbNc]; "
			 "color=black:s=" + n(NCW) + "x" + n(RH) + ":r=1:d=1,format=gray,geq=lum='clip(round(4*(Y+" + n(l.notesOff) +
			 "-4)/22),0,4)' [mbF]; "
			 "[mbNc] [mbF] lut2=c0='if(x,x*8+y,0)' [mbNa]; "
			 "[mbb] crop=3:1:0:0,scale=1:1:flags=area,scale=" + n(NCW) + ":" + n(RH) + ":flags=neighbor [mbB]; "
			 // a note is lit once the bass passes its threshold: 25% + 12% x its number
			 "[mbNa] [mbB] lut2=c0='if(mod(x,8)*gt(y,63+30.24*(floor(x/8)-1)),13+mod(x,8),0)',pad=" + n(MW) + ":" + n(R) + ":" +
			 n(NX) + ":0:black [mbN]; "
			 "[mbK1] [mbN] lut2=c0='if(y,y,x)'";
		if (!overCover)
			// the dancer: a pose four times a second
			g += " [mbK2]; [mbs3] crop=w=" + n(PW) + ":h=" + n(PH) + ":x='" + n(PW) + "*mod(floor(n/15),4)':y=" + n(R) + ",pad=" +
				 n(MW) + ":" + n(R) + ":36:11:black [mbD]; [mbK2] [mbD] lut2=c0='if(y,y,x)'";
		g += ",split=3 [mbc0] [mbc1] [mbc2]; ";

		// each class's colour; 0 the scene's (or the art's), a note its colour blended over it by its strength
		int cls[20] = {0};
		for (int k = 0; k < 8; k++)
		{
			const double v = k / 7.0;
			cls[1 + k] = hsl(45, (40 + v * 50) / 100, (35 + v * 45) / 100);
		}
		cls[9] = MB_PIN; cls[10] = MB_SKIN; cls[11] = MB_HAIR; cls[12] = MB_TUTU1; cls[13] = MB_TUTU2;
		cls[18] = MB_RAIL; cls[19] = MB_DRUM;
		const MbYuv note = mbYuv(MB_NOTE);
		auto paint = [&](int MbYuv::*p) {
			std::string e = "'if(eq(x,0),y,";
			int close = 0;
			for (int k = 1; k < 20; k++)
			{
				if (k >= 14 && k <= 17) continue;
				e += "if(eq(x," + n(k) + ")," + n(mbYuv(cls[k]).*p) + ",";
				close++;
			}
			// the notes, 14-17: a quarter to all of the note's colour
			e += "round(y+(x-13)/4*(" + n(note.*p) + "-y))";
			return e + std::string(close + 1, ')') + "'";
		};
		if (overCover)
			return g + "[mbc0] scale=640:200:flags=neighbor [mbc0s]; [mbc0s] [mbay] lut2=c0=" + paint(&MbYuv::y) + " [mbY]; "
					   "[mbc1] scale=320:100:flags=neighbor [mbc1s]; [mbc1s] [mbau] lut2=c0=" + paint(&MbYuv::u) + " [mbU]; "
					   "[mbc2] scale=320:100:flags=neighbor [mbc2s]; [mbc2s] [mbav] lut2=c0=" + paint(&MbYuv::v) + " [mbW]; "
					   "[mbY] [mbU] [mbW] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		// alone: the colours at 80x60 over the scene's stills; the luma scaled x8, the chroma x4
		return g + "movie=" + q(3) + ",format=gray [mbsy]; movie=" + q(4) + ",format=gray [mbsu]; movie=" + q(5) + ",format=gray [mbsv]; "
				   "[mbc0] [mbsy] lut2=c0=" + paint(&MbYuv::y) + ",scale=640:480:flags=neighbor [mbY]; "
				   "[mbc1] [mbsu] lut2=c0=" + paint(&MbYuv::u) + ",scale=320:240:flags=neighbor [mbU]; "
				   "[mbc2] [mbsv] lut2=c0=" + paint(&MbYuv::v) + ",scale=320:240:flags=neighbor [mbW]; "
				   "[mbY] [mbU] [mbW] mergeplanes=0x001020:yuv420p [vo]";
	}
}
