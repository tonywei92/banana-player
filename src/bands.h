#pragma once
// The manga reader's bands (docs/design/manga.md section 7): a very long strip page (a 720 x 20000 webtoon page) is
// never decoded whole. It is cut into horizontal bands of BAND_ROWS of the image's own rows, and only the bands near
// the view are decoded and kept. A band's rows are the image's, not the decoded width's, so a zoom (a wider decode)
// keeps the same bands and a sharper one takes an old one's place. Pure, in bpcore.
#include <vector>

namespace bp
{
	constexpr int BAND_ROWS = 2048;

	struct BandLayout
	{
		int srcW = 0, srcH = 0;	// the image's own size

		// a short tail joins the band before it (the last band is 1024 to 3071 rows)
		int count() const { return srcH < BAND_ROWS ? 1 : (srcH + BAND_ROWS / 2) / BAND_ROWS; }
		int top(int b) const { return b * BAND_ROWS; }
		int rows(int b) const { return b + 1 < count() ? BAND_ROWS : srcH - top(b); }
	};

	// a page worth banding: more than twice as tall as wide, and long enough for two bands
	bool bandedPage(int w, int h);

	// a band's decoded size at a decode width (never wider than the image: the GPU enlarges it as well)
	void bandSize(const BandLayout& l, int band, int targetW, int& w, int& h);

	// The bands to keep decoded for a page drawn at pageTop (view pixels from the view's top; negative above it) and
	// pageH tall, in a view viewH tall: those in the view first, top to bottom, then the ones within `below` pixels
	// under it (nearest first), then the ones within `above` pixels over it (nearest first). Empty when none is near.
	std::vector<int> bandsNeeded(const BandLayout& l, double pageTop, double pageH, double viewH, double above,
		double below);

	// A band's name where a page's index is used (the renderer's textures, the bottom panel's, the decoder's sets):
	// the page in the low 20 bits, the band + 1 above; a whole page is its own index (band -1)
	inline int bandKey(int page, int band) { return band < 0 ? page : (page | ((band + 1) << 20)); }
	inline int keyPage(int key) { return key & ((1 << 20) - 1); }
	inline int keyBand(int key) { return (key >> 20) - 1; }
}
