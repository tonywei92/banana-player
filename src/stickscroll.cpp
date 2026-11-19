#include "stickscroll.h"
#include <cmath>

namespace bp
{
	double stickScrollSpeed(int value, double maxSpeed, double deadZone)
	{
		if (!(maxSpeed > 0) || !std::isfinite(maxSpeed)) return 0;	// (also catches a NaN)
		if (!(deadZone >= 0)) deadZone = STICK_DEAD_ZONE;
		if (deadZone >= 1) return 0;	// a dead zone of the whole range never moves
		// SDL's range is -32768 … 32767; the full push is 1 whichever way
		double m = std::abs((double)value) / 32767.0;
		if (m > 1) m = 1;
		if (m <= deadZone) return 0;
		double push = (m - deadZone) / (1 - deadZone);
		double speed = maxSpeed * push * push;
		return value < 0 ? -speed : speed;
	}
}
