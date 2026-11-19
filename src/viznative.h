#pragma once
// Native visualizers (docs/notes/2026-11-19-viz-native.md): styles the player measures and draws itself, with no mpv
// lavfi-complex graph (the graph-built styles stalled mpv on the handheld). The sound is decoded a second time on a
// worker (src/viztap.h), its spectrum taken with PFFFT, and the shapes drawn on the top panel in GL (topscreen.cpp).
// Mirror bars was the first; Peaks, Dots, Steps, Barcode, Rings and Horizon followed. This header is the pure part (bpcore, tested in core_test): the spectrum, the bands, the
// levels as the graph style measured them, the easing, the shapes in 640x480 units, the clock.
#include <cstdint>
#include <vector>

namespace bp
{
	constexpr int VN_RATE = 32000;	// the copy's sample rate, mono: 31.25 Hz a bin, up to 16 kHz
	constexpr int VN_FFT = 1024;	// 32 ms at VN_RATE
	constexpr int VN_BARS = 40;	// Mirror bars' bars
	constexpr float VN_TOP = 252;	// a level's full height (the graph's codes: 0..252)

	// a 1024-point real FFT (PFFFT: NEON on aarch64, SSE on x86) with a Hann window. Not thread-safe: one per worker
	class VizSpectrum
	{
	public:
		VizSpectrum();
		~VizSpectrum();
		VizSpectrum(const VizSpectrum&) = delete;
		VizSpectrum& operator=(const VizSpectrum&) = delete;
		bool ok() const { return setup != nullptr; }
		// in: VN_FFT samples; mag: VN_FFT / 2 amplitudes (bin 0 DC .. 511), each |X| over the window's sum of squares,
		// as FFmpeg's showfreqs measures (the graph style's numbers: a full-scale sine is about 0.67)
		void magnitudes(const float* in, float* mag);

	private:
		void* setup = nullptr;
		float* buf = nullptr;
		float* work = nullptr;
		std::vector<float> window;
		float scale = 1;
	};

	constexpr int VN_MAX_BARS = 96;	// the most bands a style measures (Barcode)

	// the bands' first bins (bars + 1 edges: band i is bins [e[i], e[i+1])), log spaced from lo to hi Hz (the bass
	// left), each at least one bin wide (the lowest bands are one bin each)
	std::vector<int> vizBandEdges(int bars, int fft, int rate, double lo, double hi);

	// The native styles (the graph styles they replace: src/viz_simple.cpp, Peaks' peaksGraph in settings.cpp)
	bool vizNativeStyle(int vis);
	// what a style measures: its band count, and plain (Peaks: showfreqs' levels as they are) or the simple styles'
	// spectrumRow (contrast pushed, the bed cut)
	struct VizBands
	{
		int bars = 40;
		bool plain = false;
		bool cut = true;	// (not plain) the bottom 40 steps cut off (the spectrum's near-constant bed)
		double hi = 14000;	// the top band's upper edge, Hz
	};
	VizBands vizStyleBands(int vis);

	// each band's target level 0..VN_TOP from the magnitudes, as the graph styles' spectrumRow did it: the band's
	// loudest bin in showfreqs' log scale (-120..0 dB), a slope of +3 dB an octave from 500 Hz (music falls to the
	// right; log bands show it more than the graph's near-linear columns did); unless plain, its contrast pushed (each
	// bar against its neighbours) and the bottom 40 steps (the spectrum's near-constant bed) cut off
	void vizBandTargets(const float* mag, const std::vector<int>& edges, int rate, int fft, bool plain, float* out, bool cut = true);

	// besides the bands, the worker's hint (Starfield, Helix and Sparks): the loudness 0..1 (the window's RMS, -42 dBFS
	// to -6) and a count of the beats heard so far (a new one: the count moved on)
	struct VizHint
	{
		float loud = 0;
		uint32_t beats = 0;
		float avg = 0;	// the loudness over about the last second (Sleepy moon's awake or asleep)
	};
	float vizLoudness(const float* samples, int n);
	// the bass's energy (40..160 Hz) in the magnitudes
	float vizBassEnergy(const float* mag, int rate, int fft);
	// a beat: the bass's energy jumping past 1.5x its mean of the last second, at most four a second, never in silence
	struct VizBeat
	{
		double mean = -1, since = 1;	// (mean < 0: none yet, the first look sets it)
		bool step(float bass, double dt);
	};

	// the styles drawn over the whole panel in Art + visualizer too (behind the art and the lines, kept off them by
	// the keep-clear boxes), not in the band: Sparks and Starfield (the owner's word, 2026-11-19)
	bool vizFullPanel(int vis);
	// the styles that draw behind the art and its lines in Art + visualizer (kept off their boxes, not stopped at the
	// band): the full-panel ones, and Rocket, whose stars streak across the whole panel while the rocket and its smoke
	// stay in the band (the owner's word); the art and lines keep the band's layout for Rocket
	bool vizBehindArt(int vis);
	inline void vizMirrorTargets(const float* mag, const std::vector<int>& edges, int rate, int fft, float* out)
	{
		vizBandTargets(mag, edges, rate, fft, false, out);
	}

	// an LED meter's easing (the graph's lagfun): up at once, down by the whole height over 0.15 s
	float vizEase(float level, float target, double dt);
	// a held peak let fall at an even speed (levels a second): up at once with the level
	float vizHold(float held, float level, double perSecond, double dt);

