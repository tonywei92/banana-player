#include "bands.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	bool bandedPage(int w, int h)
	{
		if (w <= 0 || h <= 0) return false;
		return (long long)h > 2LL * w && BandLayout{w, h}.count() >= 2;
	}

	void bandSize(const BandLayout& l, int band, int targetW, int& w, int& h)
	{
		double scale = targetW > 0 && l.srcW > 0 ? std::min(1.0, double(targetW) / l.srcW) : 1.0;
		w = std::max(1, (int)std::lround(l.srcW * scale));
		h = std::max(1, (int)std::lround(l.rows(band) * scale));
	}

	std::vector<int> bandsNeeded(const BandLayout& l, double pageTop, double pageH, double viewH, double above,
		double below)
	{
		std::vector<int> in, under, over;
		if (l.srcH <= 0 || pageH <= 0) return in;
		for (int b = 0; b < l.count(); b++)
		{
			double y0 = pageTop + pageH * l.top(b) / l.srcH, y1 = pageTop + pageH * (l.top(b) + l.rows(b)) / l.srcH;
			if (y1 <= -above || y0 >= viewH + below) continue;
			if (y1 <= 0) over.push_back(b);
			else if (y0 >= viewH) under.push_back(b);
			else in.push_back(b);
		}
		in.insert(in.end(), under.begin(), under.end());
		in.insert(in.end(), over.rbegin(), over.rend());
		return in;
	}
}
