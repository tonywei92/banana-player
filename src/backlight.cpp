#include "backlight.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>

namespace bp
{
	static const char* NAMES[2] = {"backlight1", "backlight0"};	// top, bottom (as bananad has them)

	static int readInt(const std::string& path)
	{
		std::ifstream in(path);
		int v = -1;
		if (!(in >> v)) return -1;
		return v;
	}

	Backlight::Backlight()
	{
		if (const char* d = getenv("BP_BACKLIGHT_DIR"); d && *d)
		{
			sysDir = d;
			store = std::string(d) + "/store";
		}
	}

	bool Backlight::present(Panel p) const
	{
		return readInt(sysDir + "/" + NAMES[p] + "/max_brightness") > 0;
	}

	int Backlight::permille(Panel p) const
	{
		std::string d = sysDir + "/" + NAMES[p];
		int max = readInt(d + "/max_brightness"), v = readInt(d + "/brightness");
		if (max <= 0 || v < 0) return -1;
		return (v * 1000 + max / 2) / max;
	}

	bool Backlight::set(Panel p, int pm)
	{
		pm = std::clamp(pm, 100, 1000);
		std::string d = sysDir + "/" + NAMES[p];
		int max = readInt(d + "/max_brightness");
		if (max <= 0) return false;
		// rounded as bananad rounds (a level saved and restored doesn't creep), never 0 (that's off)
		{
			std::ofstream out(d + "/brightness");
			out << std::max(1, (max * pm + 500) / 1000) << "\n";
			if (!out.flush()) return false;
		}
		// both levels for bananad's restore at boot: this one, and the other as stored (else as it is). Never a 0 or a
		// level under 10%: bananad throws such a file away and starts both panels at the kernel's 50%
		Panel o = p == TOP ? BOTTOM : TOP;
		int other = -1;
		{
			std::ifstream in(store);
			int t, b;
			if (in >> t)
			{
				if (!(in >> b)) b = t;	// one number (before BananaOS 0.4.2): both
				other = o == TOP ? t : b;
			}
		}
		if (other < 100 || other > 1000) other = permille(o);
		if (other < 100 || other > 1000) other = pm;
		std::ofstream out(store);
		out << (p == TOP ? pm : other) << " " << (p == TOP ? other : pm) << "\n";
		return true;
	}

	bool Backlight::setNow(Panel p, int pm)
	{
		pm = std::clamp(pm, 1, 1000);
		std::string d = sysDir + "/" + NAMES[p];
		int max = readInt(d + "/max_brightness");
		if (max <= 0) return false;
		std::ofstream out(d + "/brightness");
		out << std::max(1, (max * pm + 500) / 1000) << "\n";
		return (bool)out.flush();
	}

	int stepBrightness(int pm, int dir)
	{
		// the nearest ten percent, then a step
		int tenth = (pm + 50) / 100;
		if (dir > 0 && tenth * 100 <= pm) tenth++;
		else if (dir < 0 && tenth * 100 >= pm) tenth--;
		return std::clamp(tenth, 1, 10) * 100;
	}
}
