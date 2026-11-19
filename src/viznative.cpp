#include "viznative.h"
#include "settings.h"

#include "../third_party/pffft/pffft.h"

#include <algorithm>
#include <cmath>

namespace bp
{
	VizSpectrum::VizSpectrum()
	{
		setup = pffft_new_setup(VN_FFT, PFFFT_REAL);
		buf = (float*)pffft_aligned_malloc(sizeof(float) * VN_FFT);
		work = (float*)pffft_aligned_malloc(sizeof(float) * VN_FFT);
		window.resize(VN_FFT);
		double sq = 0;
		for (int i = 0; i < VN_FFT; i++)
		{
			window[i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / (VN_FFT - 1)));
			sq += (double)window[i] * window[i];
		}
		scale = (float)(1.0 / sq);
	}

	VizSpectrum::~VizSpectrum()
	{
		if (setup) pffft_destroy_setup((PFFFT_Setup*)setup);
		pffft_aligned_free(buf);
		pffft_aligned_free(work);
	}

	void VizSpectrum::magnitudes(const float* in, float* mag)
	{
		if (!setup)
		{
			std::fill(mag, mag + VN_FFT / 2, 0.0f);
			return;
		}
		for (int i = 0; i < VN_FFT; i++) buf[i] = in[i] * window[i];
		pffft_transform_ordered((PFFFT_Setup*)setup, buf, buf, work, PFFFT_FORWARD);
		// ordered real output: [0] DC, [1] Nyquist, then bin k's re, im at 2k, 2k+1
		mag[0] = std::fabs(buf[0]) * scale;
		for (int k = 1; k < VN_FFT / 2; k++) mag[k] = std::sqrt(buf[2 * k] * buf[2 * k] + buf[2 * k + 1] * buf[2 * k + 1]) * scale;
	}

	std::vector<int> vizBandEdges(int bars, int fft, int rate, double lo, double hi)
	{
		std::vector<int> e(bars + 1);
		const double hz = (double)rate / fft, half = fft / 2;
		for (int i = 0; i <= bars; i++)
		{
			int b = (int)std::lround(lo * std::pow(hi / lo, (double)i / bars) / hz);
			if (i > 0) b = std::max(b, e[i - 1] + 1);
			e[i] = std::clamp(b, 1, (int)half);
		}
		return e;
	}

	void vizBandTargets(const float* mag, const std::vector<int>& edges, int rate, int fft, bool plain, float* out, bool cut)
	{
		const int n = (int)edges.size() - 1;
		std::vector<float> l(n);
		const double hz = (double)rate / fft;
		for (int i = 0; i < n; i++)
		{
			float peak = 0;
			for (int k = edges[i]; k < std::max(edges[i + 1], edges[i] + 1) && k < fft / 2; k++) peak = std::max(peak, mag[k]);
			// showfreqs' ascale=log: 1 - log(a)/log(1e-6), a clipped to 1e-6..1; as a level 0..252
			double db = 20 * std::log10(std::clamp((double)peak, 1e-6, 1.0));
			const double mid = (edges[i] + std::max(edges[i + 1], edges[i] + 1)) * 0.5 * hz;
			db += 3 * std::log2(mid / 500);
			l[i] = (float)std::clamp(VN_TOP * (1 + db / 120), 0.0, (double)VN_TOP);
		}
		if (plain)
		{
			std::copy(l.begin(), l.end(), out);
			return;
		}
		for (int i = 0; i < n; i++)
		{
			// vizContrast(t, 1, "4", "1.5", "160"): clip(160 + 1.5 (x - 160) + 4 (x - the 3-wide average), 0, 252)
			float sum = 0;
			int cnt = 0;
			for (int j = std::max(0, i - 1); j <= std::min(n - 1, i + 1); j++) sum += l[j], cnt++;
			float c = std::clamp(160 + 1.5f * (l[i] - 160) + 4 * (l[i] - sum / cnt), 0.0f, VN_TOP);
			// the bed cut: clip((v - 40) * 252 / 212, 0, 252)
			out[i] = cut ? std::clamp((c - 40) * VN_TOP / 212, 0.0f, VN_TOP) : c;
		}
	}

	float vizEase(float level, float target, double dt)
	{
		// the graph's lagfun=decay=0.311 at 60 frames a second on exp((v - 252) / 24): 28 steps a frame, the whole
		// 252 in 0.15 s
		if (target >= level) return target;
		return std::max(target, level - (float)(VN_TOP / 0.15 * std::max(0.0, dt)));
	}

	void vizCutOut(std::vector<VizRect>& rects, const VizRect& box)
	{
		std::vector<VizRect> out;
		out.reserve(rects.size() + 8);
		for (const VizRect& r : rects)
		{
			if (r.x1 <= box.x0 || r.x0 >= box.x1 || r.y1 <= box.y0 || r.y0 >= box.y1)
			{
				out.push_back(r);
				continue;
			}
			auto part = [&](float x0, float y0, float x1, float y1) {
				VizRect q = r;
				q.x0 = x0, q.y0 = y0, q.x1 = x1, q.y1 = y1;
				out.push_back(q);
			};
			if (r.y0 < box.y0) part(r.x0, r.y0, r.x1, box.y0);	// above
			if (r.y1 > box.y1) part(r.x0, box.y1, r.x1, r.y1);	// below
			const float y0 = std::max(r.y0, box.y0), y1 = std::min(r.y1, box.y1);
			if (r.x0 < box.x0) part(r.x0, y0, box.x0, y1);	// left
			if (r.x1 > box.x1) part(box.x1, y0, r.x1, y1);	// right
		}
		rects.swap(out);
	}

	void mirrorBarsRects(const float* levels, int n, bool band, std::vector<VizRect>& out)
	{
		out.clear();
		// the graph's geometry, at its half size doubled: bars 8 half-units apart (2 of them gap), the line the two
		// half-rows at the centre, a bar's half-height in half-rows 1 + floor((level - 1) * MAXD / 250)
		const float centre = band ? 380 : 240, maxd = band ? 46 : 108, pitch = 640.0f / n;
		out.push_back({0, centre - 2, 640, centre + 2, 1});
		for (int i = 0; i < n; i++)
		{
			const float v = std::clamp(levels[i], 0.0f, VN_TOP);
			if (v < 1) continue;
			const float e = 2 * (1 + std::floor((v - 1) * maxd / 250));
			const float x0 = i * pitch + pitch / 8, x1 = (i + 1) * pitch - pitch / 8;
			out.push_back({x0, centre - e, x1, centre + e, 0});
		}
	}

	bool vizNativeStyle(int vis)
	{
		return vis == VIS_MIRROR || vis == VIS_PEAKS_BANANA || vis == VIS_DOTS || vis == VIS_STEPS || vis == VIS_BARCODE || vis == VIS_RINGS ||
			   vis == VIS_HORIZON || vis == VIS_POCKET || vis >= VIS_RIBBON;
	}

	bool vizFullPanel(int vis) { return vis == VIS_SPARKS || vis == VIS_STARFIELD || vis == VIS_RAIN || vis == VIS_MOON; }
	bool vizBehindArt(int vis) { return vizFullPanel(vis) || vis == VIS_ROCKET; }

	float vizLoudness(const float* samples, int n)
	{
		double sq = 0;
		for (int i = 0; i < n; i++) sq += (double)samples[i] * samples[i];
		const double rms = n > 0 ? std::sqrt(sq / n) : 0;
		const double db = 20 * std::log10(std::max(rms, 1e-9));
		return (float)std::clamp((db + 42) / 36, 0.0, 1.0);
	}

	float vizBassEnergy(const float* mag, int rate, int fft)
	{
		const double hz = (double)rate / fft;
		float e = 0;
		for (int k = std::max(1, (int)std::lround(40 / hz)); k <= (int)std::lround(160 / hz) && k < fft / 2; k++) e += mag[k] * mag[k];
		return e;
	}

	bool VizBeat::step(float bass, double dt)
	{
		dt = std::max(0.0, dt);
		since += dt;
		if (mean < 0)
		{
			mean = bass;
			return false;
		}
		const bool beat = since >= 0.25 && bass > 1.5 * mean && bass > 1e-4;
		if (beat) since = 0;
		// the mean of about the last second
		mean += (bass - mean) * (1 - std::exp(-dt / 1.0));
		return beat;
	}

	VizBands vizStyleBands(int vis)
	{
		switch (vis)
		{
		case VIS_PEAKS_BANANA: return {64, true};
		case VIS_DOTS: return {32, false};
		case VIS_STEPS:
		case VIS_HORIZON: return {20, false};
		case VIS_BARCODE: return {96, false};
		case VIS_RINGS: return {10, false};
		// Pocket console: its graph's 15 bars, contrast pushed but nothing cut
		case VIS_POCKET: return {15, false, false, 8000};
		case VIS_RIBBON: return {48, false};
		case VIS_GRID: return {VizStyleState::GRID_BANDS, false};
		case VIS_STARFIELD: return {16, false};	// (the loudness only: a few bands for the wake)
		case VIS_STRINGS: return {8, false};
		case VIS_HELIX:
		case VIS_SPARKS: return {32, false};
		case VIS_BOUNCY: return {14, false};
		case VIS_ROCKET: return {16, false};
		case VIS_RAIN:
		case VIS_MOON: return {20, false};
		case VIS_DANCE: return {12, false};
		default: return {VN_BARS, false};
		}
	}

	float vizHold(float held, float level, double perSecond, double dt)
	{
		return std::max(level, held - (float)(perSecond * std::max(0.0, dt)));
	}

	void VizStyleState::reset(int style)
	{
		vis = style;
		n = vizStyleBands(style).bars;
		std::fill(std::begin(targets), std::end(targets), 0.0f);
		std::fill(std::begin(levels), std::end(levels), 0.0f);
		std::fill(std::begin(holds), std::end(holds), 0.0f);
		std::fill(std::begin(slow), std::end(slow), 0.0f);
		loud = loudShown = 0;
		beatsSeen = beats;
		time = sinceBeat = 0;
		interval = 0.55;
		bass = 0;
		std::fill(std::begin(hit), std::end(hit), 0.0f);
		from = 3, to = 8, trailN = 0, hop = 0.6f;
		dropN = splashN = ringN = 0;
		awake = false;
		aw = zPhase = 0;
		pkScroll = pkPulse = 0;
		for (Streak& q : streaks)
		{
			q.x = rand01() * 640;
			q.y = rand01();
			q.sp = 0.4f + rand01();
		}
		phase = 0;
		gridCount = gridHead = 0;
		sparkN = 0;
		starV = 0;
		rng = 0x9E3779B9u;
		for (Star& st : stars)
		{
			st.x = rand01() * 2 - 1;
			st.y = rand01() * 2 - 1;
			st.z = 0.05f + rand01() * 0.95f;
		}
	}

	float VizStyleState::rand01()
	{
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return (rng >> 8) * (1.0f / 16777216.0f);
	}

	// the held peaks' fall, levels a second: Peaks' caps (lagfun 0.8679 at 30 fps: 3.4 steps a frame, ~2.5 s for the
	// height), Steps' peak line (0.95 at 60: 1.23 a frame, ~3.4 s)
	static const double PEAKS_CAP_FALL = 102, STEPS_PEAK_FALL = 73.9;

	void vizBouncyBall(const VizStyleState& s, float& bx, float& by, float& r);
	// Sleepy moon's lines on the loudness over a second (vizLoudness's scale: -42..-6 dBFS): awake past -20 dBFS, asleep
	// again under -24 (the proposal's 0.31 and 0.24 were its own made-up scale)
	static const float MOON_WAKE = 0.6f, MOON_SLEEP = 0.5f;

	namespace
	{
		// the proposals' easing, told per frame at 30 a second: a step of f toward the target, over dt
		float toward(float v, float target, float f, double k)
		{
			return v + (target - v) * (float)(1 - std::pow(1 - f, k));
		}

		// the new six's moving parts (the proposals' motions, their per-frame numbers at 30 frames a second, k frames on)
		void stepNew(VizStyleState& s, double dt)
		{
			const double k = std::max(0.0, dt) * 30;
			float up = 0.5f, down = 0.14f;
			switch (s.vis)
			{
			case VIS_GRID: down = 0.16f; break;
			case VIS_SPARKS: up = 0.6f, down = 0.18f; break;
			case VIS_STRINGS: up = 0.6f, down = 0.1f; break;
			case VIS_HELIX: up = 0.55f; break;
			case VIS_BOUNCY: up = 0.55f, down = 0.18f; break;
			case VIS_ROCKET: down = 0.2f; break;
			case VIS_RAIN: up = 0.6f, down = 0.2f; break;
			case VIS_MOON: up = 0.4f, down = 0.12f; break;
			case VIS_DANCE: down = 0.22f; break;
			default: break;
			}
			// the beat (a new one since the last step) and its interval, for the motions timed by it
			dt = std::max(0.0, dt);
			s.time += dt;
			s.sinceBeat += dt;
			const bool beat = s.beats != s.beatsSeen;
			s.beatsSeen = s.beats;
			if (beat)
			{
				if (s.sinceBeat < 1.5) s.interval = 0.7 * s.interval + 0.3 * std::clamp(s.sinceBeat, 0.3, 1.2);
				s.sinceBeat = 0;
			}
			for (int i = 0; i < s.n; i++)
				s.levels[i] = toward(s.levels[i], s.targets[i], s.targets[i] > s.levels[i] ? up : down, k);
			switch (s.vis)
			{
			case VIS_RIBBON:
				for (int i = 0; i < s.n; i++) s.slow[i] = toward(s.slow[i], s.levels[i], 0.03f, k);
				break;
			case VIS_GRID:
			{
				// a row every 0.1 s (every third frame at 30), the newest at gridHead
				s.phase += std::max(0.0, dt) * 10;
				if (s.phase > 2) s.phase = 1;	// (a long gap: one row, not a burst)
				while (s.phase >= 1)
				{
					s.phase -= 1;
					s.gridHead = (s.gridHead + 1) % VizStyleState::GRID_ROWS;
					for (int i = 0; i < VizStyleState::GRID_BANDS; i++) s.gridRows[s.gridHead][i] = i < s.n ? s.levels[i] / VN_TOP : 0;
					s.gridCount = std::min(s.gridCount + 1, VizStyleState::GRID_ROWS);
				}
				break;
			}
			case VIS_STARFIELD:
			{
				s.loudShown = toward(s.loudShown, s.loud, 0.25f, k);
				s.starV = 0.003f + 0.03f * s.loudShown * s.loudShown;
				for (VizStyleState::Star& st : s.stars)
				{
					st.z -= s.starV * (float)k;
					if (st.z < 0.05f)
					{
						st.z = 1;
						st.x = s.rand01() * 2 - 1;
						st.y = s.rand01() * 2 - 1;
					}
				}
				break;
			}
			case VIS_STRINGS: s.phase += std::max(0.0, dt) * 27; break;	// (0.9 a frame at 30)
			case VIS_BOUNCY:
			{
				for (int i = 0; i < s.n; i++) s.hit[i] *= (float)std::pow(0.86, k);
				if (beat)
				{
					// landed: the key dips and lights; the next hop to a loud key within seven
					s.hit[s.to] = 1;
					s.from = s.to;
					float best = -1;
					for (int i = 0; i < s.n; i++)
					{
						if (i == s.from || std::abs(i - s.from) > 7) continue;
						const float sc = s.levels[i] / VN_TOP + s.rand01() * 0.3f;
						if (sc > best) best = sc, s.to = i;
					}
					s.hop = 0.3f + 0.7f * s.loud;
				}
				float bx, by, r;
				vizBouncyBall(s, bx, by, r);
				for (int i = std::min(s.trailN, 8); i > 0; i--) s.trail[i][0] = s.trail[i - 1][0], s.trail[i][1] = s.trail[i - 1][1];
				s.trail[0][0] = bx, s.trail[0][1] = by;
				s.trailN = std::min(s.trailN + 1, 9);
				break;
			}
			case VIS_ROCKET:
			{
				s.loudShown = toward(s.loudShown, s.loud, 0.25f, k);
				s.bass = toward(s.bass, s.levels[0] / VN_TOP, 0.6f, k);
				const float v = 1.5f + 16 * s.loudShown;
				for (auto& q : s.streaks)
				{
					q.x -= v * q.sp * (float)k;
					if (q.x < -30)
					{
						q.x = 640 + s.rand01() * 30;
						q.y = s.rand01();
					}
				}
				break;
			}
			case VIS_RAIN:
			{
				s.bass = toward(s.bass, s.levels[0] / VN_TOP, 0.4f, k);
				const float W = 640, Hh = 480, ground = Hh - 12, ux = W / 2, uy = Hh - 150 - 6 * s.bass, R = 112;
				for (int i = 0; i < s.n; i++)
				{
					const float p = 0.03f + s.levels[i] / VN_TOP * 0.95f;
					if (s.dropN < VizStyleState::DROPS && s.rand01() < (float)(1 - std::pow(1 - std::min(p, 0.999f), k)))
						s.drops[s.dropN++] = {(i + s.rand01()) / s.n * (W + 90), -20 - s.rand01() * 30, 13 + s.rand01() * 6};
				}
				const bool cover = s.band && s.cover[2] > s.cover[0];
				auto splash = [&](float px, float py) {
					for (int sg = -1; sg <= 1; sg += 2)
						if (s.splashN < VizStyleState::SPLASHES)
							s.splashes[s.splashN++] = {px, py, sg * (1 + s.rand01() * 2.5f), -(2 + s.rand01() * 3), 9};
				};
				for (int i = s.dropN - 1; i >= 0; i--)
				{
					VizStyleState::Drop& d = s.drops[i];
					const float y0 = d.y;
					d.y += d.v * (float)k;
					d.x -= d.v * 0.2f * (float)k;
					bool hit = false;
					if (cover) hit = d.x > s.cover[0] && d.x < s.cover[2] && y0 < s.cover[1] + 30 && d.y >= s.cover[1];	// (crossed its top)
					else if (!s.band)
					{
						const float dx = d.x - ux, dy = d.y - uy;
						hit = dy < 0 && dx * dx + dy * dy < R * R;
					}
					if (hit) splash(d.x, cover ? s.cover[1] : d.y);
					else if (d.y >= ground)
					{
						if (s.ringN < VizStyleState::RINGS) s.rings[s.ringN++] = {d.x, 0};
					}
					else continue;
					d = s.drops[--s.dropN];
				}
				for (int i = s.splashN - 1; i >= 0; i--)
				{
					VizStyleState::Splash& q = s.splashes[i];
					q.x += q.vx * (float)k;
					q.y += q.vy * (float)k;
					q.vy += 0.6f * (float)k;
					q.life -= (float)k;
					if (q.life <= 0) q = s.splashes[--s.splashN];
				}
				for (int i = s.ringN - 1; i >= 0; i--)
					if ((s.rings[i].a += 0.09f * (float)k) >= 1) s.rings[i] = s.rings[--s.ringN];
				break;
			}
			case VIS_MOON:
			{
				for (int i = 0; i < s.n; i++) s.slow[i] = toward(s.slow[i], s.levels[i], 0.05f, k);
				s.loudShown = toward(s.loudShown, s.loud, 0.3f, k);
				// awake: the loudness over the last second past a line (lower to fall asleep again: no flicker)
				s.awake = s.avg > (s.awake ? MOON_SLEEP : MOON_WAKE);
				s.aw = toward(s.aw, s.awake ? 1.0f : 0.0f, 0.12f, k);
				s.zPhase = (float)std::fmod(s.zPhase + 0.011 * k, 1.0);
				break;
			}
			case VIS_DANCE: s.phase += dt * M_PI / s.interval; break;	// (half a sway a beat)
			case VIS_HELIX:
				s.loudShown = toward(s.loudShown, s.loud, 0.2f, k);
				s.phase += (0.02 + 0.07 * s.loudShown) * k;
				break;
			case VIS_SPARKS:
			{
				const float H = 480, W = 640;
				auto add = [&](float px, float v, bool big) {
					if (s.sparkN >= VizStyleState::SPARKS) return;
					VizStyleState::Spark& p = s.sparks[s.sparkN++];
					const float life = 45 + s.rand01() * 55;
					p = {px, 0, (s.rand01() - 0.5f) * 0.9f, H * (0.0035f + 0.0125f * v) * (0.7f + 0.6f * s.rand01()), life, life,
						big ? 4.5f : 1.8f + v * 2.6f, s.rand01() * 6.28f, big};
				};
				for (int i = 0; i < s.n; i++)
				{
					const float v = s.levels[i] / VN_TOP, p = v * v * 0.75f;
					if (s.rand01() < (float)(1 - std::pow(1 - p, k))) add((i + s.rand01()) / s.n * W, v, false);
				}
				// a beat: a brighter handful across the width
				if (beat)
				{
					for (int i = 0; i < 14; i++) add(s.rand01() * W, 0.75f + 0.25f * s.rand01(), true);
				}
				const float drag = (float)std::pow(0.985, k);
				for (int i = s.sparkN - 1; i >= 0; i--)
				{
					VizStyleState::Spark& p = s.sparks[i];
					p.y += p.vy * (float)k;
					p.vy *= drag;
					p.x += (p.vx + std::sin(p.ph + p.life * 0.09f) * 0.5f) * (float)k;
					p.life -= (float)k;
					if (p.life <= 0) p = s.sparks[--s.sparkN];	// (the last in its place: the pool stays packed)
				}
				break;
			}
			default: break;
			}
		}
	}

	void vizBouncyBall(const VizStyleState& s, float& bx, float& by, float& r)
	{
		const float h = s.band ? 200.0f : 480.0f, base = (s.band ? 280.0f : 0.0f) + h, kw = 640.0f / 14;
		r = s.band ? 11.0f : 17.0f;
		auto top = [&](int i) { return base - h * (0.2f + 0.34f * s.levels[std::clamp(i, 0, 13)] / VN_TOP) + 9 * s.hit[std::clamp(i, 0, 15)]; };
		const float u = (float)std::clamp(s.sinceBeat / s.interval, 0.0, 1.0);
		const float xa = (s.from + 0.5f) * kw, xb = (s.to + 0.5f) * kw;
		bx = xa + (xb - xa) * u;
		by = top(s.from) + (top(s.to) - top(s.from)) * u - 4 * u * (1 - u) * h * 0.42f * s.hop - r;
	}

	void vizStep(VizStyleState& s, double dt)
	{
		if (s.vis >= VIS_RIBBON)
		{
			stepNew(s, dt);
			return;
		}
		for (int i = 0; i < s.n; i++)
		{
			// Peaks' bars are showfreqs' as measured each frame (its graph eased only the caps)
			s.levels[i] = s.vis == VIS_PEAKS_BANANA ? s.targets[i] : vizEase(s.levels[i], s.targets[i], dt);
			if (s.vis == VIS_PEAKS_BANANA || s.vis == VIS_POCKET) s.holds[i] = std::min(250.0f, vizHold(s.holds[i], s.levels[i], PEAKS_CAP_FALL, dt));
			else if (s.vis == VIS_STEPS) s.holds[i] = vizHold(s.holds[i], s.levels[i], STEPS_PEAK_FALL, dt);
		}
		if (s.vis == VIS_POCKET)
		{
			// Pocket console's scene: the beat's pulse (1 on a beat, gone in 0.4 s), the road passing by with the
			// loudness (2 to 24 dots a second; its dashes come round every 5)
			dt = std::max(0.0, dt);
			s.time += dt;
			const bool beat = s.beats != s.beatsSeen;
			s.beatsSeen = s.beats;
			s.pkPulse = beat ? 1.0f : std::max(0.0f, s.pkPulse - (float)dt * 2.5f);
			s.loudShown = toward(s.loudShown, s.loud, 0.2f, dt * 30);
			s.pkScroll = std::fmod(s.pkScroll + (float)dt * (2 + 22 * s.loudShown), 5.0f);
		}
	}

	namespace
	{
		// how many half-size rows from the foot a level q lights, for rows told by step(Y) = 1 + floor(K (H-1-Y) /
		// (H-1-T)) for Y >= T (the graphs' still "height of this row" pictures): the rows whose step is at most q
		int rowsUpTo(double q, int H, int T, int K)
		{
			q = std::floor(q);
			if (q < 1) return 0;
			return std::min(H - T, (int)std::ceil(q * (H - 1 - T) / K - 1e-9));
		}

		void box(std::vector<VizRect>& out, float x0, float y0, float x1, float y1, int ink, float mix = 0, float alpha = 1)
		{
			if (x1 > x0 && y1 > y0) out.push_back({x0, y0, x1, y1, ink, mix, alpha, 0});
		}

		void ring(std::vector<VizRect>& out, float cx, float cy, float r0, float r1, int ink)
		{
			out.push_back({cx, cy, std::max(0.0f, r0), r1, ink, 0, 1, 1});
		}

		// Peaks: 64 bars from the foot (8 units, a 2-unit gap), each with a 4-unit cap (TEXT) sitting on its held
		// peak; alone, a faint white line every 96 units over them
		void peaks(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const int H = band ? 100 : 240;
			for (int i = 0; i < s.n; i++)
			{
				const float x0 = i * 10.0f, x1 = x0 + 8;
				const int bar = rowsUpTo(s.levels[i], H, 0, 251), cap = rowsUpTo(s.holds[i], H, 0, 251);
				box(out, x0, 480.0f - 2 * bar, x1, 480, VI_ACCENT);
				box(out, x0, 480.0f - 2 * cap - 4, x1, 480.0f - 2 * cap, VI_TEXT);
			}
			if (!band)
				for (int y = 96; y < 480; y += 96) box(out, 0, (float)y, 640, y + 1.0f, VI_WHITE, 0, 0.1f);
		}

		// Dots: 32 columns of round-cornered dots (a 20-unit cell, the dot 16 with its corners cut), lit from the foot
		// (ACCENT) over the unlit ones (BUTTON). Alone 17 rows from 20 above the foot (75% of the height), in the
		// band 9, centred
		void dots(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const int S = band ? 9 : 17;
			const float top = band ? 290 : 120;
			for (int c = 0; c < s.n; c++)
				for (int j = 0; j < S; j++)
				{
					const int v = S - 1 - j;
					const bool lit = s.levels[c] >= std::floor(252.0 * (v + 0.5) / S);
					const float x = c * 20.0f, y = top + j * 20.0f;
					const int ink = lit ? VI_ACCENT : VI_BUTTON;
					box(out, x + 2, y + 4, x + 18, y + 16, ink);
					box(out, x + 4, y + 2, x + 16, y + 4, ink);
					box(out, x + 4, y + 16, x + 16, y + 18, ink);
				}
		}

		// Steps: a 20-band staircase (32 units a band), filled (BUTTON), its outline (ACCENT: the top of each step
		// and the rises and falls between bands, at a band's first 4 units) and each band's held peak (DIM). Alone it
		// reaches 75% of the height
		void steps(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const int H = band ? 100 : 240, T = band ? 0 : 60, K = band ? 83 : 82, E = band ? 2 : 1;
			const int maxStep = band ? 84 : 83;
			auto q = [&](float v) { return std::min((double)maxStep, std::floor(v / 3.0)); };
			auto y = [&](int rows) { return 480.0f - 2 * rows; };
			std::vector<int> f(s.n);
			for (int i = 0; i < s.n; i++) f[i] = rowsUpTo(q(s.levels[i]), H, T, K);
			for (int i = 0; i < s.n; i++) box(out, i * 32.0f, y(f[i]), i * 32.0f + 32, 480, VI_BUTTON);
			for (int i = 0; i < s.n; i++)
			{
				if (s.holds[i] <= 3) continue;
				const double p = std::floor(s.holds[i] / 3);
				if (p > maxStep) continue;
				box(out, i * 32.0f, y(rowsUpTo(p, H, T, K)), i * 32.0f + 32, y(rowsUpTo(p - 1, H, T, K)), VI_DIM);
			}
			for (int i = 0; i < s.n; i++)
			{
				if (f[i] > 0) box(out, i * 32.0f, y(f[i]), i * 32.0f + 32, y(rowsUpTo(q(s.levels[i]) - E, H, T, K)), VI_ACCENT);
				if (i > 0 && f[i] != f[i - 1]) box(out, i * 32.0f, y(std::max(f[i], f[i - 1])), i * 32.0f + 4, y(std::min(f[i], f[i - 1])), VI_ACCENT);
			}
		}

		// Barcode: 96 hairlines (2 units) the region's height, each from faint (BUTTON) to full (ACCENT) by its band
		void barcode(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const float top = band ? 280 : 0;
			for (int i = 0; i < s.n; i++)
			{
				const int X = (i * 320 + s.n - 1) / s.n;	// the band's first column at half size
				const float mix = std::floor(std::clamp(s.levels[i], 0.0f, VN_TOP) * 250 / 252) / 250;
				box(out, 2.0f * X, top, 2.0f * X + 2, 480, VI_RAMP, mix);
			}
		}

		// Rings: ten rings round a dot (TEXT), the bass innermost; each band's ring ACCENT, thicker as it's louder (8
		// widths), and a quiet one's faint guide (BUTTON). Alone a 480-unit square in the middle, in the band 200
		void rings(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const float cx = 320, cy = band ? 380 : 240, k = 2;	// (half size doubled)
			const double dot = band ? 3 : 6, first = band ? 7 : 16, gap = band ? 4.5 : 10.6, reach = 0.42 * gap, core = 0.75;
			for (int i = 0; i < s.n; i++)
			{
				int m = 0;	// the widths lit: a row lit when the level passes 30 + its share
				for (int my = 0; my < 8; my++)
					if (s.levels[i] > 30 + std::floor(222.0 * my / 8)) m++;
				const double r = first + i * gap, w = m == 0 ? core : m >= 8 ? reach : core + (m - 1) * (reach - core) / 7;
				ring(out, cx, cy, (float)(k * (r - w)), (float)(k * (r + w)), m == 0 ? VI_BUTTON : VI_ACCENT);
			}
			ring(out, cx, cy, 0, (float)(k * dot), VI_TEXT);
		}

		// Horizon: the 20 bands mirrored (the bass in the middle) as a skyline (ACCENT, 16 units a column, two
		// thirds of the sky at most) on a horizon line (DIM, 4 units), a low sun behind it (BUTTON) and, under the
		// line, the skyline's reflection squashed and striped (DIM, every other 2-unit row)
		void horizon(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const int H = band ? 100 : 240, SKY = band ? 64 : 160, REF = H - SKY;
			const float top = band ? 280 : 0, line = top + 2 * SKY;
			ring(out, 320, top + 2 * (SKY * 4 / 5), 0, (float)SKY, VI_BUTTON);
			box(out, 0, line, 640, 480, VI_BG);	// (the sun's part under the line)
			int rows[40];
			for (int c = 0; c < 40; c++)
			{
				const int b = c < 20 ? 19 - c : c - 20;
				rows[c] = b < s.n ? rowsUpTo(std::floor(s.levels[b] / 3), SKY, 0, 125) : 0;
				box(out, c * 16.0f, line - 2 * rows[c], c * 16.0f + 16, line, VI_ACCENT);
			}
			box(out, 0, line, 640, line + 4, VI_DIM);
			// the reflection: row Y (from the line) is the sky's floor(Y SKY / REF)-th row up, lit on odd rows from 3
			for (int Y = 3; Y < REF; Y += 2)
			{
				const int up = Y * SKY / REF;
				for (int c = 0; c < 40;)
				{
					if (rows[c] <= up) { c++; continue; }
					int e = c;
					while (e < 40 && rows[e] > up) e++;
					box(out, c * 16.0f, line + 2 * Y, e * 16.0f, line + 2 * Y + 2, VI_DIM);
					c = e;
				}
			}
		}

		// ---- the final ten's new shapes (2026-11-19): quads, lines with joins, discs ----

		void quad(std::vector<VizRect>& out, float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, int ink, float alpha,
			float alpha2)
		{
			if (alpha <= 0 && alpha2 <= 0) return;
			VizRect r{ax, ay, bx, by, ink, 0, alpha, 2};
			r.x2 = cx, r.y2 = cy, r.x3 = dx, r.y3 = dy, r.alpha2 = alpha2;
			out.push_back(r);
		}

		void disc(std::vector<VizRect>& out, float cx, float cy, float r, int ink, float alpha)
		{
			if (r > 0 && alpha > 0) out.push_back({cx, cy, 0, r, ink, 0, alpha, 1});
		}

		// the canvas's smooth curve through the points (a quadratic through each middle point, ending on the mid-points
		// between them), each piece in two: about twice the points
		void smooth(const float* p, int n, std::vector<float>& out)
		{
			out.clear();
			if (n < 3)
			{
				out.assign(p, p + 2 * n);
				return;
			}
			out.push_back(p[0]), out.push_back(p[1]);
			float sx = p[0], sy = p[1];
			for (int i = 1; i < n - 1; i++)
			{
				const float cx = p[2 * i], cy = p[2 * i + 1], ex = (p[2 * i] + p[2 * i + 2]) / 2, ey = (p[2 * i + 1] + p[2 * i + 3]) / 2;
				for (int t = 1; t <= 2; t++)
				{
					const float u = t / 2.0f, a = (1 - u) * (1 - u), b = 2 * (1 - u) * u, c = u * u;
					out.push_back(a * sx + b * cx + c * ex), out.push_back(a * sy + b * cy + c * ey);
				}
				sx = ex, sy = ey;
			}
			out.push_back(p[2 * n - 2]), out.push_back(p[2 * n - 1]);
		}

		// a line w wide through the points: a quad a piece, the pieces meeting on the mitred joins (no overlaps, so a
		// faint glow has no dots at its joins); a sharp turn's mitre kept to twice the width
		void polyline(std::vector<VizRect>& out, const std::vector<float>& p, float w, int ink, float alpha)
		{
			const int n = (int)p.size() / 2;
			if (n < 2 || alpha <= 0) return;
			float pl[2] = {}, pr[2] = {};
			for (int i = 0; i < n; i++)
			{
				// the join's normal: the two pieces' own normals averaged
				float nx = 0, ny = 0;
				auto add = [&](int a, int b) {
					const float dx = p[2 * b] - p[2 * a], dy = p[2 * b + 1] - p[2 * a + 1], l = std::sqrt(dx * dx + dy * dy);
					if (l > 1e-4f) nx += -dy / l, ny += dx / l;
				};
				if (i > 0) add(i - 1, i);
				if (i < n - 1) add(i, i + 1);
				float l = std::sqrt(nx * nx + ny * ny);
				if (l < 1e-4f) nx = 0, ny = 1, l = 1;
				nx /= l, ny /= l;
				float m = w / 2;
				if (i > 0 && i < n - 1)
				{
					// the mitre: half the width over the cosine of half the turn
					const float dx = p[2 * i + 2] - p[2 * i], dy = p[2 * i + 3] - p[2 * i + 1], dl = std::sqrt(dx * dx + dy * dy);
					if (dl > 1e-4f)
					{
						const float c = std::fabs(nx * (-dy / dl) + ny * (dx / dl));
						m = std::min(w, w / 2 / std::max(c, 0.25f));
					}
				}
				const float lx = p[2 * i] + nx * m, ly = p[2 * i + 1] + ny * m, rx = p[2 * i] - nx * m, ry = p[2 * i + 1] - ny * m;
				if (i > 0) quad(out, pl[0], pl[1], lx, ly, rx, ry, pr[0], pr[1], ink, alpha, alpha);
				pl[0] = lx, pl[1] = ly, pr[0] = rx, pr[1] = ry;
			}
		}

		// the area between the curve and the line y = base, alpha at the curve (by height: aTop at `top` rows over the
		// base, 0 at the base when fade) or even (no fade)
		void fillUnder(std::vector<VizRect>& out, const std::vector<float>& p, float base, int ink, float alpha, bool fade, float top = 1)
		{
			auto at = [&](float y) { return fade ? alpha * std::clamp((base - y) / top, 0.0f, 1.0f) : alpha; };
			for (size_t i = 0; i + 3 < p.size(); i += 2)
			{
				const float ax = p[i], ay = p[i + 1], bx = p[i + 2], by = p[i + 3];
				// (each piece's two top corners: its alpha the pair's mean, the fade's foot 0)
				const float a = (at(ay) + at(by)) / 2;
				quad(out, ax, ay, bx, by, bx, base, ax, base, ink, a, fade ? 0 : alpha);
			}
		}

		// the region a style draws in: the band (Art + visualizer) or the whole panel
		void region(bool band, float& y0, float& h)
		{
			y0 = band ? 280.0f : 0.0f;
			h = band ? 200.0f : 480.0f;
		}

		// Ribbon: the 48 bands as one smooth line (ACCENT, a soft glow round it), a fade under it to the base, a slow
		// line behind (DIM: where the song has been sitting) and three level rules and the base (BUTTON)
		void ribbon(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const int n = s.n;
			const float base = y0 + h * 0.88f, top = h * 0.72f;
			for (int i = 0; i < 3; i++)
			{
				const float y = std::round(base - top * (i + 1) / 3);
				box(out, 0, y, 640, y + 2, VI_BUTTON);
			}
			box(out, 0, std::round(base), 640, std::round(base) + 2, VI_BUTTON);
			float p[2 * VN_MAX_BARS], q[2 * VN_MAX_BARS];
			for (int i = 0; i < n; i++)
			{
				const float x = n > 1 ? i * 640.0f / (n - 1) : 0;
				p[2 * i] = q[2 * i] = x;
				p[2 * i + 1] = base - s.levels[i] / VN_TOP * top;
				q[2 * i + 1] = base - s.slow[i] / VN_TOP * top * 1.25f;
			}
			std::vector<float> line, slow;
			smooth(p, n, line);
			smooth(q, n, slow);
			fillUnder(out, line, base, VI_ACCENT, 0.34f, true, top);
			polyline(out, slow, 2, VI_DIM, 0.9f);
			polyline(out, line, 18, VI_ACCENT, 0.07f);
			polyline(out, line, 10, VI_ACCENT, 0.14f);
			polyline(out, line, 3.5f, VI_ACCENT, 1);
		}

		// Grid: a floor in perspective, the front ridge now (bass in the middle) and one a tenth of a second older each
		// row back, rolling to the horizon and fading (ACCENT); each ridge hides what's behind it (BG); the floor's lines
		// BUTTON
		void grid(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const float cx = 320, hy = y0 + h * 0.2f, yb = y0 + h * 0.95f, NW = 640 * 0.74f, amp = h * 0.5f;
			auto sc = [](float d) { return 1 / (1 + d * 2.8f); };
			auto fy = [&](float d) { return hy + (yb - hy) * sc(d); };
			std::vector<float> ln;
			for (int i = -4; i <= 4; i++)
			{
				const float u = i / 4.0f;
				ln = {cx + u * NW * 1.4f, hy + (yb - hy) * 1.4f, cx + u * NW * sc(1), fy(1)};
				polyline(out, ln, 2, VI_BUTTON, 1);
			}
			box(out, 0, fy(1) - 1, 640, fy(1) + 1, VI_BUTTON);
			const int R = VizStyleState::GRID_ROWS - 1;
			const float frac = (float)s.phase;
			float p[2 * 33];
			std::vector<float> pts, sm;
			for (int j = s.gridCount; j >= 0; j--)
			{
				const float d = j ? (j - 1 + frac) / R : 0;
				if (d >= 1) continue;
				const float* a = j ? s.gridRows[(s.gridHead - (j - 1) + VizStyleState::GRID_ROWS) % VizStyleState::GRID_ROWS] : nullptr;
				const float k = sc(d), by = fy(d);
				// (the far half of the rows every other band: a few pixels apart there)
				const int step = d > 0.45f ? 2 : 1;
				int np = 0;
				for (int i = -16; i <= 16; i += step)
				{
					const int b = std::abs(i);
					const float v = a ? a[b] : (b < s.n ? s.levels[b] / VN_TOP : 0);
					p[2 * np] = cx + i / 16.0f * NW * k;
					p[2 * np + 1] = by - v * amp * k;
					np++;
				}
				// (the front ridge smoothed; the rows behind are small enough as they are)
				if (j == 0) smooth(p, np, pts);
				else pts.assign(p, p + 2 * np);
				// what hides the rows behind: the ridge filled down only to the next row's foot (everything behind it is
				// above that line), not to its own: a quiet row fills nothing, and the fills don't stack up row on row
				const float hide = std::min(by, fy(std::min(1.0f, (j + frac) / R)) + 1);	// (far rows: under a pixel apart)
				for (size_t i = 0; i + 3 < pts.size(); i += 2)
				{
					const float ax = pts[i], ay = pts[i + 1], bx2 = pts[i + 2], by2 = pts[i + 3];
					if (ay >= hide && by2 >= hide) continue;
					quad(out, ax, ay, bx2, by2, bx2, std::max(by2, hide), ax, std::max(ay, hide), VI_BG, 1, 1);
				}
				polyline(out, pts, j ? 1 + 1.2f * k : 3, VI_ACCENT, std::pow(1 - d, 1.4f));
			}
		}

		// Starfield: 160 stars streaming out of the middle, faster as the song is louder, each a streak from where it
		// was to where it is (ACCENT; one in six TEXT, wider), over a glow at the centre (BUTTON) that swells with it.
		// Always the whole panel
		void starfield(const VizStyleState& s, std::vector<VizRect>& out)
		{
			const float cx = 320, cy = 240, f = 640 * 0.3f;
			disc(out, cx, cy, 16 + 34 * s.loudShown, VI_BUTTON, 0.9f);
			std::vector<float> seg(4);
			for (int i = 0; i < VizStyleState::STARS; i++)
			{
				const VizStyleState::Star& p = s.stars[i];
				const bool hot = i % 6 == 0;
				const float z0 = p.z + s.starV * 2.2f, near = 1 - p.z;
				const float alpha = std::clamp(0.15f + near * 1.1f, 0.0f, 1.0f) * std::clamp((1 - p.z) * 8, 0.0f, 1.0f);
				const float w = 1 + near * (hot ? 4 : 3);
				const float ax = cx + p.x / z0 * f, ay = cy + p.y / z0 * f, bx = cx + p.x / p.z * f, by = cy + p.y / p.z * f;
				if ((ax < -20 && bx < -20) || (ax > 660 && bx > 660) || (ay < -20 && by < -20) || (ay > 500 && by > 500)) continue;
				const float dx = bx - ax, dy = by - ay, l = std::sqrt(dx * dx + dy * dy);
				if (l < w * 0.5f)
				{
					disc(out, bx, by, w / 2, hot ? VI_TEXT : VI_ACCENT, alpha);	// (slow: a point)
					continue;
				}
				// the canvas's round caps: half the width on at each end
				const float ex = dx / l * w / 2, ey = dy / l * w / 2;
				seg = {ax - ex, ay - ey, bx + ex, by + ey};
				polyline(out, seg, w, hot ? VI_TEXT : VI_ACCENT, alpha);
			}
		}

		// Strings: eight strings (five in the band), the thick bass one at the foot, each swinging as wide as its band is
		// loud on its own harmonic (ACCENT), the blur of its swing behind (BUTTON), two end bars (DIM)
		void strings(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const int n = band ? 5 : std::min(8, s.n);
			float lv[8];
			for (int i = 0; i < n; i++)
			{
				// the band's five: each the loudest of the eight it covers
				const int a = i * s.n / n, b = std::max(a + 1, (i + 1) * s.n / n);
				lv[i] = 0;
				for (int j = a; j < b && j < s.n; j++) lv[i] = std::max(lv[i], s.levels[j] / VN_TOP);
			}
			const float m = 26, x0 = m, x1 = 640 - m, top = y0 + (band ? 14 : 62), bot = y0 + h - 14, gap = (bot - top) / std::max(1, n);
			const int S = 40;
			std::vector<float> line(2 * (S + 1));
			for (int i = 0; i < n; i++)
			{
				const float yc = bot - gap * (i + 0.5f), A = gap * 0.47f * std::pow(lv[i], 0.8f);
				const int hw = i + 1;
				const float sw = (float)std::cos(s.phase * (0.8 + i * 0.13) + i);
				for (int j = 0; j < S; j++)
				{
					const float xa = x0 + (x1 - x0) * j / S, xb = x0 + (x1 - x0) * (j + 1) / S;
					const float ea = A * (float)std::sin(M_PI * hw * j / S), eb = A * (float)std::sin(M_PI * hw * (j + 1) / S);
					quad(out, xa, yc - ea, xb, yc - eb, xb, yc + eb, xa, yc + ea, VI_BUTTON, 1, 1);
				}
				for (int j = 0; j <= S; j++)
				{
					line[2 * j] = x0 + (x1 - x0) * j / S;
					line[2 * j + 1] = yc + A * sw * (float)std::sin(M_PI * hw * j / S);
				}
				polyline(out, line, 1.5f + (n - i) * 0.45f, VI_ACCENT, 1);
			}
			box(out, x0 - 5, top, x0 - 1, bot, VI_DIM);
			box(out, x1 + 1, top, x1 + 5, bot, VI_DIM);
		}

		// Helix: two strands of beads twisting round each other, bass at the left: a bead swells with its band, the twist
		// turns faster as the song is louder; the front strand ACCENT, the other DIM, rungs between them BUTTON
		void helix(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const int n = s.n;
			const float cy = y0 + h / 2 + (band ? 0 : 10), A = h * (band ? 0.34f : 0.26f) * (0.8f + 0.2f * s.loudShown), k = band ? 0.75f : 1;
			float px[VN_MAX_BARS], sn[VN_MAX_BARS], cs[VN_MAX_BARS];
			for (int i = 0; i < n; i++)
			{
				px[i] = (i + 0.5f) / n * 640;
				const double phi = (double)i / n * M_PI * 3.2 - s.phase;
				sn[i] = (float)std::sin(phi), cs[i] = (float)std::cos(phi);
				const float e = A * std::fabs(sn[i]);
				box(out, px[i] - 1, cy - e, px[i] + 1, cy + e, VI_BUTTON);
			}
			for (int front = 0; front < 2; front++)
				for (int i = 0; i < n; i++)
					for (int sgn = 1; sgn >= -1; sgn -= 2)
					{
						const float d = cs[i] * sgn;
						if ((d >= 0) != (front == 1)) continue;
						const float v = s.levels[i] / VN_TOP;
						disc(out, px[i], cy + A * sn[i] * sgn, (2.5f + 9 * v) * (0.7f + 0.3f * d) * k, sgn > 0 ? VI_ACCENT : VI_DIM,
							0.6f + 0.4f * (d * 0.5f + 0.5f));
					}
		}

		// Sparks: lights lifting off the foot where the music is (ACCENT; a beat's brighter handful TEXT), over the ground
		// swell (BUTTON). Always the whole panel, the swell at the foot
		void sparks(const VizStyleState& s, std::vector<VizRect>& out)
		{
			const float base = 480, H = 480;
			float p[2 * VN_MAX_BARS];
			for (int i = 0; i < s.n; i++)
			{
				p[2 * i] = (i + 0.5f) / s.n * 640;
				p[2 * i + 1] = base - 6 - s.levels[i] / VN_TOP * H * 0.16f;
			}
			if (s.n > 0) p[0] = 0, p[2 * s.n - 2] = 640;
			std::vector<float> sm;
			smooth(p, s.n, sm);
			fillUnder(out, sm, base, VI_BUTTON, 1, false);
			for (int i = 0; i < s.sparkN; i++)
			{
				const VizStyleState::Spark& q = s.sparks[i];
				const float f = q.life / q.max;
				disc(out, q.x, base - 6 - q.y, q.r * (0.5f + 0.5f * f), q.big ? VI_TEXT : VI_ACCENT, std::min(1.0f, f * 1.6f));
			}
		}

		// Pocket console (redesigned 2026-11-19, docs/notes/2026-11-19-pocket-console-redesign.md): a little LCD in a
		// bezel, kept clean: few things, each on the dot grid (a dot 8 units, the last of them a gap), nothing blended. The
		// screen is 75 dots across (20 units of bezel each side), 56 down alone and 23 in the band; on it: the sun at the
		// top right (alone: a core in a halo, swelling with the bass, four rays while a beat rings, turning beat by
		// beat), the 15 bars (3 dots and a gap of 2, one ink) with a mid cap riding each held peak, the floor and under
		// it the road's dashes passing by with the loudness, and (alone) at the foot a heart that beats and a note that
		// hops. The band is the bars, their caps and the road, 17 rows tall.
		// Then the dots' gaps, and the screen's corners cut in steps
		const char* const PK_HEART[5] = {".#.#.", "#####", "#####", ".###.", "..#.."};
		const char* const PK_HEART_SMALL[5] = {".....", ".#.#.", ".###.", "..#..", "....."};
		const char* const PK_NOTE[5] = {"..###", "..#.#", "..#..", ".##..", ".##.."};

		void pocket(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const int COLS = 75, ROWS = band ? 23 : 56, FOOT = band ? 19 : 47, BH = band ? 17 : 32, N = 15;
			const float X0 = 20, Y0 = band ? 288 : 16, X1 = X0 + COLS * 8, Y1 = Y0 + ROWS * 8, top = band ? 280 : 0;
			// the bezel (four strips: the screen isn't painted twice), the screen's recess (a 3-unit line), the screen
			box(out, 0, top, 640, Y0, VI_P_BEZEL);
			box(out, 0, Y1, 640, 480, VI_P_BEZEL);
			box(out, 0, Y0, X0, Y1, VI_P_BEZEL);
			box(out, X1, Y0, 640, Y1, VI_P_BEZEL);
			box(out, X0 - 3, Y0 - 3, X1 + 3, Y0, VI_P_EDGE);
			box(out, X0 - 3, Y1, X1 + 3, Y1 + 3, VI_P_EDGE);
			box(out, X0 - 3, Y0, X0, Y1, VI_P_EDGE);
			box(out, X1, Y0, X1 + 3, Y1, VI_P_EDGE);
			box(out, X0, Y0, X1, Y1, VI_P_SCREEN);
			// dots, kept on the screen
			auto dot = [&](int c, int r, int w, int h, int ink) {
				const int c0 = std::max(c, 0), c1 = std::min(c + w, COLS), r0 = std::max(r, 0), r1 = std::min(r + h, ROWS);
				if (c1 > c0 && r1 > r0) box(out, X0 + c0 * 8, Y0 + r0 * 8, X0 + c1 * 8, Y0 + r1 * 8, ink);
			};
			// a little picture, each row's runs of '#' a box
			auto sprite = [&](const char* const* rows, int n, int c, int r, int ink) {
				for (int j = 0; j < n; j++)
					for (int i = 0; rows[j][i];)
					{
						int e = i;
						while (rows[j][e] == '#') e++;
						if (e > i) dot(c + i, r + j, e - i, 1, ink), i = e;
						else i++;
					}
			};
			auto disc = [&](int cx, int cy, int R, int ink) {
				for (int j = -R; j <= R; j++)
				{
					const int w = (int)std::lround(std::sqrt((float)(R * R + R - j * j)));
					dot(cx - w, cy + j, 2 * w + 1, 1, ink);
				}
			};
			if (!band)
			{
				float bass = 0;
				for (int i = 0; i < 4 && i < s.n; i++) bass += std::clamp(s.levels[i], 0.0f, VN_TOP) / (4 * VN_TOP);
				const int cx = 65, cy = 10;
				disc(cx, cy, bass > 0.6f ? 6 : 5, VI_P_GHOST);
				disc(cx, cy, s.pkPulse > 0.6f ? 4 : 3, VI_P_MID);
				if (s.pkPulse > 0.25f)
				{
					static const int RAYS[2][4][2] = {{{0, -8}, {0, 8}, {-8, 0}, {8, 0}}, {{-6, -6}, {6, -6}, {-6, 6}, {6, 6}}};
					for (const int* q : RAYS[s.beatsSeen & 1]) dot(cx + q[0], cy + q[1], 1, 1, VI_P_MID);
				}
			}
			// the bars. A level lights the rows whose step is at most it (the graph's steps: 1 + 251 k / (BH - 1))
			auto rowsOf = [&](float v) { return v < 1 ? 0 : std::min(BH, (int)((v - 1) * (BH - 1) / 251) + 1); };
			for (int b = 0; b < N && b < s.n; b++)
			{
				const int c = 1 + b * 5;
				const int lit = rowsOf(std::clamp(s.levels[b], 0.0f, VN_TOP)), cp = rowsOf(std::clamp(s.holds[b], 0.0f, VN_TOP));
				dot(c, FOOT - lit, 3, lit, VI_P_INK);
				if (cp > lit) dot(c, FOOT - cp, 3, 1, VI_P_MID);
			}
			// the floor under the bars (their width), and the road's dashes under it (2 dots every 5: one a bar)
			const int L = 1, R = COLS - 1;	// (the bars' first column, and the one past their last)
			dot(L, FOOT, R - L, 1, VI_P_MID);
			for (int c = L - (int)s.pkScroll; c < R; c += 5)
			{
				const int c0 = std::max(c, L), c1 = std::min(c + 2, R);
				if (c1 > c0) dot(c0, FOOT + 1, c1 - c0, 1, VI_P_GHOST);
			}
			// the foot: the heart (full while a beat rings) under the bars' left edge, the note (a hop each beat) under
			// their right
			// (alone: the band is the bars and their road, nothing else)
			if (!band)
			{
				const int sr = FOOT + 3;
				sprite(s.pkPulse > 0.4f ? PK_HEART : PK_HEART_SMALL, 5, L, sr, VI_P_INK);
				sprite(PK_NOTE, 5, R - 5, sr - (s.pkPulse > 0.6f ? 1 : 0), VI_P_INK);
			}
			// the dots' gaps: the 8th unit of every dot, across and down, in the screen's own colour (the lit dots are cut
			// apart, the empty screen stays flat: a grid shade over all of it read as a texture, not clean)
			for (int c = 0; c < COLS; c++) box(out, X0 + c * 8 + 7, Y0, X0 + c * 8 + 8, Y1, VI_P_SCREEN);
			for (int r = 0; r < ROWS; r++) box(out, X0, Y0 + r * 8 + 7, X1, Y0 + r * 8 + 8, VI_P_SCREEN);
			// the corners: three dots of bezel each, the recess's line going round the steps
			for (int k = 0; k < 4; k++)
			{
				const bool right = k & 1, low = k & 2;
				const int cx = right ? COLS - 1 : 0, cy = low ? ROWS - 1 : 0, dx = right ? -1 : 1, dy = low ? -1 : 1;
				const int at[3][2] = {{cx, cy}, {cx + dx, cy}, {cx, cy + dy}};
				for (const int* q : at)
					box(out, X0 + q[0] * 8 - (right ? 3 : 0), Y0 + q[1] * 8 - (low ? 3 : 0), X0 + q[0] * 8 + 8 + (right ? 0 : 3), Y0 + q[1] * 8 + 8 + (low ? 0 : 3),
						VI_P_EDGE);
				for (const int* q : at)
					box(out, X0 + q[0] * 8 - (q[0] == 0 ? 3 : 0), Y0 + q[1] * 8 - (q[1] == 0 ? 3 : 0), X0 + q[0] * 8 + 8 + (q[0] == COLS - 1 ? 3 : 0),
						Y0 + q[1] * 8 + 8 + (q[1] == ROWS - 1 ? 3 : 0), VI_P_BEZEL);
			}
		}

		// ---- the five fun ones (2026-11-19): ellipses, rounded boxes, round-ended lines, arcs, fans ----

		void oval(std::vector<VizRect>& out, float cx, float cy, float rx, float ry, int ink, float alpha = 1)
		{
			if (rx > 0 && ry > 0 && alpha > 0) out.push_back({cx, cy, rx, ry, ink, 0, alpha, 3});
		}

		void tri(std::vector<VizRect>& out, float ax, float ay, float bx, float by, float cx, float cy, int ink, float alpha = 1)
		{
			quad(out, ax, ay, bx, by, cx, cy, cx, cy, ink, alpha, alpha);
		}

		// a convex shape as a fan of triangles from its middle (no overlaps: a faint one blends evenly)
		void fan(std::vector<VizRect>& out, const std::vector<float>& p, int ink, float alpha = 1)
		{
			const int n = (int)p.size() / 2;
			if (n < 3) return;
			float cx = 0, cy = 0;
			for (int i = 0; i < n; i++) cx += p[2 * i], cy += p[2 * i + 1];
			cx /= n, cy /= n;
			for (int i = 0; i < n; i++)
			{
				const int j = (i + 1) % n;
				tri(out, cx, cy, p[2 * i], p[2 * i + 1], p[2 * j], p[2 * j + 1], ink, alpha);
			}
		}

		// a box with its top corners rounded rt and its bottom ones rb, as a fan
		void roundBox(std::vector<VizRect>& out, float x0, float y0, float x1, float y1, float rt, float rb, int ink, float alpha = 1)
		{
			if (x1 <= x0 || y1 <= y0 || alpha <= 0) return;
			const float m = std::min(x1 - x0, y1 - y0) / 2;
			rt = std::min(rt, m), rb = std::min(rb, m);
			std::vector<float> p;
			auto corner = [&](float cx, float cy, float r, float a0) {
				for (int k = 0; k <= 4; k++)
				{
					const float a = a0 + k * (float)M_PI / 8;
					p.push_back(cx + std::cos(a) * r), p.push_back(cy + std::sin(a) * r);
				}
			};
			// round: top left, top right, bottom right, bottom left (y down: angles from pi)
			if (rt > 0.5f) corner(x0 + rt, y0 + rt, rt, (float)M_PI); else p.push_back(x0), p.push_back(y0);
			if (rt > 0.5f) corner(x1 - rt, y0 + rt, rt, (float)M_PI * 1.5f); else p.push_back(x1), p.push_back(y0);
			if (rb > 0.5f) corner(x1 - rb, y1 - rb, rb, 0); else p.push_back(x1), p.push_back(y1);
			if (rb > 0.5f) corner(x0 + rb, y1 - rb, rb, (float)M_PI * 0.5f); else p.push_back(x0), p.push_back(y1);
			fan(out, p, ink, alpha);
		}

		// a line w wide with round ends (opaque: the end discs overlap it)
		void stick(std::vector<VizRect>& out, float ax, float ay, float bx, float by, float w, int ink, float alpha = 1)
		{
			std::vector<float> p = {ax, ay, bx, by};
			polyline(out, p, w, ink, alpha);
			disc(out, ax, ay, w / 2, ink, alpha);
			disc(out, bx, by, w / 2, ink, alpha);
		}

		// an arc's line round (cx, cy), from angle a0 to a1 (radians, y down as the canvas's)
		void arcLine(std::vector<VizRect>& out, float cx, float cy, float rx, float ry, float a0, float a1, float w, int ink, float alpha = 1)
		{
			std::vector<float> p;
			const int n = 16;
			for (int k = 0; k <= n; k++)
			{
				const float a = a0 + (a1 - a0) * k / n;
				p.push_back(cx + std::cos(a) * rx), p.push_back(cy + std::sin(a) * ry);
			}
			polyline(out, p, w, ink, alpha);
		}

		// a fixed hash 0..1 of two numbers (places that stay put: Sleepy moon's stars)
		float hash01(int a, int b)
		{
			uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
			h = (h ^ (h >> 13)) * 1274126177u;
			return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
		}

		// Dance line: twelve dancers, one a band, bass at the left: each jumps as high as its band is loud and throws its
		// arms up, the line swaying in time (half a sway a beat). ACCENT dancers, DIM floor tiles, BUTTON floor and shadows
		void danceLine(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const int n = 12;
			const float S = band ? 0.62f : 1.1f, floor = y0 + h * 0.84f, gap = (640 - 56) / (float)n;
			box(out, 0, floor + 3, 640, floor + 7, VI_BUTTON);
			for (int i = 0; i < n && i < s.n; i++)
			{
				const float v = s.levels[i] / VN_TOP, px0 = 28 + (i + 0.5f) * gap, sway = (float)std::sin(s.phase + i * 0.55);
				roundBox(out, px0 - gap / 2 + 4, floor + 12, px0 + gap / 2 - 4, floor + 19 + v * 10, 3, 3, VI_DIM, 0.25f + 0.75f * v);
				oval(out, px0, floor + 3, (20 - 9 * v) * S, 4 * S, VI_BUTTON);
				const float px = px0 + sway * 7 * S, fy = floor - v * h * 0.34f, hip = fy - 36 * S, sh = fy - 70 * S, lw = std::max(3.0f, 5.5f * S);
				stick(out, px, hip, px + sway * 3 * S, sh, lw, VI_ACCENT);
				for (int sg = -1; sg <= 1; sg += 2)
				{
					stick(out, px, hip, px + sg * (7 + 13 * v) * S, fy, lw, VI_ACCENT);
					const float a = 1.0f - 2.3f * std::clamp(v * 1.3f, 0.0f, 1.0f) + sg * 0.35f * sway, sx = px + sway * 3 * S;
					stick(out, sx, sh + 5 * S, sx + sg * std::cos(a) * 30 * S, sh + 5 * S + std::sin(a) * 30 * S, lw, VI_ACCENT);
				}
				disc(out, px + sway * 4 * S, sh - 17 * S, 11.5f * S, VI_ACCENT, 1);
			}
		}

		// Bouncy keys: fourteen keys rising with their bands (BUTTON, DIM tops), the black keys (DIM), a ball (ACCENT)
		// hopping from key to key a beat a hop toward a loud key, its trail (DIM); the key it lands on dips and lights
		void bouncyKeys(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const int n = 14;
			const float kw = 640.0f / n, base = y0 + h;
			for (int i = 0; i < n; i++)
			{
				const float ty = base - h * (0.2f + 0.34f * (i < s.n ? s.levels[i] / VN_TOP : 0)) + 9 * s.hit[i];
				const float x0 = i * kw + 2, x1 = (i + 1) * kw - 2;
				roundBox(out, x0, ty, x1, base, 7, 0, VI_BUTTON);
				roundBox(out, x0, ty, x1, ty + 6, 3, 0, VI_DIM);
				if (s.hit[i] > 0.03f) roundBox(out, x0, ty, x1, base, 7, 0, VI_ACCENT, s.hit[i]);
			}
			static const int BLACK[7] = {1, 1, 0, 1, 1, 1, 0};
			for (int i = 0; i < n - 1; i++)
				if (BLACK[i % 7]) box(out, (i + 1) * kw - kw * 0.2f, base - h * 0.11f, (i + 1) * kw + kw * 0.2f, base, VI_DIM);
			float bx, by, r;
			vizBouncyBall(s, bx, by, r);
			for (int k = 2; k < s.trailN; k++) disc(out, s.trail[k][0], s.trail[k][1], r * (0.75f - k * 0.06f), VI_DIM, 0.6f * (1 - (float)k / s.trailN));
			const float u = (float)std::clamp(s.sinceBeat / s.interval, 0.0, 1.0);
			const float sq = u < 1 ? std::max(0.0f, 1 - std::min(u, 1 - u) * 12) * 0.3f : 0;	// (squashed as it lands or leaves)
			oval(out, bx, by + r * sq, r * (1 + sq * 0.6f), r * (1 - sq), VI_ACCENT);
		}

		// Rocket: a little rocket flying right (TEXT body, ACCENT flame, fins and window), its flame growing with the bass;
		// the smoke behind it is the spectrum (DIM puffs, bass nearest the nozzle); the stars (DIM) streak past faster as
		// the song is louder
		void rocket(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const float S = band ? 0.8f : 1.25f, t = (float)s.time, bass = s.bass;
			const float rx = 640 * 0.76f, cy = y0 + h * 0.54f + std::sin(t * 2.2f) * h * 0.05f;
			const float v = 1.5f + 16 * s.loudShown;
			// (the stars over the whole panel in Art + visualizer too, behind the art and its lines)
			// (each a flat quad, opaque in DIM laid over the ground: the stars pass under the smoke, no blending)
			for (const auto& q : s.streaks)
			{
				const float yy = band ? 8 + q.y * (480 - 16) : y0 + 8 + q.y * (h - 16), hw = (1.5f + q.sp) / 2, x1 = q.x + 2 + v * q.sp * 1.6f;
				VizRect r{q.x, yy - hw, x1, yy - hw, VI_DIM_BG, 0.35f + 0.4f * std::min(q.sp, 1.0f), 1, 2};
				r.x2 = x1, r.y2 = yy + hw, r.x3 = q.x, r.y3 = yy + hw;
				out.push_back(r);
			}
			const int n = std::min(16, s.n);
			const float x0 = rx - 96 * S, stepx = (x0 - 10) / 16;
			for (int i = n - 1; i >= 0; i--)
			{
				const float lv = s.levels[i] / VN_TOP;
				// (opaque: DIM laid over the ground as the puff's faintness, the nearer puffs over the farther; the
				// translucent puffs overlapped five deep)
				out.push_back({x0 - i * stepx, cy + std::sin(t * 3.2f - i * 0.7f) * (3 + i * 1.3f) * S, 0, 5 * S + lv * h * 0.115f, VI_DIM_BG, 0.3f + 0.5f * lv, 1, 1});
			}
			// the rocket, shaken a little by the bass
			const float jx = (hash01((int)(t * 30), 1) - 0.5f) * 3 * bass, jy = (hash01((int)(t * 30), 2) - 0.5f) * 3 * bass;
			auto X = [&](float x) { return rx + jx + x * S; };
			auto Y = [&](float y) { return cy + jy + y * S; };
			const float L = 26 + 120 * bass, fl = std::sin(t * 40) * 4;
			tri(out, X(-56), Y(-13), X(-56), Y(13), X(-56 - L), Y(fl), VI_ACCENT);
			tri(out, X(-56), Y(-7), X(-56), Y(7), X(-56 - L * 0.5f), Y(-fl * 0.5f), VI_TEXT);
			tri(out, X(-50), Y(-14), X(-24), Y(-14), X(-62), Y(-38), VI_ACCENT);
			tri(out, X(-50), Y(14), X(-24), Y(14), X(-62), Y(38), VI_ACCENT);
			// the body: straight back, a curved nose (two quadratics), as one fan
			std::vector<float> body = {X(-52), Y(-17), X(18), Y(-17)};
			auto qd = [&](float ax, float ay, float cx, float cyy, float ex, float ey) {
				for (int k = 1; k <= 6; k++)
				{
					const float u = k / 6.0f, a = (1 - u) * (1 - u), b = 2 * (1 - u) * u, c = u * u;
					body.push_back(X(a * ax + b * cx + c * ex)), body.push_back(Y(a * ay + b * cyy + c * ey));
				}
			};
			qd(18, -17, 58, -11, 70, 0);
			qd(70, 0, 58, 11, 18, 17);
			body.push_back(X(-52)), body.push_back(Y(17));
			fan(out, body, VI_TEXT);
			box(out, X(-60), Y(-11), X(-51), Y(11), VI_DIM);
			disc(out, X(16), Y(0), 8.5f * S, VI_ACCENT, 1);
			disc(out, X(16), Y(0), 4.5f * S, VI_BG, 1);
		}

		// Rain check: rain across the whole panel where the music is (ACCENT drops), splashes (TEXT), puddle rings (DIM) on
		// the ground (BUTTON); alone, someone under an umbrella that bobs with the bass; with the art, it bounces off the
		// cover instead
		void rainCheck(const VizStyleState& s, std::vector<VizRect>& out)
		{
			const float ground = 480 - 12;
			box(out, 0, ground, 640, 480, VI_BUTTON);
			for (int i = 0; i < s.ringN; i++)
			{
				const auto& r = s.rings[i];
				arcLine(out, r.x, ground + 2, 4 + r.a * 16, 1.5f + r.a * 4, 0, (float)(2 * M_PI), 2, VI_DIM, 1 - r.a);
			}
			std::vector<float> seg(4);
			for (int i = 0; i < s.dropN; i++)
			{
				const auto& d = s.drops[i];
				seg = {d.x, d.y, d.x + d.v * 0.2f, d.y - d.v};
				polyline(out, seg, 2.5f, VI_ACCENT, 1);
			}
			for (int i = 0; i < s.splashN; i++) disc(out, s.splashes[i].x, s.splashes[i].y, 2.2f, VI_TEXT, 1);
			if (s.band) return;
			const float ux = 320, uy = 480 - 150 - 6 * s.bass, R = 112, feet = ground;
			stick(out, ux, uy - 4, ux, feet - 62, 5, VI_DIM);
			stick(out, ux - 34, feet - 30, ux - 36, feet - 3, 9, VI_TEXT);
			stick(out, ux - 18, feet - 30, ux - 16, feet - 3, 9, VI_TEXT);
			roundBox(out, ux - 44, feet - 92, ux - 8, feet - 26, 14, 14, VI_TEXT);
			disc(out, ux - 26, feet - 108, 15, VI_TEXT, 1);
			stick(out, ux - 14, feet - 76, ux + 1, feet - 64, 7, VI_TEXT);
			std::vector<float> dome;
			for (int k = 0; k <= 24; k++)
			{
				const float a = (float)M_PI + (float)M_PI * k / 24;
				dome.push_back(ux + std::cos(a) * R), dome.push_back(uy + std::sin(a) * R);
			}
			fan(out, dome, VI_ACCENT);
			for (int k = 0; k < 4; k++) oval(out, ux - R + R / 4 + k * R / 2, uy + 2, R / 4 - 1, 13, VI_BG);
			roundBox(out, ux - 3, uy - R - 12, ux + 3, uy - R + 4, 3, 3, VI_ACCENT);
		}

		// Sleepy moon: a moon (ACCENT) that dozes through quiet passages, eyes shut, breathing out Zs (TEXT), and wakes when
		// the song is loud (the loudness over a second): eyes open, a grin, rays out; stars (DIM, one in three TEXT)
		// twinkle with the treble, the clouds along the foot (DIM far, BUTTON near) are the spectrum. With the art: a
		// smaller moon at the top right
		void sleepyMoon(const VizStyleState& s, std::vector<VizRect>& out)
		{
			const bool art = s.band;
			const float W = 640, H = 480, R = art ? 46 : 98, aw = s.aw, t = (float)s.time, k = s.loudShown;
			const float mx = art ? W - 62 : W * 0.66f, my = (art ? 62 : H * 0.4f) - aw * k * 12;
			for (int i = 0; i < 36; i++)
			{
				const float sx = hash01(i, 11) * W, sy = 16 + hash01(i, 12) * H * 0.66f;
				if (std::hypot(sx - mx, sy - my) < R * 1.5f) continue;
				const int b = 10 + i % 10;
				const float v = b < s.n ? s.levels[b] / VN_TOP : 0, sz = 2.5f + v * 16 * (0.5f + hash01(i, 13)) + std::sin(t * 3 + i) * 0.8f;
				const int ink = i % 3 ? VI_DIM : VI_TEXT;
				// a bold cross: two bars 3 units thick on whole pixels (crisp; 2-unit lines were lost on the handheld)
				const float arm = sz + 1.5f;
				box(out, sx - arm, sy - 1.5f, sx + arm, sy + 1.5f, ink);
				box(out, sx - 1.5f, sy - arm, sx + 1.5f, sy + arm, ink);
			}
			// the clouds: two banks, each the outline of its discs (the far ones round the slow levels, DIM at 45% on the
			// ground; the near ones the levels, BUTTON) filled down, opaque: each pixel drawn once, the far bank only
			// where it shows over the near one (were 40 overlapping discs, the far ones blended: the panel's foot
			// filled several times over)
			const int n = std::min(20, s.n);
			constexpr int COLS = 96;
			float far[COLS + 1], near[COLS + 1];
			for (int c = 0; c <= COLS; c++)
			{
				const float x = c * W / COLS;
				float yf = H, yn = H;
				for (int i = 0; i < n; i++)
				{
					const float fx = x - ((i + 0.5f) / n * W + 14), fr = 30 + s.slow[i] / VN_TOP * H * 0.42f;
					if (std::fabs(fx) < fr) yf = std::min(yf, H + 2 - std::sqrt(fr * fr - fx * fx));
					const float nx = x - (i + 0.5f) / n * W, nr = 24 + s.levels[i] / VN_TOP * H * 0.24f;
					if (std::fabs(nx) < nr) yn = std::min(yn, H + 6 - std::sqrt(nr * nr - nx * nx));
				}
				far[c] = yf, near[c] = yn;
			}
			for (int c = 0; c < COLS; c++)
			{
				const float xa = c * W / COLS, xb = (c + 1) * W / COLS;
				if (far[c] < near[c] || far[c + 1] < near[c + 1])
				{
					VizRect r{xa, far[c], xb, far[c + 1], VI_DIM_BG, 0.45f, 1, 2};
					r.x2 = xb, r.y2 = std::max(far[c + 1], near[c + 1]), r.x3 = xa, r.y3 = std::max(far[c], near[c]);
					out.push_back(r);
				}
				if (near[c] < H || near[c + 1] < H) quad(out, xa, near[c], xb, near[c + 1], xb, H, xa, H, VI_BUTTON, 1, 1);
			}
			if (aw > 0.03f)
				for (int r = 0; r < 12; r++)
				{
					const float a = r / 12.0f * 6.283f + t * 0.3f, r0 = R * 1.16f, r1 = R * (1.24f + 0.34f * k * aw + (r % 2) * 0.06f);
					std::vector<float> p = {mx + std::cos(a) * r0, my + std::sin(a) * r0, mx + std::cos(a) * r1, my + std::sin(a) * r1};
					polyline(out, p, R * 0.07f, VI_ACCENT, aw);
				}
			disc(out, mx, my, R, VI_ACCENT, 1);
			disc(out, mx - R * 0.5f, my - R * 0.52f, R * 0.16f, VI_BG, 0.14f);
			disc(out, mx + R * 0.55f, my + R * 0.42f, R * 0.2f, VI_BG, 0.14f);
			disc(out, mx + R * 0.28f, my - R * 0.66f, R * 0.09f, VI_BG, 0.14f);
			const float lw = R * 0.075f;
			for (int sg = -1; sg <= 1; sg += 2)
			{
				const float ex = mx + sg * R * 0.34f, ey = my - R * 0.1f;
				if (aw < 0.5f) arcLine(out, ex, ey - R * 0.05f, R * 0.15f, R * 0.15f, 0.15f * (float)M_PI, 0.85f * (float)M_PI, lw, VI_BG);
				else oval(out, ex, ey, R * 0.12f, R * 0.17f * (aw - 0.5f) * 2, VI_BG);
			}
			if (aw < 0.5f) disc(out, mx, my + R * 0.32f, R * (0.07f + 0.025f * std::sin(t * 1.6f)), VI_BG, 1);
			else arcLine(out, mx, my + R * 0.16f, R * (0.3f + 0.08f * k), R * (0.3f + 0.08f * k), 0.12f * (float)M_PI, 0.88f * (float)M_PI, lw, VI_BG);
			if (aw < 0.5f)
				for (int z = 0; z < 3; z++)
				{
					const float p = std::fmod(s.zPhase + z / 3.0f, 1.0f), sz = (7 + p * 13) * (art ? 0.8f : 1.2f);
					const float zx = mx - R * 0.85f - p * R * 1.1f, zy = my - R * 0.75f - p * R * 0.8f + std::sin(p * 9) * 4;
					std::vector<float> zp = {zx - sz, zy - sz, zx + sz, zy - sz, zx - sz, zy + sz, zx + sz, zy + sz};
					polyline(out, zp, 2 + p * 2, VI_TEXT, std::sin(p * (float)M_PI) * (1 - aw * 2));
				}
		}
	}

	void vizShapes(const VizStyleState& s, bool band, std::vector<VizRect>& out)
	{
		out.clear();
		switch (s.vis)
		{
		case VIS_PEAKS_BANANA: peaks(s, band, out); break;
		case VIS_DOTS: dots(s, band, out); break;
		case VIS_STEPS: steps(s, band, out); break;
		case VIS_BARCODE: barcode(s, band, out); break;
		case VIS_RINGS: rings(s, band, out); break;
		case VIS_HORIZON: horizon(s, band, out); break;
		case VIS_POCKET: pocket(s, band, out); break;
		case VIS_RIBBON: ribbon(s, band, out); break;
		case VIS_GRID: grid(s, band, out); break;
		case VIS_STARFIELD: starfield(s, out); break;
		case VIS_STRINGS: strings(s, band, out); break;
		case VIS_HELIX: helix(s, band, out); break;
		case VIS_SPARKS: sparks(s, out); break;
		case VIS_BOUNCY: bouncyKeys(s, band, out); break;
		case VIS_ROCKET: rocket(s, band, out); break;
		case VIS_RAIN: rainCheck(s, out); break;
		case VIS_MOON: sleepyMoon(s, out); break;
		case VIS_DANCE: danceLine(s, band, out); break;
		default: mirrorBarsRects(s.levels, s.n, band, out); break;
		}
	}
}
