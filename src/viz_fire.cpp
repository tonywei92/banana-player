#include "viz_more.h"
#include "settings.h"

namespace bp
{
	// (a stub: drawn as Peaks until this style is made)
	std::string fireGraph(bool overCover, const std::string&)
	{
		return visualizerGraph(VIS_PEAKS, overCover);
	}

	bool writeFireMaps(const std::string&)
	{
		return true;
	}
}
