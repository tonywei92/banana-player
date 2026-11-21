#include "tracks.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace bp
{
	static const mpv_node* field(const mpv_node* map, const char* key)
	{
		if (!map || map->format != MPV_FORMAT_NODE_MAP) return nullptr;
		for (int i = 0; i < map->u.list->num; i++)
			if (!strcmp(map->u.list->keys[i], key)) return &map->u.list->values[i];
		return nullptr;
	}

	static std::string str(const mpv_node* map, const char* key)
	{
		const mpv_node* n = field(map, key);
		return n && n->format == MPV_FORMAT_STRING ? n->u.string : "";
	}

	static int64_t num(const mpv_node* map, const char* key)
	{
		const mpv_node* n = field(map, key);
		if (!n) return 0;
		if (n->format == MPV_FORMAT_INT64) return n->u.int64;
		if (n->format == MPV_FORMAT_DOUBLE) return (int64_t)n->u.double_;
		return 0;
	}

	static bool flag(const mpv_node* map, const char* key)
	{
		const mpv_node* n = field(map, key);
		return n && n->format == MPV_FORMAT_FLAG && n->u.flag;
	}

	std::vector<Track> parseTrackList(const mpv_node* list)
	{
		std::vector<Track> out;
		if (!list || list->format != MPV_FORMAT_NODE_ARRAY) return out;
		for (int i = 0; i < list->u.list->num; i++)
		{
			const mpv_node* m = &list->u.list->values[i];
			Track t;
			t.id = num(m, "id");
			std::string type = str(m, "type");
			t.type = type == "video" ? TrackType::Video : type == "audio" ? TrackType::Audio : type == "sub" ? TrackType::Sub : TrackType::Other;
			t.title = str(m, "title");
			t.lang = str(m, "lang");
			t.codec = str(m, "codec");
			t.channels = (int)num(m, "demux-channel-count");
			t.w = (int)num(m, "demux-w");
			t.h = (int)num(m, "demux-h");
			t.selected = flag(m, "selected");
			t.isDefault = flag(m, "default");
			t.forced = flag(m, "forced");
			t.external = flag(m, "external");
			t.albumArt = flag(m, "albumart");
			out.push_back(t);
		}
		return out;
	}

	std::vector<Chapter> parseChapterList(const mpv_node* list)
	{
		std::vector<Chapter> out;
		if (!list || list->format != MPV_FORMAT_NODE_ARRAY) return out;
		for (int i = 0; i < list->u.list->num; i++)
		{
			const mpv_node* m = &list->u.list->values[i];
			Chapter c;
			const mpv_node* t = field(m, "time");
			if (t && t->format == MPV_FORMAT_DOUBLE) c.time = t->u.double_;
			else if (t && t->format == MPV_FORMAT_INT64) c.time = (double)t->u.int64;
			c.title = str(m, "title");
			out.push_back(c);
		}
		return out;
	}

	bool chapterUntitled(const std::string& title)
	{
		// mpv's own stand-in for a Matroska chapter with no name
		if (title == "(unnamed)") return true;
		bool any = false, digits = false, colon = false;
		for (unsigned char ch : title)
		{
			if (isspace(ch)) continue;
			any = true;
			if (isdigit(ch)) digits = true;
			else if (ch == ':') colon = true;
			else if (ch != '.') return false;
		}
		// blank; or digits, colons and dots with a colon in them: a timestamp ("1.5" is a title)
		return !any || (digits && colon);
	}

	std::string chapterLabel(const Chapter& c, int n)
	{
		return chapterUntitled(c.title) ? "Chapter " + std::to_string(n) : c.title;
	}

	int chapterTarget(const std::vector<Chapter>& chapters, int current, double position, int dir)
	{
		int n = (int)chapters.size();
		if (n == 0) return CHAPTER_NONE;
		if (dir > 0) return current + 1 < n ? current + 1 : CHAPTER_NONE;
		if (current >= n) current = n - 1;
		if (current >= 0 && position - chapters[current].time > CHAPTER_BACK_S) return current;
		if (current > 0) return current - 1;
		return chapters[0].time > 0.5 ? CHAPTER_FILE_START : CHAPTER_NONE;
	}

	std::string languageName(const std::string& code)
	{
		static const struct { const char* codes; const char* name; } names[] = {
			{"en eng", "English"}, {"zh chi zho", "Chinese"}, {"ru rus", "Russian"}, {"it ita", "Italian"},
			{"fi fin", "Finnish"}, {"ja jpn", "Japanese"}, {"ko kor", "Korean"}, {"es spa", "Spanish"},
			{"fr fre fra", "French"}, {"de ger deu", "German"}, {"pt por", "Portuguese"}, {"nl dut nld", "Dutch"},
			{"sv swe", "Swedish"}, {"no nor nob", "Norwegian"}, {"da dan", "Danish"}, {"pl pol", "Polish"},
			{"tr tur", "Turkish"}, {"ar ara", "Arabic"}, {"he heb", "Hebrew"}, {"hi hin", "Hindi"},
			{"id ind", "Indonesian"}, {"ms may msa", "Malay"}, {"th tha", "Thai"}, {"vi vie", "Vietnamese"},
			{"uk ukr", "Ukrainian"}, {"cs cze ces", "Czech"}, {"hu hun", "Hungarian"}, {"el gre ell", "Greek"},
			{"ro rum ron", "Romanian"},
		};
		std::string c = code;
		std::transform(c.begin(), c.end(), c.begin(), [](unsigned char ch) { return std::tolower(ch); });
		if (c.empty() || c == "und") return "";
		for (auto& n : names)
		{
			std::string list = std::string(" ") + n.codes + " ";
			if (list.find(" " + c + " ") != std::string::npos) return n.name;
		}
		return code;
	}

	std::string codecName(const std::string& codec)
	{
		static const struct { const char* codec; const char* name; } names[] = {
			{"subrip", "SRT"}, {"ass", "ASS"}, {"ssa", "SSA"}, {"webvtt", "VTT"}, {"mov_text", "Text"},
			{"hdmv_pgs_subtitle", "PGS"}, {"dvd_subtitle", "VobSub"}, {"dvb_subtitle", "DVB"},
			{"eac3", "E-AC3"}, {"truehd", "TrueHD"}, {"dts", "DTS"}, {"opus", "Opus"}, {"vorbis", "Vorbis"},
			{"flac", "FLAC"}, {"mp3", "MP3"}, {"mp3float", "MP3"}, {"aac", "AAC"}, {"ac3", "AC3"},
		};
		for (auto& n : names)
			if (codec == n.codec) return n.name;
		if (codec.rfind("pcm_", 0) == 0) return "PCM";
		std::string up = codec;
		std::transform(up.begin(), up.end(), up.begin(), [](unsigned char ch) { return std::toupper(ch); });
		return up;
	}

	static std::string channelName(int n)
	{
		switch (n)
		{
		case 0: return "";
		case 1: return "Mono";
		case 2: return "Stereo";
		case 6: return "5.1";
		case 8: return "7.1";
		default: return std::to_string(n) + " ch";
		}
	}

	std::string trackLabel(const Track& t, int n)
	{
		std::vector<std::string> parts;
		std::string lang = languageName(t.lang);
		parts.push_back(lang.empty() ? "Track " + std::to_string(n) : lang);
		std::string tech = t.codec.empty() ? "" : codecName(t.codec);
		if (t.type == TrackType::Audio)
		{
			std::string ch = channelName(t.channels);
			if (!ch.empty()) tech += (tech.empty() ? "" : " ") + ch;
		}
		if (!tech.empty()) parts.push_back(tech);
		// a title that only repeats the language says nothing
		if (!t.title.empty() && t.title != lang && t.title != t.lang) parts.push_back(t.title);
		if (t.forced) parts.push_back("Forced");
		if (t.external) parts.push_back("File");
		std::string out;
		for (auto& p : parts) out += (out.empty() ? "" : " · ") + p;
		return out;
	}
}
