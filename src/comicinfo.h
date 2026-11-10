#pragma once
// ComicInfo.xml, the metadata a chapter's CBZ carries (docs/design/manga.md, section 7): read from a file someone
// else made, written into a downloaded chapter. Plain string code with no XML library, so a bad file is a false from
// parseComicInfo, never a crash.
#include <string>

namespace bp
{
	struct ComicInfo
	{
		// the text fields are trimmed; "" is not set. number is text: "10.5", "Extra"
		std::string series, title, number, summary, writer, web;
		// Banana Player's own fields (BPSource, BPSeriesKey): where a downloaded chapter came from, so it links back
		std::string sourceId, seriesKey;
		int volume = -1, pageCount = -1;	// -1 is not set
		bool rightToLeft = false;	// the Manga element's YesAndRightToLeft
	};

	// the ComicInfo element's fields. false (out unchanged) when there's no complete ComicInfo element, or the text
	// isn't well formed enough to find one. Input over 1 MB is refused rather than cut short.
	bool parseComicInfo(const std::string& xml, ComicInfo& out);

	// a small valid document: only the fields that are set, escaped, one per line. parseComicInfo reads it back to the
	// same struct, for values already trimmed and free of control characters (those are dropped on the way out)
	std::string writeComicInfo(const ComicInfo& in);
}
