#include "viz_more.h"
#include "settings.h"

namespace bp
{
	// (a stub: drawn as Peaks until this style is made)
	std::string vinylGraph(bool overCover, const std::string&)
	{
		return visualizerGraph(VIS_PEAKS, overCover);
	}

	bool writeVinylMaps(const std::string&)
	{
		return true;
	}
}