	// a style's moving state on the top panel: the levels shown and the held peaks (Peaks' caps, Steps' peak line)
	struct VizStyleState
	{
		int vis = -1, n = 0;
		float targets[VN_MAX_BARS] = {}, levels[VN_MAX_BARS] = {}, holds[VN_MAX_BARS] = {};
		// the final ten's new six (2026-11-19): the worker's hint, as told and as eased
		float loud = 0, loudShown = 0, avg = 0;
		uint32_t beats = 0, beatsSeen = 0;
		// told by the caller before vizStep: Art + visualizer (the band along the foot) or the whole panel
		bool band = false;
		// the five fun ones (2026-11-19): the time, the beat's interval (a beat-timed motion between beats)
		double time = 0, sinceBeat = 0, interval = 0.55;
		float bass = 0;
		// Bouncy keys: each key's dip, the ball's hop (from, to, its height), its trail
		float hit[16] = {};
		int from = 3, to = 8, trailN = 0;
		float hop = 0.6f, trail[9][2] = {};
		// Faders: how long each band's peak has held before it falls (0.35 s, as the preview)
		float holdAge[16] = {};
		// Lanterns: each lantern's swing (angle, speed), its eased light, and the beat count it last felt
		float lnA[9] = {}, lnV[9] = {}, lnLv[9] = {};
		uint32_t lnSeen = 0;
		// Rocket's stars; Sleepy moon's state
		struct Streak
		{
			float x, y, sp;
		};
		static constexpr int STREAKS = 40;
		Streak streaks[STREAKS] = {};
		bool awake = false;
		float aw = 0, zPhase = 0;
		double phase = 0;	// Strings' swing, Helix's twist; Grid's time to its next row (0..1)
		uint32_t rng = 1;	// (xorshift: the particles' and stars' chance, the same run to run)
		float slow[VN_MAX_BARS] = {};	// Ribbon's slow line
		static constexpr int GRID_ROWS = 12, GRID_BANDS = 17;	// (12 rows: 17 stacked the fills on the handheld)
		float gridRows[GRID_ROWS][GRID_BANDS] = {};	// Grid's moments ago, a ring: gridHead the newest
		int gridCount = 0, gridHead = 0;
		// Sparks: a fixed pool (nothing allocated a frame); sparkN live ones at the front
		struct Spark
		{
			float x, y, vx, vy, life, max, r, ph;
			bool big;
		};
		static constexpr int SPARKS = 400;
		Spark sparks[SPARKS] = {};
		int sparkN = 0;
		struct Star
		{
			float x, y, z;
		};
		static constexpr int STARS = 160;
		Star stars[STARS] = {};
		float starV = 0;
		void reset(int style);
		float rand01();
	};
	// a frame on: the levels eased (Peaks: as measured, its bars aren't eased) and the holds let fall
	void vizStep(VizStyleState& s, double dt);
	// Bouncy keys' ball: where it is on its hop (a parabola between two key tops, timed by the beat) and its radius
	void vizBouncyBall(const VizStyleState& s, float& bx, float& by, float& r);

	// a shape to draw, in 640x480 units. kind 0 a box (x0,y0)-(x1,y1); kind 1 a ring round (x0,y0) from radius x1 to
	// y1 (0: a disc). ink: VizInk; mix: VI_RAMP's place from BUTTON (0) to ACCENT (1), VI_INK_RAMP's from BUTTON to
	// INK, VI_DIM_BG's from BG to DIM, VI_INK_BG's from BG to INK; alpha: 1 opaque
	// VI_INK and VI_LINE: Faders' and Lanterns' derived inks (settings.h's visualizerInk/visualizerLine: the theme's
	// ACCENT made 3:1 on BG, and its RULE towards DIM)
	// VI_DIM_BG: DIM laid over BG at mix, opaque (a faint shape drawn without blending: Sleepy moon's far clouds,
	// Rocket's smoke; the ground under them is always BG)
	enum VizInk { VI_ACCENT, VI_DIM, VI_BUTTON, VI_TEXT, VI_BG, VI_RAMP, VI_WHITE, VI_INK, VI_LINE, VI_INK_RAMP, VI_INK_BG,
		VI_DIM_BG };
	// kind 2: a quad (x0,y0) (x1,y1) (x2,y2) (x3,y3) in order round, alpha at the first two corners and alpha2 at the
	// other two (a fade)
	struct VizRect
	{
		float x0, y0, x1, y1;
		int ink;
		float mix = 0, alpha = 1;
		int kind = 0;
		float x2 = 0, y2 = 0, x3 = 0, y3 = 0, alpha2 = 1;
	};
	// Mirror bars: n bars each grown up and down from a centre line by its level (the graph's: 16 units a bar, 12
	// wide; the line 4 tall). band: Art + visualizer's band along the foot (rows 280..480, 94 at most each way);
	// alone the whole panel (the centre at 240, 218 each way)
	void mirrorBarsRects(const float* levels, int n, bool band, std::vector<VizRect>& out);
	// any native style's shapes from its state (Mirror bars as above; the others as their graphs drew them, at their
	// half size doubled), in drawing order
	void vizShapes(const VizStyleState& s, bool band, std::vector<VizRect>& out);
	// the boxes with a box cut out of them: each box's parts outside it, at most four (the panel's free parts round
	// the song's lines and mpv's cards, which the shapes are kept off: they're drawn after mpv's picture)
	void vizCutOut(std::vector<VizRect>& rects, const VizRect& box);

	// where the sound heard is, from mpv's audio-pts (its output's delay already taken off) and when it came: between
	// mpv's updates (its playloop's ticks) the time runs on at the speed
	struct VizClock
	{
		double pts = -1;	// seconds in the file; < 0 unknown
		uint64_t atNs = 0;	// steady clock, when pts was told
		double speed = 1;
		bool moving = false;	// playing (not paused, seeking or waiting on the cache)
		double at(uint64_t nowNs) const
		{
			if (pts < 0) return -1;
			if (!moving || nowNs <= atNs) return pts;
			return pts + (double)(nowNs - atNs) / 1e9 * speed;
		}
	};
}
