// libavformat probe, chapters and cover art for the web dashboard (see probe.h).
#include "web/probe.h"
#include "webmedia.h"
#include "durations.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <list>
#include <mutex>
#include <sys/stat.h>
#include <unordered_map>

extern "C"
{
#include <libavcodec/codec_desc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/pixdesc.h>
}

namespace bp
{
	namespace
	{
		struct Deadline
		{
			std::chrono::steady_clock::time_point end;
		};

		int interrupted(void* opaque)
		{
			return std::chrono::steady_clock::now() > static_cast<Deadline*>(opaque)->end;
		}

		// the context is opened with no stream info: the codecs come from what the headers hold
		struct Opened
		{
			AVFormatContext* ctx = nullptr;
			Deadline dl;
			~Opened() { if (ctx) avformat_close_input(&ctx); }

			bool open(const std::string& path)
			{
				dl.end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
				ctx = avformat_alloc_context();
				if (!ctx) return false;
				ctx->interrupt_callback.callback = interrupted;
				ctx->interrupt_callback.opaque = &dl;
				AVDictionary* opts = nullptr;
				av_dict_set(&opts, "probesize", "1048576", 0);
				av_dict_set(&opts, "analyzeduration", "500000", 0);
				int r = avformat_open_input(&ctx, path.c_str(), nullptr, &opts);
				av_dict_free(&opts);
				if (r < 0)
				{
					ctx = nullptr;	// avformat_open_input freed it
					return false;
				}
				return true;
			}
		};

		std::string tag(AVDictionary* d, const char* key)
		{
			if (!d) return "";
			AVDictionaryEntry* e = av_dict_get(d, key, nullptr, 0);
			return e && e->value ? e->value : "";
		}

		std::string tagCI(AVDictionary* d, const char* key)
		{
			if (!d) return "";
			AVDictionaryEntry* e = av_dict_get(d, key, nullptr, 0);
			return e && e->value ? e->value : "";
		}

		const char* codecName(AVCodecID id)
		{
			const AVCodecDescriptor* d = avcodec_descriptor_get(id);
			return d ? d->name : "";
		}

