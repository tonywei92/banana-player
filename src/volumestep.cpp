#include "volumestep.h"

namespace bp
{
	VolumeStep volumeStep(int now, int maxVolume, bool up, bool hold)
	{
		const int top = maxVolume > 100 ? maxVolume : 100;
		int v = now < 0 ? 0 : now > top ? top : now;
		if (!up) return {v > 5 ? v - 5 : 0, false};
		// a hold stops at the handheld's 100%: only a fresh press goes past it
		if (hold && v >= 100) return {v, v == 100};
		// the step under 100% never jumps the line (a level that isn't a five lands on it)
		int ceiling = v < 100 ? 100 : top;
		v = v + 5 > ceiling ? ceiling : v + 5;
		return {v, false};
	}
}
