// The playback state the web dashboard shows, as one snapshot (bpcore, no window).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bp
{
	struct WebChapter { std::string title; double start = 0; };

	// an audio or subtitle track of the file playing (mpv's id within its type; the label as the handheld's list has it)
	struct WebTrack
	{
		int64_t id = 0;
		std::string label;
		bool selected = false, isDefault = false, forced = false, external = false;
		std::string lang;	// its language code ("" none): the Session's picks match by it
		bool operator==(const WebTrack& o) const
		{
			return id == o.id && label == o.label && lang == o.lang && selected == o.selected && isDefault == o.isDefault && forced == o.forced &&
				external == o.external;
		}
		bool operator!=(const WebTrack& o) const { return !(*this == o); }
	};

	struct WebSnapshot
	{
		std::string path, title, artist, album;	// path: virtual ("" when nothing plays); a network file's bpnet:// path
		std::string netServer;	// a network file's: its connection's name (the page says it plays on the handheld via it)
		std::string kind;	// "video", "audio", "photo" or "" for none
		std::string screen;	// the handheld's bottom screen: "player", or "browser" (it plays behind it); "" none
		std::string sourceKind, sourceTitle, sourceId;	// "folder", "playlist", "selection", "" ; id: the playlist's id
		int index = 0, count = 0;	// the file's place in the source (0-based; count 0 = none)
		bool loading = false, paused = false;
		double position = 0;	// seconds, as of positionAtMs
		int64_t positionAtMs = 0;	// the server's clock (ms) when position was read
		double duration = 0, speed = 1;
		int volume = 0;	// percent (past 100: the player's gain over the handheld's 100%)
		int volumeMax = 100;	// Max volume: how far the page's bar goes
		bool eqOn = false;
		std::string eqPreset;
		std::array<double, 10> eqGains{};	// dB
		std::string fx;	// the sound effect's key
		std::string loop;	// "no", "all", "one"
		bool shuffled = false;
		bool shuffleMode = false;	// the handheld's Shuffle setting (every list started plays shuffled)
		std::vector<WebChapter> chapters;
		int chapter = -1;	// the current one's index, -1 none
		bool hasTracks = false;	// a video (tracks shown); else "tracks" is null
		std::vector<WebTrack> audioTracks, subTracks;
		int64_t aid = 0, sid = 0;	// the chosen ones' ids, 0 none (subtitles off)
		uint64_t sessionBoot = 0, sessionV = 0;	// the Session's (boot, v)
		std::string session;	// the Session as sessionJson (session.h) wrote it; "" : {boot, v} only
		int clients = 0;
		int playlistSerial = 0;
		bool liked = false;	// what plays is in the Liked playlist
		bool showHiddenFolders = false;	// the setting: folders with a ".hidden" file are listed
		bool mangaOn = false;	// the setting: the manga reader is on (the dashboard's Manage extensions card)
		bool incognito = false;	// incognito mode (the handheld's Quick settings): the page shows its mark, can't switch it
	};

	// The JSON a page gets in `event: state`. Keys: path, title, artist, album, kind, source {kind, title, id, index,
	// count}, screen, loading, paused, position, positionAt, duration, speed, volume, eq {on, preset, gains[10]}, fx, loop,
	// shuffled, chapters [{title, start}], chapter, tracks (null, or {audio: [{id, label, selected, default, forced,
	// external, lang}], sub: [the same], aid, sid}), clients, session (sessionJson's), playlistSerial, liked, showHiddenFolders,
	// mangaOn.
	std::string toJson(const WebSnapshot&);
	// true when anything but position/positionAtMs differs, or b's position is more than 1.5 s from where a's
	// would be by now (a's position plus the time between them times its speed, unless a is paused)
	bool snapshotChanged(const WebSnapshot& a, const WebSnapshot& b);

	// A file's title for the web when its tags have none: the file name (a bpnet:// one's too) without its extension,
	// and for a song without a leading track number ("01 ", "01. ", "1 - ", "07_"): "01 - Sunny Peel.mp3" is
	// "Sunny Peel". A bare number before a space goes only when it has two or three digits ("2 Fast" stays), and a
	// name that is only a number stays as it is
	std::string webTitle(const std::string& path);
	// the title of the file playing: its tag (mpv's media-title, when that isn't the file's own name), else webTitle
	std::string webTitle(const std::string& path, const std::string& mediaTitle);
}
