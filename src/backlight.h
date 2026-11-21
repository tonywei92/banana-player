#pragma once
#include <string>

namespace bp
{
	// The panels' backlights, set as BananaOS's own menu sets them (Settings › Display there): each panel its own level
	// in thousandths of its max_brightness (backlight1 the top panel, backlight0 the bottom), written to sysfs and kept
	// in /data/banana/backlight ("<top> <bottom>") for bananad to restore at boot. bananad reads sysfs at each
	// brightness key, so the keys go on from a level set here. sysDir and store stand in for those in tests
	struct Backlight
	{
		std::string sysDir = "/sys/class/backlight", store = "/data/banana/backlight";
		enum Panel { TOP, BOTTOM };
		// BP_BACKLIGHT_DIR: a made-up sysfs (backlight0/, backlight1/ with max_brightness, brightness) and its store,
		// for test runs off the handheld
		Backlight();

		bool present(Panel p) const;
		// thousandths of the maximum, -1 where it can't be read
		int permille(Panel p) const;
		// 100 (10%) to 1000; the other panel's level kept beside it in the store
		bool set(Panel p, int permille);
		// the panel's level for now only (sysfs, never the store): the dim before Screen off, which must not be what
		// bananad restores at boot. 1 to 1000 thousandths, never 0 (that's off)
		bool setNow(Panel p, int permille);
	};

	// a level one step (10%) on or back from where it is (bananad's keys leave it between steps), 10% to 100%
	int stepBrightness(int permille, int dir);
}
