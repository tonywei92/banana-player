#include "settings.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	static const struct { const char* key; bool Settings::*field; } REMEMBER[] = {
		{"remember-position", &Settings::rememberPosition},
		{"remember-audio", &Settings::rememberAudio},
		{"remember-subtitles", &Settings::rememberSubs},
		{"remember-folder", &Settings::rememberFolder},
		{"keep-history", &Settings::keepHistory},
		{"keep-network-history", &Settings::keepNetHistory},
		{"button-hotkeys", &Settings::buttonHotkeys},
		{"show-hidden-folders", &Settings::showHiddenFolders},
		{"battery-percent", &Settings::batteryPercent},
		{"keep-photo-zoom", &Settings::keepPhotoZoom},
		{"check-updates", &Settings::checkUpdates},
		{"same-brightness", &Settings::sameBrightness},
		{"shuffle", &Settings::shuffle},	// (no row: the play mode, as loop)
		{"manga-reader", &Settings::mangaOn},
		{"manga-intro-shown", &Settings::mangaIntroShown},
		{"manga-swap-lr", &Settings::mangaSwapLR},
		{"manga-adult", &Settings::mangaAdult},
		{"manga-history", &Settings::mangaHistory},
		{"manga-recent", &Settings::mangaRecent},
		{"manga-full-seen", &Settings::mangaFullSeen},
		{"manga-open-full", &Settings::mangaOpenFull},
		{"manga-secure-dns", &Settings::mangaSecureDns},
	};

	static const char* SHOW_IDS[SHOWS] = {"art", "both", "visualizer", "none"};
	static const char* SHOW_LABELS[SHOWS] = {"Album art", "Art + visualizer", "Visualizer", "None"};
	static const char* VIS_IDS[VISUALIZERS] = {"bars", "waves", "spectrum", "circle", "peaks", "peaks-yellow"};
	static const char* VIS_LABELS[VISUALIZERS] = {"Bars", "Waves", "Spectrum", "Circle", "Peaks", "Peaks Banana"};
	static const char* LOOP_IDS[LOOPS] = {"off", "all", "one"};
	static const char* MANGA_MODE_IDS[MANGA_MODES] = {"rtl", "ltr", "strip"};
	static const char* MANGA_FIT_IDS[MANGA_FITS] = {"page", "width", "fill"};
	static const char* MANGA_CACHE_IDS[MANGA_CACHES] = {"100m", "300m", "1g"};
	static const long long MANGA_CACHE_MB[MANGA_CACHES] = {100, 300, 1024};
	static const char* CLOCK_IDS[CLOCKS] = {"24h", "12h", "off"};
	static const char* SCREEN_OFF_IDS[SCREEN_OFFS] = {"never", "15s", "30s", "1m", "2m", "5m"};

	int stepMangaPreload(int v, int dir)
	{
		int i = 0;
		for (int k = 0; k < MANGA_PRELOAD_COUNT; k++)
			if (MANGA_PRELOADS[k] == v) i = k;
		i = (i + (dir < 0 ? -1 : 1) + MANGA_PRELOAD_COUNT) % MANGA_PRELOAD_COUNT;
		return MANGA_PRELOADS[i];
	}

	uint64_t mangaCacheBytes(int v)
	{
		int k = v >= 0 && v < MANGA_CACHES ? v : MANGA_CACHE_DEFAULT;
		return (uint64_t)MANGA_CACHE_MB[k] * 1024 * 1024;
	}

	int screenOffSeconds(int v)
	{
		static const int SECS[SCREEN_OFFS] = {0, 15, 30, 60, 120, 300};
		return v >= 0 && v < SCREEN_OFFS ? SECS[v] : 0;
	}

	const char* screenOffLabel(int v)
	{
		static const char* L[SCREEN_OFFS] = {"Never", "15 seconds", "30 seconds", "1 minute", "2 minutes", "5 minutes"};
		return v >= 0 && v < SCREEN_OFFS ? L[v] : L[0];
	}
	static const char* LOOP_LABELS[LOOPS] = {"No loop", "Loop all", "Loop one"};

	const char* musicShowLabel(int v) { return SHOW_LABELS[v >= 0 && v < SHOWS ? v : 0]; }
	const char* visualizerLabel(int v) { return VIS_LABELS[v >= 0 && v < VISUALIZERS ? v : 0]; }

	int visualizerSlot(int v)
	{
		for (int i = 0; i < VIS_SHOWN_COUNT; i++)
			if (VIS_SHOWN[i] == v) return i;
		return 0;
	}

	int stepVisualizer(int v, int dir)
	{
		int i = visualizerSlot(v) + (dir < 0 ? -1 : 1);
		return VIS_SHOWN[(i % VIS_SHOWN_COUNT + VIS_SHOWN_COUNT) % VIS_SHOWN_COUNT];
	}
	const char* loopLabel(int v) { return LOOP_LABELS[v >= 0 && v < LOOPS ? v : 0]; }

	// Circle: the spectrum's bars (BARS_W x BARS_H, low notes left, growing up) and beside them PALETTE columns, the
	// ring's glow fading out to black; each pixel of the square picture is one of theirs (remap), so a frame costs a
	// lookup a pixel. The bars run from the top down both sides, mirrored, SEGMENTS a side
	static const int BARS_W = 256, BARS_H = 128, PALETTE = 16, SEGMENTS = 40;
	static const double BAR_FROM = 84, BAR_TO = 200, RING = 76, CENTRE_Y = 274;
	// the share of the measured spectrum the ring shows: showfreqs runs to half the sample rate, and its top ninth
	// (16-24 kHz at 44.1/48 kHz, as Peaks cuts) has next to nothing in music, so the bars at the bottom never moved
	static const double SHOWN = 8.0 / 9;
	static const int SHOWN_W = (int)(BARS_W * SHOWN);
	// a new name whenever the tables change: they're cached by size
	static const char* CIRCLE_MAPS[2] = {"circle3-x.pgm", "circle3-y.pgm"};

	bool writeCircleMaps(const std::string& dir)
	{
		const int w = CIRCLE_BOX, h = CIRCLE_BOX;
		size_t bytes = (size_t)w * h * 2;
		std::string header = "P5\n" + std::to_string(w) + " " + std::to_string(h) + "\n65535\n";
		auto ready = [&](const std::string& path) {
			struct stat st;
			return stat(path.c_str(), &st) == 0 && (size_t)st.st_size == header.size() + bytes;
		};
		if (ready(dir + "/" + CIRCLE_MAPS[0]) && ready(dir + "/" + CIRCLE_MAPS[1])) return true;
		std::vector<unsigned char> xs(bytes), ys(bytes);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				double dx = x + CIRCLE_X - CIRCLE_W / 2.0 + 0.5, dy = y + CIRCLE_Y - CENTRE_Y + 0.5, r = std::hypot(dx, dy);
				double seg = std::fabs(std::atan2(dx, -dy)) / M_PI * SEGMENTS;	// 0 at the top, SEGMENTS at the bottom
				int mx, my;
				if (r >= BAR_FROM && r <= BAR_TO && seg - std::floor(seg) < 0.6)
				{
					// a bar: its frequency by the angle, its height by the distance out
					mx = (int)((std::floor(seg) + 0.5) / SEGMENTS * SHOWN_W);
					my = (int)std::lround((BARS_H - 1) * (1 - (r - BAR_FROM) / (BAR_TO - BAR_FROM)));
				}
				else
				{
					// the ring and its glow, else black (the palette's last column)
					mx = BARS_W + std::min(PALETTE - 1, (int)std::lround(std::fabs(r - RING) * 2));
					my = 0;
				}
				size_t i = ((size_t)y * w + x) * 2;
				xs[i] = (unsigned char)(mx >> 8);
				xs[i + 1] = (unsigned char)mx;
				ys[i] = (unsigned char)(my >> 8);
				ys[i + 1] = (unsigned char)my;
			}
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < 2; k++)
		{
			std::string path = dir + "/" + CIRCLE_MAPS[k], tmp = path + ".tmp";
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			out << header;
			const std::vector<unsigned char>& v = k ? ys : xs;
			out.write((const char*)v.data(), (std::streamsize)v.size());
			if (!out.flush()) return false;
			out.close();
			if (rename(tmp.c_str(), path.c_str()) != 0) return false;
		}
		return true;
	}

	bool lavfiSafePath(const std::string& dir)
	{
		return !dir.empty() && dir.find_first_of(":='\\") == std::string::npos;
	}

	// a path as a filter's argument: quoted (spaces, commas, brackets, ';'); one with ':', '=', a quote or a
	// backslash can't be named at all
	static bool lavfiPath(const std::string& p, std::string& out)
	{
		if (!lavfiSafePath(p)) return false;
		out = "'" + p + "'";
		return true;
	}

	static std::string circleGraph(bool overCover, const std::string& dir)
	{
		std::string xm, ym;
		lavfiPath(dir + "/" + CIRCLE_MAPS[0], xm);
		lavfiPath(dir + "/" + CIRCLE_MAPS[1], ym);
		std::string bars = std::to_string(BARS_W) + "x" + std::to_string(BARS_H), sw = std::to_string(SHOWN_W);
		// cyan (left: the low notes, at the top) to blue-purple to pink (the high, at the bottom), multiplied into
		// the white bars; the ring a pale blue, fading over the palette's columns
		// The still inputs (the gradient, the palette, the tables) are pictures that end, never loops: blend, hstack
		// and remap go on with an ended input's last frame, and end when the bars (the sound) do. A loop never ends,
		// so neither did the graph: past the song's end its sources poured out frames as fast as they could, mpv's
		// clock stopped (the song never ended) and its memory grew by the gigabyte. One frame each (r=1): nullsrc's
		// 25 a second over d=1 made the first second 55 frames, each one remapped
		std::string g =
			"[aid1] asplit [ao] [a]; "
			"[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + bars + ":mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=white|white,format=gbrp [bars]; "
			"nullsrc=s=" + bars + ":r=1:d=1,format=gbrp,"
			"geq=r='59+196*clip(2*X/" + sw + "-1,0,1)':g='232-140*clip(2*X/" + sw + ",0,1)-13*clip(2*X/" + sw + "-1,0,1)':b='224+31*clip(2*X/" + sw + ",0,1)-39*clip(2*X/" + sw + "-1,0,1)' [grad]; "
			"[bars] [grad] blend=all_mode=multiply [cbars]; "
			"nullsrc=s=" + std::to_string(PALETTE) + "x" + std::to_string(BARS_H) + ":r=1:d=1,format=gbrp,"
			"geq=r='207*exp(-pow(X/4,2))':g='232*exp(-pow(X/4,2))':b='255*exp(-pow(X/4,2))' [pal]; "
			"[cbars] [pal] hstack [src]; "
			"movie=" + xm + " [xm]; movie=" + ym + " [ym]; ";
		std::string pad = ",pad=" + std::to_string(CIRCLE_W) + ":" + std::to_string(CIRCLE_H) + ":" + std::to_string(CIRCLE_X) + ":" + std::to_string(CIRCLE_Y) + ":black";
		if (!overCover) return g + "[src] [xm] [ym] remap" + pad + " [vo]";
		// the art inside the ring
		return g + "[src] [xm] [ym] remap" + pad + " [ring]; "
			"[vid1] scale=96:96:force_original_aspect_ratio=decrease,format=gbrp [cover]; "
			"[ring] [cover] overlay=x=(W-w)/2:y=" + std::to_string((int)CENTRE_Y) + "-h/2:eof_action=repeat:format=gbrp [vo]";
	}

	// Peaks: the spectrum in 64 bars (coral), each with a cap (green) that sits on the bar's highest point and falls
	// back slowly. Bars draws each frame at full size in showfreqs, which paints pixel by pixel; here showfreqs only
	// measures (64 columns, averaged down to one row, one value a bar), and the picture is made from those numbers:
	// - the caps: a value held and let fall by lagfun, which keeps max(now, last x decay). Its decay is a product, so
	//   the values go through exp() before it and log() after (16 bits for the range): a product in exp() is a
	//   constant step in the value, a cap falling at an even speed
	// - the drawing at half size (320x240; 320x100 over the art): each row of numbers stretched to a picture (scale
	//   on one row, then pad and fillborders: row copies), and two lut2 against a still "height of this row" picture
	//   (one frame, repeated: lut2's secondary input) give each pixel a cap, a bar or nothing (gaps: a 1-column one
	//   every 5). Half size is the chroma's size at full size, so the colour planes come from it as they are
	// - full size: only the luma, scaled x2 (nearest: a bar 8 pixels, a gap 2, a cap 4)
	// No RGB anywhere, nothing converted: YUV straight to mpv. Over the art no alpha either: the art's slice (made
	// once, one frame) is split into its planes and each plane is a lut2's second input, kept where nothing's drawn
	struct PeaksColours { int barY, barU, barV, capY, capU, capV; };
	static std::string peaksGraph(bool overCover, const PeaksColours& c)
	{
		const int N = 64, BW = 5, W = N * BW;	// at half size: a bar 4 columns and a 1-column gap
		const int H = overCover ? 100 : 240;
		const std::string w = std::to_string(W), h = std::to_string(H);
		// the cap 4 pixels at full size (2 rows here), in the height's 251 steps; the fall, ~2.4 s for the whole
		// height: 3.4 steps a frame, a decay of exp(-3.4/24) in the exp(value/24) domain
		const std::string cap = std::to_string((2 * 251 + H / 2) / H);
		const int mid = H / 2 - 1;
		const std::string stretch = "scale=" + w + ":1:flags=neighbor,pad=" + w + ":" + h + ":0:" + std::to_string(mid) +
									",fillborders=top=" + std::to_string(mid) + ":bottom=" + std::to_string(H - 1 - mid) +
									":mode=smear,setsar=1";
		// the colours in BT.601 limited YUV (mpv's reading of a picture this size); black is 16/128/128. Coral
		// #F0806E and green #9EDB98: bar 153/103/178, cap 182/108/106. A bar's and a cap's Y must differ (the planes
		// are told apart by it)
		const std::string bY = std::to_string(c.barY), bU = std::to_string(c.barU), bV = std::to_string(c.barV),
						  cY = std::to_string(c.capY), cU = std::to_string(c.capU), cV = std::to_string(c.capV);
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [art1] [art2]; [art1] crop=640:280:0:0 [top]; [art2] crop=640:200:0:280,extractplanes=y+u+v "
				 "[pkay] [pkau] [pkav]; ";
		// TOP more bars measured than drawn, the highest cut off: at 44.1/48 kHz they're 16-24 kHz, where music has next
		// to nothing, so the rightmost bars sat still or twitched at the noise floor
		const int TOP = 8;
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + std::to_string(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=white,crop=" + std::to_string(N) +
			 ":256:0:0,scale=" + std::to_string(N) +
			 ":1:flags=area,format=gray,lut=c0='val*252/255',split [pkv] [pkp]; "
			 "[pkp] format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.8679,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*250)',format=gray," + stretch + " [pkP]; "
			 "[pkv] " + stretch + " [pkV]; "
			 "color=black:s=" + w + "x" + h + ":r=1:d=1,format=gray,geq=lum='if(gte(mod(X," + std::to_string(BW) + ")," +
			 std::to_string(BW - 1) + "),253,1+floor(251*(" + h + "-1-Y)/(" + h + "-1)))' [pkR]; "
			 // 254: a cap (over the cap's value, within cap steps of it), else the row's height (253: a gap)
			 "[pkP] [pkR] lut2=c0='if(gt(y,x)*lte(y,x+" + cap + "),254,y)' [pkQ]; ";
		if (overCover)
		{
			// what each pixel is (0 nothing, 1 bar, 2 cap), then each plane: the art's where nothing, else a colour
			auto paint = [](const char* bar, const char* capc) {
				return std::string("'if(eq(x,0),y,if(eq(x,1),") + bar + "," + capc + "))'";
			};
			return g + "[pkV] [pkQ] lut2=c0='if(eq(y,254),2,if(lte(y,x)*lt(y,253),1,0))',split=3 [pkc0] [pkc1] [pkc2]; "
					   "[pkc0] scale=640:200:flags=neighbor [pkcy]; "
					   "[pkcy] [pkay] lut2=c0=" + paint(bY.c_str(), cY.c_str()) + " [pkY]; "
					   "[pkc1] [pkau] lut2=c0=" + paint(bU.c_str(), cU.c_str()) + " [pkU]; "
					   "[pkc2] [pkav] lut2=c0=" + paint(bV.c_str(), cV.c_str()) + " [pkW]; "
					   "[pkY] [pkU] [pkW] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		}
		// the luma at half size, then U and V from it (a lut each) and the luma scaled up
		g += "[pkV] [pkQ] lut2=c0='if(eq(y,254)," + cY + ",if(lte(y,x)*lt(y,253)," + bY + ",16))',split=3 [pky] [pku] [pkw]; "
			 "[pky] scale=640:480:flags=neighbor [pkY]; "
			 "[pku] lut=c0='if(eq(val," + cY + ")," + cU + ",if(eq(val," + bY + ")," + bU + ",128))' [pkU]; "
			 "[pkw] lut=c0='if(eq(val," + cY + ")," + cV + ",if(eq(val," + bY + ")," + bV + ",128))' [pkW]; "
			 "[pkY] [pkU] [pkW] mergeplanes=0x001020:yuv420p";
		// a faint line every 96 pixels (white at 10%), over the bars too: four 1-pixel boxes
		for (int y = 96; y < 480; y += 96) g += ",drawbox=x=0:y=" + std::to_string(y) + ":w=640:h=1:c=white@0.1:t=fill";
		return g + " [vo]";
	}

	std::string visualizerGraph(int vis, bool overCover, const std::string& mapsDir)
	{
		std::string quoted;
		if (vis == VIS_CIRCLE && !mapsDir.empty() && lavfiPath(mapsDir, quoted)) return circleGraph(overCover, mapsDir);
		if (vis == VIS_CIRCLE) vis = VIS_BARS;
		// drawn at the panel's shape (mpv scales it to the panel) and 30 frames a second: light enough for the A55s
		// beside the decoding
		int h = overCover ? 200 : 480;
		std::string size = "s=640x" + std::to_string(h);
		std::string draw;
		// a soft amber, not the menu's bright yellow: it fills much of a dark screen. One colour a channel: the filters
		// give a channel with none its own (white for the second), and stereo is most music
		const std::string ink = "0xC08A22|0xC08A22";
		// The sound as the visualizers see it (the speakers' branch, [ao], keeps the file's own): 44.1 or 48 kHz, so a
		// 96/192 kHz file costs no more than a CD (and its spectrum isn't mostly empty ultrasound), and Bars in mono
		// (its two channels are drawn in one colour anyway, and every bar is painted pixel by pixel: half the cost)
		const std::string in = "aformat=sample_rates=44100|48000";
		switch (vis)
		{
		case VIS_WAVES: draw = in + ",showwaves=" + size + ":mode=cline:split_channels=0:rate=30:colors=" + ink; break;
		// its own fps: a frame made only every 30th of a second (an fps filter after it took one per column, each a
		// copy of the whole picture, and dropped most)
		case VIS_SPECTRUM: draw = in + ",showspectrum=" + size + ":slide=scroll:mode=combined:color=fire:scale=log:fps=30"; break;
		default: draw = in + ":channel_layouts=mono,showfreqs=" + size + ":mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=" + ink; break;
		}
		// Peaks: coral and green; Peaks Banana: Midnight's yellow #FFD23F bars, cream #E8E2D3 caps
		if (vis == VIS_PEAKS) return peaksGraph(overCover, {153, 103, 178, 182, 108, 106});
		if (vis == VIS_PEAKS_BANANA) return peaksGraph(overCover, {193, 57, 158, 210, 121, 132});
		// near the format the filter draws in, which mpv shows as it is: converting each frame to yuv420p cost 6.5-7 ms
		// a frame on the A55s, more than drawing Waves or Spectrum. RGBA's alpha dropped (rgb0, a byte shuffle: 0.5 ms):
		// mpv would lay a clear background over a checkerboard; Spectrum's YUV as it comes
		if (!overCover) return "[aid1] asplit [ao] [v]; [v] " + draw + (vis == VIS_SPECTRUM ? "" : ",format=rgb0") + " [vo]";
		// the art fitted once, as one frame, and cut in two; only the 640x200 strip under it is drawn each frame (the
		// visualizer over its slice of the art), in the visualizer's own format so no frame is converted.
		// Redrawing the whole picture cost 3.5-4.4 ms a frame on a Mac and over the 33 ms budget on the A55s, on the
		// thread that feeds the sound. The art's slice is overlay's main input: its one frame is held, and a frame is
		// made for each of the visualizer's, till the sound ends (its timebase fine, or the frames' times would be
		// rounded to the picture's 25ths). Bars' and Waves' background is already clear, and their colours already
		// scaled by their alpha (Waves' edges fade), hence premultiplied; Spectrum's is black, so it's drawn with alpha
		// (its loudness, 4x: only the near-black clear, as colorkey did, without its 5 ms a frame).
		// vstack repeats the top's ended frame, as Circle's hstack does
		bool yuv = vis == VIS_SPECTRUM;
		std::string strip = yuv ? "[a] " + draw + ":opacity=4,format=yuva444p" : "[a] " + draw;
		return "[aid1] asplit [ao] [a]; "
			   "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=rgba" +
			   std::string(yuv ? ",scale=out_range=full,format=yuv444p" : "") + ",settb=AVTB,split [art1] [art2]; "
			   "[art1] crop=640:280:0:0 [top]; "
			   "[art2] crop=640:200:0:280 [under]; " +
			   strip + " [s]; "
			   "[under] [s] overlay=format=" + (yuv ? "yuv444" : "rgb:alpha=premultiplied") + " [strip]; "
			   "[top] [strip] vstack [vo]";
	}

	std::string blankGraph()
	{
		return "[aid1] asplit [ao] [a]; [a] showwaves=s=32x24:rate=5,drawbox=c=black:t=fill,format=yuv420p [vo]";
	}

	std::string serializeSettings(const Settings& s)
	{
		std::string out = serializeSubStyle(s.style);
		out += std::string("music-show = ") + SHOW_IDS[s.musicShow] + "\n";
		out += std::string("visualizer = ") + VIS_IDS[s.visualizer] + "\n";
		out += std::string("loop = ") + LOOP_IDS[s.loop] + "\n";
		out += std::string("manga-mode = ") + MANGA_MODE_IDS[s.mangaMode] + "\n";
		out += std::string("manga-fit = ") + MANGA_FIT_IDS[s.mangaFit] + "\n";
		out += "manga-preload = " + std::to_string(s.mangaPreload) + "\n";
		out += "manga-hinge = " + std::to_string(s.mangaHinge) + "\n";
		out += std::string("manga-cache = ") + MANGA_CACHE_IDS[s.mangaCacheSize] + "\n";
		for (auto& r : REMEMBER) out += std::string(r.key) + " = " + (s.*r.field ? "yes" : "no") + "\n";
		out += std::string("clock = ") + CLOCK_IDS[s.clock] + "\n";
		out += std::string("screen-off = ") + SCREEN_OFF_IDS[s.screenOffAfter] + "\n";
		out += std::string("pan-area = ") + (s.panRight ? "right" : "left") + "\n";
		out += std::string("pan-drag = ") + (s.dragBox ? "box" : "photo") + "\n";
		out += std::string("eq = ") + (s.eqOn ? "on" : "off") + "\n";
		out += "eq-preset = " + s.eqPreset + "\n";
		out += "manga-langs = " + s.mangaLangs + "\n";
		out += "eq-origin = " + s.eqOrigin + "\n";
		out += "eq-gains = " + formatEqGains(s.eqGains) + "\n";
		out += std::string("sound-effect = ") + soundFxKey(s.soundFx) + "\n";
		out += std::string("web-dashboard = ") + (s.webDashboard ? "on" : "off") + "\n";
		if (s.updateRemindAfter > 0) out += "update-remind-after = " + std::to_string(s.updateRemindAfter) + "\n";
		return out;
	}

	Settings parseSettings(const std::string& text)
	{
		Settings s;
		s.style = parseSubStyle(text);	// it leaves out the keys below
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			size_t eq = line.find('=');
			if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
			auto trim = [](std::string v) {
				size_t a = v.find_first_not_of(" \t\r"), b = v.find_last_not_of(" \t\r");
				return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
			};
			std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
			for (auto& r : REMEMBER)
				if (key == r.key && (value == "yes" || value == "no")) s.*r.field = value == "yes";
			for (int v = 0; v < SHOWS; v++)
				if (key == "music-show" && value == SHOW_IDS[v]) s.musicShow = v;
			for (int v = 0; v < VISUALIZERS; v++)
				if (key == "visualizer" && value == VIS_IDS[v]) s.visualizer = v;
			// Timeline (0.5.x, briefly) became Peaks; Bars, Waves and Spectrum are retired, read as Peaks
			if (key == "visualizer" && value == "timeline") s.visualizer = VIS_PEAKS;
			if (key == "visualizer" && (s.visualizer == VIS_BARS || s.visualizer == VIS_WAVES || s.visualizer == VIS_SPECTRUM))
				s.visualizer = VIS_PEAKS;
			for (int v = 0; v < LOOPS; v++)
				if (key == "loop" && value == LOOP_IDS[v]) s.loop = v;
			for (int v = 0; v < CLOCKS; v++)
				if (key == "clock" && value == CLOCK_IDS[v]) s.clock = v;
			for (int v = 0; v < SCREEN_OFFS; v++)
				if (key == "screen-off" && value == SCREEN_OFF_IDS[v]) s.screenOffAfter = v;
			for (int v = 0; v < MANGA_MODES; v++)
				if (key == "manga-mode" && value == MANGA_MODE_IDS[v]) s.mangaMode = v;
			for (int v = 0; v < MANGA_FITS; v++)
				if (key == "manga-fit" && value == MANGA_FIT_IDS[v]) s.mangaFit = v;
			for (int v = 0; v < MANGA_PRELOAD_COUNT; v++)
				if (key == "manga-preload" && value == std::to_string(MANGA_PRELOADS[v])) s.mangaPreload = MANGA_PRELOADS[v];
			for (int v = 0; v < MANGA_CACHES; v++)
				if (key == "manga-cache" && value == MANGA_CACHE_IDS[v]) s.mangaCacheSize = v;
			// (0 to 120: a gap no panel pair has more of)
			if (key == "manga-hinge") { int g = atoi(value.c_str()); if (g >= 0 && g <= 120 && !value.empty()) s.mangaHinge = g; }
			if (key == "pan-area" && (value == "left" || value == "right")) s.panRight = value == "right";
			if (key == "pan-drag" && (value == "photo" || value == "box")) s.dragBox = value == "box";
			if (key == "eq" && (value == "on" || value == "off")) s.eqOn = value == "on";
			if (key == "eq-preset") s.eqPreset = value;
			if (key == "manga-langs") s.mangaLangs = value;
			if (key == "eq-origin") s.eqOrigin = value;
			if (key == "eq-gains") parseEqGains(value, s.eqGains);
			if (key == "sound-effect") s.soundFx = soundFxFromKey(value);
			if (key == "web-dashboard" && (value == "on" || value == "off")) s.webDashboard = value == "on";
			if (key == "update-remind-after") s.updateRemindAfter = atoll(value.c_str());
			// the one setting before 0.1.1, kept: auto, cover-bars, bars, waves, spectrum, cover
			if (key == "audio-display")
			{
				if (value == "cover-bars") { s.musicShow = SHOW_BOTH; s.visualizer = VIS_PEAKS; }
				else if (value == "bars" || value == "waves" || value == "spectrum")
				{
					s.musicShow = SHOW_VIS;
					s.visualizer = VIS_PEAKS;	// retired: read as Peaks
				}
				else s.musicShow = SHOW_ART;
			}
		}
		return s;
	}
}
