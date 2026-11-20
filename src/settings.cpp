#include <cstdio>
#include "settings.h"
#include "theme.h"
#include "viz_more.h"
#include "vizcache.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace bp
{
	// the document viewers' choices, by name (textreader.h's order)
	static const char* TXT_FONT_IDS[] = {"serif", "sans", "mono", "pixel", "cjk-pixel"};
	static const char* TXT_SIZE_IDS[] = {"small", "medium", "large", "larger", "largest"};
	static const char* TXT_SPACING_IDS[] = {"tight", "normal", "loose"};
	static const char* TXT_MARGIN_IDS[] = {"narrow", "normal", "wide"};
	static const char* TXT_THEME_IDS[] = {"match", "dark", "light", "sepia", "black"};
	static const char* DOC_FILTER_IDS[] = {"off", "warm", "warmer", "dim"};
	static const char* TXT_VIEW_IDS[] = {"scroll", "book-left", "book-right"};
	static const char* TXT_TURN_IDS[] = {"fold", "off"};

	static const char* SHOW_IDS[SHOWS] = {"art", "both", "visualizer", "none"};
	static const char* SHOW_LABELS[SHOWS] = {"Album art only", "Art + visualizer", "Visualizer only", "None"};
	static const char* VIS_IDS[VISUALIZERS] = {"bars", "waves", "spectrum", "circle", "peaks", "peaks-yellow", "faders", "led",
		"synthwave", "banana-stack", "night-city", "oscilloscope", "heartbeat", "campfire", "aurora", "sound-garden", "turntable", "pixel-sea",
		"bunny-hop", "bubble-tea", "jellyfish", "music-box", "fishbowl", "disco-ball", "banana-dancer", "vibing-cat",
		"popcorn-machine", "jelly-wobble", "tube-man", "long-cat", "washing-machine", "mirror-bars", "wave", "dots", "levels", "area",
		"radial", "horizon", "steps", "barcode", "rings", "ribbon", "grid", "starfield", "strings", "helix", "sparks", "bouncy-keys", "rocket", "lanterns", "sleepy-moon", "dance-line"};
	static const char* VIS_LABELS[VISUALIZERS] = {"Bars", "Waves", "Spectrum", "Circle", "Peaks", "Peaks", "Faders",
		"Hi-fi LED", "Synthwave", "Banana stack", "Night city", "Oscilloscope", "Heartbeat", "Campfire", "Aurora",
		"Sound garden", "Turntable", "Pixel sea", "Bunny hop",
		"Bubble tea", "Jellyfish", "Music box", "Fishbowl", "Disco ball", "Banana dancer", "Vibing cat",
		"Popcorn", "Jelly wobble", "Tube man", "Long cat", "Washing machine", "Mirror bars", "Wave", "Dots", "Levels", "Area", "Radial", "Horizon", "Steps", "Barcode", "Rings",
		"Ribbon", "Grid", "Starfield", "Strings", "Helix", "Sparks", "Bouncy keys", "Rocket", "Lanterns",
		"Sleepy moon", "Dance line"};
	static const char* LOOP_IDS[LOOPS] = {"off", "all", "one"};
	static const char* MANGA_MODE_IDS[MANGA_MODES] = {"rtl", "ltr", "strip"};
	static const char* MANGA_FIT_IDS[MANGA_FITS] = {"page", "width", "fill"};
	static const char* MANGA_CACHE_IDS[MANGA_CACHES] = {"100m", "300m", "1g"};
	static const long long MANGA_CACHE_MB[MANGA_CACHES] = {100, 300, 1024};
	static const char* CLOCK_IDS[CLOCKS] = {"24h", "12h", "off"};
	static const char* SCREEN_OFF_IDS[SCREEN_OFFS] = {"never", "15s", "30s", "1m", "2m", "5m"};
	static const char* SCREEN_OFF_LABELS[SCREEN_OFFS] = {"Never", "15 seconds", "30 seconds", "1 minute", "2 minutes", "5 minutes"};
	static const char* LID_SHOULDER_IDS[LID_SHOULDERS] = {"ignored", "hold", "as-usual"};
	static const char* LID_SHOULDER_LABELS[LID_SHOULDERS] = {"Ignored", "Hold", "As usual"};
	static const char* LID_POWER_IDS[LID_POWERS] = {"play-pause", "sleep"};
	static const char* LID_POWER_LABELS[LID_POWERS] = {"Play / pause", "Sleep"};
	static const char* LID_VOLUME_IDS[LID_VOLUMES] = {"volume-and-skip", "volume-only"};
	static const char* LID_VOLUME_LABELS[LID_VOLUMES] = {"Volume and skip", "Volume only"};
	static const char* LID_INCOGNITO_IDS[LID_INCOGNITOS] = {"nothing", "close-all", "quit"};
	static const char* LID_INCOGNITO_LABELS[LID_INCOGNITOS] = {"Nothing", "Close all", "Quit"};

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

	static const char* LOOK_IDS[PICTURE_LOOKS] = {"off", "sharpen", "soften", "scanlines", "lcd-grid", "retro"};
	static const char* LOOK_LABELS[PICTURE_LOOKS] = {"Off", "Sharpen", "Soften", "Scanlines", "LCD grid", "Retro"};

	// One software filter each (decoding is software only, so no shader: a GLES 2 chip's GPU stays with the picture).
	// The lines and cells are in the film's own pixels, so they are an approximation of the spec's panel pixels.
	const char* pictureLookFilter(int look)
	{
		switch (look)
		{
		case LOOK_SHARPEN: return "lavfi=[unsharp=5:5:0.9:5:5:0.0]";
		case LOOK_SOFTEN: return "lavfi=[unsharp=5:5:-0.7:5:5:0.0]";
		case LOOK_SCANLINES: return "lavfi=[drawgrid=w=0:h=4:t=2:c=black@0.35]";
		case LOOK_LCD: return "lavfi=[drawgrid=w=4:h=4:t=1:c=black@0.3]";
		case LOOK_RETRO: return "lavfi=[drawgrid=w=0:h=4:t=2:c=black@0.3,eq=saturation=1.2:contrast=1.05]";
		}
		return "";
	}

	static void readVideoAdjust(Settings& s, const std::string& v)
	{
		int a[4];
		if (sscanf(v.c_str(), "%d,%d,%d,%d", &a[0], &a[1], &a[2], &a[3]) == 4)
			for (int i = 0; i < 4; i++) s.videoAdjust[i] = std::clamp(a[i], -100, 100);
	}
	static bool writeVideoAdjust(const Settings& s, std::string& v)
	{
		if (!s.videoAdjust[0] && !s.videoAdjust[1] && !s.videoAdjust[2] && !s.videoAdjust[3]) return false;
		v = std::to_string(s.videoAdjust[0]) + "," + std::to_string(s.videoAdjust[1]) + "," + std::to_string(s.videoAdjust[2]) + "," + std::to_string(s.videoAdjust[3]);
		return true;
	}

	int screenOffSeconds(int v)
	{
		static const int SECS[SCREEN_OFFS] = {0, 15, 30, 60, 120, 300};
		return v >= 0 && v < SCREEN_OFFS ? SECS[v] : 0;
	}

	const char* screenOffLabel(int v)
	{
		return v >= 0 && v < SCREEN_OFFS ? SCREEN_OFF_LABELS[v] : SCREEN_OFF_LABELS[0];
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

	int visualizerReplacement(int v)
	{
		switch (v)
		{
		case VIS_CIRCLE: return VIS_MIRROR;
		case VIS_BOBA: return VIS_DOTS;
		case VIS_GARDEN:
		case VIS_AREA: return VIS_HORIZON;
		case VIS_LONGCAT:
		case VIS_RADIAL: return VIS_STEPS;
		// dropped (the owner's final list, 2026-11-19): their code is gone
		case VIS_MUSICBOX:
		case VIS_JELLY:
		case VIS_WAVE:
		case VIS_LEVELS:
		case VIS_FIRE:
		case VIS_BUNNY:
		case VIS_FISHBOWL:
		case VIS_CAT:
		case VIS_POPCORN: return VIS_PEAKS_BANANA;
		default: return v;
		}
	}

	bool visualizerOnGround(int v)
	{
		return v == VIS_PEAKS_BANANA || v == VIS_MIRROR || v == VIS_DOTS || v == VIS_HORIZON || v == VIS_STEPS || v == VIS_BARCODE ||
			   v == VIS_RINGS || v == VIS_FADERS || v == VIS_RIBBON || v == VIS_GRID || v == VIS_STARFIELD || v == VIS_STRINGS || v == VIS_HELIX || v == VIS_SPARKS ||
			   v >= VIS_BOUNCY;
	}
	const char* loopLabel(int v) { return LOOP_LABELS[v >= 0 && v < LOOPS ? v : 0]; }

	bool lavfiSafePath(const std::string& dir)
	{
		return !dir.empty() && dir.find_first_of(":='\\") == std::string::npos;
	}

	// a path as a filter's argument: quoted (spaces, commas, brackets, ';'); one with ':', '=', a quote or a
	// backslash can't be named at all
	bool lavfiPath(const std::string& p, std::string& out)
	{
		if (!lavfiSafePath(p)) return false;
		out = "'" + p + "'";
		return true;
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
	struct PeaksColours { int barY, barU, barV, capY, capU, capV; int groundY = 16, groundU = 128, groundV = 128; };
	// a colour in BT.601 limited YUV, as mpv reads a picture this size (Peaks' numbers: #FFD23F is 193/57/158)
	static void yuv601(theme::Rgb c, int& y, int& u, int& v)
	{
		double r = c.r / 255.0, g = c.g / 255.0, b = c.b / 255.0;
		y = (int)std::floor(16 + 65.481 * r + 128.553 * g + 24.966 * b + 1e-9);	// (down: as Peaks' own numbers were)
		u = (int)std::lround(128 - 37.797 * r - 74.203 * g + 112.0 * b);
		v = (int)std::lround(128 + 112.0 * r - 93.786 * g - 18.214 * b);
	}
	// Peaks in the theme (docs/redesign, Themes: the bars ACCENT, the caps TEXT, on BG when alone; Yellow's are
	// today's yellow, cream and black). A bar's, a cap's and the ground's Y must differ (the planes are told apart by it)
	static PeaksColours peaksThemed()
	{
		PeaksColours c{};
		yuv601(theme::ACCENT, c.barY, c.barU, c.barV);
		yuv601(theme::TEXT, c.capY, c.capU, c.capV);
		yuv601(theme::BG, c.groundY, c.groundU, c.groundV);
		auto apart = [](int& y, int from) { if (y == from) y += y < 235 ? 1 : -1; };
		apart(c.capY, c.barY);
		apart(c.barY, c.groundY);
		apart(c.capY, c.groundY);
		apart(c.capY, c.barY);
		return c;
	}
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
			 ":1:flags=area,format=gray,bplut=c0='val*252/255',split [pkv] [pkp]; "
			 "[pkp] format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.8679,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*250)',format=gray," + stretch + " [pkP]; "
			 "[pkv] " + stretch + " [pkV]; "
			 "color=black:s=" + w + "x" + h + ":r=1:d=1,format=gray,geq=lum='if(gte(mod(X," + std::to_string(BW) + ")," +
			 std::to_string(BW - 1) + "),253,1+floor(251*(" + h + "-1-Y)/(" + h + "-1)))' [pkR]; "
			 // 254: a cap (over the cap's value, within cap steps of it), else the row's height (253: a gap)
			 "[pkP] [pkR] bplut2=c0='if(gt(y,x)*lte(y,x+" + cap + "),254,y)' [pkQ]; ";
		if (overCover)
		{
			// what each pixel is (0 nothing, 1 bar, 2 cap), then each plane: the art's where nothing, else a colour
			auto paint = [](const char* bar, const char* capc) {
				return std::string("'if(eq(x,0),y,if(eq(x,1),") + bar + "," + capc + "))'";
			};
			return g + "[pkV] [pkQ] bplut2=c0='if(eq(y,254),2,if(lte(y,x)*lt(y,253),1,0))',split=3 [pkc0] [pkc1] [pkc2]; "
					   "[pkc0] scale=640:200:flags=neighbor [pkcy]; "
					   "[pkcy] [pkay] bplut2=c0=" + paint(bY.c_str(), cY.c_str()) + " [pkY]; "
					   "[pkc1] [pkau] bplut2=c0=" + paint(bU.c_str(), cU.c_str()) + " [pkU]; "
					   "[pkc2] [pkav] bplut2=c0=" + paint(bV.c_str(), cV.c_str()) + " [pkW]; "
					   "[pkY] [pkU] [pkW] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		}
		// the luma at half size, then U and V from it (a lut each) and the luma scaled up
		g += "[pkV] [pkQ] bplut2=c0='if(eq(y,254)," + cY + ",if(lte(y,x)*lt(y,253)," + bY + "," + std::to_string(c.groundY) + "))',split=3 [pky] [pku] [pkw]; "
			 "[pky] scale=640:480:flags=neighbor [pkY]; "
			 "[pku] bplut=c0='if(eq(val," + cY + ")," + cU + ",if(eq(val," + bY + ")," + bU + "," + std::to_string(c.groundU) + "))' [pkU]; "
			 "[pkw] bplut=c0='if(eq(val," + cY + ")," + cV + ",if(eq(val," + bY + ")," + bV + "," + std::to_string(c.groundV) + "))' [pkW]; "
			 "[pkY] [pkU] [pkW] mergeplanes=0x001020:yuv420p";
		// a faint line every 96 pixels (white at 10%), over the bars too: four 1-pixel boxes
		for (int y = 96; y < 480; y += 96) g += ",drawbox=x=0:y=" + std::to_string(y) + ":w=640:h=1:c=white@0.1:t=fill";
		return g + " [vo]";
	}

	// Pocket console: Peaks as an old handheld's screen. The spectrum in 15 bars and their falling caps, as Peaks measures
	// them, drawn at the LCD's own size (80 dots across, a dot each pixel: 15 bars of 4 dots and a gap 1), every pixel one of
	// three greens by a lookup (the rows' "height" picture is still: one frame, repeated), then scaled 8x (nearest: a dot
	// 8 pixels square) and a still grid of 1-pixel lines laid over the luma, so the dots show. The bars are dark, their
	// top third mid green; the caps and the floor mid green; the screen the lightest. YUV only, nothing converted.
	// Over the art: the art's top as it is, and under it the console's 25 dots high with no screen of their own: the art
	// shows through everywhere but the lit dots
	double colourContrast(theme::Rgb a, theme::Rgb b)
	{
		auto lum = [](theme::Rgb c) {
			auto ch = [](int v) { double x = v / 255.0; return x <= 0.03928 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4); };
			return 0.2126 * ch(c.r) + 0.7152 * ch(c.g) + 0.0722 * ch(c.b);
		};
		double la = lum(a), lb = lum(b);
		return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
	}

	theme::Rgb inkOn(theme::Rgb c, theme::Rgb on, theme::Rgb to, double ratio)
	{
		// 10% steps towards `to`, then on to black or white (whichever is further from `on`)
		for (int k = 0; k <= 100; k += 10)
		{
			theme::Rgb t = theme::mix(c, to, k);
			if (colourContrast(t, on) >= ratio) return t;
		}
		theme::Rgb far = colourContrast(theme::Rgb{0, 0, 0}, on) > colourContrast(theme::Rgb{255, 255, 255}, on) ? theme::Rgb{0, 0, 0}
			: theme::Rgb{255, 255, 255};
		for (int k = 10; k <= 100; k += 10)
		{
			theme::Rgb t = theme::mix(to, far, k);
			if (colourContrast(t, on) >= ratio) return t;
		}
		return far;
	}

	theme::Rgb visualizerInk()
	{
		// the preview's derived ink: ACCENT moved towards TEXT in 10% steps till it reaches 3:1 on BG (so a light
		// theme's accent doesn't vanish on its ground)
		return inkOn(theme::ACCENT, theme::BG, theme::TEXT, 3.0);
	}

	theme::Rgb visualizerLine()
	{
		// a quiet rule that still shows: RULE is very close to BG in the dark themes, so it goes towards DIM
		const bool light = colourContrast(theme::BG, theme::Rgb{0, 0, 0}) > colourContrast(theme::BG, theme::Rgb{255, 255, 255});
		return theme::mix(theme::RULE, theme::DIM, light ? 25 : 35);
	}

	bool visualizerNeedsMaps(int vis)
	{
		vis = visualizerReplacement(vis);
		return (vis >= VIS_LED && vis < VIS_MIRROR) || vis == VIS_RINGS;
	}

	bool writeVisualizerMaps(const std::string& dir)
	{
		// the retired and dropped styles' stills (Circle's and Radial's tables, Sound garden's, Bubble tea's,
		// Jellyfish's, Music box's, Long cat's, Campfire's, Bunny hop's, Fishbowl's, Vibing cat's and Popcorn's
		// pictures): nothing reads them any more
		static const char* RETIRED[] = {"circle3-x.pgm", "circle3-y.pgm", "garden1-alone.pgm", "garden1-strip.pgm", "garden1-bees.pgm",
			"musicbox1-comb.pgm", "musicbox1-combs.pgm", "musicbox1-atlas.pgm", "musicbox1-y.pgm", "musicbox1-u.pgm", "musicbox1-v.pgm",
			"lcat9-head-a.pgm", "lcat9-trow-a.pgm", "lcat9-tcol-a.pgm", "lcat9-hue-a.pgm", "lcat9-dist-a.pgm", "lcat9-head-s.pgm",
			"lcat9-trow-s.pgm", "lcat9-tcol-s.pgm", "lcat9-hue-s.pgm", "lcat9-dist-s.pgm", "lcat9-row.pgm", "lcat9-col.pgm",
			"boba1-alone.pgm", "boba2-strip.pgm", "boba1-wave.pgm", "boba1-fizz.pgm", "jelly2-s0.pgm", "jelly2-s1.pgm", "jelly2-s2.pgm",
			"jelly2-s3.pgm", "jelly2-o0.pgm", "jelly2-o1.pgm", "jelly2-o2.pgm", "jelly2-o3.pgm", "jelly2-halo.pgm", "jelly2-ohalo.pgm",
			"jelly2-speck.pgm", "jelly2-row.pgm", "radial1-ax.pgm", "radial1-ay.pgm", "radial1-sx.pgm", "radial1-sy.pgm",
			"radial2-ax.pgm", "radial2-ay.pgm", "radial2-sx.pgm", "radial2-sy.pgm", "radial3-ax.pgm", "radial3-ay.pgm", "radial3-sx.pgm",
			"radial3-sy.pgm", "fire1-a.pgm", "fire2-as.pgm", "fire1-n.pgm", "fire1-s.pgm", "fire1-y.pgm", "fire1-u.pgm", "fire1-v.pgm",
			"bun1-d.pgm", "bun1-c.pgm", "bun1-sd.pgm", "bun1-sc.pgm", "bun1-cloud.pgm", "fishbowl1-y.pgm", "fishbowl1-u.pgm",
			"fishbowl1-v.pgm", "fishbowl1-row.pgm", "fishbowl1-srow.pgm", "fishbowl1-col.pgm", "fishbowl1-ramp.pgm", "fishbowl1-weed.pgm",
			"fishbowl1-bub.pgm", "fishbowl1-sbub.pgm", "cat2-scene-a.pgm", "cat2-row-a.pgm", "cat2-col-a.pgm", "cat2-scene-s.pgm",
			"cat2-row-s.pgm", "cat2-col-s.pgm", "cat2-tail.pgm", "cat2-notes.pgm", "cat2-steam.pgm", "popcorn2-scene.pgm",
			"popcorn2-scenes.pgm", "popcorn2-pile.pgm", "popcorn2-piles.pgm", "popcorn2-pops.pgm", "popcorn2-popss.pgm", "popcorn2-lid.pgm",
			"popcorn2-phase.pgm"};
		int removed = 0;
		for (const char* f : RETIRED) removed += unlink((dir + "/" + f).c_str()) == 0;
		if (removed) fprintf(stderr, "visualizer: %d stills of the retired styles removed from %s\n", removed, dir.c_str());
		bool ok = writeLedMaps(dir);
		ok = writeSynthwaveMaps(dir) && ok;
		ok = writeBananaMaps(dir) && ok;
		ok = writeCityMaps(dir) && ok;
		ok = writeScopeMaps(dir) && ok;
		ok = writeHeartMaps(dir) && ok;
		ok = writeAuroraMaps(dir) && ok;
		ok = writeVinylMaps(dir) && ok;
		ok = writeSeaMaps(dir) && ok;
		ok = writeDiscoMaps(dir) && ok;
		ok = writeDancerMaps(dir) && ok;
		ok = writeWobbleMaps(dir) && ok;
		ok = writeTubemanMaps(dir) && ok;
		ok = writeWasherMaps(dir) && ok;
		ok = writeRingsMaps(dir) && ok;
		return ok;
	}

	std::string visualizerGraph(int vis, bool overCover, const std::string& mapsDir, int fps)
	{
		return vizPlainLuts(vizAtFps(visualizerGraphMarked(vis, overCover, mapsDir), fps));
	}

	std::string visualizerGraphMarked(int vis, bool overCover, const std::string& mapsDir)
	{
		vis = visualizerReplacement(vis);
		// the styles drawn only by the player (2026-11-19, no graph of their own): as Peaks where a graph is needed
		if (vis == VIS_FADERS || vis >= VIS_RIBBON) vis = VIS_PEAKS_BANANA;
		// the simple ones (src/viz_simple.cpp)
		switch (vis)
		{
		case VIS_MIRROR: return mirrorGraph(overCover);
		case VIS_DOTS: return dotsGraph(overCover);
		case VIS_HORIZON: return horizonGraph(overCover);
		case VIS_STEPS: return stepsGraph(overCover);
		case VIS_BARCODE: return barcodeGraph(overCover);
		default: break;
		}
		// the newer ones (a file each, src/viz_*.cpp); with no stills yet, as Peaks
		if (vis >= VIS_LED && vis < VISUALIZERS)
		{
			std::string q;
			if (mapsDir.empty() || !lavfiPath(mapsDir, q)) vis = VIS_PEAKS_BANANA;
			else switch (vis)
			{
			case VIS_LED: return ledGraph(overCover, mapsDir);
			case VIS_SYNTHWAVE: return synthwaveGraph(overCover, mapsDir);
			case VIS_BANANA_STACK: return bananaGraph(overCover, mapsDir);
			case VIS_CITY: return cityGraph(overCover, mapsDir);
			case VIS_SCOPE: return scopeGraph(overCover, mapsDir);
			case VIS_HEART: return heartGraph(overCover, mapsDir);
			case VIS_AURORA: return auroraGraph(overCover, mapsDir);
			case VIS_VINYL: return vinylGraph(overCover, mapsDir);
			case VIS_SEA: return seaGraph(overCover, mapsDir);
			case VIS_DISCO: return discoGraph(overCover, mapsDir);
			case VIS_DANCER: return dancerGraph(overCover, mapsDir);
			case VIS_WOBBLE: return wobbleGraph(overCover, mapsDir);
			case VIS_TUBEMAN: return tubemanGraph(overCover, mapsDir);
			case VIS_RINGS: return ringsGraph(overCover, mapsDir);
			default: return washerGraph(overCover, mapsDir);
			}
		}
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
		if (vis == VIS_PEAKS_BANANA) return peaksGraph(overCover, peaksThemed());
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
		// vstack repeats the top's ended frame
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

	std::string blankGraph(const std::string& ground)
	{
		return "[aid1] asplit [ao] [a]; [a] showwaves=s=32x24:rate=5,drawbox=c=" + ground + ":t=fill,format=yuv420p [vo]";
	}


	SettingDef SettingDef::flag(const char* key, bool Settings::*f, const char* no, const char* yes)
	{
		// the words live as long as the table: a pair is kept per distinct (no, yes)
		static std::vector<std::vector<const char*>*> pairs;
		std::vector<const char*>* found = nullptr;
		for (auto* p : pairs)
			if (std::strcmp((*p)[0], no) == 0 && std::strcmp((*p)[1], yes) == 0) found = p;
		if (!found) pairs.push_back(found = new std::vector<const char*>{no, yes});
		SettingDef d;
		d.kind = FLAG; d.key = key; d.flagField = f; d.ids = found->data(); d.count = 2;
		return d;
	}

	SettingDef SettingDef::choice(const char* key, int Settings::*f, const char* const* ids, int count)
	{
		SettingDef d;
		d.kind = CHOICE; d.key = key; d.intField = f; d.ids = ids; d.count = count;
		return d;
	}

	SettingDef SettingDef::ints(const char* key, int Settings::*f, const int* values, int count)
	{
		SettingDef d;
		d.kind = INT_LIST; d.key = key; d.intField = f; d.values = values; d.count = count;
		return d;
	}

	SettingDef SettingDef::range(const char* key, int Settings::*f, int min, int max, int snap)
	{
		SettingDef d;
		d.kind = INT_RANGE; d.key = key; d.intField = f; d.min = min; d.max = max; d.snap = snap;
		return d;
	}

	SettingDef SettingDef::text(const char* key, std::string Settings::*f)
	{
		SettingDef d;
		d.kind = TEXT; d.key = key; d.textField = f;
		return d;
	}

	SettingDef SettingDef::custom(const char* key, void (*parse)(Settings&, const std::string&), bool (*write)(const Settings&, std::string&))
	{
		SettingDef d;
		d.kind = CUSTOM; d.key = key; d.parse = parse; d.write = write;
		return d;
	}

	// ---- the table: what a line of settings.conf means, and what the Settings page shows for it ----
	// In the order the file is written (an old file is read by key, so that order matters only for byte-for-byte
	// output). A choice is stored as its index in `ids`; the page's `place` orders a group's rows (the page's own rows,
	// such as Clear play history, have places of their own in ui_settings.cpp); a default is the member's initializer.

	// a style not offered (Bars, Waves, Spectrum, the ones kept but not offered, and Lite, removed for now: VIS_SHOWN)
	// is read as VIS_PEAKS_BANANA ("Peaks") (the default), and so is Timeline (0.5.x, briefly: it became Peaks)
	static void readVisualizer(Settings& s, const std::string& value)
	{
		// a retired style (2026-11-19): its replacement, in its place (Circle: Mirror bars, Long cat and Radial: Steps...)
		s.visualizer = visualizerReplacement(s.visualizer);
		// the two replaced styles (2026-11-19): their saved ids load as the new styles in the same places
		if (value == "pocket") s.visualizer = VIS_FADERS;
		if (value == "rain-check") s.visualizer = VIS_LANTERNS;
		if (value == "timeline") s.visualizer = VIS_PEAKS_BANANA;
		// Lite isn't a choice any more: its values stay on branch vizlite
		if (value == "lite") s.visualizer = VIS_PEAKS_BANANA;
		if (std::find(std::begin(VIS_SHOWN), std::end(VIS_SHOWN), s.visualizer) == std::end(VIS_SHOWN)) s.visualizer = VIS_PEAKS_BANANA;
	}

	static void readSoundFx(Settings& s, const std::string& value) { s.soundFx = soundFxFromKey(value); }
	static bool writeSoundFx(const Settings& s, std::string& value) { value = soundFxKey(s.soundFx); return true; }
	static void readEqGains(Settings& s, const std::string& value) { parseEqGains(value, s.eqGains); }
	static bool writeEqGains(const Settings& s, std::string& value) { value = formatEqGains(s.eqGains); return true; }
	static void readRemindAfter(Settings& s, const std::string& value) { s.updateRemindAfter = atoll(value.c_str()); }
	// written only once there's a time to remember
	static bool writeRemindAfter(const Settings& s, std::string& value)
	{
		if (s.updateRemindAfter <= 0) return false;
		value = std::to_string(s.updateRemindAfter);
		return true;
	}

	const std::vector<SettingDef>& settingDefs()
	{
		using D = SettingDef;
		static const char* MODE_NAMES[MANGA_MODES] = {"Right to left", "Left to right", "Vertical scroll"};
		static const char* FIT_NAMES[MANGA_FITS] = {"Page", "Width", "Fill"};
		static const char* CACHE_NAMES[MANGA_CACHES] = {"100 MB", "300 MB", "1 GB"};
		static const char* CLOCK_NAMES[CLOCKS] = {"24-hour", "12-hour", "Off"};
		static const char* SHOWN_ADULT[2] = {"Hidden", "Shown"};
		static const char* SHOWN_DNS[2] = {"Off", "Cloudflare (1.1.1.1)"};
		static const char* SHOWN_ASK_VIDEO[2] = {"Stop", "Ask"};
		static const char* SHOWN_PAN[2] = {"Left", "Right"};
		static const char* SHOWN_DRAG[2] = {"The photo", "The box"};
		static const char* SHOWN_LID[2] = {"Pause", "Keep playing"};
		static const char* TXT_FONT_LABELS[5] = {"Serif", "Sans", "Mono", "Pixel", "CJK pixel"};
		static const char* TXT_SIZE_LABELS[5] = {"Small", "Medium", "Large", "Larger", "Largest"};
		static const char* TXT_SPACING_LABELS[3] = {"Tight", "Normal", "Loose"};
		static const char* TXT_MARGIN_LABELS[3] = {"Narrow", "Normal", "Wide"};
		static const char* TXT_THEME_LABELS[5] = {"Match theme", "Dark", "Light", "Sepia", "Black"};
		static const char* TXT_VIEW_LABELS[3] = {"Scroll", "Book, left", "Book, right"};
		static const char* PDF_VIEW_LABELS[2] = {"Single page", "Strip"};
		static const int VIZ_FPS[] = {30, 45, 60};
		static const int VOLUME_TOPS[] = {100, 120, 150};
		static const std::vector<SettingDef> table = {
			D::choice("music-show", &Settings::musicShow, SHOW_IDS, SHOWS)
				.page(SG_MUSIC, 10, "Music display", "What the top screen shows while a song plays (the Display chip, on the player or in More, while playing).").shownAs(SHOW_LABELS).stepped(true),
			D::flag("match-album-art", &Settings::matchAlbumArt)
				.page(SG_MUSIC, 15, "Match album art", "A song's art, blurred and tinted by the theme's own ground, lies behind the player and the top screen. Every key, chip and word keeps the theme's colours; a grey or clashing art falls back to the plain ground."),
			D::choice("visualizer", &Settings::visualizer, VIS_IDS, VISUALIZERS)
				.page(SG_MUSIC, 20, "Visualizer style", "Which visualizer is drawn: fifteen styles, from Peaks to Dance line (the Display chip, on the player or in More, while playing).")
				.shownAs(VIS_LABELS).stepped(true, stepVisualizer).afterRead(readVisualizer),
			D::ints("visualizer-fps", &Settings::vizFps, VIZ_FPS, 3)
				.page(SG_MUSIC, 30, "Visualizer frame rate", "How smoothly the visualizers move: 60 is smoothest, 30 lightest on the battery.")
				.shownAs(nullptr, " fps"),
			D::range("manga-filter", &Settings::mangaFilter, 0, 3),
			D::range("read-dim", &Settings::readDim, 0, 80, 10).stepped(false, nullptr, 10),
			D::ints("volume-max", &Settings::volumeMax, VOLUME_TOPS, 3)
				.page(SG_PLAYBACK, 40, "Max volume", "How far the volume keys go: past 100% the player turns the sound up itself (louder than the handheld's own top, a little rougher on loud parts). Over 120% can damage your hearing and the speakers.")
				.shownAs(nullptr, "%"),
			D::choice("loop", &Settings::loop, LOOP_IDS, LOOPS),
			D::choice("manga-mode", &Settings::mangaMode, MANGA_MODE_IDS, MANGA_MODES)
				.page(SG_MANGA, 10, "Reading mode", "How a series opens until you pick one for it (X while reading).").shownAs(MODE_NAMES).stepped(true),
			D::choice("manga-fit", &Settings::mangaFit, MANGA_FIT_IDS, MANGA_FITS)
				.page(SG_MANGA, 20, "Page fit", "Width: a page as wide as the screen, read down with RIGHT a screenful at a time.").shownAs(FIT_NAMES).stepped(true),
			// the document viewers' choices, also listed in Settings › Reading (a book's own Text sheet keeps them too)
			D::choice("text-font", &Settings::txtFont, TXT_FONT_IDS, 5)
				.page(SG_READING, 10, "Font", "The font a TXT or EPUB is read in.").shownAs(TXT_FONT_LABELS).stepped(true),
			D::choice("text-size", &Settings::txtSize, TXT_SIZE_IDS, 5)
				.page(SG_READING, 20, "Text size", "Five sizes. Medium is the smallest that reads comfortably on the handheld.").shownAs(TXT_SIZE_LABELS).stepped(true),
			D::choice("text-spacing", &Settings::txtSpacing, TXT_SPACING_IDS, 3)
				.page(SG_READING, 30, "Line spacing", "How far apart the lines are.").shownAs(TXT_SPACING_LABELS).stepped(true),
			D::choice("text-margins", &Settings::txtMargins, TXT_MARGIN_IDS, 3)
				.page(SG_READING, 40, "Margins", "The room either side of the text.").shownAs(TXT_MARGIN_LABELS).stepped(true),
			D::choice("text-theme", &Settings::txtTheme, TXT_THEME_IDS, TXT_PAGES)
				.page(SG_READING, 50, "Colours", "The page's colours. Match theme: the Light page with the Light theme, Black with High contrast, else Dark. Or dark, light, sepia or black whatever the theme.").shownAs(TXT_THEME_LABELS).stepped(true),
			D::choice("text-filter", &Settings::txtFilter, DOC_FILTER_IDS, 4),
			D::choice("pdf-filter", &Settings::pdfFilter, DOC_FILTER_IDS, 4),
			D::choice("text-view", &Settings::txtBook, TXT_VIEW_IDS, 3)
				.page(SG_READING, 70, "Text opens as", "Scroll: down the page. Book: held sideways, a page on each screen.").shownAs(TXT_VIEW_LABELS).stepped(true),
			D::choice("text-turn", &Settings::txtTurn, TXT_TURN_IDS, 2),
			D::flag("pdf-view", &Settings::pdfStrip, "page", "strip")
				.page(SG_READING, 80, "PDF opens as", "Single page, or Strip: the pages one after another.").shownAs(PDF_VIEW_LABELS),
			D::ints("manga-preload", &Settings::mangaPreload, MANGA_PRELOADS, MANGA_PRELOAD_COUNT)
				.page(SG_MANGA, 60, "Preload", "More: smoother on a slow server, more data.").shownAs(nullptr, " pages").stepped(true).digitsOnly(),
			// 0 to 200 in steps of 4 (a value a file kept from before may sit off the step: it snaps onto one on the page)
			D::range("manga-hinge", &Settings::mangaHinge, 0, 200)
				.page(SG_MANGA, 40, "Hinge gap", "The part of the page hidden under the hinge in fullscreen, so pictures line up across the gap. Measure on your handheld.")
				.shownAs(nullptr, " px").stepped(false, nullptr, 4).ignoreBlank(),
			D::choice("manga-cache", &Settings::mangaCacheSize, MANGA_CACHE_IDS, MANGA_CACHES)
				.page(SG_MANGA, 70, "Page cache", "Pages kept on the card to read again without downloading. Covers: 50 MB more.").shownAs(CACHE_NAMES).stepped(true),
			D::flag("remember-position", &Settings::rememberPosition)
				.page(SG_PLAYBACK, 10, "Remember position", "A video opens where you stopped. Songs always start at the beginning."),
			D::flag("remember-audio", &Settings::rememberAudio)
				.page(SG_PLAYBACK, 20, "Remember audio track", "Keeps each file's audio track. A new file starts in the language you chose last."),
			D::flag("remember-subtitles", &Settings::rememberSubs)
				.page(SG_PLAYBACK, 30, "Remember subtitles", "Keeps each file's subtitles. A new file starts in the language you chose last."),
			D::flag("remember-folder", &Settings::rememberFolder)
				.page(SG_FILES, 10, "Remember last folder", "The file list opens where you last were."),
			D::flag("keep-history", &Settings::keepHistory)
				.page(SG_FILES, 30, "Keep play history", "Keeps Recent. Turning it off also deletes the history."),
			D::flag("keep-network-history", &Settings::keepNetHistory)
				.page(SG_FILES, 40, "Network files in Recent", "Files played from a network folder go in Recent too. Off: they're left out, and the ones there now go."),
			D::flag("button-hotkeys", &Settings::buttonHotkeys)
				.page(SG_PLAYBACK, 50, "Button hotkeys", "Off: the buttons can't pause, skip or close while something plays (touch still can)."),
			D::flag("show-hidden-folders", &Settings::showHiddenFolders)
				.page(SG_FILES, 20, "Show hidden folders", "Shows the folders you've hidden (X on a folder), so you can unhide them."),
			D::flag("battery-percent", &Settings::batteryPercent)
				.page(SG_SCREEN, 120, "Battery percentage", "Shows the battery's percent beside its icon."),
			D::flag("scroll-titles", &Settings::scrollTitles)
				.page(SG_SCREEN, 125, "Scroll long titles", "A title too long for its place moves along after a moment, twice, then rests at its start. Off: it is cut with \u201c\u2026\u201d."),
			D::flag("keep-photo-zoom", &Settings::keepPhotoZoom)
				.page(SG_PHOTOS, 10, "Keep zoom between photos", "The next photo opens at the same zoom and place. Off: each opens whole."),
			D::flag("check-updates", &Settings::checkUpdates)
				.page(SG_UPDATES, 10, "Check automatically", "Looks for a new version a few seconds after the start (Wi-Fi needed)."),
			D::flag("same-brightness", &Settings::sameBrightness)
				.page(SG_SCREEN, 30, "Same for both screens", "One brightness for both screens. Off: each its own."),
			D::flag("shuffle", &Settings::shuffle),	// (no row: the play mode, as loop)
			D::flag("manga-reader", &Settings::mangaOn),
			D::flag("manga-intro-shown", &Settings::mangaIntroShown),
			D::flag("manga-ask-video", &Settings::mangaAskVideo, "stop", "ask")
				.page(SG_MANGA, 20, "Manga and a video", "Ask: opening Manga while a video plays asks first, and the video stops only when you say so (the same question as the Extensions page's Manga row). Stop: the video stops at once and Manga opens. A song or a photo never asks.").shownAs(SHOWN_ASK_VIDEO, ""),
			D::flag("manga-swap-lr", &Settings::mangaSwapLR)
				.page(SG_MANGA, 30, "Swap Left/Right", "Off: RIGHT is always forward. On: LEFT is forward (to match a right-to-left book)."),
			D::flag("manga-adult", &Settings::mangaAdult)
				.page(SG_MANGA, 100, "Adult sources", "Sources a repository marks as adult.").shownAs(SHOWN_ADULT),
			D::flag("manga-history", &Settings::mangaHistory)
				.page(SG_MANGA, 110, "Reading history", "Off clears it."),
			D::flag("manga-recent", &Settings::mangaRecent),	// (kept, not shown: the reader stays off Home)
			D::flag("manga-full-seen", &Settings::mangaFullSeen),
			D::flag("manga-open-full", &Settings::mangaOpenFull)
				.page(SG_MANGA, 50, "Open fullscreen", "A chapter opens on both screens. SELECT leaves."),
			D::flag("manga-secure-dns", &Settings::mangaSecureDns)
				.page(SG_MANGA, 120, "Secure DNS", "Looks up sites through Cloudflare over HTTPS (one.one.one.one), past a network that blocks them.").shownAs(SHOWN_DNS),
			// (the ids and names are theme.cpp's table: a theme added there is a value here)
			D::choice("theme", &Settings::theme, theme::themeIds(), theme::themeCount())
				.page(SG_SCREEN, 5, "Theme", "The app's colours, on both screens and on the Web dashboard. Every theme but High contrast is drawn Clean: the page has no edges, and the keys stay keys. High contrast is Classic. Videos, pictures, album art, subtitles and a book's page keep their own.")
				.shownAs(theme::themeNames()).stepped(true),
			D::choice("clock", &Settings::clock, CLOCK_IDS, CLOCKS)
				.page(SG_SCREEN, 130, "Clock", "The time in the top corner of this screen").shownAs(CLOCK_NAMES).stepped(true),
			// (not round: Never at one end, 5 minutes at the other)
			D::choice("screen-off", &Settings::screenOffAfter, SCREEN_OFF_IDS, SCREEN_OFFS)
				.page(SG_SCREEN, 40, "Screen off (music)", "While a song plays, this screen goes dark after that long untouched. A touch brings it back.").shownAs(SCREEN_OFF_LABELS),
			D::choice("top-screen-off", &Settings::topScreenOff, SCREEN_OFF_IDS, SCREEN_OFFS)
				.page(SG_SCREEN, 52, "Top screen off (music)", "While a song plays, the top screen goes black after that long without a press or touch. Any press or touch, or the next song, brings it back. Never keeps it lit.").shownAs(SCREEN_OFF_LABELS),
			D::flag("dim-before-off", &Settings::dimBeforeOff)
				.page(SG_SCREEN, 55, "Dim before turning off", "For the last 5 seconds of Screen off's timer this screen drops to a third of its brightness and counts down. A press or a touch then only cancels. Turned off by hand: no warning."),
			D::flag("stay-awake-jobs", &Settings::stayAwakeJobs)
				.page(SG_SCREEN, 128, "Stay awake for copies and downloads", "A copy, a move, a chapter download or an update keeps the handheld from sleeping on its own until it is done. Off: it may sleep in the middle."),
			D::choice("read-dim-after", &Settings::readDimAfter, SCREEN_OFF_IDS, SCREEN_OFFS)
				.page(SG_READING, 90, "Dim the controls while reading", "While a book, a comic or a document is open, this screen dims after that long untouched. A touch brings it back, and is still a page turn.").shownAs(SCREEN_OFF_LABELS),
			D::choice("picture-look", &Settings::pictureLook, LOOK_IDS, PICTURE_LOOKS)
				.page(SG_PLAYBACK, 35, "Picture look", "A look on every video: Sharpen, Soften, Scanlines, LCD grid or Retro. Songs and photos are left as they are. Software filters: a heavy film may stutter.").shownAs(LOOK_LABELS).stepped(true),
			D::custom("video-adjust", readVideoAdjust, writeVideoAdjust),
			D::choice("screen-off-video", &Settings::screenOffVideo, SCREEN_OFF_IDS, SCREEN_OFFS)
				.page(SG_SCREEN, 50, "Screen off (video)", "While a video plays, this screen goes dark after that long untouched. A touch brings it back.").shownAs(SCREEN_OFF_LABELS),
			D::flag("lid-close", &Settings::lidKeepPlaying, "pause", "keep-playing")
				.page(SG_SCREEN, 70, "When the lid closes", "Pause: closing the lid pauses and the handheld sleeps. Keep playing: a song or a video goes on with both screens off, and the handheld does not sleep while the lid is shut and something is loaded. Volume and Power still work.")
				.shownAs(SHOWN_LID),
			D::choice("lid-power", &Settings::lidPower, LID_POWER_IDS, LID_POWERS)
				.page(SG_SCREEN, 75, "Power key, lid shut", "With the lid shut. Play / pause: a press of the power key plays or pauses; hold it 2 s to sleep. Sleep: a press sleeps the handheld, as with the lid open.")
				.shownAs(LID_POWER_LABELS).stepped(true),
			D::choice("lid-volume", &Settings::lidVolume, LID_VOLUME_IDS, LID_VOLUMES)
				.page(SG_SCREEN, 76, "Volume keys, lid shut", "With the lid shut, a press of a volume key is volume. Volume and skip: holding one for 0.6 s goes to the next (+) or the previous (-). Volume only: holding does what it does with the lid open.")
				.shownAs(LID_VOLUME_LABELS).stepped(true),
			D::choice("lid-shoulder", &Settings::lidShoulder, LID_SHOULDER_IDS, LID_SHOULDERS)
				.page(SG_SCREEN, 80, "Shoulder keys, lid shut", "What L, R, L2 and R2 do while the lid is shut and something plays. Ignored: they do nothing. Hold: they act when held for 0.6 s. As usual: they act at once.")
				.shownAs(LID_SHOULDER_LABELS).stepped(true),
			D::choice("lid-incognito", &Settings::lidIncognito, LID_INCOGNITO_IDS, LID_INCOGNITOS)
				.page(SG_SCREEN, 90, "Lid or sleep in incognito", "What happens while incognito is on when the lid closes or the handheld goes to sleep. Close all: what plays stops, books and photos close, Home shows, and incognito is off.")
				.shownAs(LID_INCOGNITO_LABELS).stepped(true),
			D::flag("pan-area", &Settings::panRight, "left", "right")
				.page(SG_PHOTOS, 20, "Pan area", "Which side the pan area (the small photo) is on.").shownAs(SHOWN_PAN),
			D::flag("pan-drag", &Settings::dragBox, "photo", "box")
				.page(SG_PHOTOS, 30, "Drag moves", "The box: a drag moves the frame over the small photo. The photo: a drag moves the photo, as on a phone.").shownAs(SHOWN_DRAG),
			D::flag("eq", &Settings::eqOn, "off", "on"),
			D::text("eq-preset", &Settings::eqPreset),
			D::text("manga-langs", &Settings::mangaLangs),
			D::text("eq-origin", &Settings::eqOrigin),
			D::custom("eq-gains", readEqGains, writeEqGains),
			D::custom("sound-effect", readSoundFx, writeSoundFx),
			D::flag("web-dashboard", &Settings::webDashboard, "off", "on"),
			D::custom("update-remind-after", readRemindAfter, writeRemindAfter),
		};
		return table;
	}

	int settingIndex(const char* key)
	{
		const auto& defs = settingDefs();
		for (size_t i = 0; i < defs.size(); i++)
			if (std::strcmp(defs[i].key, key) == 0) return (int)i;
		return -1;
	}

	std::vector<int> settingsOfGroup(int group)
	{
		const auto& defs = settingDefs();
		std::vector<int> out;
		for (size_t i = 0; i < defs.size(); i++)
			if (defs[i].group == group) out.push_back((int)i);
		std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return defs[a].place < defs[b].place; });
		return out;
	}

	std::string settingValueText(const SettingDef& d, const Settings& s)
	{
		switch (d.kind)
		{
		case SettingDef::FLAG:
		{
			bool on = s.*d.flagField;
			return d.shown ? d.shown[on] : on ? "On" : "Off";
		}
		case SettingDef::CHOICE:
		{
			int v = s.*d.intField;
			return d.shown[v >= 0 && v < d.count ? v : 0];
		}
		default: return std::to_string(s.*d.intField) + d.unit;
		}
	}

	void stepSetting(const SettingDef& d, Settings& s, int dir)
	{
		switch (d.kind)
		{
		case SettingDef::FLAG: s.*d.flagField = !(s.*d.flagField); break;
		case SettingDef::CHOICE:
		{
			int& v = s.*d.intField;
			if (d.stepFn) v = d.stepFn(v, dir);
			else if (d.wrap) v = ((v + dir) % d.count + d.count) % d.count;
			else v = std::clamp(v + dir, 0, d.count - 1);
			break;
		}
		case SettingDef::INT_LIST:
		{
			// a value that isn't listed starts from the first
			int i = 0;
			for (int k = 0; k < d.count; k++)
				if (d.values[k] == s.*d.intField) i = k;
			i = d.wrap ? (i + (dir < 0 ? -1 : 1) + d.count) % d.count : std::clamp(i + dir, 0, d.count - 1);
			s.*d.intField = d.values[i];
			break;
		}
		case SettingDef::INT_RANGE:
		{
			int v = std::clamp(s.*d.intField + dir * d.stepBy, d.min, d.max);
			s.*d.intField = v - v % d.stepBy;
			break;
		}
		default: break;
		}
	}

	std::string serializeSettings(const Settings& s)
	{
		std::string out = serializeSubStyle(s.style);
		for (const auto& d : settingDefs())
		{
			std::string value;
			switch (d.kind)
			{
			case SettingDef::FLAG: value = d.ids[s.*d.flagField ? 1 : 0]; break;
			case SettingDef::CHOICE: value = d.ids[std::clamp(s.*d.intField, 0, d.count - 1)]; break;
			case SettingDef::INT_LIST: case SettingDef::INT_RANGE: value = std::to_string(s.*d.intField); break;
			case SettingDef::TEXT: value = s.*d.textField; break;
			case SettingDef::CUSTOM: if (!d.write(s, value)) continue; break;
			}
			out += std::string(d.key) + " = " + value + "\n";
		}
		return out;
	}

	// one line's value into the setting it names; what isn't a value of it leaves the setting as it was
	static void readSetting(const SettingDef& d, Settings& s, const std::string& value)
	{
		switch (d.kind)
		{
		case SettingDef::FLAG:
			if (value == d.ids[0]) s.*d.flagField = false;
			else if (value == d.ids[1]) s.*d.flagField = true;
			break;
		case SettingDef::CHOICE:
			for (int v = 0; v < d.count; v++)
				if (value == d.ids[v]) s.*d.intField = v;
			break;
		case SettingDef::INT_LIST:
		{
			int n = std::atoi(value.c_str());
			for (int v = 0; v < d.count; v++)
				if (d.canonical ? value == std::to_string(d.values[v]) : n == d.values[v]) s.*d.intField = d.values[v];
			break;
		}
		case SettingDef::INT_RANGE:
		{
			int n = std::atoi(value.c_str());
			if (n >= d.min && n <= d.max && !(d.blankIgnored && value.empty())) s.*d.intField = n / d.snap * d.snap;
			break;
		}
		case SettingDef::TEXT: s.*d.textField = value; break;
		case SettingDef::CUSTOM: d.parse(s, value); break;
		}
		if (d.after) d.after(s, value);
	}

	Settings parseSettings(const std::string& text)
	{
		Settings s;
		s.style = parseSubStyle(text);	// it leaves out the keys below
		s.lidAsk = true;	// (until its line is read below)
		const auto& defs = settingDefs();
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
			for (const auto& d : defs)
				if (key == d.key) { readSetting(d, s, value); break; }
			if (key == "lid-close") s.lidAsk = false;
			// the one setting before 0.1.1, kept: auto, cover-bars, bars, waves, spectrum, cover
			if (key == "audio-display")
			{
				if (value == "cover-bars") { s.musicShow = SHOW_BOTH; s.visualizer = VIS_PEAKS_BANANA; }
				else if (value == "bars" || value == "waves" || value == "spectrum")
				{
					s.musicShow = SHOW_VIS;
					s.visualizer = VIS_PEAKS_BANANA;	// retired: read as "Peaks"
				}
				else s.musicShow = SHOW_ART;
			}
		}
		return s;
	}

	int txtPage(const Settings& s)
	{
		if (s.txtTheme <= TXT_PAGE_MATCH || s.txtTheme >= TXT_PAGES) return theme::matchedPage(s.theme);
		return s.txtTheme - 1;
	}
}
