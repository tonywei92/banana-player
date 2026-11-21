#include "videocache.h"
#include "format.h"
#include "tracks.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace bp
{
	namespace
	{
		const char* const VERSION = "bp-video-meta 1";

		std::string clean(std::string s)
		{
			for (char& c : s)
				if (c == '\t' || c == '\n' || c == '\r') c = ' ';
			return s;
		}

		std::vector<std::string> fields(const std::string& line)
		{
			std::vector<std::string> out;
			size_t i = 0;
			for (;;)
			{
				size_t t = line.find('\t', i);
				out.push_back(line.substr(i, t == std::string::npos ? std::string::npos : t - i));
				if (t == std::string::npos) break;
				i = t + 1;
			}
			return out;
		}

		// a video codec's usual name ("h264" -> "H.264"); the rest in capitals
		std::string videoCodecName(const std::string& c)
		{
			static const struct { const char* codec; const char* name; } names[] = {
				{"h264", "H.264"}, {"hevc", "HEVC"}, {"h265", "HEVC"}, {"av1", "AV1"}, {"vp9", "VP9"}, {"vp8", "VP8"},
				{"mpeg4", "MPEG-4"}, {"mpeg2video", "MPEG-2"}, {"mpeg1video", "MPEG-1"}, {"msmpeg4v3", "DivX 3"},
				{"wmv3", "WMV"}, {"vc1", "VC-1"}, {"theora", "Theora"}, {"mjpeg", "MJPEG"}, {"prores", "ProRes"},
			};
			for (auto& n : names)
				if (c == n.codec) return n.name;
			return codecName(c);
		}

		std::string channelWords(int n)
		{
			switch (n)
			{
			case 1: return "Mono";
			case 2: return "";	// (stereo is what a film's track is unless it says otherwise)
			case 6: return "5.1";
			case 8: return "7.1";
			default: return n > 0 ? std::to_string(n) + " ch" : "";
			}
		}

		std::string streamName(const VideoMeta::Stream& s, int n)
		{
			std::string lang = languageName(s.lang);
			if (!lang.empty()) return lang;
			if (!s.title.empty()) return s.title;
			return "Track " + std::to_string(n);
		}
	}

	std::string videoCacheName(const std::string& path, long long size, int64_t mtime)
	{
		std::string key = path + "|" + std::to_string(size) + "|" + std::to_string(mtime);
		uint64_t h = 1469598103934665603ULL;
		for (unsigned char c : key)
		{
			h ^= c;
			h *= 1099511628211ULL;
		}
		char buf[17];
		snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
		return buf;
	}

	VideoCacheFiles videoCacheFiles(const std::string& dir, const std::string& name)
	{
		std::string base = dir + "/" + name;
		return {base + ".meta", base + ".s.jpg", base + ".l.jpg"};
	}

	std::string videoMetaText(const VideoMeta& m)
	{
		std::ostringstream o;
		char d[32];
		snprintf(d, sizeof d, "%.3f", m.duration);
		o << VERSION << "\n";
		o << "duration\t" << d << "\n";
		o << "size\t" << m.width << "\t" << m.height << "\n";
		o << "vcodec\t" << clean(m.vcodec) << "\n";
		o << "chapters\t" << m.chapters << "\n";
		o << "from\t" << (m.fromPlayer ? "player" : "probe") << "\n";
		for (auto& s : m.audio)
			o << "a\t" << clean(s.lang) << "\t" << clean(s.codec) << "\t" << s.channels << "\t" << clean(s.title) << "\n";
		for (auto& s : m.subs)
			o << "s\t" << clean(s.lang) << "\t" << clean(s.codec) << "\t" << s.channels << "\t" << clean(s.title) << "\n";
		return o.str();
	}

	bool parseVideoMeta(const std::string& text, VideoMeta& out)
	{
		out = VideoMeta{};
		std::istringstream in(text);
		std::string line;
		if (!std::getline(in, line) || line != VERSION) return false;
		while (std::getline(in, line))
		{
			std::vector<std::string> f = fields(line);
			const std::string& k = f[0];
			if (k == "duration" && f.size() >= 2) out.duration = std::max(0.0, atof(f[1].c_str()));
			else if (k == "size" && f.size() >= 3)
			{
				out.width = std::max(0, atoi(f[1].c_str()));
				out.height = std::max(0, atoi(f[2].c_str()));
			}
			else if (k == "vcodec" && f.size() >= 2) out.vcodec = f[1];
			else if (k == "chapters" && f.size() >= 2) out.chapters = std::max(0, atoi(f[1].c_str()));
			else if (k == "from" && f.size() >= 2) out.fromPlayer = f[1] == "player";
			else if ((k == "a" || k == "s") && f.size() >= 5)
			{
				VideoMeta::Stream s{f[1], f[2], f[4], std::max(0, atoi(f[3].c_str()))};
				(k == "a" ? out.audio : out.subs).push_back(s);
			}
			// (an unknown line: a later version's; skipped)
		}
		return true;
	}

	VideoMeta videoMetaFromTracks(const std::vector<Track>& tracks, double duration, int chapters)
	{
		VideoMeta m;
		m.fromPlayer = true;
		m.duration = duration > 0 ? duration : 0;
		m.chapters = std::max(0, chapters);
		for (const Track& t : tracks)
		{
			if (t.type == TrackType::Video && !t.albumArt && m.vcodec.empty())
			{
				m.vcodec = t.codec;
				m.width = t.w;
				m.height = t.h;
			}
			else if (t.type == TrackType::Audio || t.type == TrackType::Sub)
			{
				VideoMeta::Stream s{t.lang, t.codec, t.title, t.channels};
				(t.type == TrackType::Audio ? m.audio : m.subs).push_back(s);
			}
		}
		return m;
	}

	std::string videoPictureLabel(const VideoMeta& m)
	{
		std::string codec = m.vcodec.empty() ? "" : videoCodecName(m.vcodec);
		std::string size = m.width > 0 && m.height > 0
			? std::to_string(m.width) + " \xC3\x97 " + std::to_string(m.height) : "";
		if (codec.empty() || size.empty()) return codec + size;
		return codec + " \xC2\xB7 " + size;
	}

	std::string videoAudioLabel(const VideoMeta& m)
	{
		// the first two tracks named, the rest counted
		std::string out;
		for (size_t i = 0; i < m.audio.size() && i < 2; i++)
		{
			std::string ch = channelWords(m.audio[i].channels);
			out += (i ? ", " : "") + streamName(m.audio[i], (int)i + 1) + (ch.empty() ? "" : " " + ch);
		}
		if (m.audio.size() > 2) out += " + " + std::to_string(m.audio.size() - 2);
		return out;
	}

	std::string videoSubsLabel(const VideoMeta& m)
	{
		// the languages once each, the first two named
		std::vector<std::string> names;
		for (size_t i = 0; i < m.subs.size(); i++)
		{
			std::string n = streamName(m.subs[i], (int)i + 1);
			if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
		}
		std::string out;
		for (size_t i = 0; i < names.size() && i < 2; i++) out += (i ? ", " : "") + names[i];
		if (names.size() > 2) out += " + " + std::to_string(names.size() - 2);
		return out;
	}

	std::string videoLengthLabel(const VideoMeta& m)
	{
		if (m.duration <= 0) return "";
		std::string out = formatTime(m.duration);
		if (m.chapters > 1) out += " \xC2\xB7 " + std::to_string(m.chapters) + " chapters";
		return out;
	}
}
