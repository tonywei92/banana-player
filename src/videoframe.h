// A video's look and facts in one open of the file (libavformat + libavcodec + libswscale): what it holds (the
// header's streams, never avformat_find_stream_info unless the header has no picture size) and one frame, a tenth of
// the way in, as the top screen's frame and the list's thumbnail. Runs on the calling thread (videoartworker's, never
// the main one): a deadline cuts a file that makes libavformat dig, one decoder thread, every buffer freed on return.
#pragma once
#include "pagedecode.h"
#include "videocache.h"
#include <string>
#include <vector>

namespace bp
{
	struct VideoGrab
	{
		VideoMeta meta;
		bool metaOk = false;	// the header was read
		PageImage large;	// the frame, its aspect kept (the display aspect), at most largeW wide; empty: none
		PageImage small;	// the frame cropped to smallW x smallH (the middle), for the list's rows
	};
	// false: not opened (not media, or past the deadline); true with no frame: no picture decoded (an audio-only file,
	// a codec this build lacks, a dark film whose two tries were black — the darker one is kept then)
	bool grabVideo(const std::string& path, VideoGrab& out, int largeW = 960, int smallW = 128, int smallH = 72);

	// RGBA (alpha ignored) as a baseline JPEG's bytes; empty on failure
	std::string encodeJpeg(const PageImage& img, int quality = 82);
}
