// A Music folder file's tags, read from the container's own header (see mediatags.h). The open is probe.cpp's:
// a small probesize, no find_stream_info, a 2 s interrupt.
#include "mediatags.h"
#include "durations.h"
#include "webmedia.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sys/stat.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

namespace bp
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		struct Deadline
		{
			Clock::time_point end;
			bool hit = false;
		};

		int interrupted(void* opaque)
		{
			auto* d = static_cast<Deadline*>(opaque);
			if (Clock::now() > d->end) d->hit = true;
			return d->hit;
		}

		std::string tag(AVDictionary* d, const char* key)
		{
			if (!d) return "";
			AVDictionaryEntry* e = av_dict_get(d, key, nullptr, 0);
			return e && e->value ? e->value : "";
		}

		// "3/12" and "3" both: the number before the slash (the index keeps a track's place, not the count)
		int numberTag(AVDictionary* d, const char* key, const char* alt)
		{
			std::string t = trackText(tag(d, key));
			if (t.empty() && alt) t = trackText(tag(d, alt));
			return t.empty() ? 0 : std::max(0, atoi(t.c_str()));
		}

		double headerDuration(const AVFormatContext* f)
		{
			if (f->duration != AV_NOPTS_VALUE && f->duration > 0) return double(f->duration) / AV_TIME_BASE;
			double d = -1;
			// read_header alone fills the streams' own (an MP3's Xing frame, an MP4's moov) but not always the context's
			for (unsigned i = 0; i < f->nb_streams; i++)
			{
				const AVStream* s = f->streams[i];
				if (s->disposition & AV_DISPOSITION_ATTACHED_PIC) continue;
				if (s->duration != AV_NOPTS_VALUE && s->duration > 0 && s->time_base.num > 0 && s->time_base.den > 0)
					d = std::max(d, double(s->duration) * av_q2d(s->time_base));
			}
			return d;
		}
	}

	bool probeMediaTags(const std::string& path, long long size, MediaTags& out)
	{
		out = MediaTags{};
		struct stat st;
		if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
		AVFormatContext* ctx = avformat_alloc_context();
		if (!ctx) return false;
		Deadline dl;
		dl.end = Clock::now() + std::chrono::seconds(2);
		ctx->interrupt_callback.callback = interrupted;
		ctx->interrupt_callback.opaque = &dl;
		AVDictionary* opts = nullptr;
		// half a megabyte is enough to name the container and its words (probe.cpp's megabyte is for codecs too,
		// and find_stream_info is never called here), and never reaches a decoder
		av_dict_set(&opts, "probesize", "524288", 0);
		int r = avformat_open_input(&ctx, path.c_str(), nullptr, &opts);
		av_dict_free(&opts);
		if (r < 0) return false;	// (ctx freed by avformat_open_input)
		if (dl.hit)
		{
			avformat_close_input(&ctx);
			out.slow = true;
			return false;
		}
		AVFormatContext* f = ctx;
		out.title = tag(f->metadata, "title");
		out.artist = tag(f->metadata, "artist");
		out.album = tag(f->metadata, "album");
		out.albumArtist = tag(f->metadata, "album_artist");
		if (out.albumArtist.empty()) out.albumArtist = tag(f->metadata, "albumartist");
		// a date written any of the three ways; a video's year is its "date" too
		std::string date = tag(f->metadata, "date");
		if (yearFromDate(date).empty()) date = tag(f->metadata, "year");
		if (yearFromDate(date).empty()) date = tag(f->metadata, "creation_time");
		std::string year = yearFromDate(date);
		out.year = year.empty() ? 0 : std::max(0, atoi(year.c_str()));
		out.track = numberTag(f->metadata, "track", nullptr);
		out.disc = numberTag(f->metadata, "disc", "discnumber");
		double d = saneDuration(headerDuration(f), size);
		out.duration = d;
		avformat_close_input(&f);
		return true;
	}
}
