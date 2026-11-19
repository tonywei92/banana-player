// A Music folder file's own words — title, artist, album, track, disc, year, its length — through libavformat,
// for the media index (mediaindex.h). The tags the headers hold, never a decoder and never mpv, read the same way
// the dashboard's probe reads them (web/probe.cpp) and on whatever thread calls it (never the main one: up to the
// deadline per file, as durprobe).
#pragma once
#include <string>

namespace bp
{
	struct MediaTags
	{
		std::string title, artist, albumArtist, album;
		int track = 0, disc = 0, year = 0;	// 0: the tag is absent, or holds no number
		double duration = -1;				// seconds from the header; -1 unknown
		bool slow = false;					// the read was cut short by the deadline: the card's busy state, not the
											// file's own answer, so nothing of it is kept
	};

	// false: not a media file, unreadable, corrupt, or no answer within 2 s (out.slow set when it was the deadline:
	// a busy card, so the caller asks again next pass). A file that opens fine but carries no tags is true, with
	// empty words: that is its answer.
	bool probeMediaTags(const std::string& path, long long size, MediaTags& out);
}
