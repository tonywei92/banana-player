#pragma once
// The left stick's up/down push as a scrolling speed (issue 16): SDL's SDL_CONTROLLER_AXIS_LEFTY in -32768 … 32767,
// up negative. A rest near the middle is ignored (the dead zone), and past it the speed rises gently (squared), so a
// small push crawls and a full one is the reader's own panel height a second. Pure: the tests in core_test.cpp pin it.

namespace bp
{
	// the share of a full push where the stick is still at rest; a resting stick's drift stays under it
	constexpr double STICK_DEAD_ZONE = 0.15;

	// px/s, signed as the axis is (up negative); 0 inside the dead zone. maxSpeed: the full push's speed (a reader
	// passes its panel's height), deadZone: STICK_DEAD_ZONE, here so the curve can be read in one place and tested
	double stickScrollSpeed(int value, double maxSpeed, double deadZone = STICK_DEAD_ZONE);
}
