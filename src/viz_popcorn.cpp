#include "viz_more.h"
#include "settings.h"

namespace bp
{
	// (a stub: drawn as Peaks until this style is made)
	std::string popcornGraph(bool overCover, const std::string&)
	{
		return visualizerGraph(VIS_PEAKS_BANANA, overCover);
	}

	bool writePopcornMaps(const std::string&)
	{
		return true;
	}
}
