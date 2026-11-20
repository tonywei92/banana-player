// Match album art's pure half (artbackdrop.h): median cut, the filters, the blurred 160 x 120 picture, the theme's
// ground and stops, and the cache's bytes. No window, no drawing, no threads: the worker and the panels call it.
#include "artbackdrop.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace artbg
{
	namespace
	{
		struct Hsl
		{
			double h = 0, s = 0, l = 0;	// h in degrees, s and l 0..1
		};

		double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
		unsigned char quant(double v) { return (unsigned char)std::lround(clamp01(v) * 255); }
		Rgb rgb3(double r, double g, double b) { return {quant(r), quant(g), quant(b)}; }
		Rgb mixRgb(Rgb under, Rgb c, int percent)
		{
			return {(unsigned char)(under.r + (c.r - under.r) * percent / 100),
				(unsigned char)(under.g + (c.g - under.g) * percent / 100),
				(unsigned char)(under.b + (c.b - under.b) * percent / 100)};
		}

		Hsl toHsl(double r, double g, double b)
		{
			double mx = std::max({r, g, b}), mn = std::min({r, g, b});
			Hsl h;
			h.l = (mx + mn) / 2;
			double d = mx - mn;
			if (d <= 0) return h;
			h.s = h.l > 0.5 ? d / std::max(1e-9, 2 - mx - mn) : d / std::max(1e-9, mx + mn);
			if (mx == r) h.h = (g - b) / d + (g < b ? 6 : 0);
			else if (mx == g) h.h = (b - r) / d + 2;
			else h.h = (r - g) / d + 4;
			h.h *= 60;
			return h;
		}

		Rgb fromHsl(Hsl h)
		{
			h.h = std::fmod(h.h, 360);
			if (h.h < 0) h.h += 360;
			double c = (1 - std::fabs(2 * h.l - 1)) * h.s;
			double x = c * (1 - std::fabs(std::fmod(h.h / 60, 2) - 1));
			double m = h.l - c / 2;
			double r = 0, g = 0, b = 0;
			if (h.h < 60) { r = c; g = x; }
			else if (h.h < 120) { r = x; g = c; }
			else if (h.h < 180) { g = c; b = x; }
			else if (h.h < 240) { g = x; b = c; }
			else if (h.h < 300) { r = x; b = c; }
			else { r = c; b = x; }
			return {quant(r + m), quant(g + m), quant(b + m)};
		}

		double linear(unsigned char v)
		{
			double c = v / 255.0;
			return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
		}
		// relative luminance (WCAG), 0..1
		double relLum(Rgb c) { return 0.2126 * linear(c.r) + 0.7152 * linear(c.g) + 0.0722 * linear(c.b); }
		double contrast(double a, double b)
		{
			double hi = std::max(a, b), lo = std::min(a, b);
			return (hi + 0.05) / (lo + 0.05);
		}
		double hueGap(double a, double b)
		{
			double d = std::fabs(std::fmod(a - b, 360.0));
			return d > 180 ? 360 - d : d;
		}

		struct Pixel
		{
			double r = 0, g = 0, b = 0;
		};

		// The median cut's buckets: the indices of the 32 x 32 copy's pixels that are still together.
		struct Bucket
		{
			std::vector<int> px;
		};

		void bounds(const std::vector<Pixel>& px, const Bucket& b, double mn[3], double mx[3])
		{
			for (int k = 0; k < 3; k++) { mn[k] = 255; mx[k] = 0; }
			for (int i : b.px)
			{
				double v[3] = {px[i].r, px[i].g, px[i].b};
				for (int k = 0; k < 3; k++) { mn[k] = std::min(mn[k], v[k]); mx[k] = std::max(mx[k], v[k]); }
			}
		}

		// a percentile of the picture's relative luminance over a band of rows (q 0..1: the 95th says how bright a
		// dark theme's veil must be, the 5th how white a light theme's must be)
		int percentile(std::vector<double> ys, double q)
		{
			if (ys.empty()) return 0;
			std::sort(ys.begin(), ys.end());
			size_t i = (size_t)std::ceil(std::clamp(q, 0.0, 1.0) * (double)ys.size()) - 1;
			return std::clamp((int)std::lround(ys[std::min(i, ys.size() - 1)] * 255), 0, 255);
		}
	}

	Art artBackdrop(const uint8_t* rgba, int w, int h)
	{
		Art art;
		if (!rgba || w <= 0 || h <= 0) return art;

		// 1. the 32 x 32 copy: each cell the average of its share of the art (a transparent pixel says nothing)
		std::vector<Pixel> small;
		small.reserve(32 * 32);
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++)
			{
				int sx0 = x * w / 32, sx1 = std::max(sx0 + 1, (x + 1) * w / 32);
				int sy0 = y * h / 32, sy1 = std::max(sy0 + 1, (y + 1) * h / 32);
				double r = 0, g = 0, b = 0;
				int n = 0;
				for (int sy = sy0; sy < sy1 && sy < h; sy++)
					for (int sx = sx0; sx < sx1 && sx < w; sx++)
					{
						const uint8_t* p = rgba + ((size_t)sy * w + sx) * 4;
						if (p[3] == 0) continue;
						r += p[0]; g += p[1]; b += p[2];
						n++;
					}
				if (n) small.push_back({r / n, g / n, b / n});
			}
		if (small.empty()) return art;

		// 2. median cut into 8 colours: split the widest bucket on its widest channel at its median
		std::vector<Bucket> boxes(1);
		boxes[0].px.resize(small.size());
		std::iota(boxes[0].px.begin(), boxes[0].px.end(), 0);
		while (boxes.size() < 8)
		{
			int best = -1, bestCh = 0;
			double bestRange = 0;
			for (size_t i = 0; i < boxes.size(); i++)
			{
				if (boxes[i].px.size() < 2) continue;
				double mn[3], mx[3];
				bounds(small, boxes[i], mn, mx);
				for (int k = 0; k < 3; k++)
					if (mx[k] - mn[k] > bestRange) { bestRange = mx[k] - mn[k]; best = (int)i; bestCh = k; }
			}
			if (best < 0 || bestRange <= 0) break;
			Bucket& b = boxes[best];
			std::sort(b.px.begin(), b.px.end(), [&](int a, int c) {
				double va = bestCh == 0 ? small[a].r : bestCh == 1 ? small[a].g : small[a].b;
				double vc = bestCh == 0 ? small[c].r : bestCh == 1 ? small[c].g : small[c].b;
				return va < vc;
			});
			size_t mid = b.px.size() / 2;
			if (mid == 0 || mid >= b.px.size()) break;
			Bucket right;
			right.px.assign(b.px.begin() + mid, b.px.end());
			b.px.resize(mid);
			boxes.push_back(std::move(right));
		}

		// 3. drop what cannot give a hue, and take the largest that is left (a grey has saturation to none)
		double bestShare = -1;
		Hsl best;
		for (const Bucket& b : boxes)
		{
			Pixel sum;
			for (int i : b.px) { sum.r += small[i].r; sum.g += small[i].g; sum.b += small[i].b; }
			double n = (double)b.px.size();
			Hsl hsl = toHsl(sum.r / n / 255, sum.g / n / 255, sum.b / n / 255);
			double share = n / (double)small.size();
			if (hsl.l < 0.10 || hsl.l > 0.90) continue;	// near-black or near-white
			if (hsl.s > 0.85 || hsl.s < 0.10) continue;	// neon, or a grey with no hue to give
			if (share < 0.05) continue;	// rare
			if (share > bestShare) { bestShare = share; best = hsl; art.hasHue = true; }
		}
		if (art.hasHue) art.hue = (int)std::lround(best.h) % 360;

		// 4. the picture: crop to 4:3, box-filter to 160 x 120
		double ar = 4.0 / 3.0;
		int cropW = w, cropH = h, ox = 0, oy = 0;
		if (w * 3.0 > h * 4.0)
		{
			cropW = std::max(1, (int)std::lround(h * ar));
			ox = (w - cropW) / 2;
		}
		else
		{
			cropH = std::max(1, (int)std::lround(w / ar));
			oy = (h - cropH) / 2;
		}
		std::vector<float> img(PIXELS * 3, 0.0f);
		for (int dy = 0; dy < H; dy++)
			for (int dx = 0; dx < W; dx++)
			{
				double sx0 = ox + dx * (double)cropW / W, sx1 = ox + (dx + 1) * (double)cropW / W;
				double sy0 = oy + dy * (double)cropH / H, sy1 = oy + (dy + 1) * (double)cropH / H;
				double r = 0, g = 0, b = 0, sum = 0;
				for (int sy = (int)std::floor(sy0); sy < (int)std::ceil(sy1); sy++)
				{
					if (sy < 0 || sy >= h) continue;
					double wy = std::min(sy1, sy + 1.0) - std::max(sy0, (double)sy);
					if (wy <= 0) continue;
					for (int sx = (int)std::floor(sx0); sx < (int)std::ceil(sx1); sx++)
					{
						if (sx < 0 || sx >= w) continue;
						double wx = std::min(sx1, sx + 1.0) - std::max(sx0, (double)sx);
						double wt = wx * wy;
						if (wt <= 0) continue;
						const uint8_t* p = rgba + ((size_t)sy * w + sx) * 4;
						r += p[0] * wt; g += p[1] * wt; b += p[2] * wt;
						sum += wt;
					}
				}
				size_t at = ((size_t)dy * W + dx) * 3;
				if (sum > 0) { img[at] = (float)(r / sum); img[at + 1] = (float)(g / sum); img[at + 2] = (float)(b / sum); }
			}

		// 5. the Gaussian blur: separable, 61 taps, edges clamped
		const double sigma = 10.0;
		float kernel[2 * BLUR_RADIUS + 1];
		double total = 0;
		for (int i = -BLUR_RADIUS; i <= BLUR_RADIUS; i++)
		{
			double v = std::exp(-(i * i) / (2 * sigma * sigma));
			kernel[i + BLUR_RADIUS] = (float)v;
			total += v;
		}
		for (float& v : kernel) v = (float)(v / total);
		std::vector<float> tmp(PIXELS * 3, 0.0f), blurred(PIXELS * 3, 0.0f);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				for (int c = 0; c < 3; c++)
				{
					float sum = 0;
					for (int k = -BLUR_RADIUS; k <= BLUR_RADIUS; k++)
					{
						int sx = std::clamp(x + k, 0, W - 1);
						sum += kernel[k + BLUR_RADIUS] * img[((size_t)y * W + sx) * 3 + c];
					}
					tmp[((size_t)y * W + x) * 3 + c] = sum;
				}
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				for (int c = 0; c < 3; c++)
				{
					float sum = 0;
					for (int k = -BLUR_RADIUS; k <= BLUR_RADIUS; k++)
					{
						int sy = std::clamp(y + k, 0, H - 1);
						sum += kernel[k + BLUR_RADIUS] * tmp[((size_t)sy * W + x) * 3 + c];
					}
					blurred[((size_t)y * W + x) * 3 + c] = sum;
				}

		// 6. the saturation to 55%, the 95th- and 5th-percentile luminances of the two bands (above and below y 168
		// on the panel: the bands are 42 and 78 rows of the working size)
		art.rgb.resize(PIXELS * 3);
		std::vector<double> top, low;
		top.reserve(42 * W);
		low.reserve((H - 42) * W);
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				size_t at = ((size_t)y * W + x) * 3;
				Hsl hsl = toHsl(blurred[at] / 255.0, blurred[at + 1] / 255.0, blurred[at + 2] / 255.0);
				hsl.s *= 0.55;
				Rgb c = fromHsl(hsl);
				art.rgb[at] = c.r;
				art.rgb[at + 1] = c.g;
				art.rgb[at + 2] = c.b;
				(y < 42 ? top : low).push_back(relLum(c));
			}
		art.l95top = percentile(top, 0.95);
		art.l95low = percentile(low, 0.95);
		art.l5top = percentile(top, 0.05);
		art.l5low = percentile(low, 0.05);
		return art;
	}

	Backdrop backdropFor(const ThemeColours& t, int hue, int l95top, int l95low, int l5top, int l5low)
	{
		Backdrop b;
		// a light-bodied theme (its BG lighter than its TEXT: Light, Sakura, Gundam, the 16-bit ones) draws the
		// light version on its own paper, and falls back to its plain ground
		const bool light = relLum(t.bg) > relLum(t.text);
		Rgb charcoal = mixRgb(t.bg, t.surface, 50);	// the dark ground's charcoal, the light one's paper
		b.ground = charcoal;
		if (light)
		{
			// the light fallback: the theme's plain ground, nothing over it (a gradient of one colour)
			b.top = t.bg;
			b.bottom = t.bg;
		}
		else
		{
			b.top = mixRgb(charcoal, t.surface, 35);
			b.bottom = t.bg;
		}
		for (int i = 0; i < 5; i++) { b.stop[i] = charcoal; b.veil[i] = 0; }

		// the accent clash: an accent with a colour of its own, 150..210 degrees from the art's hue, is the
		// fallback (in a light theme as in a dark one: the art must not fight the theme's one colour)
		bool clash = false;
		if (hue >= 0)
		{
			Hsl acc = toHsl(t.accent.r / 255.0, t.accent.g / 255.0, t.accent.b / 255.0);
			if (acc.s >= 0.30 && hueGap(acc.h, hue) >= 150) clash = true;
		}
		if (hue < 0 || clash) return b;	// fallback: the gradient (a light theme's plain BG)

		if (light)
		{
			// the art's hue at S 34% / L 82%, blended 70% into the theme's paper: a white veil hides the picture
			// more than a dark one does, so the paper itself carries the art's colour or nobody sees it is there
			Hsl tintH;
			tintH.h = hue;
			tintH.s = 0.34;
			tintH.l = 0.82;
			b.ground = mixRgb(charcoal, fromHsl(tintH), 70);
			b.fallback = false;
			// the same five stops, whitened for a dark art: each stop's see-through is scaled until the picture's
			// 5th-percentile luminance under it leaves the theme's TEXT, DIM_ON and ACCENT at 4.5:1
			const int* base = VEIL_BASE;
			const double pic[5] = {l5top / 255.0, l5top / 255.0, l5low / 255.0, l5low / 255.0, l5low / 255.0};
			const double yGround = relLum(b.ground);
			bool allHard = true;
			for (int i = 0; i < 5; i++)
			{
				int need = 97;	// more than the cap: this stop would hide the picture
				for (int p = base[i]; p <= 96; p++)
				{
					double under = yGround * (p / 100.0) + pic[i] * (1 - p / 100.0);
					// body text 4.5:1; the dim and accent (large or non-text) 3:1, or the veil whites the art out
					if (contrast(relLum(t.text), under) >= 4.5 && contrast(relLum(t.dimOn), under) >= 3.0 &&
						contrast(relLum(t.accent), under) >= 3.0) { need = p; break; }
				}
				if (need <= 96) allHard = false;
				b.stop[i] = b.ground;
				b.veil[i] = std::min(need, 96);
			}
			if (allHard)
			{
				// nothing of the art would show: the theme's plain ground
				Backdrop f;
				f.ground = t.bg;
				f.top = t.bg;
				f.bottom = t.bg;
				for (int i = 0; i < 5; i++) { f.stop[i] = t.bg; f.veil[i] = 0; }
				return f;
			}
			return b;
		}

		// the art's hue at S 15% / L 13%, blended 45% into the theme's charcoal
		Hsl tintH;
		tintH.h = hue;
		tintH.s = 0.15;
		tintH.l = 0.13;
		Rgb tint = fromHsl(tintH);
		b.ground = mixRgb(charcoal, tint, 45);
		b.fallback = false;

		// the five stops: VEIL_BASE, raised until the picture under them leaves the
		// theme's TEXT 4.5:1 above y 168 and its DIM_ON 4.5:1 below. More than 96 at every stop: nothing shows.
		const int* base = VEIL_BASE;
		const double pic[5] = {l95top / 255.0, l95top / 255.0, l95low / 255.0, l95low / 255.0, l95low / 255.0};
		const double yGround = relLum(b.ground);
		bool allHard = true;
		for (int i = 0; i < 5; i++)
		{
			double ink = relLum(i < 2 ? t.text : t.dimOn);
			int need = 97;	// more than the cap: this stop would hide the picture
			for (int p = base[i]; p <= 96; p++)
			{
				double under = yGround * (p / 100.0) + pic[i] * (1 - p / 100.0);
				if (contrast(ink, under) >= 4.5) { need = p; break; }
			}
			if (need <= 96) allHard = false;
			b.stop[i] = b.ground;
			b.veil[i] = std::min(need, 96);
		}
		if (allHard)
		{
			// every stop would have to be past the cap: nothing of the art would show, so the neutral gradient
			Backdrop f;
			f.ground = charcoal;
			f.top = b.top;
			f.bottom = b.bottom;
			for (int i = 0; i < 5; i++) { f.stop[i] = charcoal; f.veil[i] = 0; }
			return f;
		}
		return b;
	}

	Backdrop backdropFor(const ThemeColours& t, const Art& art)
	{
		return backdropFor(t, art.hasHue ? art.hue : -1, art.l95top, art.l95low, art.l5top, art.l5low);
	}

	// the veil's opacity at a row of a page h tall (STOP_Y's stops, linear between them, as the panels draw it)
	static int veilAt(const Backdrop& b, double f)
	{
		int at = b.veil[4];
		for (int i = 1; i < 5; i++)
		{
			double s0 = (double)STOP_Y[i - 1] / STOP_Y[4], s1 = (double)STOP_Y[i] / STOP_Y[4];
			if (f <= s1)
			{
				double t = s1 > s0 ? std::max(0.0, (f - s0) / (s1 - s0)) : 0.0;
				return (int)std::lround(b.veil[i - 1] + t * (b.veil[i] - b.veil[i - 1]));
			}
		}
		return at;
	}

	std::vector<uint8_t> composePage(const Art& art, const Backdrop& b, int w, int h)
	{
		// the backdrop as one page of RGBA (a panel draws it stretched, linearly filtered): the picture with the
		// veil baked in per row — the theme's part applied here, on the main thread, so the theme can change
		// without the art being read again (the cache holds the art's own bytes only). A fallback page is the
		// gradient, opaque (a light theme's two ends are one colour: a plain page)
		if (w <= 0 || h <= 0) return {};
		std::vector<uint8_t> out((size_t)w * h * 4, 255);
		bool picture = !b.fallback && art.rgb.size() == PIXELS * 3;
		for (int y = 0; y < h; y++)
		{
			double f = h > 1 ? (double)y / (h - 1) : 1.0;
			uint8_t* row = &out[(size_t)y * w * 4];
			if (!picture)
			{
				Rgb c{(unsigned char)(b.top.r + (int)std::lround((b.bottom.r - b.top.r) * f)),
					(unsigned char)(b.top.g + (int)std::lround((b.bottom.g - b.top.g) * f)),
					(unsigned char)(b.top.b + (int)std::lround((b.bottom.b - b.top.b) * f))};
				for (int x = 0; x < w; x++) row[x * 4] = c.r, row[x * 4 + 1] = c.g, row[x * 4 + 2] = c.b;
				continue;
			}
			// the picture's two rows this page row lies between, mixed by where it falls (a page taller than the
			// picture is stretched smoothly, not in steps of whole rows: the GPU's filter then smooths across)
			const int at256 = std::max(0, ((2 * y + 1) * H * 256) / (2 * h) - 128);
			const int r0 = std::min(at256 >> 8, H - 1), r1 = std::min(r0 + 1, H - 1), t = at256 & 255;
			const uint8_t* src0 = &art.rgb[(size_t)r0 * W * 3];
			const uint8_t* src1 = &art.rgb[(size_t)r1 * W * 3];
			int veil = veilAt(b, f);
			const uint8_t ground[3] = {b.ground.r, b.ground.g, b.ground.b};
			for (int x = 0; x < w; x++)
			{
				int s = x * W / w;
				for (int c = 0; c < 3; c++)
				{
					int px = (src0[s * 3 + c] * (256 - t) + src1[s * 3 + c] * t + 128) >> 8;
					row[x * 4 + c] = (uint8_t)(px + (ground[c] - px) * veil / 100);
				}
			}
		}
		return out;
	}

	uint64_t fnv64(const uint8_t* data, size_t n)
	{
		uint64_t h = 14695981039346656037ull;
		for (size_t i = 0; i < n; i++) h = (h ^ data[i]) * 1099511628211ull;
		return h;
	}

	std::string cacheName(uint64_t hash)
	{
		char buf[32];
		snprintf(buf, sizeof buf, "%016llx.bg", (unsigned long long)hash);
		return buf;
	}

	std::string cachePath(uint64_t hash) { return "artbg/" + cacheName(hash); }

	std::vector<uint8_t> encode(const Art& art)
	{
		if (art.rgb.size() != PIXELS * 3 || art.hue < -1 || art.hue >= 360 || art.l95top < 0 || art.l95top > 255 ||
			art.l95low < 0 || art.l95low > 255 || art.l5top < 0 || art.l5top > 255 || art.l5low < 0 || art.l5low > 255)
			return {};
		std::vector<uint8_t> out(7 + PIXELS * 3);
		out[0] = CACHE_VERSION;
		int hue = art.hasHue ? art.hue : 0xFFFF;
		out[1] = (uint8_t)(hue & 0xFF);
		out[2] = (uint8_t)(hue >> 8);
		out[3] = (uint8_t)art.l95top;
		out[4] = (uint8_t)art.l95low;
		out[5] = (uint8_t)art.l5top;
		out[6] = (uint8_t)art.l5low;
		std::copy(art.rgb.begin(), art.rgb.end(), out.begin() + 7);
		return out;
	}

	bool decode(const uint8_t* data, size_t n, Art& art)
	{
		if (!data || n != 7 + PIXELS * 3 || data[0] != CACHE_VERSION) return false;
		int hue = data[1] | (data[2] << 8);
		if (hue != 0xFFFF && hue >= 360) return false;
		art.hasHue = hue != 0xFFFF;
		art.hue = art.hasHue ? hue : 0;
		art.l95top = data[3];
		art.l95low = data[4];
		art.l5top = data[5];
		art.l5low = data[6];
		art.rgb.assign(data + 7, data + 7 + PIXELS * 3);
		return true;
	}

	std::vector<size_t> evict(const std::vector<CacheFile>& files, size_t cap)
	{
		std::vector<size_t> order(files.size());
		std::iota(order.begin(), order.end(), 0);
		std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			if (files[a].used != files[b].used) return files[a].used < files[b].used;
			return files[a].name < files[b].name;
		});
		size_t total = 0;
		for (const CacheFile& f : files) total += f.bytes;
		std::vector<size_t> drop;
		for (size_t i : order)
		{
			if (total <= cap) break;
			total -= std::min(total, files[i].bytes);
			drop.push_back(i);
		}
		return drop;
	}
}