		std::string lower(std::string s)
		{
			for (auto& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		std::string baseName(const std::string& p)
		{
			size_t i = p.rfind('/');
			return i == std::string::npos ? p : p.substr(i + 1);
		}

		bool readAll(const std::string& path, std::string& out)
		{
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return false;
			char buf[65536];
			size_t n;
			out.clear();
			while ((n = fread(buf, 1, sizeof buf, f)) > 0)
			{
				out.append(buf, n);
				if (out.size() > (size_t)16 << 20) break;	// no cover is that big
			}
			fclose(f);
			return !out.empty();
		}

		std::string sniffImage(const std::string& b)
		{
			if (b.size() >= 4 && (unsigned char)b[0] == 0x89 && b[1] == 'P') return "image/png";
			if (b.size() >= 12 && !memcmp(b.data(), "RIFF", 4) && !memcmp(b.data() + 8, "WEBP", 4)) return "image/webp";
			return "image/jpeg";
		}

		// cover.* folder.* front.* beside the file (any case)
		std::string sidecarCover(const std::string& real)
		{
			size_t i = real.rfind('/');
			std::string dir = i == std::string::npos ? "." : real.substr(0, i ? i : 1);
			DIR* d = opendir(dir.c_str());
			if (!d) return "";
			std::string found;
			int bestRank = 99;
			while (dirent* e = readdir(d))
			{
				std::string n = lower(e->d_name);
				size_t dot = n.rfind('.');
				if (dot == std::string::npos) continue;
				std::string stem = n.substr(0, dot), ext = n.substr(dot + 1);
				if (ext != "jpg" && ext != "jpeg" && ext != "png" && ext != "webp") continue;
				int rank = stem == "cover" ? 0 : stem == "folder" ? 1 : stem == "front" ? 2 : 99;
				if (rank < bestRank)
				{
					bestRank = rank;
					found = dir + "/" + e->d_name;
				}
			}
			closedir(d);
			return found;
		}

		// the LRU: most recent first
		struct Cache
		{
			std::mutex m;
			std::list<std::pair<std::string, MediaInfo>> order;
			std::unordered_map<std::string, std::list<std::pair<std::string, MediaInfo>>::iterator> index;
		};
		Cache& cache()
		{
			static Cache* c = new Cache;	// never destroyed: a probe may still run at exit
			return *c;
		}
	}

	bool probeMedia(const std::string& real, MediaInfo& out)
	{
		struct stat st;
		if (stat(real.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
		std::string key = real + '\n' + std::to_string((long long)st.st_mtime) + '\n' + std::to_string((long long)st.st_size);
		Cache& c = cache();
		{
			std::lock_guard<std::mutex> g(c.m);
			auto it = c.index.find(key);
			if (it != c.index.end())
			{
				c.order.splice(c.order.begin(), c.order, it->second);
				out = it->second->second;
				return true;
			}
		}

		Opened o;
		if (!o.open(real)) return false;
		AVFormatContext* f = o.ctx;
		MediaInfo m;
		m.container = f->iformat && f->iformat->name ? f->iformat->name : "";
		// an MP3's reader names every MPEG audio stream "mp3" until a frame is read; layer I/II inside a ".mp3" (game
		// and broadcast rips) plays in no browser, so for these alone the first frames are read (an audio file's
		// few kilobytes), and the stream comes back as mp2 when it is
		if (m.container == "mp3") avformat_find_stream_info(f, nullptr);
		bool pic = false;
		for (unsigned i = 0; i < f->nb_streams; i++)
		{
			AVStream* s = f->streams[i];
			if (s->disposition & AV_DISPOSITION_ATTACHED_PIC)
			{
				pic = true;
				continue;
			}
			if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && m.vcodec.empty()) m.vcodec = codecName(s->codecpar->codec_id);
			else if (s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO || s->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
			{
				MediaStream ms;
				ms.index = (int)i;
				ms.lang = tag(s->metadata, "language");
				if (ms.lang == "und") ms.lang.clear();
				ms.title = tag(s->metadata, "title");
				ms.codec = codecName(s->codecpar->codec_id);
				ms.channels = s->codecpar->ch_layout.nb_channels;
				ms.isDefault = s->disposition & AV_DISPOSITION_DEFAULT;
				ms.forced = s->disposition & AV_DISPOSITION_FORCED;
				(s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO ? m.audio : m.subs).push_back(ms);
			}
		}
		if (!m.audio.empty())
		{
			// the browser plays the default stream: it's the one that decides whether there's sound
			const MediaStream* d = &m.audio[0];
			for (auto& a : m.audio)
				if (a.isDefault)
				{
					d = &a;
					break;
				}
			m.acodec = d->codec;
		}
		if (f->duration != AV_NOPTS_VALUE && f->duration > 0) m.duration = double(f->duration) / AV_TIME_BASE;
		if (m.duration <= 0)
			for (unsigned i = 0; i < f->nb_streams; i++)	// no find_stream_info: the streams' own, as mp4 gives them
			{
				AVStream* s = f->streams[i];
				if (s->duration != AV_NOPTS_VALUE && s->duration > 0)
					m.duration = std::max(m.duration, double(s->duration) * av_q2d(s->time_base));
			}
		m.mime = mimeFor(baseName(real));
		Playable pl = playability(m.container, m.vcodec, m.acodec);
		m.playable = pl == Playable::Yes ? "yes" : pl == Playable::Maybe ? "maybe" : "no";
		for (unsigned i = 0; i < f->nb_chapters; i++)
		{
			AVChapter* ch = f->chapters[i];
			MediaChapter mc;
			mc.title = tag(ch->metadata, "title");
			mc.start = std::round(double(ch->start) * av_q2d(ch->time_base) * 1000) / 1000;
			m.chapters.push_back(mc);
		}
		m.title = tag(f->metadata, "title");
		m.artist = tag(f->metadata, "artist");
		if (m.artist.empty()) m.artist = tag(f->metadata, "album_artist");
		m.album = tag(f->metadata, "album");
		std::string date = tagCI(f->metadata, "date");
		if (yearFromDate(date).empty()) date = tagCI(f->metadata, "year");
		if (yearFromDate(date).empty()) date = tagCI(f->metadata, "creation_time");
		m.year = yearFromDate(date);
		m.genre = tagCI(f->metadata, "genre");
		m.track = trackText(tagCI(f->metadata, "track"));
		m.disc = trackText(tagCI(f->metadata, "disc"));
		if (m.disc.empty()) m.disc = trackText(tagCI(f->metadata, "discnumber"));
		m.albumArtist = tagCI(f->metadata, "album_artist");
		if (m.albumArtist.empty()) m.albumArtist = tagCI(f->metadata, "albumartist");
		m.composer = tagCI(f->metadata, "composer");
		m.size = (long long)st.st_size;
		const AVStream* as = nullptr;
		const AVStream* vs = nullptr;
		long long sumRate = 0;
		for (unsigned i = 0; i < f->nb_streams; i++)
		{
			const AVStream* s = f->streams[i];
			if (s->disposition & AV_DISPOSITION_ATTACHED_PIC) continue;
			sumRate += std::max<int64_t>(0, s->codecpar->bit_rate);
			if (s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				if (!as || ((s->disposition & AV_DISPOSITION_DEFAULT) && !(as->disposition & AV_DISPOSITION_DEFAULT))) as = s;
			}
			else if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !vs) vs = s;
		}
		m.bitrate = f->bit_rate > 0 ? f->bit_rate : sumRate;
		if (m.bitrate <= 0 && m.duration > 0) m.bitrate = (long long)(double(m.size) * 8 / m.duration);
		if (as)
		{
			m.sampleRate = std::max(0, as->codecpar->sample_rate);
			m.channels = as->codecpar->ch_layout.nb_channels;
			if (m.channels > 0)
			{
				// without stream info the order may be unspecified: the usual layout for that many channels
				AVChannelLayout l = as->codecpar->ch_layout;
				AVChannelLayout def = {};
				if (l.order == AV_CHANNEL_ORDER_UNSPEC)
				{
					av_channel_layout_default(&def, m.channels);
					l = def;
				}
				char buf[64] = "";
				if (l.order != AV_CHANNEL_ORDER_UNSPEC && av_channel_layout_describe(&l, buf, sizeof buf) > 0) m.channelLayout = buf;
				av_channel_layout_uninit(&def);
			}
			m.audioBitrate = std::max<int64_t>(0, as->codecpar->bit_rate);
		}
		if (vs)
		{
			m.width = std::max(0, vs->codecpar->width);
			m.height = std::max(0, vs->codecpar->height);
			AVRational r = vs->avg_frame_rate.num > 0 && vs->avg_frame_rate.den > 0 ? vs->avg_frame_rate : vs->r_frame_rate;
			if (r.num > 0 && r.den > 0) m.fps = std::round(av_q2d(r) * 1000) / 1000;
			m.videoBitrate = std::max<int64_t>(0, vs->codecpar->bit_rate);
			const char* pf = vs->codecpar->format >= 0 ? av_get_pix_fmt_name((AVPixelFormat)vs->codecpar->format) : nullptr;
			if (pf) m.pixFmt = pf;
		}
		m.hasCover = pic || !sidecarCover(real).empty();
		// (the file lists' lengths: known now, so never probed for them)
		if (saneDuration(m.duration) > 0) DurationCache::shared().put(durationKey(real, (long long)st.st_size, (long long)st.st_mtime), m.duration);

		{
			std::lock_guard<std::mutex> g(c.m);
			if (!c.index.count(key))
			{
				c.order.emplace_front(key, m);
				c.index[key] = c.order.begin();
				while (c.order.size() > 256)
				{
					c.index.erase(c.order.back().first);
					c.order.pop_back();
				}
			}
		}
		out = m;
		return true;
	}

	bool probeCover(const std::string& real, std::string& bytes, std::string& mime)
	{
		{
			Opened o;
			if (o.open(real))
				for (unsigned i = 0; i < o.ctx->nb_streams; i++)
				{
					AVStream* s = o.ctx->streams[i];
					if ((s->disposition & AV_DISPOSITION_ATTACHED_PIC) && s->attached_pic.size > 0)
					{
						bytes.assign((const char*)s->attached_pic.data, s->attached_pic.size);
						mime = sniffImage(bytes);
						return true;
					}
				}
		}
		std::string side = sidecarCover(real);
		if (side.empty() || !readAll(side, bytes)) return false;
		mime = sniffImage(bytes);
		return true;
	}
}
