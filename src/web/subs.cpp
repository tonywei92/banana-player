// Subtitles for the web dashboard's player (see subs.h).
#include "web/subs.h"
#include "json.h"
#include "sidecar.h"
#include "tracks.h"
#include "webmedia.h"
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <cstdio>
#include <deque>
#include <dirent.h>
#include <functional>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

namespace bp
{
	namespace
	{
		const size_t CACHE_MAX = (size_t)50 << 20;

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

		std::string dirName(const std::string& p)
		{
			size_t i = p.rfind('/');
			return i == std::string::npos ? "." : i == 0 ? "/" : p.substr(0, i);
		}

		bool textCodec(const std::string& c)
		{
			static const char* T[] = {"subrip", "srt", "ass", "ssa", "mov_text", "webvtt", "text", "microdvd", "subviewer",
				"subviewer1", "sami", "realtext", "jacosub", "mpl2", "pjs", "stl", "vplayer"};
			for (auto t : T)
				if (c == t) return true;
			return false;
		}

		std::mutex& dirLock()
		{
			static std::mutex* m = new std::mutex;
			return *m;
		}
		std::string& cacheDirStore()
		{
			static std::string* s = new std::string;
			return *s;
		}

		std::string cacheDir()
		{
			std::string d;
			{
				std::lock_guard<std::mutex> g(dirLock());
				d = cacheDirStore();
			}
			if (const char* e = getenv("BP_WEB_CACHE_DIR")) d = e;
			if (d.empty())
			{
				if (const char* c = getenv("XDG_CONFIG_HOME")) d = std::string(c) + "/banana-player/webcache";
				else if (const char* h = getenv("HOME")) d = std::string(h) + "/.local/state/banana-player/webcache";
				else d = "/tmp/banana-player-webcache";
			}
			return d;
		}

		void mkdirs(const std::string& path)
		{
			for (size_t i = 1; i <= path.size(); i++)
				if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0700);
		}

		std::string hashOf(const std::string& s)
		{
			uint64_t h = 1469598103934665603ULL;
			for (unsigned char c : s)
			{
				h ^= c;
				h *= 1099511628211ULL;
			}
			char b[20];
			snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
			return b;
		}

		bool readFile(const std::string& path, std::string& out, size_t max)
		{
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return false;
			char buf[65536];
			size_t n;
			out.clear();
			while ((n = fread(buf, 1, sizeof buf, f)) > 0)
			{
				out.append(buf, n);
				if (out.size() > max)
				{
					fclose(f);
					return false;
				}
			}
			fclose(f);
			return true;
		}

		// oldest first out, to 50 MB
		void trimCache(const std::string& dir)
		{
			struct F { std::string path; time_t mtime; size_t size; };
			std::vector<F> files;
			size_t total = 0;
			if (DIR* d = opendir(dir.c_str()))
			{
				while (dirent* e = readdir(d))
				{
					if (e->d_name[0] == '.') continue;
					struct stat st;
					std::string p = dir + "/" + e->d_name;
					if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
					files.push_back({p, st.st_mtime, (size_t)st.st_size});
					total += (size_t)st.st_size;
				}
				closedir(d);
			}
			std::sort(files.begin(), files.end(), [](const F& a, const F& b) { return a.mtime < b.mtime; });
			for (size_t i = 0; i < files.size() && total > CACHE_MAX; i++)
			{
				unlink(files[i].path.c_str());
				total -= files[i].size;
			}
		}

		struct Cue { double start, end; std::string text; };

		// ASS dialogue as ffmpeg's decoders hand it over: "ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text"
		std::string assEventText(const char* ass)
		{
			std::string s = ass ? ass : "";
			size_t pos = 0;
			for (int k = 0; k < 8; k++)
			{
				size_t c = s.find(',', pos);
				if (c == std::string::npos) return s;
				pos = c + 1;
			}
			return s.substr(pos);
		}

