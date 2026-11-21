// The videos' cache on the card (<state dir>/cache/videos/): for each video a small thumbnail for the lists, a larger
// frame for the top screen's preview and what the file holds (length, picture, tracks), made once in the background
// (videoartworker.h) and read back at once on every later visit. Each video's three files share one name: a hash of
// its path, size and mtime, so a file replaced or rewritten is another name and nothing goes stale; the folder is kept
// under a size by treecopy.h's trimCacheFolder (the files used longest ago go first: a hit touches their mtime).
//
// The pure half: the names, the info's text form, the labels the top screen shows (core_test).
#pragma once
#include "tracks.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	// a video's tracks and picture, as the probe (or, when it plays, mpv) tells them
	struct VideoMeta
	{
		struct Stream
		{
			std::string lang, codec, title;
			int channels = 0;
		};
		double duration = 0;	// seconds; 0 unknown
		int width = 0, height = 0;	// the picture's; 0 unknown
		std::string vcodec;	// avcodec's / mpv's name ("h264"); "" none
		int chapters = 0;
		std::vector<Stream> audio, subs;
		bool fromPlayer = false;	// written from what mpv saw while it played (external subtitles too)
	};

	// the three files' shared name: 16 hex digits of a 64-bit FNV-1a over path, size and mtime
	std::string videoCacheName(const std::string& path, long long size, int64_t mtime);

	// the files in the cache's folder for that name: the info, the list's thumbnail, the top screen's frame
	struct VideoCacheFiles
	{
		std::string meta, small, large;
	};
	VideoCacheFiles videoCacheFiles(const std::string& dir, const std::string& name);

	// the info as text (a line a fact: "key<TAB>value", a stream "a<TAB>lang<TAB>codec<TAB>channels<TAB>title") and
	// back. A tab or newline in a value is made a space. parse: false for text that isn't one (no version line)
	std::string videoMetaText(const VideoMeta& m);
	bool parseVideoMeta(const std::string& text, VideoMeta& out);

	// what mpv tells of a video it opened (its track list, length and chapters), as the cache keeps it: fromPlayer
	VideoMeta videoMetaFromTracks(const std::vector<Track>& tracks, double duration, int chapters);

	// the top screen's words for it: "H.264 · 1920 × 818", "English 5.1, Italian", "English, Spanish + 6",
	// "14:48 · 8 chapters". "" when there is nothing to say
	std::string videoPictureLabel(const VideoMeta& m);
	std::string videoAudioLabel(const VideoMeta& m);
	std::string videoSubsLabel(const VideoMeta& m);
	std::string videoLengthLabel(const VideoMeta& m);
}
