#include "viz_more.h"
#include "settings.h"

namespace bp
{
	// (a stub: drawn as Peaks until this style is made)
	std::string fishbowlGraph(bool overCover, const std::string&)
	{
		return visualizerGraph(VIS_PEAKS_BANANA, overCover);
	}

	bool writeFishbowlMaps(const std::string&)
	{
		return true;
	}
}
