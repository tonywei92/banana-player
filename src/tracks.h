#pragma once
#include <cstdint>
#include <mpv/client.h>
#include <string>
#include <vector>

namespace bp
{
	enum class TrackType { Video, Audio, Sub, Other };

	// one entry of mpv's track-list
	struct Track
	{
		int64_t id = 0;
		TrackType type = TrackType::Other;
		std::string title, lang, codec;
		int channels = 0;
		int w = 0, h = 0;	// a video track's picture size (mpv's demux-w / demux-h); 0 unknown
		bool selected = false, isDefault = false, forced = false, external = false, albumArt = false;
	};

	std::vector<Track> parseTrackList(const mpv_node* list);

	// "English · AC3 5.1 · Director's commentary"; n (1-based, within its type) names a track with nothing else to
	// tell it by: "Track 2 · AAC Stereo"
	std::string trackLabel(const Track& t, int n);

	// one entry of mpv's chapter-list
	struct Chapter
	{
		double time = 0;
		std::string title;
	};

	std::vector<Chapter> parseChapterList(const mpv_node* list);

	// no title, mpv's "(unnamed)", or one that is only a timestamp (mkvmerge's "00:12:30.000"): the number says more
	bool chapterUntitled(const std::string& title);

	// the title, or "Chapter n" (n 1-based) when untitled
	std::string chapterLabel(const Chapter& c, int n);

	const int CHAPTER_FILE_START = -1;	// chapterTarget: seek to 0 (the file's first chapter starts later)
	const int CHAPTER_NONE = -2;		// chapterTarget: nowhere to go
	const double CHAPTER_BACK_S = 5;	// previous: further into a chapter than this goes to its start

	// where previous (dir < 0) or next (dir > 0) goes from chapter `current` (-1 before the first) at `position`:
	// an index, CHAPTER_FILE_START or CHAPTER_NONE. Next never runs on into the next file
	int chapterTarget(const std::vector<Chapter>& chapters, int current, double position, int dir);

	// ISO 639-1/-2 codes the menu knows -> their names; anything else comes back as it is
	std::string languageName(const std::string& code);

	// "subrip" -> "SRT", "hdmv_pgs_subtitle" -> "PGS", "eac3" -> "E-AC3"...
	std::string codecName(const std::string& codec);
}
