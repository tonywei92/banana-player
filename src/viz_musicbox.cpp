#include "viz_more.h"
#include "settings.h"

namespace bp
{
	// (a stub: drawn as Peaks until this style is made)
	std::string musicboxGraph(bool overCover, const std::string&)
	{
		return visualizerGraph(VIS_PEAKS_BANANA, overCover);
	}

	bool writeMusicboxMaps(const std::string&)
	{
		return true;
	}
}
