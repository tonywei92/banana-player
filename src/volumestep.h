#pragma once
// The volume keys' step, the handheld's own and the player's D-pad UP / DOWN: five a press on the handheld's 0-100%,
// then the player's gain past it up to Max volume. A hold never boosts: its steps stop at 100% (leaning on UP could
// otherwise reach Max volume); going past 100% takes a fresh press. Pure: core_test.cpp pins it.

namespace bp
{
	struct VolumeStep
	{
		int v = 0;		// the volume as heard after the step (0 to Max volume)
		bool stopped = false;	// an UP hold's step that the 100% line stopped (a fresh press would boost)
	};
	// now: the volume as heard; maxVolume: Settings::volumeMax; hold: this is the key repeating, not the press itself
	VolumeStep volumeStep(int now, int maxVolume, bool up, bool hold);
}