		// the cues of stream `index`, reading the file through; progress 0..1 as it goes. "" and an error on failure
		bool extract(const std::string& path, int index, std::string& vtt, std::string& err, const std::function<void(double)>& progress)
		{
			AVFormatContext* f = nullptr;
			if (avformat_open_input(&f, path.c_str(), nullptr, nullptr) < 0)
			{
				err = "can't open the file";
				return false;
			}
			struct Closer { AVFormatContext*& f; ~Closer() { avformat_close_input(&f); } } closer{f};
			if (index < 0 || index >= (int)f->nb_streams || f->streams[index]->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
			{
				err = "no such subtitle track";
				return false;
			}
			AVStream* st = f->streams[index];
			const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
			if (!codec)
			{
				err = "can't decode these subtitles";
				return false;
			}
			AVCodecContext* cc = avcodec_alloc_context3(codec);
			if (!cc) { err = "out of memory"; return false; }
			struct CcFree { AVCodecContext*& c; ~CcFree() { avcodec_free_context(&c); } } ccFree{cc};
			avcodec_parameters_to_context(cc, st->codecpar);
			cc->pkt_timebase = st->time_base;
			if (avcodec_open2(cc, codec, nullptr) < 0)
			{
				err = "can't decode these subtitles";
				return false;
			}
			for (unsigned i = 0; i < f->nb_streams; i++)
				if ((int)i != index) f->streams[i]->discard = AVDISCARD_ALL;

			struct stat sb;
			double total = stat(path.c_str(), &sb) == 0 && sb.st_size > 0 ? (double)sb.st_size : 0;
			std::vector<Cue> cues;
			AVPacket* pkt = av_packet_alloc();
			int lastPct = -1;
			while (av_read_frame(f, pkt) >= 0)
			{
				if (total > 0)
				{
					int pct = (int)(100.0 * avio_tell(f->pb) / total);
					if (pct != lastPct)
					{
						lastPct = pct;
						progress(std::min(1.0, pct / 100.0));
					}
				}
				if (pkt->stream_index == index)
				{
					AVSubtitle sub;
					int got = 0;
					if (avcodec_decode_subtitle2(cc, &sub, &got, pkt) >= 0 && got)
					{
						double base = sub.pts != AV_NOPTS_VALUE ? double(sub.pts) / AV_TIME_BASE
							: pkt->pts != AV_NOPTS_VALUE ? double(pkt->pts) * av_q2d(st->time_base) : -1;
						if (base >= 0)
						{
							double start = base + sub.start_display_time / 1000.0, end = 0;
							if (sub.end_display_time > 0 && sub.end_display_time != UINT32_MAX) end = base + sub.end_display_time / 1000.0;
							else if (pkt->duration > 0) end = base + double(pkt->duration) * av_q2d(st->time_base);
							if (end <= start) end = start + 3;
							std::string text;
							for (unsigned r = 0; r < sub.num_rects; r++)
							{
								AVSubtitleRect* rc = sub.rects[r];
								std::string t = rc->type == SUBTITLE_ASS ? assEventText(rc->ass) : rc->type == SUBTITLE_TEXT && rc->text ? rc->text : "";
								t = assCueText(t);
								if (!t.empty()) text += (text.empty() ? "" : "\n") + t;
							}
							if (!text.empty()) cues.push_back({start, end, text});
						}
						avsubtitle_free(&sub);
					}
				}
				av_packet_unref(pkt);
			}
			av_packet_free(&pkt);
			std::stable_sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) { return a.start < b.start; });
			vtt = "WEBVTT\n\n";
			for (auto& c : cues)
			{
				std::string t = vttCue(c.start, c.end, c.text);
				if (!t.empty()) vtt += t.substr(8);
			}
			return true;
		}

		std::string assTime(double t)
		{
			if (t < 0) t = 0;
			long long cs = (long long)(t * 100 + 0.5);
			char b[32];
			snprintf(b, sizeof b, "%lld:%02lld:%02lld.%02lld", cs / 360000, cs / 6000 % 60, cs / 100 % 60, cs % 100);
			return b;
		}

		// an ASS/SSA track as a whole script, as libass (JASSUB, in the browser) reads it: the header the container
		// keeps (Script Info, styles) and a Dialogue line per packet. Packets are copied, not decoded: Matroska keeps
		// them as "ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text"
		bool extractAss(const std::string& path, int index, std::string& out, std::string& err, const std::function<void(double)>& progress)
		{
			AVFormatContext* f = nullptr;
			if (avformat_open_input(&f, path.c_str(), nullptr, nullptr) < 0)
			{
				err = "can't open the file";
				return false;
			}
			struct Closer { AVFormatContext*& f; ~Closer() { avformat_close_input(&f); } } closer{f};
			if (index < 0 || index >= (int)f->nb_streams || f->streams[index]->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
			{
				err = "no such subtitle track";
				return false;
			}
			AVStream* st = f->streams[index];
			std::string header = st->codecpar->extradata ? std::string((const char*)st->codecpar->extradata, st->codecpar->extradata_size) : "";
			while (!header.empty() && header.back() == '\0') header.pop_back();
			if (header.find("[Script Info]") == std::string::npos) header = "[Script Info]\nScriptType: v4.00+\n\n" + header;
			if (header.find("[Events]") == std::string::npos)
				header += "\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
			if (header.back() != '\n') header += '\n';
			for (unsigned i = 0; i < f->nb_streams; i++)
				if ((int)i != index) f->streams[i]->discard = AVDISCARD_ALL;
			struct stat sb;
			double total = stat(path.c_str(), &sb) == 0 && sb.st_size > 0 ? (double)sb.st_size : 0;
			struct Line { long long order; double start; std::string text; };
			std::vector<Line> lines;
			AVPacket* pkt = av_packet_alloc();
			int lastPct = -1;
			while (av_read_frame(f, pkt) >= 0)
			{
				if (total > 0)
				{
					int pct = (int)(100.0 * avio_tell(f->pb) / total);
					if (pct != lastPct)
					{
						lastPct = pct;
						progress(std::min(1.0, pct / 100.0));
					}
				}
				if (pkt->stream_index == index && pkt->size > 0 && pkt->pts != AV_NOPTS_VALUE)
				{
					std::string d((const char*)pkt->data, pkt->size);
					while (!d.empty() && (d.back() == '\0' || d.back() == '\n' || d.back() == '\r')) d.pop_back();
					double start = pkt->pts * av_q2d(st->time_base), end = start + (pkt->duration > 0 ? pkt->duration * av_q2d(st->time_base) : 3);
					if (d.compare(0, 9, "Dialogue:") == 0) lines.push_back({(long long)lines.size(), start, d});
					else
					{
						// ReadOrder, then Layer, then the rest from Style on
						size_t c1 = d.find(','), c2 = c1 == std::string::npos ? c1 : d.find(',', c1 + 1);
						if (c2 != std::string::npos)
						{
							long long order = atoll(d.substr(0, c1).c_str());
							lines.push_back({order, start, "Dialogue: " + d.substr(c1 + 1, c2 - c1 - 1) + "," + assTime(start) + "," + assTime(end) + "," + d.substr(c2 + 1)});
						}
					}
				}
				av_packet_unref(pkt);
			}
			av_packet_free(&pkt);
			// the file's order (ReadOrder) is libass's order for lines that overlap
			std::stable_sort(lines.begin(), lines.end(), [](const Line& a, const Line& b) { return a.order < b.order; });
			out = header;
			for (auto& l : lines) out += l.text + "\n";
			return true;
		}

		// ---- the worker: one extraction at a time -----------------------------------------------------------------
		struct Job
		{
			std::string path, cacheFile;
			int index = 0;
			bool ass = false;	// the whole ASS script, not WebVTT
			double progress = 0;
			bool done = false, failed = false;
			std::string error;
		};
		struct Worker
		{
			std::mutex m;
			std::condition_variable cv;
			std::map<std::string, Job> jobs;	// by key
			std::deque<std::string> queue;
			bool running = false;
		};
		Worker& worker()
		{
			static Worker* w = new Worker;	// never destroyed: a job may still run at exit
			return *w;
		}

		void runWorker()
		{
			Worker& w = worker();
			while (true)
			{
				std::string key;
				Job job;
				{
					std::lock_guard<std::mutex> g(w.m);
					if (w.queue.empty())
					{
						w.running = false;
						return;
					}
					key = w.queue.front();
					w.queue.pop_front();
					job = w.jobs[key];
				}
				std::string vtt, err;
				auto prog = [&](double p) {
					std::lock_guard<std::mutex> g(w.m);
					w.jobs[key].progress = p;
				};
				bool ok = job.ass ? extractAss(job.path, job.index, vtt, err, prog) : extract(job.path, job.index, vtt, err, prog);
				if (ok)
				{
					std::string dir = dirName(job.cacheFile);
					mkdirs(dir);
					std::string tmp = job.cacheFile + ".part";
					FILE* f = fopen(tmp.c_str(), "wb");
					if (f && fwrite(vtt.data(), 1, vtt.size(), f) == vtt.size())
					{
						fclose(f);
						rename(tmp.c_str(), job.cacheFile.c_str());
						trimCache(dir);
					}
					else
					{
						if (f) fclose(f);
						unlink(tmp.c_str());
						ok = false;
						err = "can't write the cache";
					}
				}
				std::lock_guard<std::mutex> g(w.m);
				Job& j = w.jobs[key];
				j.done = ok;
				j.failed = !ok;
				j.error = err;
				j.progress = 1;
			}
		}

		std::string cueTypeOf(const std::string&) { return "text/vtt; charset=utf-8"; }
	}

	void setSubsCacheDir(const std::string& dir)
	{
		std::lock_guard<std::mutex> g(dirLock());
		cacheDirStore() = dir;
	}

	std::vector<AudioEntry> listAudio(const MediaInfo& m)
	{
		std::vector<AudioEntry> out;
		bool anyDefault = false;
		for (auto& a : m.audio) anyDefault = anyDefault || a.isDefault;
		int n = 0;
		for (auto& a : m.audio)
		{
			Track t;
			t.type = TrackType::Audio;
			t.lang = a.lang;
			t.title = a.title;
			t.codec = a.codec;
			t.channels = a.channels;
			t.forced = a.forced;
			AudioEntry e;
			e.i = n;
			e.label = trackLabel(t, ++n);
			e.isDefault = anyDefault ? a.isDefault : e.i == 0;
			e.codec = a.codec;
			e.lang = a.lang;
			e.channels = a.channels;
			e.stream = a.index;
			out.push_back(e);
		}
		return out;
	}

	std::vector<SubEntry> listSubtitles(const std::string& real, const MediaInfo& m)
	{
		std::vector<SubEntry> out;
		int n = 0;
		for (auto& s : m.subs)
		{
			Track t;
			t.type = TrackType::Sub;
			t.lang = s.lang;
			t.title = s.title;
			t.codec = s.codec;
			t.forced = s.forced;
			SubEntry e;
			e.i = (int)out.size();
			e.label = trackLabel(t, ++n);
			e.lang = s.lang;
			e.codec = s.codec;
			e.kind = textCodec(s.codec) ? "text" : pictureCodec(s.codec) ? "picture" : "image";
			e.styled = s.codec == "ass" || s.codec == "ssa";
			e.isDefault = s.isDefault;
			e.forced = s.forced;
			e.stream = s.index;
			out.push_back(e);
		}
		std::vector<std::string> names;
		if (DIR* d = opendir(dirName(real).c_str()))
		{
			while (dirent* e = readdir(d))
				if (e->d_name[0] != '.') names.push_back(e->d_name);
			closedir(d);
		}
		std::sort(names.begin(), names.end());
		std::vector<std::string> side = subtitleFilesFor(baseName(real), names);
		for (auto& name : side)
		{
			size_t dot = name.rfind('.');
			std::string ext = dot == std::string::npos ? "" : lower(name.substr(dot + 1)), stem = dot == std::string::npos ? name : name.substr(0, dot);
			if (ext == "sub" && std::find(side.begin(), side.end(), stem + ".idx") != side.end()) continue;	// the .idx shows both
			bool text = ext == "srt" || ext == "ass" || ext == "ssa" || ext == "vtt";
			Track t;
			t.type = TrackType::Sub;
			t.external = true;
			t.codec = ext == "srt" ? "subrip" : ext == "vtt" ? "webvtt" : text ? ext : ext == "sup" ? "hdmv_pgs_subtitle" : "dvd_subtitle";
			// "Movie.en.srt": a short last part of the name is the language
			size_t d2 = stem.rfind('.');
			std::string tok = d2 == std::string::npos ? "" : lower(stem.substr(d2 + 1));
			if (!tok.empty() && tok.size() <= 3 && languageName(tok) != tok) t.lang = tok;
			SubEntry e;
			e.i = (int)out.size();
			e.label = trackLabel(t, ++n);
			e.lang = t.lang;
			e.codec = t.codec;
			e.kind = text ? "text" : ext == "idx" || ext == "sup" ? "picture" : "image";
			e.styled = ext == "ass" || ext == "ssa";
			e.external = true;
			e.file = dirName(real) + "/" + name;
			out.push_back(e);
		}
		return out;
	}

	namespace
	{
		// an embedded track out of the cache, else queued for the worker (202 with its progress meanwhile)
		SubsAnswer embedded(const std::string& real, const SubEntry& sub, bool ass)
		{
			SubsAnswer a;
			a.type = "application/json; charset=utf-8";
			auto failWith = [&](int status, const std::string& msg) {
				a.status = status;
				JsonWriter w;
				w.obj().key("error").str(msg).end();
				a.body = w.out();
				return a;
			};
			const std::string ext = ass ? ".ass" : ".vtt", type = ass ? "text/plain; charset=utf-8" : cueTypeOf("");
			struct stat st;
			if (stat(real.c_str(), &st) != 0) return failWith(404, "not found");
			std::string key = real + '\n' + std::to_string((long long)st.st_mtime) + '\n' + std::to_string((long long)st.st_size) + '\n' + std::to_string(sub.stream) + ext;
			std::string cacheFile = cacheDir() + "/subs/" + hashOf(key) + ext;
			std::string cached;
			if (readFile(cacheFile, cached, (size_t)64 << 20))
			{
				utimes(cacheFile.c_str(), nullptr);	// most recent for the LRU
				a.status = 200;
				a.body = cached;
				a.type = type;
				return a;
			}

			Worker& w = worker();
			std::lock_guard<std::mutex> g(w.m);
			auto it = w.jobs.find(key);
			if (it != w.jobs.end() && it->second.failed)
			{
				std::string err = it->second.error;
				w.jobs.erase(it);	// the next request tries again
				return failWith(500, err.empty() ? "couldn't read the subtitles" : err);
			}
			if (it != w.jobs.end() && it->second.done)
			{
				// finished but the cache file isn't there (trimmed since): run it again
				w.jobs.erase(it);
				it = w.jobs.end();
			}
			if (it == w.jobs.end())
			{
				Job j;
				j.path = real;
				j.index = sub.stream;
				j.ass = ass;
				j.cacheFile = cacheFile;
				w.jobs[key] = j;
				w.queue.push_back(key);
				if (!w.running)
				{
					w.running = true;
					std::thread(runWorker).detach();
				}
				it = w.jobs.find(key);
			}
			JsonWriter jw;
			jw.obj().key("progress").num(it->second.progress).end();
			a.status = 202;
			a.body = jw.out();
			return a;
		}
	}

	SubsAnswer subtitleVtt(const std::string& real, const SubEntry& sub)
	{
		SubsAnswer a;
		a.type = "application/json; charset=utf-8";
		if (sub.kind != "text")
		{
			a.status = 415;
			JsonWriter w;
			w.obj().key("error").str("Picture subtitles show on the handheld only").key("code").str("type").end();
			a.body = w.out();
			return a;
		}
		auto failWith = [&](int status, const std::string& msg) {
			a.status = status;
			JsonWriter w;
			w.obj().key("error").str(msg).end();
			a.body = w.out();
			return a;
		};
		if (sub.external)
		{
			std::string text;
			if (!readFile(sub.file, text, (size_t)8 << 20)) return failWith(404, "can't read the subtitle file");
			size_t dot = sub.file.rfind('.');
			std::string vtt = subtitleFileToVtt(dot == std::string::npos ? "" : sub.file.substr(dot + 1), text, sub.lang);
			if (vtt.empty()) return failWith(422, "not a subtitle file");
			a.status = 200;
			a.body = vtt;
			a.type = cueTypeOf("");
			return a;
		}

		return embedded(real, sub, false);
	}

	SubsAnswer subtitleAss(const std::string& real, const SubEntry& sub)
	{
		SubsAnswer a;
		a.type = "application/json; charset=utf-8";
		if (!sub.styled)
		{
			a.status = 415;
			a.body = JsonWriter().obj().key("error").str("Not an ASS track").end().out();
			return a;
		}
		if (sub.external)
		{
			std::string bytes;
			if (!readFile(sub.file, bytes, (size_t)16 << 20))
			{
				a.status = 404;
				a.body = JsonWriter().obj().key("error").str("can't read the subtitle file").end().out();
				return a;
			}
			a.body = subtitleTextToUtf8(bytes, sub.lang);	// (an old code page's file: libass in the browser reads UTF-8)
			a.status = 200;
			a.type = "text/plain; charset=utf-8";
			return a;
		}
		return embedded(real, sub, true);
	}

	namespace
	{
		bool isFont(AVStream* s)
		{
			if (s->codecpar->codec_type != AVMEDIA_TYPE_ATTACHMENT || !s->codecpar->extradata_size) return false;
			std::string mime = lower(s->metadata ? (av_dict_get(s->metadata, "mimetype", nullptr, 0) ? av_dict_get(s->metadata, "mimetype", nullptr, 0)->value : "") : "");
			std::string name = lower(s->metadata && av_dict_get(s->metadata, "filename", nullptr, 0) ? av_dict_get(s->metadata, "filename", nullptr, 0)->value : "");
			auto ends = [&](const char* e) { size_t n = strlen(e); return name.size() >= n && name.compare(name.size() - n, n, e) == 0; };
			return mime.find("font") != std::string::npos || mime.find("truetype") != std::string::npos || mime.find("opentype") != std::string::npos ||
				ends(".ttf") || ends(".otf") || ends(".ttc") || ends(".woff") || ends(".woff2");
		}

		// attachments are in Matroska's header: opening is enough
		AVFormatContext* openHeader(const std::string& real)
		{
			AVFormatContext* f = nullptr;
			AVDictionary* o = nullptr;
			av_dict_set(&o, "probesize", "1048576", 0);
			int r = avformat_open_input(&f, real.c_str(), nullptr, &o);
			av_dict_free(&o);
			return r < 0 ? nullptr : f;
		}
	}

	std::vector<FontAttachment> listFonts(const std::string& real)
	{
		std::vector<FontAttachment> out;
		AVFormatContext* f = openHeader(real);
		if (!f) return out;
		for (unsigned i = 0; i < f->nb_streams; i++)
		{
			AVStream* s = f->streams[i];
			if (!isFont(s)) continue;
			FontAttachment a;
			a.i = (int)i;
			AVDictionaryEntry* n = av_dict_get(s->metadata, "filename", nullptr, 0);
			AVDictionaryEntry* m = av_dict_get(s->metadata, "mimetype", nullptr, 0);
			a.name = n && n->value ? n->value : "";
			a.mime = m && m->value ? m->value : "";
			a.size = (size_t)s->codecpar->extradata_size;
			out.push_back(a);
		}
		avformat_close_input(&f);
		return out;
	}

	bool fontAttachment(const std::string& real, int i, std::string& bytes, std::string& mime)
	{
		AVFormatContext* f = openHeader(real);
		if (!f) return false;
		bool ok = i >= 0 && i < (int)f->nb_streams && isFont(f->streams[i]);
		if (ok)
		{
			AVStream* s = f->streams[i];
			bytes.assign((const char*)s->codecpar->extradata, s->codecpar->extradata_size);
			// a font's own signature decides the type (Matroska's mimetypes are all over the place)
			mime = bytes.compare(0, 4, "OTTO") == 0 ? "font/otf" : bytes.compare(0, 4, "ttcf") == 0 ? "font/collection"
				: bytes.compare(0, 4, "wOFF") == 0 ? "font/woff" : bytes.compare(0, 4, "wOF2") == 0 ? "font/woff2" : "font/ttf";
		}
		avformat_close_input(&f);
		return ok;
	}
}
