#include "player.h"
#include "durations.h"
#include "cardcanvas.h"
#include "filesave.h"
#include "format.h"
#include "levelcard.h"
#include "webmedia.h"
#include "logo.h"
#include "pagedecode.h"
#include "netpath.h"
#include "netstore.h"
#include "netstream.h"
#include "sidecar.h"
#include "songlayout.h"
#include "artph.h"
#include "kitlayout.h"
#include "soundfx.h"
#include "playlist.h"
#include "theme.h"
#include "ui_glyphs.h"
#include "vizcache.h"
#include "web/probe.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace bp
{
	enum Observed : uint64_t { TIME_POS = 1, DURATION, PAUSE, TRACK_LIST, MEDIA_TITLE, AID, SID, SPEED, METADATA, PAUSED_FOR_CACHE, SEEKING, CHAPTER_LIST, CHAPTER,
		AUDIO_IN, AUDIO_OUT, CURRENT_AO, AF_CHAIN, LAVFI_GRAPH, EOF_REACHED, CORE_IDLE, IDLE_ACTIVE, AUDIO_PTS };

	// the audio diagnostics (log.txt, "audio:" lines): an mpv node as "key=value key=value" (a map) or its value
	static std::string nodeText(const mpv_node* n)
	{
		if (!n) return "-";
		switch (n->format)
		{
		case MPV_FORMAT_STRING: return n->u.string ? n->u.string : "";
		case MPV_FORMAT_INT64: return std::to_string((long long)n->u.int64);
		case MPV_FORMAT_DOUBLE: { char b[32]; snprintf(b, sizeof b, "%g", n->u.double_); return b; }
		case MPV_FORMAT_FLAG: return n->u.flag ? "yes" : "no";
		case MPV_FORMAT_NODE_MAP:
		{
			std::string out;
			for (int i = 0; i < n->u.list->num; i++)
				out += (out.empty() ? "" : " ") + std::string(n->u.list->keys[i]) + "=" + nodeText(&n->u.list->values[i]);
			return out;
		}
		case MPV_FORMAT_NODE_ARRAY:
		{
			std::string out;
			for (int i = 0; i < n->u.list->num; i++) out += (out.empty() ? "" : ", ") + nodeText(&n->u.list->values[i]);
			return "[" + out + "]";
		}
		default: return "-";
		}
	}
	// screenshot-raw's answers for the photo's thumbnail: this plus the load they were asked in
	static const uint64_t THUMB_REPLY = 1000;
	// af-command's answers (the equalizer's bands): far above THUMB_REPLY's, which grow with each file
	static const uint64_t EQ_REPLY = 1ull << 40;
	// loadfile's answers (the playlist entry it made), plus the load's serial
	static const uint64_t LOAD_REPLY = 1ull << 41;
	// the level card's overlay-add and overlay-remove answers, plus its serial (between EQ_REPLY and LOAD_REPLY)
	static const uint64_t LEVEL_REPLY = (1ull << 40) + (1ull << 32);
	// the song art's overlay-add answers, plus its serial (above the level card's, below LOAD_REPLY)
	static const uint64_t ART_REPLY = (1ull << 40) + (1ull << 39);
	// the answers of prop() for the properties in SET_WATCHED (a failed set is in the log), plus the name's index
	static const uint64_t SETP_REPLY = 1ull << 43;
	static const char* const SET_WATCHED[] = {"lavfi-complex", "aid", "vid", "af", "pause", "background-color",
		"video-margin-ratio-top", "video-margin-ratio-bottom", "video-margin-ratio-left", "video-margin-ratio-right", "panscan"};
	static const int SET_WATCHED_N = (int)(sizeof(SET_WATCHED) / sizeof(SET_WATCHED[0]));
	// (log.txt, for the freeze after a visualizer switch: grep "mpv: (set|graph|core-idle|..)|end-file|play pressed|music: graph")
	static const uint64_t WATCH_MS = 10000;
	static std::string shortHash(const std::string& v)
	{
		char b[24];
		snprintf(b, sizeof b, "%08x", (unsigned)(std::hash<std::string>()(v) & 0xffffffffu));
		return b;
	}
	// the panels' shape (both are 4:3)
	static const double PANEL_ASPECT = 4.0 / 3;
	// the thumbnail's largest size: the pan area's on the Plus's 1024x768 panel
	static const int THUMB_MAX_W = 560, THUMB_MAX_H = 420;

	using namespace theme;

	// an ASS drawing of a w x h box from 0 0, its corners quarter circles of radius r (cubic beziers)
	static std::string roundBox(int w, int h, int r)
	{
		r = std::max(0, std::min({r, w / 2, h / 2}));
		char d[256];
		if (!r)
		{
			snprintf(d, sizeof d, "m 0 0 l %d 0 %d %d 0 %d", w, w, h, h);
			return d;
		}
		int c = (int)std::lround(r * 0.4477);	// the control points' distance from the corner (1 - 0.5523 of r)
		snprintf(d, sizeof d,
			"m %d 0 l %d 0 b %d 0 %d %d %d %d l %d %d b %d %d %d %d %d %d l %d %d b %d %d %d %d %d %d l %d %d b %d %d %d %d %d %d",
			r, w - r, w - c, w, c, w, r, w, h - r, w, h - c, w - c, h, w - r, h, r, h, c, h, 0, h - c, 0, h - r, 0, r,
			0, c, c, 0, r, 0);
		return d;
	}

	static std::string baseName(const std::string& p)
	{
		size_t slash = p.rfind('/');
		return slash == std::string::npos ? p : p.substr(slash + 1);
	}

	static std::string readFile(const std::string& path)
	{
		std::ifstream in(path);
		std::stringstream text;
		text << in.rdbuf();
		return text.str();
	}

	bool Player::init(const PlayerOptions& o, void (*wakeup)(void*), void* ctx)
	{
		opts = o;
		mpv = mpv_create();
		if (!mpv) return false;
		auto opt = [&](const char* k, const char* v) { mpv_set_option_string(mpv, k, v); };
		opt("config", "no");
		opt("vo", "libmpv");
		// Software decoding on four Cortex-A55s (BananaOS's kernel has no V4L2: no hardware decoders to ask), so
		// everything cheap that keeps the picture right: mpv's fast profile (bilinear scaling, no dithering nor
		// high-quality downscaling: the panels are small), FFmpeg's fast decoding, the loop filter left out on
		// frames no other frame is built from (a flaw lasts one frame), and frames dropped in the decoder too when
		// it falls behind, so the sound and the picture keep together
		opt("hwdec", "no");
		opt("profile", "fast");
		opt("vd-lavc-threads", "0");		// one per core
		opt("vd-lavc-fast", "yes");
		opt("vd-lavc-skiploopfilter", "nonref");
		opt("framedrop", "decoder+vo");
		opt("video-sync", "audio");
		opt("audio-client-name", "banana-player");
		// stereo out, mixed by mpv: ALSA's plug (BananaOS has no sound server) puts a mono track on the left speaker
		// only, and takes a 5.1 track's first two channels (no centre: no dialogue)
		opt("audio-channels", "stereo");
		// the stream kept going through pauses and seeks (silence then): on the RG DS Plus each start of a stream starts
		// its speaker amplifiers again, and a start that fails leaves a speaker silent (ampwatch.h); fewer starts, fewer
		// failures. (A suspend stops it all the same)
		if (!o.photosOnly) opt("audio-stream-silence", "yes");
		opt("volume-max", "150");	// (the boost's ceiling: Max volume's highest)
		opt("input-default-bindings", "no");
		opt("input-vo-keyboard", "no");
		opt("osc", "no");
		// none of mpv's own scripts (they load even with config=no): each file waits on their hooks as it opens
		// (ytdl_hook, auto_profiles...), a stall at every Next. Options this mpv doesn't have are refused, harmlessly
		for (const char* o : {"ytdl", "load-scripts", "load-stats-overlay", "load-osd-console", "load-auto-profiles",
				 "load-select", "load-commands", "load-context-menu", "load-positioning"})
			opt(o, "no");
		opt("sub-auto", "fuzzy");			// movie.srt, movie.en.srt... next to movie.mkv
		// mpv's default, kept: "scale" is its "yes" plus sub-scale, so a styled subtitle (ASS/SSA) keeps the
		// script's own sizes and colours while a font it names that the card lacks falls back to the style's
		opt("sub-ass-override", "scale");
		if (!o.fontsDir.empty())
		{
			// the bundled fonts for the subtitles and mpv's own messages: BananaOS has no fontconfig, so "sans-serif"
			// would find nothing there
			opt("sub-fonts-dir", o.fontsDir.c_str());
			opt("osd-fonts-dir", o.fontsDir.c_str());
			opt("osd-font", "Inter");
			// tests: libass with no system fonts, as on BananaOS (BP_FONT_PROVIDER=none)
			if (const char* fp = getenv("BP_FONT_PROVIDER")) opt("sub-font-provider", fp);
		}
		opt("osd-font-size", "40");
		opt("idle", "yes");				// stay up between files and after the last: we decide when to leave
		opt("keep-open", "no");
		opt("resume-playback", "no");		// resume.tsv instead: each of position, audio and subtitles can be off
		// tests: what reaches the speakers, recorded (BP_AO_PCM=file: mpv's pcm output, untimed, as raw 44.1 kHz s16
		// stereo, appended so an output reopened keeps what came before)
		if (o.photosOnly)
		{
			// no sound device opened: the first mpv has ALSA's; an animated photo's sound (a clip as GIF) never mattered
			opt("ao", "null");
			opt("aid", "no");
		}
		else if (const char* pcm = getenv("BP_AO_PCM"))
		{
			opt("ao", "pcm");
			opt("ao-pcm-file", pcm);
			opt("ao-pcm-waveheader", "no");
			opt("ao-pcm-append", "yes");
			opt("audio-format", "s16");
			opt("audio-samplerate", "44100");
		}
		mpv_request_log_messages(mpv, o.verbose ? "v" : "warn");
		if (mpv_initialize(mpv) < 0) return false;

		if (!o.stateDir.empty())
		{
			// and the folders above it (/data/home/.config on a card that never ran a Store app)
			for (size_t i = 1; i <= o.stateDir.size(); i++)
				if (i == o.stateDir.size() || o.stateDir[i] == '/') mkdir(o.stateDir.substr(0, i).c_str(), 0755);
			settings = parseSettings(readFile(o.stateDir + "/settings.conf"));
			resume.parse(readFile(o.stateDir + "/resume.tsv"));
			if (settings.keepHistory) played.parse(readFile(o.stateDir + "/history.tsv"));
		}
		// (parsed even with no file: the list of built-ins is made there)
		eqStore.parse(o.stateDir.empty() ? "" : readFile(o.stateDir + "/eq-presets.tsv"));

		mpv_observe_property(mpv, TIME_POS, "time-pos", MPV_FORMAT_DOUBLE);
		mpv_observe_property(mpv, DURATION, "duration", MPV_FORMAT_DOUBLE);
		mpv_observe_property(mpv, PAUSE, "pause", MPV_FORMAT_FLAG);
		// a song kept open at its end (keep-open below) sends no END_FILE: this is where its end is heard
		mpv_observe_property(mpv, EOF_REACHED, "eof-reached", MPV_FORMAT_FLAG);
		// the track list and the tags come with their change (a read of them, as a file opens, waits for mpv while
		// mpv waits for this thread to draw); a track chosen changes the list too
		mpv_observe_property(mpv, TRACK_LIST, "track-list", MPV_FORMAT_NODE);
		mpv_observe_property(mpv, METADATA, "metadata", MPV_FORMAT_NODE);
		mpv_observe_property(mpv, MEDIA_TITLE, "media-title", MPV_FORMAT_STRING);
		mpv_observe_property(mpv, SPEED, "speed", MPV_FORMAT_DOUBLE);
		mpv_observe_property(mpv, PAUSED_FOR_CACHE, "paused-for-cache", MPV_FORMAT_FLAG);
		mpv_observe_property(mpv, SEEKING, "seeking", MPV_FORMAT_FLAG);
		mpv_observe_property(mpv, CHAPTER_LIST, "chapter-list", MPV_FORMAT_NODE);
		mpv_observe_property(mpv, CHAPTER, "chapter", MPV_FORMAT_INT64);
		// the audio as it goes in and out (log.txt's "audio:" lines: a sound on one side only is told by them)
		mpv_observe_property(mpv, AUDIO_IN, "audio-params", MPV_FORMAT_NODE);
		mpv_observe_property(mpv, AUDIO_OUT, "audio-out-params", MPV_FORMAT_NODE);
		mpv_observe_property(mpv, CURRENT_AO, "current-ao", MPV_FORMAT_STRING);
		mpv_observe_property(mpv, AF_CHAIN, "af", MPV_FORMAT_NODE);
		mpv_observe_property(mpv, LAVFI_GRAPH, "lavfi-complex", MPV_FORMAT_STRING);
		mpv_observe_property(mpv, CORE_IDLE, "core-idle", MPV_FORMAT_FLAG);
		mpv_observe_property(mpv, IDLE_ACTIVE, "idle-active", MPV_FORMAT_FLAG);
		// the sound heard (mpv's output delay already taken off), for the native visualizers' clock (syncNative):
		// time-pos follows a graph's picture (5 frames a second under the blank one)
		mpv_observe_property(mpv, AUDIO_PTS, "audio-pts", MPV_FORMAT_DOUBLE);
		if (o.net && !o.net->attach(mpv)) fprintf(stderr, "net: mpv has no stream callbacks: network files won't open\n");
		mpv_set_wakeup_callback(mpv, wakeup, ctx);
		setAspect(aspect);
		for (auto& p : subStyleProperties(settings.style)) prop(p.first.c_str(), p.second);
		setSubtitleCharset(subEncodingCharset(settings.style));	// (the web's subtitles read the same way)
		return true;
	}

	bool Player::initRender(void* (*getProc)(void*, const char*), void* procCtx, void (*update)(void*), void* ctx)
	{
		mpv_opengl_init_params gl{getProc, procCtx};
		mpv_render_param params[] = {
			{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
			{MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
			{MPV_RENDER_PARAM_INVALID, nullptr},
		};
		if (mpv_render_context_create(&rc, mpv, params) < 0) return false;
		mpv_render_context_set_update_callback(rc, update, ctx);
		// an audio file has no picture of its own: the window stays, for its cover, the visualizer and the title.
		// Set once the render context is there: mpv opens its window at once, and it needs the context
		prop("force-window", "yes");
		return true;
	}

	void Player::shutdown()
	{
		flushWrites(true);	// (the last of a burst of saves waits at most a second; not past the quit)
		// the render context goes first, while its GL context is still current
		if (rc) mpv_render_context_free(rc);
		rc = nullptr;
		if (mpv) mpv_terminate_destroy(mpv);
		mpv = nullptr;
	}

	// Settings and commands are sent without waiting for mpv's answer (it takes them in order): while it switches
	// files mpv waits for this thread to draw the top panel, and a call that waited for mpv meanwhile held both
	// till mpv gave up ("mpv_render_context_render() not being called or stuck"): seconds on the handheld at each
	// Next. Only a read (get) waits
	void Player::prop(const char* name, const std::string& value)
	{
		const char* v = value.c_str();
		uint64_t reply = 0;
		for (int k = 0; k < SET_WATCHED_N; k++)
			if (!strcmp(name, SET_WATCHED[k]))
			{
				reply = SETP_REPLY + (uint64_t)k;
				setValues[k] = value;
			}
		if (!strcmp(name, "lavfi-complex"))
		{
			static const char* const shows[] = {"art", "both", "visualizer", "none"};
			char lab[160];
			snprintf(lab, sizeof lab, "%s, %s, %d", visualizerLabel(settings.visualizer),
				settings.musicShow >= 0 && settings.musicShow < SHOWS ? shows[settings.musicShow] : "?", settings.theme);
			graphLabel = lab;
			graphHash = shortHash(value);
			graphChangeAt = ticks();
			fprintf(stderr, "music: graph for %s (%s, theme %d): %zu chars, #%s\n", visualizerLabel(settings.visualizer),
				shows[settings.musicShow >= 0 && settings.musicShow < SHOWS ? settings.musicShow : 0], settings.theme, value.size(), graphHash.c_str());
		}
		mpv_set_property_async(mpv, reply, name, MPV_FORMAT_STRING, &v);
		if (!strcmp(name, "lavfi-complex")) graphSet = value;	// known here, not asked of mpv (a read waits)
		else if (!strcmp(name, "af"))
		{
			afSet = value;
			size_t fx = value.find("@bpfx:");	// (the effect's entry is the last)
			afFxSet = fx == std::string::npos ? "" : value.substr(fx);
		}
	}

	void Player::open(const std::vector<std::string>& list, int index, const std::vector<std::string>& folderNames)
	{
		// the file playing keeps its place (the web's Play replaces it with no stop between: Recent lost it)
		saveResume();
		playlist = list;
		current = index;
		ghost = -1;
		netNamesByDir.clear();
		netOrder.clear();
		src = PlaySource();
		if (!list.empty())
		{
			const std::string& path = list[index];
			bool net = isNetPath(path);
			std::string dir = net ? netParent(path) : path.substr(0, path.rfind('/') == std::string::npos ? 0 : path.rfind('/'));
			src.kind = net ? SourceKind::NetFolder : SourceKind::Folder;
			src.id = dir;
			src.title = net ? netBaseName(dir) : baseName(dir);
			if (net) keepNames(dir, folderNames);
		}
		// a video picked plays, whatever the one before was left at
		prop("pause", "no");
		load();
	}

	void Player::openPlaylist(const std::vector<std::string>& paths, int index, const PlaySource& source)
	{
		saveResume();
		// (a playlist has none, but one written by hand could: the photo player is another thing)
		playlist.clear();
		int start = -1;
		for (int i = 0; i < (int)paths.size(); i++)
		{
			if (isPhotoFile(paths[i])) continue;
			if (start < 0 && i >= index) start = (int)playlist.size();
			playlist.push_back(paths[i]);
		}
		if (playlist.empty()) return;
		ghost = -1;
		netNamesByDir.clear();
		netOrder.clear();
		src = source;
		src.kind = SourceKind::Playlist;
		// past the end (the photos after it dropped): the last
		if (start < 0) start = (int)playlist.size() - 1;
		// the one picked gone (the card out): the next one there is, else the one before
		int there = step(start - 1, 1);
		if (there < 0) there = step(start, -1);
		current = there < 0 ? start : there;	// none there at all: its error shows
		prop("pause", "no");
		load();
	}

	void Player::updatePlaylist(const std::vector<std::string>& paths, int now)
	{
		if (!playing() || src.kind != SourceKind::Playlist) return;
		std::string path = currentPath();
		playlist = paths;
		now = std::clamp(now, 0, (int)playlist.size());
		// the one playing removed: kept at its place (a ghost) till another plays
		ghost = -1;
		if (now >= (int)playlist.size() || playlist[now] != path)
		{
			playlist.insert(playlist.begin() + now, path);
			ghost = now;
		}
		current = now;
	}

	void Player::renameSource(const std::string& id, const std::string& title, const std::string& newId)
	{
		if (src.kind != SourceKind::Playlist || src.id != id) return;
		src.title = title;
		if (!newId.empty()) src.id = newId;
	}

	int Player::step(int from, int dir)
	{
		int skipped = 0;
		for (int i = from + dir; i >= 0 && i < (int)playlist.size(); i += dir)
		{
			if (i == ghost) continue;
			// a playlist's local item that's gone (its card out, the file deleted): passed over, as a folder's never
			// is (it was listed a moment ago). A network one opens and fails as it would anyway (no wait for a server
			// here)
			if (src.kind == SourceKind::Playlist && !isNetPath(playlist[i]) && access(playlist[i].c_str(), F_OK) != 0)
			{
				skipped++;
				continue;
			}
			// (said once the file is up: a message sent before its loadfile is gone with the file before)
			skipNote = !skipped ? "" : skipped == 1 ? "Skipped a missing file" : "Skipped " + std::to_string(skipped) + " missing files";
			return i;
		}
		return -1;
	}

	void Player::moveTo(int index)
	{
		saveResume();
		// the ghost leaves the list now that something else plays
		if (ghost >= 0)
		{
			playlist.erase(playlist.begin() + ghost);
			if (index > ghost) index--;
			ghost = -1;
		}
		current = index;
		load();
	}

	bool Player::advance()
	{
		if (next()) return true;
		// Loop all: from the last back to the first (songs and videos)
		if (settings.loop != LOOP_ALL || playlist.empty()) return false;
		int first = step(-1, 1);
		if (first < 0) return false;
		moveTo(first);
		return true;
	}

	bool Player::next()
	{
		int to = step(current, 1);
		if (to < 0) return false;
		moveTo(to);
		return true;
	}

	bool Player::prev()
	{
		int to = step(current, -1);
		if (to < 0) return false;
		moveTo(to);
		return true;
	}

	bool Player::jumpTo(const std::string& path, int hint)
	{
		int best = -1;
		for (int i = 0; i < (int)playlist.size(); i++)
			if (i != ghost && playlist[i] == path && (best < 0 || std::abs(i - hint) < std::abs(best - hint))) best = i;
		if (best < 0) return false;
		moveTo(best);
		return true;
	}

	bool Player::back()
	{
		if (!playingPhoto() && loaded && position >= RESTART_S)
		{
			seekFraction(0);
			return true;
		}
		return prev();
	}

	const std::vector<std::string>& Player::namesIn(const std::string& dir) const
	{
		static const std::vector<std::string> none;
		auto it = netNamesByDir.find(dir);
		return it == netNamesByDir.end() ? none : it->second;
	}

	void Player::keepNames(const std::string& dir, const std::vector<std::string>& names)
	{
		if (!netNamesByDir.count(dir)) netOrder.push_back(dir);
		netNamesByDir[dir] = names;
		// 32 folders at most (a playlist of a whole server's worth): the oldest go, but the playing item's own
		std::string keep = playing() && isNetPath(currentPath()) ? netParent(currentPath()) : "";
		for (size_t i = 0; netOrder.size() > 32 && i < netOrder.size();)
		{
			if (netOrder[i] == keep || netOrder[i] == dir) i++;
			else
			{
				netNamesByDir.erase(netOrder[i]);
				netOrder.erase(netOrder.begin() + i);
			}
		}
	}

	std::string Player::netFolderWanted() const
	{
		if (!netWait.dir.empty()) return netWait.dir;
		if (src.kind != SourceKind::Playlist || !playing()) return "";
		// ahead of time: the next network item's, so that it needn't wait
		for (int i = current + 1; i < (int)playlist.size(); i++)
		{
			if (i == ghost || !isNetPath(playlist[i])) continue;
			std::string dir = netParent(playlist[i]);
			return netNamesByDir.count(dir) ? "" : dir;
		}
		return "";
	}

	void Player::netFolderListed(const std::string& dir, const std::vector<std::string>& names, bool ok)
	{
		// a failed listing is kept as an empty one: its items open without sidecars, not after a wait each
		keepNames(dir, ok ? names : std::vector<std::string>());
		if (netWait.dir != dir) return;
		netWait = NetWait();
		sendLoad();
	}

	void Player::load()
	{
		const std::string& path = playlist[current];
		heldUnplayed = false;
		loaded = finished = false;
		error.clear();
		errorNet = false;
		title.clear();
		tracks.clear();
		loadEntry = -1;
		startedEntry = -1;	// (an end that comes before this file starts is the one before's)
		tracksMine = false;
		clearChapters();
		position = duration = -1;
		seenAid = seenSid = -1;
		isAudio = isAudioFile(path);
		isPhoto = isPhotoFile(path);
		isNet = isNetPath(path);
		pausedForCache = false;
		seekingSince = 0;
		// the last network file still connecting is given up: mpv waits on its open before it takes this one
		if (opts.net) opts.net->cancelOpens();
		netWait = NetWait();
		// a playlist's network item whose folder isn't listed yet: its loadfile waits for the listing (the caller's),
		// for its subtitles and cover. The file before it stopped meanwhile: it would play on under the new one's
		// settings (graph, filters), and its end would read as this one's
		if (isNet && opts.net && src.kind == SourceKind::Playlist && !netNamesByDir.count(netParent(path)))
		{
			netWait.dir = netParent(path);
			netWait.deadline = ticks() + 1500;
			const char* cmd[] = {"stop", nullptr};
			mpv_command_async(mpv, 0, cmd);
		}
		loadSerial++;
		loadAt = ticks();
		readyPending = false;
		started = false;
		thumb.clear();
		thumbW = thumbH = 0;
		thumbId++;
		thumbAt = 0;
		thumbTries = 0;
		thumbAsked = thumbTurnedByMpv = false;
		photoW = photoH = fileTurn = exifTurn = 0;
		displayDueAt = 0;	// the graph below is the display's as it is now
		nativeFailed = false;	// (a new file: the tap tries it; syncNative starts it once it's loaded)
		audioClock = VizClock{};
		nativeStop("another file");

		showIdle(false);
		dropSongArt();
		if (!isAudio) setSongMargins("");
		// A song has no filter graph (2026-11-19, docs/notes/2026-11-19-viz-native.md, No graph for songs): mpv plays
		// its sound and nothing else is rendered by FFmpeg; the player draws the top panel (the native styles, the art
		// as an image overlay, the lines as ASS). A network song alone shows its art as mpv's own picture (below)
		const std::string graph;
		// the equalizer and effect for this file (none on a video), before the loadfile rebuilds the chain from af: a
		// song after a song with the same ones, nothing sent
		syncEq(true);

		// what this file is remembered with, as the file's own options (loadfile's last argument: they hold for this
		// file alone). Not set as properties beforehand: a property reaches the file still playing, and an aid it
		// doesn't have switches its sound to nothing, which makes aid "no" for good (mpv's track switch marks the
		// choice): the next file then opens without sound
		const ResumeEntry* e = isPhoto ? nullptr : resume.find(path);
		char start[32] = "none";
		// a song always from its start (an album plays through; music isn't a film to come back to)
		if (settings.rememberPosition && !isAudio && e && e->position >= 0) snprintf(start, sizeof start, "%.3f", e->position);
		std::string fileOpts = std::string("start=") + start;
		// an AVI from its start: the sound starts at once, not when the picture's second frame is due. One that holds
		// its first frame for seconds (MEncoder's, with a still opening: frames at 0, then 7 s) was silent till then.
		// AVI only: its streams all start at 0, so mpv's start trim has nothing to do there, while an MKV or TS whose
		// sound starts later or earlier than its picture needs it (without, out of step for seconds). Back on at the
		// first PLAYBACK_RESTART: after a seek (a resume too) the trim keeps the sound in step
		if (!strcmp(start, "none") && isAviFile(path)) fileOpts += ",initial-audio-sync=no";
		// a video's picture: always on (one under a photo decodes on, unseen: hideVideo); the file's own option, so a
		// vid set as a property by an older build never reaches the next file
		if (!isAudio && !isPhoto) fileOpts += ",vid=auto";
		// a song's picture: none (its art is the player's overlay), but a network song's art, which the overlay's decode
		// can't read, is mpv's own (applyAudioDisplay: in the art's box by the video's margins)
		else if (isAudio) fileOpts += isNet ? ",vid=auto" : ",vid=no";
		else if (isAudio && vidOff)
		{
			// a song after a video hidden under a photo: its art or graph needs the picture back (the video still
			// playing gets it a moment, under the photo)
			prop("vid", "auto");
			vidOff = false;
		}

		// (a graph takes the song's first audio track itself: no track to choose)
		wantAid = graph.empty() && settings.rememberAudio && e && e->aid > 0 ? e->aid : 0;
		if (graph.empty()) fileOpts += ",aid=" + (wantAid ? std::to_string(wantAid) : std::string("auto"));
		std::string alang = opts.lang;
		if (settings.rememberAudio && !resume.lastAudioLang.empty()) alang = resume.lastAudioLang + (alang.empty() ? "" : "," + alang);
		prop("alang", alang);
		alangPref = alang;

		wantSid = 0;
		std::string sid = "auto";
		if (settings.rememberSubs && e && e->sid >= 0)
		{
			wantSid = e->sid;
			sid = e->sid ? std::to_string(e->sid) : "no";
		}
		else if (settings.rememberSubs && !e && resume.lastSubOff) sid = "no";
		if (graph.empty()) fileOpts += ",sid=" + sid;
		else wantSid = 0;
		std::string slang = opts.lang;
		if (settings.rememberSubs && !resume.lastSubLang.empty()) slang = resume.lastSubLang + (slang.empty() ? "" : "," + slang);
		prop("slang", slang);

		// a song that plays to its end stays loaded there (keep-open; its end is heard as eof-reached, above): the
		// Session moves on to the next, or takes the list's last song back to its start, paused. A video's file ends
		// as before (keep-open no)
		prop("keep-open", isAudio ? "always" : "no");
		// Loop one: the song again and again (mpv's own); a video never loops; a photo stays (an animated one goes round)
		prop("loop-file", isPhoto || (isAudio && settings.loop == LOOP_ONE) ? "inf" : "no");

		if (isPhoto)
		{
			exif = isNet ? ExifInfo() : readExif(path);	// (a network photo: as FFmpeg turns it)
			view.rotate = 0;
			if (!settings.keepPhotoZoom)
			{
				view.zoom = 1;
				view.fill = false;
				panShareX = panShareY = 0;
			}
			// a photo has no Play: one that opens paused (the lid closed over the last) would freeze an animation
			prop("pause", "no");
			view.panX = view.panY = 0;
			// Fill is the view's (by zoom, below), so mpv fits it
			for (auto& a : aspectProperties(AspectMode::Fit)) prop(a.first, a.second);
			// a camera's 4000 pixels on the panel's 640: a filter that keeps fine lines, not the fast profile's bilinear
			// (drawn once, not 30 times a second)
			prop("dscale", "hermite");
			// a photo is never colour-adjusted: a film's Adjust page leaves it as it was taken
			for (const char* n : {"brightness", "contrast", "saturation", "gamma"}) prop(n, "0");
			prop("correct-downscaling", "yes");
			prop("image-display-duration", "inf");
			// at most 4096 pixels across: the RG DS's GPU may take no picture over 8192 (a 50 MP photo would stay
			// black), and it's a quarter of a 50 MP photo's memory; still more than the panel at 8x zoom
			prop("vf", "lavfi=[scale=w='min(iw,4096)':h='min(ih,4096)':force_original_aspect_ratio=decrease]");
		}
		else
		{
			exif = ExifInfo();
			prop("vf", "");
			for (auto& a : aspectProperties(aspect)) prop(a.first, a.second);
			// the Picture sheet's Adjust, kept for every video while the app runs (0: the film as it is); never a
			// song's album art or visualizer
			{
				static const char* NAMES[4] = {"brightness", "contrast", "saturation", "gamma"};
				for (int i = 0; i < 4; i++) prop(NAMES[i], std::to_string(isAudio ? 0 : (int)std::lround(videoAdj[i])));
			}
			// (that panscan is the aspect mode's: a song's Album art only sets its own again, margins and all, at its
			// next updateOverlay: the next song's art filled its square no longer)
			marginsSet = "?";
			prop("dscale", "bilinear");
			prop("correct-downscaling", "no");
		}
		applyPhotoView();	// a video's: none

		// photos aren't kept in Recent: fifty of them would push every video out
		if (settings.keepHistory && !incognito && !isPhoto && (settings.keepNetHistory || !isNet))
		{
			played.add(path);
			recentSer++;
			if (!opts.stateDir.empty()) writeFile(opts.stateDir + "/history.tsv", played.serialize());
		}

		pendingOpts = fileOpts;
		if (netWait.dir.empty()) sendLoad();
	}

	void Player::sendLoad()
	{
		const std::string& path = playlist[current];
		std::string fileOpts = pendingOpts;
		if (isNet) fileOpts += netFileOptions(path);
		else if (!isAudio && !isPhoto) fileOpts += localSubOptions(path);
		// (the file's options take loadfile's index argument before them: -1, "replace" has no place)
		const char* cmd[] = {"loadfile", path.c_str(), "replace", "-1", fileOpts.c_str(), nullptr};
		mpv_command_async(mpv, LOAD_REPLY + (uint64_t)loadSerial, cmd);
	}

	// what the panel says when a network file couldn't be played on
	static std::string netPlayError(NetError e)
	{
		if (e == NetError::NotFound) return "Can't find the file or its server";
		if (e == NetError::Other) return "This file can't be opened";
		return netErrorText(e, "The server");
	}

	void Player::checkNet()
	{
		// the folder's listing didn't come in time (a slow or gone server): the file opens without its sidecars
		if (!netWait.dir.empty() && ticks() >= netWait.deadline)
		{
			fprintf(stderr, "net: no listing of %s in time: opened without its subtitles and cover\n", netWait.dir.c_str());
			netWait = NetWait();
			sendLoad();
			return;
		}
		if (!isNet || !opts.net || !playing() || !error.empty() || !buffering()) return;
		NetResult f = opts.net->failure(currentPath());
		if (f.ok()) return;
		opts.net->takeFailure(currentPath());
		error = netPlayError(f.error);
		errorNet = f.error != NetError::Other;
		errorNetFound = f.error != NetError::NotFound;
		const char* cmd[] = {"stop", nullptr};	// (its END_FILE is a stop: the error stays)
		mpv_command_async(mpv, 0, cmd);
	}

	void Player::logAudio(const char* what) const
	{
		// one line for each change: what it is now, all of it, so one line says the whole state. An output that isn't
		// two channels, or a layout with one side only, is marked: that's a sound on one speaker
		std::string name = currentPath().empty() ? "-" : baseName(currentPath());
		std::string aid = "none";
		for (auto& t : tracks)
			if (t.type == TrackType::Audio && t.selected) aid = std::to_string((long long)t.id) + " " + t.codec + " " + std::to_string(t.channels) + "ch" + (t.lang.empty() ? "" : " " + t.lang);
		// (a key's value in "key=value key=value": the key at the start or after a space, so channels isn't hr-channels)
		auto val = [](const std::string& t, const std::string& k) {
			size_t a = t.rfind(k + "=", 0) == 0 ? 0 : t.find(" " + k + "=");
			if (a == std::string::npos) return std::string();
			a = t.find('=', a) + 1;
			return t.substr(a, t.find(' ', a) == std::string::npos ? std::string::npos : t.find(' ', a) - a);
		};
		std::string cc = val(audioOut, "channel-count"), ch = val(audioOut, "channels");
		bool suspect = !audioOut.empty() && ((!cc.empty() && cc != "2") || ch == "mono" || ch == "fl" || ch == "fr" || ch == "fc");
		// (one line: compact, the whole state; mono in is normal, mpv mixes it to both speakers)
		auto brief = [&](const std::string& t) {
			if (t.empty() || t == "-") return std::string("-");
			return val(t, "channels") + " " + val(t, "samplerate") + " " + val(t, "format");
		};
		fprintf(stderr, "audio: %s %s | in %s | out %s | ao %s | af %s | graph %s | aid %s | fx %s eq %s%s\n", what, name.c_str(),
			brief(audioIn).c_str(), brief(audioOut).c_str(), audioAo.empty() ? "-" : audioAo.c_str(), audioAf.empty() ? "-" : audioAf.c_str(),
			audioLavfi.empty() || audioLavfi == "-" ? "-" : (audioLavfi.size() > 80 ? audioLavfi.substr(0, 80) + "..." : audioLavfi).c_str(), aid.c_str(),
			soundFxKey(settings.soundFx), settings.eqOn ? "on" : "off", suspect ? "  SUSPECT: the output isn't two channels" : "");
	}

	void Player::setBoost(int v)
	{
		boost = std::clamp(v, 100, std::max(100, settings.volumeMax));
		prop("volume", std::to_string(boost));
	}

	void Player::reloadAudio(const char* why)
	{
		if (!mpv || !playing() || playingPhoto() || audioOut.empty() || audioOut == "-") return;
		fprintf(stderr, "audio: the output opened again (%s)\n", why);
		const char* cmd[] = {"ao-reload", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	namespace
	{
		// UTF-8 already (or UTF-16 with its mark, which mpv reads itself): nothing to convert
		bool subtitleIsUnicode(const std::string& s)
		{
			if (s.size() >= 2 && (((uint8_t)s[0] == 0xFF && (uint8_t)s[1] == 0xFE) || ((uint8_t)s[0] == 0xFE && (uint8_t)s[1] == 0xFF)))
				return true;
			for (size_t i = 0; i < s.size();)
			{
				uint8_t c = (uint8_t)s[i];
				if (c < 0x80) { i++; continue; }
				int len = (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
				if (!len || i + len > s.size()) return false;
				for (int k = 1; k < len; k++)
					if (((uint8_t)s[i + k] >> 6) != 2) return false;
				i += len;
			}
			return true;
		}

		// "movie.cs.srt" for "movie.mkv": "cs", the language the converter weighs its guess by ("" none)
		std::string subtitleLang(const std::string& videoName, const std::string& subName)
		{
			size_t vd = videoName.rfind('.'), sd = subName.rfind('.');
			std::string vb = videoName.substr(0, vd), sb = subName.substr(0, sd);
			if (sb.size() <= vb.size() + 1 || sb.compare(0, vb.size(), vb) != 0) return "";
			std::string tag = sb.substr(sb.rfind('.') + 1);
			return tag.size() >= 2 && tag.size() <= 3 ? tag : "";
		}
	}

	std::string Player::localSubOptions(const std::string& path) const
	{
		if (opts.stateDir.empty()) return "";
		size_t slash = path.rfind('/');
		if (slash == std::string::npos) return "";
		std::string dir = path.substr(0, slash), name = path.substr(slash + 1);
		std::vector<std::string> names;
		if (DIR* d = opendir(dir.c_str()))
		{
			while (dirent* e = readdir(d)) names.push_back(e->d_name);
			closedir(d);
		}
		std::vector<std::string> subs = subtitleFilesFor(name, names);
		if (subs.empty()) return "";
		// the copies of the file before this one go: one video's at a time (mpv has read what it took)
		std::string out = opts.stateDir + "/subs-utf8";
		if (DIR* d = opendir(out.c_str()))
		{
			while (dirent* e = readdir(d))
				if (e->d_name[0] != '.') unlink((out + "/" + e->d_name).c_str());
			closedir(d);
		}
		std::vector<std::string> files;
		bool converted = false;
		for (auto& n : subs)
		{
			std::string full = dir + "/" + n, ext = n.substr(n.rfind('.') + 1);
			for (auto& c : ext) c = (char)tolower((unsigned char)c);
			struct stat st;
			bool text = ext == "srt" || ext == "ass" || ext == "ssa" || ext == "vtt";
			// (a text file, and not a film's worth: 8 MB is far past any real one)
			if (!text || stat(full.c_str(), &st) != 0 || st.st_size > 8 * 1024 * 1024)
			{
				files.push_back(full);
				continue;
			}
			std::string bytes = readFile(full);
			if (subtitleIsUnicode(bytes))
			{
				files.push_back(full);
				continue;
			}
			// (the copy keeps the file's name: the track's name in the Subtitles list stays the same)
			mkdir(out.c_str(), 0755);
			std::string copy = out + "/" + n;
			if (!writeFileAtomic(copy, subtitleTextToUtf8(bytes, subtitleLang(name, n))))
			{
				files.push_back(full);
				continue;
			}
			fprintf(stderr, "subtitles: %s converted to UTF-8 for mpv\n", n.c_str());
			files.push_back(copy);
			converted = true;
		}
		if (!converted) return "";
		// mpv finds none itself (it would add the old code page's too): these, the copies in place of the originals
		std::string v;
		for (auto& f : files)
		{
			if (!v.empty()) v += ':';
			for (char c : f) v += c == ':' ? std::string("\\:") : std::string(1, c);
		}
		return ",sub-auto=no,sub-files=%" + std::to_string(v.size()) + "%" + v;
	}

	std::string Player::netFileOptions(const std::string& path) const
	{
		// read ahead (mpv's cache, which a stream callback has only when asked): a Wi-Fi's hiccup isn't a stall. 64 MiB
		// ahead, 16 behind for a step back, of the handheld's 1 GB
		std::string o = ",cache=yes,demuxer-max-bytes=64MiB,demuxer-max-back-bytes=16MiB";
		// what mpv finds next to a local file by itself (sub-auto, cover-art-auto), from the folder's listing: a URL's
		// folder isn't looked in. A path list, ':' between, so each ':' of a URL escaped; the whole quoted (%len%)
		auto list = [](const std::vector<std::string>& urls) {
			std::string v;
			for (auto& u : urls)
			{
				if (!v.empty()) v += ':';
				for (char c : u) v += c == ':' ? std::string("\\:") : std::string(1, c);
			}
			return "%" + std::to_string(v.size()) + "%" + v;
		};
		std::string dir = netParent(path);
		std::vector<std::string> extra;
		if (isAudio)
		{
			std::string cover = coverFileFor(namesIn(dir));
			if (!cover.empty()) o += ",cover-art-files=" + list({netJoin(dir, cover)});
		}
		else if (!isPhoto)
		{
			for (auto& n : subtitleFilesFor(netBaseName(path), namesIn(dir))) extra.push_back(netJoin(dir, n));
			if (!extra.empty()) o += ",sub-files=" + list(extra);
		}
		return o;
	}

	void Player::pollEvents()
	{
		while (mpv)
		{
			mpv_event* ev = mpv_wait_event(mpv, 0);
			if (ev->event_id == MPV_EVENT_NONE) break;
			switch (ev->event_id)
			{
			case MPV_EVENT_PROPERTY_CHANGE:
			{
				auto* p = (mpv_event_property*)ev->data;
				if (ev->reply_userdata == CORE_IDLE || ev->reply_userdata == IDLE_ACTIVE || ev->reply_userdata == EOF_REACHED ||
					ev->reply_userdata == PAUSE || ev->reply_userdata == PAUSED_FOR_CACHE || ev->reply_userdata == CURRENT_AO)
				{
					std::string v = p->format == MPV_FORMAT_FLAG ? (*(int*)p->data ? "yes" : "no") : p->format == MPV_FORMAT_STRING ? *(char**)p->data : "-";
					const char* nm = ev->reply_userdata == CORE_IDLE ? "core-idle" : ev->reply_userdata == IDLE_ACTIVE ? "idle-active"
						: ev->reply_userdata == EOF_REACHED ? "eof-reached" : ev->reply_userdata == PAUSE ? "pause"
						: ev->reply_userdata == PAUSED_FOR_CACHE ? "paused-for-cache" : "ao";
					if (ev->reply_userdata == CORE_IDLE) seenCoreIdle = v;
					else if (ev->reply_userdata == IDLE_ACTIVE) seenIdleActive = v;
					else if (ev->reply_userdata == EOF_REACHED) seenEof = v;
					else if (ev->reply_userdata == PAUSE) seenPause = v;
					watchLog(nm, v.c_str());
				}
				switch (ev->reply_userdata)
				{
				case AUDIO_IN: case AUDIO_OUT: case CURRENT_AO: case AF_CHAIN: case LAVFI_GRAPH:
				{
					std::string v = p->format == MPV_FORMAT_NODE ? nodeText((mpv_node*)p->data) : p->format == MPV_FORMAT_STRING ? *(char**)p->data : "-";
					std::string& was = ev->reply_userdata == AUDIO_IN ? audioIn : ev->reply_userdata == AUDIO_OUT ? audioOut
						: ev->reply_userdata == CURRENT_AO ? audioAo : ev->reply_userdata == AF_CHAIN ? audioAf : audioLavfi;
					if (v == was) break;
					was = v;
					// (a property going away as a file closes, or anything before the output opens: not worth a line)
					if (v == "-" || audioOut.empty() || audioOut == "-" || currentPath().empty()) break;
					logAudio(ev->reply_userdata == AUDIO_IN ? "in" : ev->reply_userdata == AUDIO_OUT ? "out" : ev->reply_userdata == CURRENT_AO ? "ao"
						: ev->reply_userdata == AF_CHAIN ? "af" : "graph");
					break;
				}
				case TIME_POS: position = p->format == MPV_FORMAT_DOUBLE ? *(double*)p->data : -1; break;
				case AUDIO_PTS:
					audioClock.pts = p->format == MPV_FORMAT_DOUBLE ? *(double*)p->data : -1;
					audioClock.atNs = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
						std::chrono::steady_clock::now().time_since_epoch()).count();
					break;
				case DURATION:
				{
					duration = p->format == MPV_FORMAT_DOUBLE ? *(double*)p->data : -1;
					// mpv has just read the whole length: a better answer than a probe's guess for the lists. Only a
					// local file (a network path or a URL's length is that stream's, not the file's, and realpath() fails)
					std::string path = currentPath();
					if (duration > 0 && !isNetPath(path) && path.find("://") == std::string::npos)
						recordDuration(path, duration);
					break;
				}
				case PAUSE: paused = p->format == MPV_FORMAT_FLAG && *(int*)p->data; break;
				case EOF_REACHED:
					// keep-open leaves a song at its end (mpv pauses there and says nothing else), so its end comes
					// here into the same path as a file mpv closed: the caller moves on to the next, or the Session
					// keeps the list's last song at its start. (A video's file still ends by itself: keep-open no)
					if (isAudio && loaded && p->format == MPV_FORMAT_FLAG && *(int*)p->data) finished = true;
					break;
				case SPEED: if (p->format == MPV_FORMAT_DOUBLE) speed = *(double*)p->data; break;
				case PAUSED_FOR_CACHE: pausedForCache = p->format == MPV_FORMAT_FLAG && *(int*)p->data; break;
				case SEEKING:
				{
					bool on = p->format == MPV_FORMAT_FLAG && *(int*)p->data;
					if (!on) seekingSince = 0;
					else if (!seekingSince) seekingSince = ticks();
					break;
				}
				case MEDIA_TITLE:
					title = p->format == MPV_FORMAT_STRING ? *(char**)p->data : "";
					if (isAudio && loaded) updateOverlay();	// it can come after the file's start
					break;
				case CHAPTER_LIST:
					chapters = parseChapterList(p->format == MPV_FORMAT_NODE ? (mpv_node*)p->data : nullptr);
					chapterSerial++;
					if (isAudio && loaded) updateOverlay();	// a song's chapter line under its title
					break;
				case CHAPTER:
					chapter = p->format == MPV_FORMAT_INT64 ? (int)*(int64_t*)p->data : -1;
					if (pendingOn && chapter == pendingChapter) pendingOn = false;
					if (isAudio && loaded) updateOverlay();
					break;
				case TRACK_LIST:
					// the last file's list, read before it closed and come after this one's load(): its album art
					// would put the art's graph on a song with none ("Pad vid1 is not connected"), which leaves it
					// silent, frozen at its first second
					if (!tracksMine) break;
					refreshTracks(p->format == MPV_FORMAT_NODE ? (mpv_node*)p->data : nullptr);
					if (loaded && readyPending && !tracks.empty()) fileReady();
					break;
				case METADATA:
				{
					// the song's artist and album, for its title over the top panel (any case: ID3's are lower)
					tagArtist.clear();
					tagAlbum.clear();
					tagYear.clear();
					auto* n = p->format == MPV_FORMAT_NODE ? (mpv_node*)p->data : nullptr;
					if (n && n->format == MPV_FORMAT_NODE_MAP)
						for (int i = 0; i < n->u.list->num; i++)
						{
							std::string k = n->u.list->keys[i];
							for (char& c : k) c = (char)tolower((unsigned char)c);
							if (n->u.list->values[i].format != MPV_FORMAT_STRING) continue;
							if (k == "artist") tagArtist = n->u.list->values[i].u.string;
							else if (k == "album") tagAlbum = n->u.list->values[i].u.string;
							else if ((k == "date" || k == "year") && tagYear.empty()) tagYear = std::string(n->u.list->values[i].u.string).substr(0, 4);
						}
					if (isAudio && loaded) updateOverlay();
					break;
				}
				}
				break;
			}
			case MPV_EVENT_FILE_LOADED:
			{
				if (!netWait.dir.empty()) break;	// the file before (a quick Next), not the one waiting
				loaded = true;
				loadedAt = ticks();
				audioChecks = 0;
				audioCheckAt = ticks() + 1500;
				// what needs the file's tracks waits for its list, if that comes after this event (not read here: as
				// mpv sets the picture up it waits for this thread to draw, and a read would wait for mpv)
				if (!tracks.empty()) fileReady();
				else readyPending = true;
				break;
			}
			case MPV_EVENT_START_FILE:
				startedEntry = ((mpv_event_start_file*)ev->data)->playlist_entry_id;
				if (loadEntry >= 0 && startedEntry == loadEntry) tracksMine = true;
				break;
			case MPV_EVENT_SET_PROPERTY_REPLY:
			{
				int k = (int)(ev->reply_userdata - SETP_REPLY);
				if (ev->reply_userdata < SETP_REPLY || k >= SET_WATCHED_N) break;
				bool graph = k == 0;
				if (ev->error < 0)
				{
					fprintf(stderr, "mpv: set %s failed: %s (%s)\n", SET_WATCHED[k], mpv_error_string(ev->error), setValues[k].substr(0, 200).c_str());
					if (graph) fprintf(stderr, "mpv: the graph that failed (#%s, %s):\n%s\n", graphHash.c_str(), graphLabel.c_str(), setValues[k].c_str());
				}
				else if (graph)
					fprintf(stderr, "mpv: graph set ok (%s, %zu chars, #%s)\n", graphLabel.c_str(), setValues[k].size(), shortHash(setValues[k]).c_str());
				break;
			}
			case MPV_EVENT_COMMAND_REPLY:
			{
				if (ev->reply_userdata >= LOAD_REPLY)
				{
					// (an earlier load's answer: not this file's entry)
					if (ev->reply_userdata != LOAD_REPLY + (uint64_t)loadSerial) break;
					if (ev->error < 0)
					{
						tracksMine = true;	// (no entry to tell by: whatever list comes is taken, as before)
						break;
					}
					mpv_node* r = &((mpv_event_command*)ev->data)->result;
					if (r->format == MPV_FORMAT_NODE_MAP)
						for (int i = 0; i < r->u.list->num; i++)
							if (!strcmp(r->u.list->keys[i], "playlist_entry_id") && r->u.list->values[i].format == MPV_FORMAT_INT64)
								loadEntry = r->u.list->values[i].u.int64;
					// (it starts after the answer, but as a safeguard)
					if (loadEntry >= 0 && startedEntry == loadEntry) tracksMine = true;
					break;
				}
				if (ev->reply_userdata > ART_REPLY)
				{
					artReplied(ev->reply_userdata - ART_REPLY);
					break;
				}
				if (ev->reply_userdata > LEVEL_REPLY)
				{
					levelReplied(ev->reply_userdata - LEVEL_REPLY);
					break;
				}
				if (ev->reply_userdata == EQ_REPLY)
				{
					if (ev->error < 0)
					{
						fprintf(stderr, "banana-player: af-command failed (%s): the equalizer set as a whole instead\n", mpv_error_string(ev->error));
						eqFailed = true;
					}
					// the whole af once the last answer is in (one change, not one per band)
					if (eqInFlight > 0 && --eqInFlight == 0 && eqFailed)
					{
						eqFailed = false;
						std::string want = afWant();
						// af set to what it already is does nothing: off and on again then
						if (want == afSet && !want.empty()) prop("af", "");
						prop("af", want);
						eqLive = settings.eqGains;
					}
					break;
				}
				if (ev->reply_userdata != THUMB_REPLY + (uint64_t)loadSerial) break;	// an earlier photo's
				thumbAsked = false;
				if (ev->error < 0) thumbAt = ticks() + 250;	// no frame up yet: again soon
				else takeThumb(&((mpv_event_command*)ev->data)->result);
				break;
			}
			case MPV_EVENT_END_FILE:
			{
				if (wakeLoads > 0)
				{
					wakeLoads--;	// wakeOutput's empty load, not a file of the person's
					break;
				}
				{
					auto* ef = (mpv_event_end_file*)ev->data;
					const char* why = ef->reason == MPV_END_FILE_REASON_EOF ? "eof" : ef->reason == MPV_END_FILE_REASON_STOP ? "stop"
						: ef->reason == MPV_END_FILE_REASON_QUIT ? "quit" : ef->reason == MPV_END_FILE_REASON_ERROR ? "error"
						: ef->reason == MPV_END_FILE_REASON_REDIRECT ? "redirect" : "?";
					uint64_t ago = graphChangeAt ? ticks() - graphChangeAt : WATCH_MS + 1;
					char since[48];
					if (ago <= WATCH_MS) snprintf(since, sizeof since, "a graph change %llu ms ago", (unsigned long long)ago);
					else snprintf(since, sizeof since, "-");
					fprintf(stderr, "player: end-file reason=%s error=%s entry=%lld current=%lld after %s\n", why,
						ef->reason == MPV_END_FILE_REASON_ERROR ? mpv_error_string(ef->error) : "-", (long long)ef->playlist_entry_id,
						(long long)startedEntry, since);
				}
				// the file before one waiting for its folder's listing, stopped (or ended): not the one to come
				if (!netWait.dir.empty()) break;
				auto* e = (mpv_event_end_file*)ev->data;
				// an error ending an earlier entry (a retried file's failed try, ending after the new load began): not
				// this file's
				if (e->reason == MPV_END_FILE_REASON_ERROR && (startedEntry < 0 || e->playlist_entry_id != startedEntry))
				{
					fprintf(stderr, "player: an earlier entry's error end, not this file's (entry %lld)\n", (long long)e->playlist_entry_id);
					break;
				}
				// a network file: a read that failed is an end of file to mpv; the connection's error, kept by the
				// stream, says it wasn't
				// (only an error's or an end's: the stop of the file before, which comes once the next is already the
				// current one, would take the next one's failure away)
				bool ends = e->reason == MPV_END_FILE_REASON_ERROR || e->reason == MPV_END_FILE_REASON_EOF;
				NetResult netFail = ends && isNet && opts.net && !playlist.empty() ? opts.net->takeFailure(currentPath()) : NetResult();
				if (isNet && e->reason == MPV_END_FILE_REASON_ERROR)
					fprintf(stderr, "net: %s ended with an error (%s)%s\n", currentPath().c_str(), mpv_error_string(e->error),
						netFail.ok() ? (errorNet ? ", the connection's said already" : ", the stream kept none") : (", the stream's: " + netFail.detail).c_str());
				// (checkNet may have taken the stream's error first, and stopped it: that error stays, not mpv's word)
				if (netFail.ok() && errorNet && !error.empty()) {}
				else if (e->reason == MPV_END_FILE_REASON_ERROR || (e->reason == MPV_END_FILE_REASON_EOF && !netFail.ok()))
				{
					error = netFail.ok() ? mpv_error_string(e->error) : netPlayError(netFail.error);
					errorNet = !netFail.ok() && netFail.error != NetError::Other;
					errorNetFound = netFail.error != NetError::NotFound;
				}
				else if (e->reason == MPV_END_FILE_REASON_EOF) finished = true;
				break;
			}
			case MPV_EVENT_AUDIO_RECONFIG:
			case MPV_EVENT_VIDEO_RECONFIG:
				if (ev->event_id == MPV_EVENT_AUDIO_RECONFIG) watchLog("audio-reconfig", "");
				if (graphAskedAt)
				{
					bool video = ev->event_id == MPV_EVENT_VIDEO_RECONFIG;
					fprintf(stderr, "timing: graph switch: %s back in %llu ms\n", video ? "picture" : "sound",
						(unsigned long long)(ticks() - graphAskedAt));
					if (video) graphAskedAt = 0;
				}
				break;
			case MPV_EVENT_PLAYBACK_RESTART:
				watchLog("playback-restart", "");
				// this file's (it comes after its FILE_LOADED): one left over from the last file's seek isn't
				if (loaded && !started && netWait.dir.empty())
				{
					started = true;
					prop("initial-audio-sync", "yes");	// for this file's seeks (see load())
					// in the log: how long a file takes to start, from the press (the handheld's numbers)
					fprintf(stderr, "timing: %s started in %llu ms (opened in %llu)\n", baseName(currentPath()).c_str(),
						(unsigned long long)(ticks() - loadAt), (unsigned long long)(loadedAt - loadAt));
				}
				break;
			case MPV_EVENT_SHUTDOWN:
				quit = true;
				break;
			case MPV_EVENT_LOG_MESSAGE:
			{
				auto* m = (mpv_event_log_message*)ev->data;
				if (opts.verbose) fprintf(stderr, "%6llu ", (unsigned long long)(ticks() % 1000000));
				fprintf(stderr, "[%s] %s: %s", m->level, m->prefix, m->text);
				break;
			}
			default: break;
			}
		}
	}

	void Player::fileReady()
	{
		readyPending = false;
		const Track* a = selected(TrackType::Audio);
		const Track* s = selected(TrackType::Sub);
		seenAid = a ? a->id : -1;
		seenSid = s ? s->id : 0;
		// a remembered track that isn't there any more (a subtitle file taken away): mpv's own choice instead
		auto missing = [&](TrackType type, int64_t id) {
			for (auto& t : tracks)
				if (t.type == type && t.id == id) return false;
			return true;
		};
		if (wantAid > 0 && missing(TrackType::Audio, wantAid)) prop("aid", songAid(0));
		if (wantSid > 0 && missing(TrackType::Sub, wantSid)) prop("sid", "auto");
		if (isAudio && (settings.musicShow == SHOW_ART || settings.musicShow == SHOW_BOTH)) applyAudioDisplay();
		updateOverlay();
		if (!skipNote.empty()) osd(skipNote, 2000);
		skipNote.clear();
		// a photo's name and its place in the folder, for a moment
		if (isPhoto) osd(baseName(playlist[current]) + (count() > 1 ? "   " + std::to_string(current + 1) + " / " + std::to_string(count()) : ""), 2000);
	}

	void Player::refreshTracks(mpv_node* node)
	{
		if (!node) return;
		tracks = parseTrackList(node);
		if (loaded && !tracks.empty())
		{
			const Track* a = selected(TrackType::Audio);
			const Track* s = selected(TrackType::Sub);
			seenAid = a ? a->id : -1;
			seenSid = s ? s->id : 0;
		}
	}

	bool Player::frameDue()
	{
		return rc && (mpv_render_context_update(rc) & MPV_RENDER_UPDATE_FRAME);
	}

	int Player::render(int w, int h, unsigned target, bool skip, bool noWait)
	{
		mpv_opengl_fbo fbo{(int)target, w, h, 0};
		int flip = 1;
		// skip: the frame is taken (mpv's queue moves on, its timing kept) but nothing is drawn: no scaling, no
		// shaders, no GL work stealing time from the bottom panel's drawing
		int skipping = skip ? 1 : 0;
		// and, skipped, not waited for: mpv's default holds the call till the frame's display time, which for a frame
		// nobody sees (the music's resting visualizer under the manga reader or a photo) stalled the main loop up to
		// a frame's span, five times a second: the reader's scroll stuttered only while music played
		int block = skip || noWait ? 0 : 1;
		mpv_render_param params[] = {
			{MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
			{MPV_RENDER_PARAM_FLIP_Y, &flip},
			{MPV_RENDER_PARAM_SKIP_RENDERING, &skipping},
			{MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
			{MPV_RENDER_PARAM_INVALID, nullptr},
		};
		int r = mpv_render_context_render(rc, params);
		// (in the log: a graph switch's first frame, as the top panel takes it; with the sound's own reconfig line
		// below, which some outputs never send, the time a style or a theme's ground takes to come up)
		if (graphAskedAt && !graphFrameLogged)
		{
			graphFrameLogged = true;
			fprintf(stderr, "timing: graph switch: first frame taken %llu ms after it was asked\n", (unsigned long long)(ticks() - graphAskedAt));
		}
		return r;
	}

	void Player::reportSwap()
	{
		if (rc) mpv_render_context_report_swap(rc);
	}

	uint64_t Player::ticks()
	{
		return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	std::string Player::songAid(int64_t prefer) const
	{
		if (prefer > 0) return std::to_string(prefer);
		std::vector<const Track*> audio;
		for (auto& t : tracks)
			if (t.type == TrackType::Audio) audio.push_back(&t);
		if (audio.empty()) return "auto";
		auto lower = [](std::string v) { for (auto& c : v) c = (char)tolower((unsigned char)c); return v; };
		for (size_t a = 0; a <= alangPref.size();)
		{
			size_t b = alangPref.find(',', a);
			if (b == std::string::npos) b = alangPref.size();
			std::string want = lower(alangPref.substr(a, b - a));
			if (!want.empty())
				for (const Track* t : audio)
					if (lower(t->lang) == want) return std::to_string(t->id);
			a = b + 1;
		}
		for (const Track* t : audio)
			if (t->isDefault) return std::to_string(t->id);
		return std::to_string(audio.front()->id);
	}

	void Player::checkAudio()
	{
		// a file with sound that came out silent: the sound device wasn't free when mpv opened it (and mpv doesn't
		// try again by itself). Opened again, three times at most, each in the log
		if (!loaded || !audioCheckAt || ticks() < audioCheckAt) return;
		audioCheckAt = 0;
		// a file with audio tracks and none chosen (not under a graph, which chooses its own): mpv dropped the track
		// when the sound device wouldn't open (its error_on_track), and ao-reload does nothing with no device. The
		// track chosen again opens the device anew. (The Audio page has no Off: this is never the person's choice)
		if (!selected(TrackType::Audio) && !tracksOf(TrackType::Audio).empty())
		{
			if (audioChecks >= 3) return;
			audioChecks++;
			fprintf(stderr, "banana-player: no audio track chosen; choosing it again (try %d)\n", audioChecks);
			syncEq(true);	// (a track chosen rebuilds the chain from af)
			prop("aid", songAid(wantAid));
			audioCheckAt = ticks() + 1500;
			return;
		}
		if (!selected(TrackType::Audio) || !get("current-ao").empty()) return;
		if (audioChecks >= 3)
		{
			fprintf(stderr, "banana-player: still no sound output after %d tries\n", audioChecks);
			return;
		}
		audioChecks++;
		fprintf(stderr, "banana-player: no sound output; opening the sound device again (try %d)\n", audioChecks);
		syncEq(true);
		const char* cmd[] = {"ao-reload", nullptr};
		mpv_command_async(mpv, 0, cmd);
		audioCheckAt = ticks() + 1500;
	}

	void Player::checkSongEnd()
	{
		if (!isAudio || !loaded || finished || !(duration > 0) || position < duration + 0.25) return;
		if (graphSet.empty()) return;	// no graph: mpv's own end (eof-reached) takes it
		if (settings.loop == LOOP_ONE) seekFraction(0);
		else finished = true;
	}

	bool Player::loading() const
	{
		// till mpv starts it: its first frame up (or its sound playing)
		return playing() && error.empty() && !started;
	}

	bool Player::buffering() const
	{
		// (a quick seek shows nothing: half a second first)
		return playing() && error.empty() && started && (pausedForCache || (seekingSince && ticks() - seekingSince >= 500));
	}

	void Player::checkLoading()
	{
		// the note's time is up (the loop draws the panel once more so a card on an idle one goes)
		if (noteUntil && ticks() >= noteUntil)
		{
			noteUntil = 0;
			dropImage(OV_NOTE);
			cardMs = 1;
		}
		// (buffering: the network file waits, as a file opening does, but with a word of its own)
		bool show = (loading() && ticks() - loadAt >= 300) || buffering();
		if (!show)
		{
			if (spinnerStep >= 0)
			{
				dropImage(OV_LOAD);
				spinnerStep = -1;
			}
			return;
		}
		int step = (int)(ticks() / 100 % 8) + (loading() ? 0 : 8);	// the word changes with the next step
		if (step == spinnerStep) return;
		spinnerStep = step;
		// the state card (levelcard.h) with the spinner in its tile, over the middle (the last file's picture can still be up)
		StateCardSpec spec;
		spec.spinner = step % 8;
		spec.title = loading() ? "Loading" : "Buffering";
		spec.full = false;
		const int pw = topW > 0 ? topW : 960, ph = topH > 0 ? topH : 720;
		LevelCardImage card = drawStateCard(opts.fontsDir, pw, ph, spec);
		putImage(OV_LOAD, card, (pw - card.w) / 2, (ph - card.h) / 2);
	}

	void Player::checkPhoto()
	{
		// (after the start: a read waits for mpv, which may be waiting for this thread to draw while it opens a file)
		if (!isPhoto || !loaded || !started) return;
		if (!photoW)
		{
			int w = atoi(get("video-params/dw").c_str()), h = atoi(get("video-params/dh").c_str());
			if (w <= 0 || h <= 0) return;
			photoW = w;
			photoH = h;
			// upright: by the file's rotation metadata (mpv's own turn), else by its EXIF orientation, which
			// FFmpeg 7.1 leaves to the player
			fileTurn = atoi(get("video-params/rotate").c_str()) % 360;
			exifTurn = fileTurn ? 0 : exifRotation(exif.orientation);
			// the last photo's zoom, at the same share of the way across
			PhotoScale s = photoScaleNow();
			setPhotoView(view.zoom, panShareX * maxPan(s.x), panShareY * maxPan(s.y));
			thumbAt = ticks() + 100;
		}
		if (thumbW || thumbAsked || !thumbAt || ticks() < thumbAt || thumbTries >= 10) return;
		// the frame as decoded (no OSD, full size), answered later: a big photo's copy doesn't hold the loop up
		thumbAsked = true;
		thumbTries++;
		mpv_node items[2];
		items[0].format = items[1].format = MPV_FORMAT_STRING;
		items[0].u.string = const_cast<char*>("screenshot-raw");
		items[1].u.string = const_cast<char*>("video");
		mpv_node_list list{2, items, nullptr};
		mpv_node args;
		args.format = MPV_FORMAT_NODE_ARRAY;
		args.u.list = &list;
		mpv_command_node_async(mpv, THUMB_REPLY + (uint64_t)loadSerial, &args);
	}

	void Player::takeThumb(mpv_node* r)
	{
		if (!r || r->format != MPV_FORMAT_NODE_MAP) return;
		int64_t w = 0, h = 0, stride = 0;
		const unsigned char* px = nullptr;
		size_t size = 0;
		std::string format;
		for (int i = 0; i < r->u.list->num; i++)
		{
			std::string k = r->u.list->keys[i];
			mpv_node& v = r->u.list->values[i];
			if (v.format == MPV_FORMAT_INT64 && k == "w") w = v.u.int64;
			else if (v.format == MPV_FORMAT_INT64 && k == "h") h = v.u.int64;
			else if (v.format == MPV_FORMAT_INT64 && k == "stride") stride = v.u.int64;
			else if (v.format == MPV_FORMAT_STRING && k == "format") format = v.u.string;
			else if (v.format == MPV_FORMAT_BYTE_ARRAY && k == "data")
			{
				px = (const unsigned char*)v.u.ba->data;
				size = v.u.ba->size;
			}
		}
		if (w <= 0 || h <= 0 || !px || format != "bgr0" || size < (size_t)(stride * (h - 1) + w * 4)) return;
		// the whole photo, or (some builds) already turned by its metadata
		// (by its shape: the screenshot is after the vf's scale, the size isn't the photo's)
		thumbTurnedByMpv = fileTurn % 180 == 90 && photoW != photoH && (w > h) != (photoW > photoH);
		double k = std::max({1.0, w / (double)THUMB_MAX_W, h / (double)THUMB_MAX_H});
		int tw = std::max(1, (int)(w / k)), th = std::max(1, (int)(h / k));
		thumb.assign((size_t)tw * th * 4, 255);
		// each pixel the mean of four of the photo's, spread over its share
		for (int y = 0; y < th; y++)
			for (int x = 0; x < tw; x++)
			{
				unsigned sum[3] = {};
				for (int sy = 0; sy < 2; sy++)
					for (int sx = 0; sx < 2; sx++)
					{
						int64_t ix = std::min(w - 1, (int64_t)((x + 0.25 + sx * 0.5) * w / tw));
						int64_t iy = std::min(h - 1, (int64_t)((y + 0.25 + sy * 0.5) * h / th));
						const unsigned char* q = px + iy * stride + ix * 4;
						for (int c = 0; c < 3; c++) sum[c] += q[c];
					}
				unsigned char* o = &thumb[((size_t)y * tw + x) * 4];
				for (int c = 0; c < 3; c++) o[c] = (unsigned char)(sum[c] / 4);
			}
		thumbW = tw;
		thumbH = th;
		thumbId++;
	}

	int Player::thumbTurn() const
	{
		return (exifTurn + view.rotate + (thumbTurnedByMpv ? 0 : fileTurn)) % 360;
	}

	PhotoScale Player::photoScaleNow() const
	{
		if (!photoW) return {view.zoom, view.zoom};
		return photoScale(photoWidth() / (double)photoHeight(), PANEL_ASPECT, view.fill, view.zoom);
	}

	void Player::applyPhotoView()
	{
		double zoom = 0, px = 0, py = 0;
		int64_t turn = 0;
		if (isPhoto)
		{
			// mpv's zoom is over its Fit: Fill's own factor times the view's
			PhotoScale fit = photoW ? photoScale(photoWidth() / (double)photoHeight(), PANEL_ASPECT, false, 1) : PhotoScale{1, 1};
			zoom = std::log2(photoScaleNow().x / fit.x);
			px = view.panX;
			py = view.panY;
			turn = (exifTurn + view.rotate) % 360;
		}
		mpv_set_property_async(mpv, 0, "video-zoom", MPV_FORMAT_DOUBLE, &zoom);
		mpv_set_property_async(mpv, 0, "video-pan-x", MPV_FORMAT_DOUBLE, &px);
		mpv_set_property_async(mpv, 0, "video-pan-y", MPV_FORMAT_DOUBLE, &py);
		mpv_set_property_async(mpv, 0, "video-rotate", MPV_FORMAT_INT64, &turn);
	}

	void Player::setPhotoView(double zoom, double panX, double panY)
	{
		view.zoom = std::clamp(zoom, 1.0, MAX_ZOOM);
		PhotoScale s = photoScaleNow();
		double mx = maxPan(s.x), my = maxPan(s.y);
		view.panX = std::clamp(panX, -mx, mx);
		view.panY = std::clamp(panY, -my, my);
		panShareX = mx > 0 ? view.panX / mx : 0;
		panShareY = my > 0 ? view.panY / my : 0;
		applyPhotoView();
	}

	void Player::rotatePhoto()
	{
		view.rotate = (view.rotate + 90) % 360;
		// the same zoom, centred (the place was across the other way)
		setPhotoView(view.zoom, 0, 0);
	}

	void Player::togglePhotoFill()
	{
		view.fill = !view.fill;
		setPhotoView(view.zoom, view.panX, view.panY);
	}

	void Player::unpause()
	{
		prop("pause", "no");
		wakeHeld();
	}

	void Player::holdPaused()
	{
		pause();
		heldUnplayed = true;
	}

	void Player::wakeHeld()
	{
		if (!heldUnplayed || !mpv) return;
		heldUnplayed = false;
		const char* cmd[] = {"ao-reload", nullptr};
		mpv_command_async(mpv, 0, cmd);
		fprintf(stderr, "audio: %s held paused for a page: the sound output reopened as it plays\n", baseName(currentPath()).c_str());
	}

	void Player::hideVideo(bool hide)
	{
		// a video under a photo goes on decoding, its frames taken by main's hidden target (drainUnder) and never shown:
		// closing the photo shows it at once. (Its picture switched off instead, vid=no, came back only after a seek's
		// buffering, or seconds of black till the next keyframe: the owner chose the CPU over the wait)
		if (hide == hidingVideo || !mpv) return;
		hidingVideo = hide;
		if (!hide && vidOff)
		{
			prop("vid", "auto");
			vidOff = false;
		}
		if (playing() && !isAudio && !isPhoto)
			fprintf(stderr, "photo: %s %s\n", baseName(currentPath()).c_str(), hide ? "decodes on under the photo, unseen" : "shown again");
	}

	void Player::pause()
	{
		prop("pause", "yes");
	}

	void Player::watchLog(const char* name, const char* value) const
	{
		if (!graphChangeAt) return;
		uint64_t ago = ticks() - graphChangeAt;
		if (ago > WATCH_MS) return;
		fprintf(stderr, "mpv: %s -> %s (+%llu ms after graph)\n", name, *value ? value : "event", (unsigned long long)ago);
	}

	void Player::logPlayPressed(const char* sessionState) const
	{
		fprintf(stderr, "session: play pressed: session %s, mpv pause=%s, core-idle=%s, idle-active=%s, eof-reached=%s, graph #%s\n", sessionState,
			seenPause.c_str(), seenCoreIdle.c_str(), seenIdleActive.c_str(), seenEof.c_str(), graphHash.empty() ? "-" : graphHash.c_str());
	}

	void Player::togglePause()
	{
		if (heldUnplayed && paused) { unpause(); return; }
		const char* cmd[] = {"cycle", "pause", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	void Player::pauseNow()
	{
		// libmpv's calls are thread-safe; the pause property's change reaches pollEvents as any other
		int yes = 1;
		if (mpv) mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &yes);
	}

	void Player::seekRelative(double seconds)
	{
		std::string s = std::to_string(seconds);
		const char* cmd[] = {"seek", s.c_str(), "relative", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	void Player::scrub(double fraction)
	{
		std::string s = std::to_string(fraction * 100.0);
		const char* cmd[] = {"seek", s.c_str(), "absolute-percent+keyframes", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	void Player::seekFraction(double fraction)
	{
		std::string s = std::to_string(fraction * 100.0);
		const char* cmd[] = {"seek", s.c_str(), "absolute-percent+exact", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	void Player::setSpeed(double s)
	{
		mpv_set_property_async(mpv, 0, "speed", MPV_FORMAT_DOUBLE, &s);
	}

	void Player::selectTrack(TrackType type, int64_t id)
	{
		const char* name = type == TrackType::Audio ? "aid" : type == TrackType::Sub ? "sid" : "vid";
		if (type == TrackType::Audio) syncEq(true);	// a track change rebuilds the chain from af
		prop(name, id > 0 ? std::to_string(id) : "no");
		std::string lang;
		for (auto& t : tracks)
			if (t.type == type && t.id == id) lang = t.lang;
		if (type == TrackType::Audio && settings.rememberAudio && !lang.empty()) resume.lastAudioLang = lang;
		if (type == TrackType::Sub && settings.rememberSubs)
		{
			resume.lastSubOff = id <= 0;
			if (id > 0 && !lang.empty()) resume.lastSubLang = lang;
		}
	}

	void Player::setAspect(AspectMode m)
	{
		aspect = m;
		for (auto& p : aspectProperties(m)) prop(p.first, p.second);
	}

	void Player::setVideoAdjust(int i, double v)
	{
		i = std::clamp(i, 0, 3);
		v = std::clamp(v, -100.0, 100.0);
		videoAdj[i] = v;
		static const char* NAMES[4] = {"brightness", "contrast", "saturation", "gamma"};
		mpv_set_property_async(mpv, 0, NAMES[i], MPV_FORMAT_DOUBLE, &v);
		fprintf(stderr, "video: adjust %s %.0f\n", NAMES[i], v);
	}

	void Player::writeFile(const std::string& path, const std::string& text)
	{
		// Durable (synced to the card: a power cut leaves the old file or the new one whole). A sync can take tens
		// of ms on an SD card and this runs on the main thread, so a file written again within a second waits for
		// the loop's flushWrites (and for shutdown): the first save of a burst goes at once, the last value of the
		// burst always lands. Settings flicked through, Next pressed over and over: at most one sync a second each.
		uint64_t now = ticks();
		auto last = lastWritten.find(path);
		if (last != lastWritten.end() && now - last->second < WRITE_GAP_MS)
		{
			pendingWrites[path] = {text, last->second + WRITE_GAP_MS};
			return;
		}
		pendingWrites.erase(path);
		writeFileDurable(path, text);
		lastWritten[path] = now;
	}

	void Player::flushWrites(bool all)
	{
		if (pendingWrites.empty()) return;
		uint64_t now = ticks();
		for (auto it = pendingWrites.begin(); it != pendingWrites.end();)
		{
			if (!all && now < it->second.dueAt) { ++it; continue; }
			writeFileDurable(it->first, it->second.text);
			lastWritten[it->first] = now;
			it = pendingWrites.erase(it);
		}
	}

	void Player::setSettings(const Settings& s, bool persist)
	{
		bool lookChanged = false;
		for (int k = 0; k < ENCODING; k++)
			if (s.style.pick[k] != settings.style.pick[k]) lookChanged = true;
		for (auto& p : subStyleProperties(s.style)) prop(p.first.c_str(), p.second);
		// A subtitle showing now keeps the look its track was made with: the new sub-* values reached a track mpv made
		// after them, so a style change only showed once the file was reopened (a track made again). Make the showing
		// one again at once: a local external file by mpv's sub-reload, an embedded track by choosing it again off and
		// on, by its id as a track is chosen (both async, as every set). Styled subtitles (ASS/SSA) go the same way:
		// sub-ass-override stays mpv's "scale" (the script's sizes and colours), but a font it names that the card
		// lacks falls back to the style's, so it too is made again. A network file's subtitle isn't fetched again at
		// each step; pictures (PGS, VobSub) have no style to change.
		if (lookChanged)
		{
			const Track* sub = selected(TrackType::Sub);
			if (sub && !imageSubtitleCodec(sub->codec))
			{
				bool local = current >= 0 && current < (int)playlist.size() && !isNetPath(playlist[current]);
				if (sub->external && local)
				{
					fprintf(stderr, "subtitles: the style changed; its file read again\n");
					const char* cmd[] = {"sub-reload", nullptr};
					mpv_command_async(mpv, 0, cmd);
				}
				else if (!sub->external)
				{
					fprintf(stderr, "subtitles: the style changed; track %lld made again\n", (long long)sub->id);
					prop("sid", "no");
					prop("sid", std::to_string(sub->id));
				}
			}
		}
		if (s.style.pick[ENCODING] != settings.style.pick[ENCODING])
		{
			setSubtitleCharset(subEncodingCharset(s.style));
			// a subtitle file showing now, read again in the new code page (mpv reads one when it's added)
			const Track* sub = selected(TrackType::Sub);
			if (sub && sub->external)
			{
				const char* cmd[] = {"sub-reload", nullptr};
				mpv_command_async(mpv, 0, cmd);
			}
		}
		bool displayChanged = s.musicShow != settings.musicShow || s.visualizer != settings.visualizer || s.vizFps != settings.vizFps;
		bool loopChanged = s.loop != settings.loop;
		bool soundChanged = s.eqOn != settings.eqOn || s.eqGains != settings.eqGains || s.soundFx != settings.soundFx;
		settings = s;
		if (displayChanged && isAudio && loaded)
		{
			// at once: no graph to wait for (the song's sound chosen again, its picture off; the native style follows in
			// syncNative)
			applyAudioDisplay();
		}
		if (soundChanged) syncEq(false);
		if (loopChanged && isAudio) prop("loop-file", s.loop == LOOP_ONE ? "inf" : "no");
		// switched off: nothing kept from now, and what was kept goes
		if (!s.keepHistory) clearHistory();
		// the network's switched off: its files kept so far go (the card's stay)
		else if (!s.keepNetHistory)
		{
			size_t before = played.items().size();
			played.removeIf([](const std::string& p) { return isNetPath(p); });
			recentSer++;
			if (played.items().size() != before && !opts.stateDir.empty())
				writeFile(opts.stateDir + "/history.tsv", played.serialize());
		}
		// switched off: the folder kept so far goes too
		if (!s.rememberFolder && !resume.lastFolder.empty()) setLastFolder("");
		eqDirty = !persist;
		if (persist && !opts.stateDir.empty()) writeFile(opts.stateDir + "/settings.conf", serializeSettings(s));
	}

	void Player::checkDisplay()
	{
		if (!displayDueAt || ticks() < displayDueAt) return;
		displayDueAt = 0;
		if (isAudio && loaded) applyAudioDisplay();
	}

	void Player::checkCpu()
	{
		static const bool on = getenv("BP_CPU_LOG") && !strcmp(getenv("BP_CPU_LOG"), "1");
		if (!on) return;
		// from the file's start (or the end of a pause): the 30 s measured are all playing
		if (!playing() || !started || paused)
		{
			cpuAt = 0;
			return;
		}
		rusage u;
		getrusage(RUSAGE_SELF, &u);
		double used = u.ru_utime.tv_sec + u.ru_utime.tv_usec / 1e6 + u.ru_stime.tv_sec + u.ru_stime.tv_usec / 1e6;
		uint64_t now = ticks();
		if (!cpuAt)
		{
			cpuAt = now;
			cpuSeconds = used;
			return;
		}
		if (now - cpuAt < 30000) return;
		// of one core (the handheld has four: 400% is all of them)
		fprintf(stderr, "cpu: %d%% over 30 s (%s, eq %s, effect %s)\n", (int)std::lround((used - cpuSeconds) * 100000 / (now - cpuAt)),
			baseName(currentPath()).c_str(), isAudio && !eqPart().empty() ? "on" : "off",
			isAudio ? std::string(soundFxKey(settings.soundFx)).c_str() : "none");
		cpuAt = now;
		cpuSeconds = used;
	}

	std::string Player::eqPart() const
	{
		return isAudio && settings.eqOn ? eqFilterString(settings.eqGains) : "";
	}

	std::string Player::fxPart()
	{
		SoundFx fx = isAudio ? settings.soundFx : SoundFx::None;
		if (fx == SoundFx::None) return "";
		std::string key = soundFxKey(fx);
		// a reverb's impulse response, a file amovie reads: beside the settings (else, none kept or a folder a filter
		// graph can't name, in /tmp), written when it isn't there as this build makes it. Checked once a run
		std::string dir = lavfiSafePath(opts.stateDir) ? opts.stateDir : "/tmp/banana-player";
		std::string path = dir + "/ir-" + key + ".wav";
		if (irWritten.find(" " + key + " ") != std::string::npos) return soundFxFilter(fx, path);
		std::string wav = soundFxIrWav(fx);
		if (wav.empty()) return soundFxFilter(fx, "");	// not a reverb: no file
		if (readFile(path) != wav)
		{
			mkdir(dir.c_str(), 0755);
			writeFileAtomic(path, wav);	// (read back at once, and made again if lost: no sync, no wait)
			if (readFile(path) != wav)
			{
				// an af that can't open its file would take the sound with it: the music plays dry
				fprintf(stderr, "banana-player: %s couldn't be written: no sound effect\n", path.c_str());
				return "";
			}
		}
		irWritten += (irWritten.empty() ? " " : "") + key + " ";
		return soundFxFilter(fx, path);
	}

	void Player::sendEqCommand(const char* command, const std::string& arg, const std::string& target)
	{
		const char* cmd[] = {"af-command", "bpeq", command, arg.c_str(), target.c_str(), nullptr};
		eqInFlight++;
		mpv_command_async(mpv, EQ_REPLY, cmd);
	}

	void Player::syncEq(bool rebuilding)
	{
		std::string eq = eqPart(), fx = fxPart(), want = joinAf(eq, fx);
		// the filters themselves as they are (the EQ in, the same effect) and no rebuild coming: each band that moved,
		// by af-command, heard at once with no gap in the sound (mpv's af stays the string it was set to). Else af as
		// a whole, unless it's the one mpv has (a rebuild starts from it; af set to itself does nothing anyway)
		bool live = !rebuilding && !eq.empty() && afSet.find("@bpeq:") != std::string::npos && fx == afFxSet;
		if (!live)
		{
			if (want != afSet) prop("af", want);
		}
		else
		{
			for (int i = 0; i < EQ_BANDS; i++)
				if (settings.eqGains[i] != eqLive[i])
				{
					char g[32];
					snprintf(g, sizeof g, "%.1f", settings.eqGains[i] + 0.0);
					sendEqCommand("g", g, eqBandTarget(i));
				}
			std::string pre = eqPreGainArg(settings.eqGains);
			if (pre != eqPreGainArg(eqLive)) sendEqCommand("volume", pre, "volume@pre");
		}
		eqLive = settings.eqGains;
	}

	void Player::commitEq()
	{
		// af made what plays: what mpv shows, and what a rebuild starts from
		std::string want = afWant();
		if (want != afSet) prop("af", want);
		eqLive = settings.eqGains;
		if (eqDirty && !opts.stateDir.empty()) writeFile(opts.stateDir + "/settings.conf", serializeSettings(settings));
		eqDirty = false;
	}

	void Player::writeEqPresets()
	{
		eqSerial++;
		if (!opts.stateDir.empty()) writeFile(opts.stateDir + "/eq-presets.tsv", eqStore.serialize());
	}

	// the user preset a name stands for (as the store keeps it: trimmed), or none
	static const EqPreset* userPreset(const std::vector<EqPreset>& user, const std::string& name)
	{
		for (auto& u : user)
			if (checkEqPresetName(name, {u}) == EqNameError::Taken) return &u;
		return nullptr;
	}

	EqNameError Player::saveEqPreset(const std::string& name, const EqGains& gains, bool overwrite)
	{
		if (!overwrite)
		{
			EqNameError e = checkEqPresetName(name, eqStore.user());
			if (e != EqNameError::None) return e;
		}
		EqNameError e = eqStore.put(name, gains);
		if (e != EqNameError::None) return e;
		writeEqPresets();
		// the one playing now
		const EqPreset* p = userPreset(eqStore.user(), name);
		Settings s = settings;
		s.eqPreset = p ? p->name : name;
		s.eqOrigin.clear();
		s.eqGains = p ? p->gains : gains;
		setSettings(s);
		return EqNameError::None;
	}

	EqNameError Player::renameEqPreset(const std::string& from, const std::string& to)
	{
		EqNameError e = eqStore.rename(from, to);
		if (e != EqNameError::None) return e;
		writeEqPresets();
		const EqPreset* p = userPreset(eqStore.user(), to);
		std::string name = p ? p->name : to;
		if (settings.eqPreset == from || settings.eqOrigin == from)
		{
			Settings s = settings;
			if (s.eqPreset == from) s.eqPreset = name;
			if (s.eqOrigin == from) s.eqOrigin = name;
			setSettings(s);
		}
		return EqNameError::None;
	}

	bool Player::deleteEqPreset(const std::string& name)
	{
		if (!eqStore.remove(name)) return false;
		writeEqPresets();
		if (settings.eqPreset == name || settings.eqOrigin == name)
		{
			Settings s = settings;
			if (s.eqPreset == name) s.eqPreset.clear();	// Custom: the same curve, unsaved
			s.eqOrigin.clear();
			setSettings(s);
		}
		return true;
	}

	void Player::putImage(int id, const LevelCardImage& card, int x, int y)
	{
		if (!card.ok() || id < 0 || id >= OV_COUNT) return;
		// overlay-add reads premultiplied BGRA from our memory ("&" and its address) when the command runs, which is
		// later (async: never a wait on mpv here): the pixels are kept till its answer, and the ones shown till the
		// card is replaced or removed
		auto px = std::make_shared<std::vector<uint8_t>>(premultipliedBgra(card));
		uint64_t serial = ++levelSerial;
		levelPixels[serial] = px;
		if (!shownSerial[id] || id != OV_LOAD) fprintf(stderr, "card: show overlay %d %dx%d at %d,%d\n", id, card.w, card.h, x, y);
		shownSerial[id] = serial;
		{
			const float pw = topW > 0 ? topW : 960, ph = topH > 0 ? topH : 720;
			cardAt[id] = {x * 640 / pw, y * 480 / ph, (x + card.w) * 640 / pw, (y + card.h) * 480 / ph, 0};
		}
		std::string ids = std::to_string(id), xs = std::to_string(x), ys = std::to_string(y), src = "&" + std::to_string((uintptr_t)px->data()),
			ws = std::to_string(card.w), hs = std::to_string(card.h), st = std::to_string(card.w * 4);
		const char* add[] = {"overlay-add", ids.c_str(), xs.c_str(), ys.c_str(), src.c_str(), "0", "bgra", ws.c_str(), hs.c_str(),
			st.c_str(), nullptr};
		mpv_command_async(mpv, LEVEL_REPLY + serial, add);
	}

	std::vector<VizRect> Player::cardsUp() const
	{
		std::vector<VizRect> r;
		for (int id = 0; id < OV_COUNT; id++)
			if (shownSerial[id]) r.push_back(cardAt[id]);
		return r;
	}

	void Player::dropImage(int id)
	{
		if (id < 0 || id >= OV_COUNT || !shownSerial[id]) return;
		std::string ids = std::to_string(id);
		const char* rm[] = {"overlay-remove", ids.c_str(), nullptr};
		// (its pixels go with this command's answer: an add still queued before it may read them)
		uint64_t serial = ++levelSerial;
		shownSerial[id] = 0;
		if (id != OV_LOAD) fprintf(stderr, "card: drop overlay %d\n", id);
		mpv_command_async(mpv, LEVEL_REPLY + serial, rm);
	}

	void Player::showLevelCard(const LevelCardImage& card, int ms)
	{
		if (!card.ok()) return;
		const int pw = topW > 0 ? topW : 960, ph = topH > 0 ? topH : 720;
		int x, y;
		levelCardPos(pw, ph, card, x, y);
		putImage(OV_LEVEL, card, x, y);
		cardMs = ms;
	}

	void Player::showLevel(const std::string& label, int percent)
	{
		// the one level card (levelcard.h: the readers show the same picture over their pages), as an mpv image
		// overlay at the panel's own pixels, top right; it takes the place of a chapter or equalizer card
		const int pw = topW > 0 ? topW : 960, ph = topH > 0 ? topH : 720;
		LevelCardImage card = drawLevelCard(opts.fontsDir, ph, label, percent, levelTop(label, settings.volumeMax), levelIcon(label));
		int x, y;
		levelCardPos(pw, ph, card, x, y);
		putImage(OV_LEVEL, card, x, y);
	}

	void Player::levelReplied(uint64_t serial)
	{
		// the core has run every command up to this one: the pixels of earlier cards aren't read any more (the ones
		// shown are kept: they may be read again till replaced or removed)
		for (auto it = levelPixels.begin(); it != levelPixels.end() && it->first <= serial;)
		{
			bool shown = false;
			for (uint64_t sh : shownSerial) shown = shown || sh == it->first;
			it = shown ? std::next(it) : levelPixels.erase(it);
		}
	}

	void Player::themeChanged()
	{
		int sent = 0;
		if (idleShown) { showIdle(true); sent++; }
		if (isAudio && loaded)
		{
			// the lines' colours and mpv's ground at once (no graph holds the ground; the native styles read the theme
			// each frame)
			updateOverlay();
			sent++;
		}
		// (a Loading card is drawn again at its next step, every 100 ms; a level card lasts a second or two)
		fprintf(stderr, "theme: top panel overlays sent again: %d\n", sent);
	}

	void Player::showIdle(bool on)
	{
		idleShown = on;
		// nothing playing: app UI on the theme's ground (docs/redesign Themes › The top screen), as a song's modes;
		// what plays next sets its own (a video, a photo: black; a song: its mode's)
		if (on) setGround(true, "nothing plays");
		if (!on)
		{
			const char* cmd[] = {"osd-overlay", "3", "none", "", nullptr};
			mpv_command_async(mpv, 0, cmd);
			const char* rm[] = {"overlay-remove", "0", nullptr};
			mpv_command_async(mpv, 0, rm);
			return;
		}
		// the icon as ASS drawings, a run of pixels a rectangle, one event a colour; on a 960x720 canvas
		const int px = 6, x0 = 480 - 16 * px, y0 = 110;
		std::string ev;
		// the app's picture (assets/icons/app.png beside the fonts) as an mpv image overlay at the panel's own pixels,
		// decoded once at that size (smooth, as drawn); the generated logo when it can't be read
		bool picture = false;
		{
			const int pw = topW > 0 ? topW : 960, ph = topH > 0 ? topH : 720;
			const int size = (int)std::lround(240.0 * ph / 720), ix = pw / 2 - size / 2, iy = (int)std::lround(84.0 * ph / 720);
			if (idleIconPx != size && !opts.fontsDir.empty())
			{
				PageImage img = decodePageFile(opts.fontsDir + "/../icons/app.png", size);
				if (img.ok() && img.w == size)
				{
					// mpv's overlay-add takes premultiplied BGRA from a file it maps: written once, kept while shown
					std::vector<uint8_t> bgra((size_t)img.w * img.h * 4);
					for (size_t p = 0; p < (size_t)img.w * img.h; p++)
					{
						const uint8_t* c = img.rgba.data() + p * 4;
						uint8_t a = c[3];
						bgra[p * 4 + 0] = (uint8_t)(c[2] * a / 255);
						bgra[p * 4 + 1] = (uint8_t)(c[1] * a / 255);
						bgra[p * 4 + 2] = (uint8_t)(c[0] * a / 255);
						bgra[p * 4 + 3] = a;
					}
					idleIconFile = (opts.stateDir.empty() ? std::string("/tmp") : opts.stateDir) + "/.idle-icon.bgra";
					if (FILE* f = fopen(idleIconFile.c_str(), "wb"))
					{
						bool ok = fwrite(bgra.data(), 1, bgra.size(), f) == bgra.size();
						if (fclose(f) == 0 && ok) { idleIconPx = size; idleIconH = img.h; }
					}
				}
			}
			if (idleIconPx == size)
			{
				std::string xs = std::to_string(ix), ys = std::to_string(iy), ws = std::to_string(size), hs = std::to_string(idleIconH),
					st = std::to_string(size * 4);
				const char* add[] = {"overlay-add", "0", xs.c_str(), ys.c_str(), idleIconFile.c_str(), "0", "bgra", ws.c_str(), hs.c_str(),
					st.c_str(), nullptr};
				mpv_command_async(mpv, 0, add);
				picture = true;
			}
		}
		if (!picture)
		{
			const struct { char c; std::string colour; } inks[] = {{'.', ass(BUTTON)}, {'Y', ass(ACCENT)}, {'W', ass(TEXT)}};
			for (auto& ink : inks)
			{
				std::string path;
				for (int y = 0; y < 32; y++)
					for (int x = 0; x < 32;)
					{
						if (LOGO[y][x] != ink.c) { x++; continue; }
						int e = x;
						while (e < 32 && LOGO[y][e] == ink.c) e++;
						char r[96];
						snprintf(r, sizeof r, "m %d %d l %d %d %d %d %d %d ", x * px, y * px, e * px, y * px, e * px, (y + 1) * px, x * px, (y + 1) * px);
						path += r;
						x = e;
					}
				char head[128];
				snprintf(head, sizeof head, "{\\an7\\pos(%d,%d)\\bord0\\shad0\\1c%s\\p1}", x0, y0, ink.colour.c_str());
				ev += head + path + "{\\p0}\n";
			}
		}
		// in the main tokens, on BG (a light theme's top screen is light)
		ev += "{\\an8\\pos(480,330)\\bord0\\shad0\\fnPixelify Sans\\fs72\\1c" + ass(ACCENT) + "}Banana Player\n";
		ev += "{\\an8\\pos(480,420)\\bord0\\shad0\\fnInter\\fs30\\1c" + ass(DIM) + "}Pick a video or a song on the bottom screen";
		const char* cmd[] = {"osd-overlay", "3", "ass-events", ev.c_str(), "960", "720", "0", "no", "no", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	void Player::showElsewhere(bool on, bool playing, const std::string& where, bool waiting, int hint, const char* const* glyph, int glyphH)
	{
		if (!on)
		{
			dropImage(OV_STATE);
			return;
		}
		// the state card (levelcard.h), the whole top panel: the page's mark in the tile, who plays, what, and what to press
		StateCardSpec spec;
		spec.icon = {glyph, glyphH};
		spec.title = std::string(waiting ? "Waiting for " : playing ? "Playing in " : "Paused in ") + where;
		spec.line = waiting ? "Tap Play on that page to start the sound" : baseName(currentPath());
		if (waiting) { spec.key = "B"; spec.hint = "play on the handheld instead"; }
		// (only what works on the page shown below: A is the player's; elsewhere, where to go for it)
		else if (hint == 1) { spec.key = "A"; spec.hint = "hold to play it here"; }
		else if (hint == 2) spec.hint = "Go to the player to play it here";
		// a song with the equalizer or an effect on: the browser plays it without them (they're the handheld's)
		if (isAudio && (settings.eqOn || settings.soundFx != SoundFx::None)) spec.foot = "Your equalizer and effects don't apply in the browser";
		const int pw = topW > 0 ? topW : 960, ph = topH > 0 ? topH : 720;
		putImage(OV_STATE, drawStateCard(opts.fontsDir, pw, ph, spec), 0, 0);
	}

	void Player::wakeOutput()
	{
		wakeLoads++;
		const char* cmd[] = {"loadfile", "memory://", "replace", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	void Player::clearChapters()
	{
		chapters.clear();
		chapter = -1;
		chapterSerial++;
		pendingOn = false;
	}

	int Player::currentChapter() const
	{
		return pendingOn && ticks() - pendingAt < 1000 ? pendingChapter : chapter;
	}

	void Player::seekChapter(int index)
	{
		if (index == CHAPTER_NONE || playlist.empty()) return;
		pendingOn = true;
		pendingChapter = index;
		pendingAt = ticks();
		if (index == CHAPTER_FILE_START)
		{
			const char* cmd[] = {"seek", "0", "absolute", nullptr};
			mpv_command_async(mpv, 0, cmd);
		}
		else prop("chapter", std::to_string(index));
		// a song's chapter line follows the jump at once, not after mpv's answer
		if (isAudio && loaded) updateOverlay();
	}

	int Player::stepChapter(int dir, double at)
	{
		if (!loaded || !error.empty() || isPhoto || playlist.empty()) return CHAPTER_NONE;
		if (chapters.empty())
		{
			osd("No chapters in this file");
			return CHAPTER_NONE;
		}
		int cur = currentChapter();
		if (at >= 0)
		{
			cur = -1;
			for (int i = 0; i < (int)chapters.size() && chapters[i].time <= at; i++) cur = i;
		}
		if (cur >= (int)chapters.size()) cur = (int)chapters.size() - 1;
		// no earlier than the chapter's start: the position can lag a jump, and would read as before it
		double pos = cur >= 0 ? std::max(at >= 0 ? at : position, chapters[cur].time) : 0;
		int target = chapterTarget(chapters, cur, pos, dir);
		if (target == CHAPTER_NONE)
			showChapterCard(dir < 0 ? 0 : (int)chapters.size() - 1, dir < 0 ? -1 : 1);
		else
		{
			seekChapter(target);
			if (target >= 0) showChapterCard(target);
			else osd("Start of the file");
		}
		return target;
	}

	// a card's text: libass has no way to say a brace or a backslash literally, so they become look-alikes
	static std::string cardText(const std::string& s, size_t maxChars)
	{
		std::string out;
		size_t chars = 0;
		for (size_t i = 0; i < s.size(); i++)
		{
			unsigned char c = (unsigned char)s[i];
			if ((c & 0xC0) != 0x80)
			{
				if (chars == maxChars) { out += "\xe2\x80\xa6"; break; }
				chars++;
			}
			out += c == '{' ? '(' : c == '}' ? ')' : c == '\\' ? '/' : c == '\n' || c == '\r' ? ' ' : (char)c;
		}
		return out;
	}

	void Player::showChapterCard(int index, int edge)
	{
		int n = (int)chapters.size();
		if (index < 0 || index >= n || playlist.empty()) return;
		std::string of = "Chapter " + std::to_string(index + 1) + " of " + std::to_string(n);
		// the note over the first or last chapter, after the count
		std::string note = edge < 0 ? " \xc2\xb7 This is the first chapter" : edge > 0 ? " \xc2\xb7 This is the last chapter" : "";
		bool untitled = chapterUntitled(chapters[index].title);
		std::vector<float> starts;
		if (duration > 0)
			for (auto& c : chapters) starts.push_back((float)(c.time / duration));
		const int ph = topH > 0 ? topH : 720;
		// (untitled: the count is the value, nothing above it but the word)
		LevelCardImage card = untitled ? drawLabelCard(opts.fontsDir, ph, "Chapter" + note, std::to_string(index + 1) + " of " + std::to_string(n), starts, index)
									   : drawLabelCard(opts.fontsDir, ph, of + note, chapters[index].title, starts, index);
		showLevelCard(card, 2000);
	}

	void Player::showEqCard()
	{
		bool on = settings.eqOn, fx = settings.soundFx != SoundFx::None;
		std::string preset = settings.eqPreset.empty() ? "Custom" : settings.eqPreset, effect = soundFxName(settings.soundFx);
		// "Rock \xc2\xb7 On", "Off", "Rock \xc2\xb7 Concert hall", "EQ off \xc2\xb7 Cave"
		std::string value = !fx ? (on ? preset + " \xc2\xb7 On" : "Off") : (on ? preset : "EQ off") + " \xc2\xb7 " + effect;
		showLevelCard(drawLabelCard(opts.fontsDir, topH > 0 ? topH : 720, "Equalizer", value), 1500);
	}

	void Player::showNoteCard(const char* const* rows, int h, const std::string& line1, const std::string& line2, bool danger)
	{
		const int pw = topW > 0 ? topW : 960, ph = topH > 0 ? topH : 720;
		LevelCardImage card = drawNoteCard(opts.fontsDir, ph, CardGlyph{rows, h}, line1, line2, danger);
		const bool overVideo = !isAudio && !isPhoto && !playlist.empty();
		int x, y;
		noteCardPos(pw, ph, card, x, y, overVideo);	// a video: in the middle, off the subtitles and the level card
		putImage(OV_NOTE, card, x, y);
		// over a video it's in the middle of the picture: briefer
		noteUntil = ticks() + (overVideo ? 1500 : 3000);
		cardMs = overVideo ? 1500 : 3000;	// (the caller draws the panel while it is up)
	}

	void Player::hideLevel()
	{
		dropLevelCard();
	}

	void Player::osd(const std::string& text, int ms)
	{
		// every short note goes through the note (levelcard.h); a failure's in red
		bool bad = text.find("ouldn't") != std::string::npos || text.find("an't ") != std::string::npos || text.find("ailed") != std::string::npos;
		const int pw = topW > 0 ? topW : 960, ph = topH > 0 ? topH : 720;
		LevelCardImage card = drawNoteCard(opts.fontsDir, ph, CardGlyph{}, text, "", bad);
		const bool overVideo = !isAudio && !isPhoto && !playlist.empty();
		int x, y;
		noteCardPos(pw, ph, card, x, y, overVideo);	// a video: in the middle, off the subtitles and the level card
		putImage(OV_NOTE, card, x, y);
		// over a video it's in the middle of the picture: briefer, except a note asked for longer (a browser signing in: 6 s)
		ms = overVideo && ms <= 3000 ? 1500 : std::max(ms, 3000);
		noteUntil = ticks() + ms;
		cardMs = ms;
	}

	void Player::saveResume()
	{
		commitEq();	// (the Equalizer page left open as a file ends, or as the player quits)
		// a photo has no place to come back to
		if (!loaded || playlist.empty() || opts.stateDir.empty() || isPhoto) return;
		ResumeEntry e;
		e.path = playlist[current];
		// played to the end (or as good as): next time from the start
		if (settings.rememberPosition && !isAudio && !finished && worthResuming(position, duration)) e.position = position;
		// the tracks as last seen open (at the end, mpv's list is already gone: it isn't "subtitles off")
		if (settings.rememberAudio && seenAid > 0) e.aid = seenAid;
		if (settings.rememberSubs && seenSid >= 0) e.sid = seenSid;
		e.duration = duration;	// (Home's Recent cards: how far into it)
		resume.put(e);
		recentSer++;
		writeFile(opts.stateDir + "/resume.tsv", resume.serialize());
	}

	void Player::stop()
	{
		saveResume();
		playlist.clear();
		current = 0;
		loaded = finished = false;
		error.clear();
		errorNet = false;
		title.clear();
		tracks.clear();
		clearChapters();
		position = duration = -1;
		const char* cmd[] = {"stop", nullptr};
		mpv_command_async(mpv, 0, cmd);
		isAudio = isPhoto = isNet = false;
		pausedForCache = false;
		seekingSince = 0;
		if (opts.net) opts.net->cancelOpens();
		netWait = NetWait();	// (its loadfile never sent: nothing of it to end)
		ghost = -1;
		src = PlaySource();
		skipNote.clear();
		checkLoading();	// the spinner down
		applyPhotoView();
		updateOverlay();	// the title off the top panel
		showIdle(true);
	}

	void Player::pathsMoved(const std::string& from, const std::string& to)
	{
		if (opts.stateDir.empty()) return;
		if (played.moved(from, to))
		{
			recentSer++;
			writeFile(opts.stateDir + "/history.tsv", played.serialize());
		}
		if (resume.moved(from, to)) writeFile(opts.stateDir + "/resume.tsv", resume.serialize());
	}

	void Player::addRecent(const std::string& path)
	{
		if (!settings.keepHistory || incognito) return;
		played.add(path);
		recentSer++;
		if (!opts.stateDir.empty()) writeFile(opts.stateDir + "/history.tsv", played.serialize());
	}

	void Player::clearHistory()
	{
		played.clear();
		recentSer++;
		if (!opts.stateDir.empty())
		{
			pendingWrites.erase(opts.stateDir + "/history.tsv");	// (a save waiting would bring it back)
			unlink((opts.stateDir + "/history.tsv").c_str());
		}
	}

	void Player::removeFromHistory(const std::string& path)
	{
		if (!played.remove(path)) return;
		recentSer++;
		if (!opts.stateDir.empty()) writeFile(opts.stateDir + "/history.tsv", played.serialize());
	}

	void Player::forgetPosition(const std::string& path)
	{
		const ResumeEntry* e = resume.find(path);
		if (!e || e->position < 0) return;
		ResumeEntry keep = *e;
		keep.position = -1;
		resume.put(keep);
		recentSer++;
		if (!opts.stateDir.empty()) writeFile(opts.stateDir + "/resume.tsv", resume.serialize());
	}

	void Player::setLastFolder(const std::string& dir)
	{
		std::string keep = settings.rememberFolder ? dir : "";
		if (keep == resume.lastFolder || opts.stateDir.empty()) return;
		resume.lastFolder = keep;
		writeFile(opts.stateDir + "/resume.tsv", resume.serialize());
	}

	static bool hasCover(const std::vector<Track>& tracks)
	{
		for (auto& t : tracks)
			if (t.type == TrackType::Video && t.albumArt) return true;
		return false;
	}

	int Player::shownMusic() const
	{
		// the mode chosen is the mode shown: a song with no art has a placeholder in the art's box (songGraph,
		// updateOverlay), the same layout as with art
		int show = settings.musicShow;
		// the manga reader over the top panel: a visualizer nobody sees would still make its 30 frames a second
		// (mpv's sound waits on the graph) on the cores the reader decodes on; the bare graph meanwhile
		// (and while the manga engine starts: setEngineLoading)
		if (vizResting() && (show == SHOW_VIS || show == SHOW_BOTH)) return SHOW_NONE;
		return show;
	}

	void Player::setTopCovered(bool on)
	{
		if (on == topCovered) return;
		bool was = vizResting();
		topCovered = on;
		fprintf(stderr, "music: top panel %s\n", on ? "covered: the visualizer rests" : "shown again: the visualizer back");
		if (isAudio && loaded && was != vizResting()) applyAudioDisplay();
	}

	void Player::setEngineLoading(bool on)
	{
		if (on == engineLoading) return;
		bool was = vizResting();
		engineLoading = on;
		fprintf(stderr, "music: manga engine %s\n", on ? "starting: the visualizer rests" : "started: the visualizer back");
		if (isAudio && loaded && was != vizResting()) applyAudioDisplay();
		updateOverlay();	// (the line under the title, whatever the top panel shows)
	}

	void Player::applyAudioDisplay()
	{
		// for the next song's guess (load): an album's songs mostly all have art, or none
		size_t slash = currentPath().rfind('/');
		lastArtDir = slash == std::string::npos ? "." : currentPath().substr(0, slash);
		lastArt = hasCover(tracks);
		const int show = shownMusic();
		// no graph, whatever the mode: the song's sound chosen by its id every time (a song must never be left with no
		// audio track: it would read as ended and the Session would play the next), and its picture off, but for a
		// network song's art shown as mpv's own (Album art only, Art + visualizer)
		prop("aid", songAid(seenAid));
		int64_t art = 0;
		if (isNet && (show == SHOW_ART || show == SHOW_BOTH))
			for (auto& t : tracks)
				if (t.type == TrackType::Video && !art) art = t.id;
		prop("vid", art ? std::to_string(art) : "no");
		const std::string why = std::string(show == SHOW_ART ? "Album art only" : show == SHOW_BOTH ? "Art + visualizer" : show == SHOW_VIS ? "Visualizer only" : "None") +
								(art ? ", the art mpv's" : "");
		if (why != displayLogged)
		{
			displayLogged = why;
			fprintf(stderr, "music: %s: %s, no graph (the player draws the top panel)\n", baseName(currentPath()).c_str(), why.c_str());
		}
		updateOverlay();
	}

	void Player::nativeLayerFailed()
	{
		if (noLayer) return;
		noLayer = true;
		setGround(true, "the theme's: no layer to compose");
	}

	bool Player::nativeViz() const
	{
		return vizNativeStyle(settings.visualizer) && isAudio;
	}

	bool Player::nativeCanTap() const
	{
		// a network song: the tap's second decode opens local files only (bpnet:// goes through mpv's stream callbacks,
		// and a second read of a remote file would double its traffic), so its style is drawn idle (flat); the same for a
		// file the tap couldn't read
		return !isNet && currentPath().find("://") == std::string::npos && !nativeFailed;
	}

	static uint64_t steadyNs()
	{
		return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void Player::nativeStop(const char* why)
	{
		if (!vizTap.running()) return;
		VizTap::Stats st = vizTap.stats();
		fprintf(stderr, "viz-native: off for %s after %llu frames drawn, %llu spectra, %llu seeks (%s)\n", baseName(vizTap.path()).c_str(),
			(unsigned long long)nativeFrames, (unsigned long long)st.spectra, (unsigned long long)st.seeks, why);
		vizTap.stop();
		nativeMoving = false;
		nativeStyleSent = -1;
	}

	std::vector<VizRect> Player::nativeKeepClear() const
	{
		// (the lines and the art aren't kept clear: mpv's layer is composed over the shapes, the lines with their
		// outline and no box round them; with no layer, as before: the lines' and the art's boxes too)
		std::vector<VizRect> r = cardsUp();
		if (!noLayer) return r;
		if (shownMusic() == SHOW_VIS && linesBox.x1 > linesBox.x0) r.push_back(linesBox);
		if (shownMusic() == SHOW_BOTH && vizBehindArt(settings.visualizer))
		{
			if (groupArtBox.x1 > groupArtBox.x0) r.push_back(groupArtBox);
			if (groupTextBox.x1 > groupTextBox.x0) r.push_back(groupTextBox);
		}
		return r;
	}

	int Player::drawnStyle() const
	{
		return settings.visualizer;
	}

	void Player::syncNative()
	{
		const int show = shownMusic();
		// what the top panel shows is the native style: a song, a visualizer shown (no graph: mpv's picture is the
		// theme's ground and the lines)
		const bool want = isAudio && loaded && playing() && nativeViz() && (show == SHOW_VIS || show == SHOW_BOTH);
		const std::string path = currentPath();
		if (!want)
		{
			nativeIdle = false;
			nativeStop(!playing() || !isAudio ? "nothing plays" : !nativeViz() ? "another style" : "the display shows no visualizer");
			return;
		}
		nativeOverArt = show == SHOW_BOTH;
		if (!nativeCanTap())
		{
			// a network song (or a file the tap couldn't read): the style drawn idle, flat, no tap
			nativeStop("no tap for this song");
			if (!nativeIdle || nativeIdlePath != path)
			{
				nativeIdle = true;
				nativeIdlePath = path;
				fprintf(stderr, "viz-native: %s: %s drawn idle (flat): %s\n", baseName(path).c_str(), visualizerLabel(settings.visualizer),
					nativeFailed ? "the second decode couldn't read it" : "a network song (the tap reads local files only)");
			}
			return;
		}
		nativeIdle = false;
		if (!vizTap.running() || vizTap.path() != path)
		{
			nativeStop("another file");
			nativeSeq = nativeFrames = 0;
			nativeAt = ticks();
			nativeLoggedAt = 0;
			std::function<void()> wake = nativeWake;
			vizTap.start(path, settings.vizFps, settings.visualizer, wake);
			nativeStyleSent = settings.visualizer;
			fprintf(stderr, "viz-native: %s for %s (%s), %d fps: a second decode, mono %d Hz, FFT %d (PFFFT), on mpv's audio-pts "
				"(the sound heard: mpv's output delay already off), drawn a frame ahead\n", visualizerLabel(settings.visualizer), baseName(path).c_str(),
				nativeOverArt ? "Art + visualizer: the band" : "Visualizer only", settings.vizFps, VN_RATE, VN_FFT);
		}
		if (vizTap.failed())
		{
			// the tap couldn't read the file: the style idle for this song (next pass)
			nativeStop("the second decode failed");
			nativeFailed = true;
			return;
		}
		vizTap.setFps(settings.vizFps);
		if (settings.visualizer != nativeStyleSent)
		{
			// a native style to another: the same tap, the same blank graph (no sound reopen), its own bands
			if (nativeStyleSent >= 0) fprintf(stderr, "viz-native: %s now (the same tap, no graph change)\n", visualizerLabel(settings.visualizer));
			nativeStyleSent = settings.visualizer;
			vizTap.setStyle(settings.visualizer);
		}
		VizClock c = audioClock;
		c.speed = speed > 0 ? speed : 1;
		c.moving = !paused && !pausedForCache && !seekingSince && c.pts >= 0;
		// started again (play after a pause, a seek done): the time runs on from now, not from the last update
		if (c.moving && !nativeMoving) audioClock.atNs = c.atNs = steadyNs();
		nativeMoving = c.moving;
		vizTap.setClock(c);
		// once, 5 s in: the frames a second drawn, the spectra taken, how often mpv told the time
		uint64_t now = ticks();
		if (!nativeLoggedAt && now - nativeAt >= 5000)
		{
			nativeLoggedAt = now;
			VizTap::Stats st = vizTap.stats();
			double s = (now - nativeAt) / 1000.0;
			fprintf(stderr, "viz-native: %s: in %.1f s %.1f frames drawn a second, %.1f spectra a second (the worker %.2f ms a look, at most %.1f, "
				"%llu behind), audio-pts told %.1f a second, %llu seeks; decoding %s\n", baseName(path).c_str(), s, nativeFrames / s, st.spectra / s,
				st.spectra ? st.workMs / st.spectra : 0.0, st.workMax, (unsigned long long)st.behind, st.clockTold / s, (unsigned long long)st.seeks,
				vizTap.info().c_str());
		}
	}

	static std::string assText(std::string s);

	void Player::setTopMarquee(const std::string& text, const std::string& style, int an, double x, double y, double boxW, int fs, int textW, bool fade)
	{
		const std::string key = text + "|" + style + "|" + std::to_string((int)x) + "," + std::to_string((int)y) + "," + std::to_string((int)boxW);
		if (key != topMarq.key)
		{
			// a new title or place: from its start, its rest first
			if (topMarq.text != text) fprintf(stderr, "music: title marquee: %s (%d units in %d)\n", text.c_str(), textW, (int)boxW);
			topMarq.start = ticks();
			topMarq.key = key;
		}
		topMarq.on = true;
		topMarq.text = text, topMarq.style = style;
		topMarq.an = an, topMarq.x = x, topMarq.y = y, topMarq.boxW = boxW, topMarq.fs = fs, topMarq.textW = textW, topMarq.fade = fade;
		topMarq.lastStep = -1;	// (sent again: the look may have changed)
	}

	bool Player::marqueeMoving() const
	{
		return topMarq.on && !lay::marquee((int)(ticks() - topMarq.start), topMarq.textW, (int)topMarq.boxW, topMarq.fs).done;
	}

	void Player::tickMarquee()
	{
		if (!topMarq.on) return;
		const lay::MarqueeAt m = lay::marquee((int)(ticks() - topMarq.start), topMarq.textW, (int)topMarq.boxW, topMarq.fs);
		// while it moves: smoothly, its place sent at most every ~16 ms (about 60 a second) with a fractional \pos (libass
		// places text to 1/8 of a pixel); before, it moved in the spec's whole-unit steps, 24 a second, and looked choppy
		const bool sliding = m.offsetF > 0 && m.offsetF < topMarq.textW - topMarq.boxW && m.alpha >= 1;
		const uint64_t now = ticks();
		const int key = sliding ? 1000000 + (int)(m.offsetF * 8) : m.step;
		if (key == topMarq.lastStep) return;
		if (sliding && now - topMarq.sentAt < 16) return;
		topMarq.lastStep = key;
		topMarq.sentAt = now;
		const double off = sliding ? m.offsetF : m.offset;
		// on the 960x720 canvas: the box's clip, the text moved left by whole units (1.5 canvas units each)
		const double K = 1.5;
		auto n = [](double v) { return std::to_string((long)std::lround(v)); };
		auto f2 = [](double v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); return std::string(b); };
		const double top = topMarq.an == 4 ? topMarq.y - topMarq.fs * 0.8 : topMarq.y - 2, bot = topMarq.an == 4 ? topMarq.y + topMarq.fs * 0.8 : topMarq.y + topMarq.fs * 1.35;
		const double x0 = topMarq.x, x1 = topMarq.x + topMarq.boxW;
		// the text, clipped to (cx0, cx1), seen at vis (0..1)
		std::string ev;
		auto part = [&](double cx0, double cx1, float vis) {
			char a[8];
			snprintf(a, sizeof a, "%02X", (int)std::lround(255 * (1 - std::clamp(vis, 0.0f, 1.0f))));
			ev += "{\\an" + std::to_string(topMarq.an) + "\\pos(" + f2((topMarq.x - off) * K) + "," + n(topMarq.y * K) + ")\\clip(" + n(cx0 * K) +
				  "," + n(top * K) + "," + n(cx1 * K) + "," + n(bot * K) + ")" + topMarq.style + "\\alpha&H" + a + "&}" + assText(topMarq.text) + "\n";
		};
		const bool fadeL = topMarq.fade && m.fadeLeft, fadeR = topMarq.fade && m.fadeRight;
		// the fades: the text itself fainter in four 4-unit strips (80, 60, 40, 20% seen) at an edge it goes on past
		// (not strips of the ground over it: no band of BG over a visualizer under the lines)
		const double W = lay::MARQ_FADE_W;
		part(x0 + (fadeL ? W : 0), x1 - (fadeR ? W : 0), m.alpha);
		for (int k = 0; k < 4; k++)
		{
			const float vis = m.alpha * (1 - (k + 1) / 5.0f);
			if (fadeR) part(x1 - W + k * 4, x1 - W + k * 4 + 4, vis);
			if (fadeL) part(x0 + (3 - k) * 4, x0 + (3 - k) * 4 + 4, vis);
		}
		const char* cmd[] = {"osd-overlay", "2", "ass-events", ev.c_str(), "960", "720", "0", "no", "no", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	// ASS text: braces and backslashes would be its codes
	static std::string assText(std::string s)
	{
		for (char& c : s)
		{
			if (c == '{') c = '(';
			else if (c == '}') c = ')';
			else if (c == '\\') c = '/';
		}
		return s;
	}

	// an ASS drawing of a rounded box at (x, y), w x h, radius r, one contour; reverse: drawn the other way round
	// (inside another contour: a hole, by either fill rule)
	static std::string roundPath(int x, int y, int w, int h, int r, bool reverse)
	{
		int c = (int)std::lround(r * 0.4477);
		char d[512];
		if (!reverse)
			snprintf(d, sizeof d, "m %d %d l %d %d b %d %d %d %d %d %d l %d %d b %d %d %d %d %d %d l %d %d b %d %d %d %d %d %d l %d %d b %d %d %d %d %d %d ",
				x + r, y, x + w - r, y, x + w - c, y, x + w, y + c, x + w, y + r, x + w, y + h - r, x + w, y + h - c, x + w - c, y + h,
				x + w - r, y + h, x + r, y + h, x + c, y + h, x, y + h - c, x, y + h - r, x, y + r, x, y + c, x + c, y, x + r, y);
		else
			snprintf(d, sizeof d, "m %d %d b %d %d %d %d %d %d l %d %d b %d %d %d %d %d %d l %d %d b %d %d %d %d %d %d l %d %d b %d %d %d %d %d %d ",
				x + r, y, x + c, y, x, y + c, x, y + r, x, y + h - r, x, y + h - c, x + c, y + h, x + r, y + h, x + w - r, y + h,
				x + w - c, y + h, x + w, y + h - c, x + w, y + h - r, x + w, y + r, x + w, y + c, x + w - c, y, x + w - r, y);
		return d;
	}

	bool Player::songArtMissing() const
	{
		if (!isAudio) return false;
		return isNet ? !tracks.empty() && !hasCover(tracks) : artState == ArtState::None;
	}

	std::string Player::artPlaceholderKey() const
	{
		if (!tagAlbum.empty()) return tagAlbum;
		if (!title.empty()) return title;
		std::string f = currentPath();
		size_t k = f.find_last_of('/');
		return k == std::string::npos ? f : f.substr(k + 1);
	}

	void Player::setSongMargins(const std::string& lrtb)
	{
		if (lrtb == marginsSet) return;
		marginsSet = lrtb;
		double m[4] = {0, 0, 0, 0};
		if (!lrtb.empty()) sscanf(lrtb.c_str(), "%lf %lf %lf %lf", &m[0], &m[1], &m[2], &m[3]);
		// (sent again after a file's own panscan: load sets the aspect mode's, so marginsSet is cleared there)
		const char* names[4] = {"video-margin-ratio-left", "video-margin-ratio-right", "video-margin-ratio-top", "video-margin-ratio-bottom"};
		for (int i = 0; i < 4; i++) mpv_set_property_async(mpv, 0, names[i], MPV_FORMAT_DOUBLE, &m[i]);
		// the art fills its square (cropped from a picture that isn't square), as the overlay's decode does: panscan
		// crops to the box the margins leave. 0 with no margins (Art + visualizer, Visualizer only, None, a video)
		double pan = lrtb.empty() ? (aspect == AspectMode::Fill ? 1 : 0) : 1;	// (none: a video's own aspect mode)
		mpv_set_property_async(mpv, 0, "panscan", MPV_FORMAT_DOUBLE, &pan);
		fprintf(stderr, "music: top panel margins %s, panscan %g\n", lrtb.empty() ? "none (the whole panel)" : lrtb.c_str(), pan);
	}

	void Player::setGround(bool themed, const char* why, bool clear)
	{
		// mpv's own ground, round its picture where none is drawn: Album art only's margins (a graph's picture fills
		// the panel, so there it is the graph's). Set on a change only: a property, no per-frame work. Clear under a
		// native visualizer: the top panel draws the ground and the shapes, mpv's picture goes over them
		char c[16];
		const theme::Rgb g = themed ? theme::BG : theme::Rgb{0, 0, 0};
		if (clear) snprintf(c, sizeof c, "%s", GROUND_CLEAR);
		else snprintf(c, sizeof c, "#%02X%02X%02X", g.r, g.g, g.b);
		if (clear && !why) why = "clear: the visualizer under mpv's art and lines";
		if (groundSet == c) return;
		groundSet = c;
		const char* v = groundSet.c_str();	// (copied by mpv before it returns)
		mpv_set_property_async(mpv, 0, "background-color", MPV_FORMAT_STRING, &v);
		fprintf(stderr, "top: ground %s (%s)\n", c, why ? why : themed ? "the theme's BG under a song" : "black: a picture");
	}

	void Player::dropSongArt()
	{
		if (artOverlayUp)
		{
			const std::string id = std::to_string(ART_OVERLAY);
			const char* rm[] = {"overlay-remove", id.c_str(), nullptr};
			mpv_command_async(mpv, ART_REPLY + ++artSerial, rm);
		}
		artOverlayUp = false;
		artShownSerial = 0;
		artOverlayX = artOverlayY = -1;
		artJob.reset();
		artKey.clear();
		artState = ArtState::Unknown;
	}

	void Player::artReplied(uint64_t serial)
	{
		// as the level card's: the core has run every command up to this one
		for (auto it = artPixels.begin(); it != artPixels.end() && it->first <= serial;)
			it = it->first == artShownSerial ? std::next(it) : artPixels.erase(it);
	}

	void Player::checkSongArt()
	{
		if (!artJob || artState != ArtState::Unknown) return;
		{
			std::lock_guard<std::mutex> g(artJob->m);
			if (!artJob->done) return;
			artState = artJob->found ? ArtState::Shown : ArtState::None;
			fprintf(stderr, "music: %s: art for the overlay %s in %.0f ms (%d px)\n", baseName(currentPath()).c_str(),
				artJob->found ? "decoded" : "none in the file or beside it", artJob->ms, artJob->px);
		}
		updateOverlay();
	}

	// the art decoded for the overlay: the picture inside the file or beside it (cover.jpg), cut to a px square from
	// its middle, its corners rounded (radius r px, smoothed), premultiplied BGRA. On a thread of its own: nothing here
	// touches the player
	static bool songArtPixels(const std::string& path, int px, int r, std::vector<uint8_t>& out)
	{
		std::string bytes, mime;
		if (!probeCover(path, bytes, mime)) return false;
		PageImage img = decodePage((const uint8_t*)bytes.data(), bytes.size(), px);
		if (!img.ok()) return false;
		// a wide picture: decoded wider, so its height fills the square
		if (img.h < px && img.srcH > 0)
		{
			PageImage wide = decodePage((const uint8_t*)bytes.data(), bytes.size(), (px * img.srcW + img.srcH - 1) / img.srcH);
			if (wide.ok() && wide.h >= px) img = std::move(wide);
		}
		if (img.w < px || img.h < px) return false;
		const int ox = (img.w - px) / 2, oy = (img.h - px) / 2;
		out.assign((size_t)px * px * 4, 0);
		for (int y = 0; y < px; y++)
			for (int x = 0; x < px; x++)
			{
				const uint8_t* c = &img.rgba[((size_t)(y + oy) * img.w + x + ox) * 4];
				// the corner's cover: 1 inside the quarter circle, 0 outside, smoothed over a pixel
				double cover = 1;
				int cx = x < r ? r : x >= px - r ? px - r : -1, cy = y < r ? r : y >= px - r ? px - r : -1;
				if (cx >= 0 && cy >= 0)
				{
					double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
					cover = std::clamp(r - std::sqrt(dx * dx + dy * dy) + 0.5, 0.0, 1.0);
				}
				int a = (int)std::lround(c[3] * cover);
				uint8_t* o = &out[((size_t)y * px + x) * 4];
				o[0] = (uint8_t)(c[2] * a / 255);
				o[1] = (uint8_t)(c[1] * a / 255);
				o[2] = (uint8_t)(c[0] * a / 255);
				o[3] = (uint8_t)a;
			}
		return true;
	}

	void Player::updateOverlay()
	{
		if (!isAudio)
		{
			const char* cmd[] = {"osd-overlay", "1", "none", "", nullptr};
			mpv_command_async(mpv, 0, cmd);
			if (topMarq.on)
			{
				topMarq = TopMarq{};
				const char* none[] = {"osd-overlay", "2", "none", "", nullptr};
				mpv_command_async(mpv, 0, none);
			}
			setSongMargins("");
			setGround(false);
			dropSongArt();
			return;
		}
		linesBox = groupArtBox = groupTextBox = {0, 0, 0, 0, 0};
		// the song's lines (docs/redesign/components.md, Song layout); the ASS canvas is 960x720, 1.5x the units
		const double K = 1.5;
		auto u = [&](double v) { return (int)std::lround(v * K); };
		auto num = [](int v) { return std::to_string(v); };
		// over a visualizer alone: white with a shadow, as subtitles (a picture under them)
		const std::string WHITE = "&HFFFFFF&", CREAM = "&HD3E2E8&", GREY = "&H97A5AB&";
		int show = shownMusic();
		std::string title = this->title, artist = tagArtist;
		std::string album = tagAlbum + (tagYear.empty() ? "" : (tagAlbum.empty() ? "" : " \xC2\xB7 ") + tagYear);
		// an audiobook's (or a song's) chapter, under the title in yellow: where in the book this is
		std::string chapter;
		int c = currentChapter();
		if (c >= 0 && c < (int)chapters.size())
			chapter = chapterUntitled(chapters[c].title) ? "Chapter " + std::to_string(c + 1) + " of " + std::to_string(chapters.size())
				: chapters[c].title;
		const int style = drawnStyle();
		bool ownsArt = show == SHOW_BOTH && style == VIS_VINYL;
		bool group = (show == SHOW_ART || show == SHOW_BOTH) && !ownsArt;
		// the theme's ground (Album art only, Art + visualizer, None: app UI, docs/redesign Themes › The top screen):
		// the lines TEXT, DIM_ON, DIM, the art's edge edgeOf(SURFACE); a visualizer alone keeps its picture
		// Peaks alone is drawn on BG too (its bars ACCENT, caps TEXT: settings.cpp's peaksThemed), and so are the six
		// simple ones (src/viz_simple.cpp), so their lines are the theme's; the other styles alone keep their picture
		// and the white lines with a shadow
		// (and Night city alone in a theme but Yellow: its sky is the theme's BG, docs/notes/2026-11-19-viz-theme.md)
		const bool peaksAlone = show == SHOW_VIS && (visualizerOnGround(style) || (style == VIS_CITY && theme::currentTheme() != 0));
		const bool themed = group || show == SHOW_NONE || peaksAlone;
		// (a song with a native style shown: mpv's picture composed over the shapes, its ground clear)
		setGround(themed, nullptr, themed && isAudio && nativeViz() && (show == SHOW_VIS || show == SHOW_BOTH) && !noLayer);
		const std::string artKeyText = artPlaceholderKey();
		const std::string INK = ass(TEXT), INK2 = ass(DIM_ON), INK3 = ass(DIM), MARK = ass(ACCENT), EDGE = ass(edgeOf(SURFACE)),
			GROUND = ass(BG);
		// the lines over a visualizer: no box round them (the shapes pass under them: mpv's layer, topviz.cpp), kept
		// readable by an outline and a shadow in the ground's colour (libass draws them once a change; nothing a frame)
		const std::string OUTLINE = "\\bord3\\3c" + GROUND + "\\shad2\\4c" + GROUND;
		const std::string groupEdge = show == SHOW_BOTH ? OUTLINE : "\\bord0\\shad0";
		std::string ev;
		std::string why;	// for the log: what the art box holds

		// the art shown as mpv's own video (a network song's: the overlay's decode reads local files only): put in the
		// art's box by the video's margins, in the layout of the mode it is shown in
		bool videoArt = isNet && (show == SHOW_ART || show == SHOW_BOTH) && hasCover(tracks);
		// the lines' widths as libass will draw them: measured in the bundle's Inter (what libass uses on the handheld),
		// \fs being the font's ascent + descent; the letter estimate (songTextWidth) where the fonts can't be read. The
		// estimate ran ~8% wide, and the group (art + lines) centred on it sat left of the middle
		int estimated = 0;
		auto lineW = [&](const std::string& t, int fs, bool bold) {
			if (t.empty()) return 0;
			int est = songTextWidth(t, fs, bold);
			estimated = std::max(estimated, std::min(est, 560));
			TTF_Font* f = opts.fontsDir.empty() ? nullptr : card::faceFont(opts.fontsDir, bold ? card::INTER_BOLD : card::INTER, 100);
			int w = 0, h = 0;
			if (!f || TTF_SizeUTF8(f, t.c_str(), &w, &h) != 0) return est;
			int em = TTF_FontAscent(f) - TTF_FontDescent(f);
			return em > 0 ? (int)std::ceil(w * (double)fs / em) : est;
		};
		// a line cut to w units with "…", by the same measure (the estimate cut "Sunny Peel" in a column made for it)
		auto fitLine = [&](const std::string& t, int fs, int w, bool bold) {
			if (lineW(t, fs, bold) <= w) return t;
			std::vector<size_t> ends;
			for (size_t i = 0; i < t.size(); i += card::u8len(t, i)) ends.push_back(i);
			size_t lo = 0, hi = ends.size();	// prefixes of ends[k] bytes; the longest whose "…" fits
			auto cut = [&](size_t k) {
				std::string p = t.substr(0, ends[k]);
				while (!p.empty() && p.back() == ' ') p.pop_back();
				return p + "\xe2\x80\xa6";
			};
			while (lo + 1 < hi)
			{
				size_t mid = (lo + hi) / 2;
				if (lineW(cut(mid), fs, bold) <= w) lo = mid;
				else hi = mid;
			}
			return cut(lo);
		};
		const int widest = std::max({lineW(title, SONG_TITLE_FS, true), lineW(chapter, SONG_ARTIST_FS, false),
									 lineW(artist, SONG_ARTIST_FS, false), lineW(album, SONG_ALBUM_FS, false)});
		SongLayout artOnly = songLayout(true, widest);
		// the title as shown in a box (Repeated start: a start it shares with its artist, album, folder or list left out
		// when it doesn't fit) and whether it moves there (the Title marquee: it doesn't fit, Scroll long titles on)
		std::vector<std::string> listNames;
		for (const std::string& path : playlist) listNames.push_back(baseName(path));
		const std::vector<std::string> starts = titleStarts(tagArtist, tagAlbum, currentPath(), listNames);
		auto titleIn = [&](int fs, bool bold, int box) {
			return shownTitle(title, starts, [&](const std::string& t) { return lineW(t, fs, bold) <= box; });
		};
		bool marq = false;	// the title is the marquee's overlay, not in ev
		// a style over the whole panel in Art + visualizer (vizFullPanel: Sparks, Starfield...): the art and its lines
		// as Album art only lays them out, centred both ways (the owner's word), not lifted over a band
		const bool fullGroup = show == SHOW_BOTH && vizFullPanel(style);
		if (videoArt)
		{
			const SongLayout v = show == SHOW_ART || fullGroup ? artOnly : songLayout(false, widest);
			char m[96];
			snprintf(m, sizeof m, "%.5f %.5f %.5f %.5f", v.artX / 640.0, (640 - v.artX - v.artS) / 640.0, v.artY / 480.0,
					 (480 - v.artY - v.artS) / 480.0);
			setSongMargins(m);
		}
		else setSongMargins("");

		if (group)
		{
			SongLayout l = show == SHOW_ART || fullGroup ? artOnly : songLayout(false, widest);
			// the art: mpv's (a network song), or the overlay's (decoded here, both modes)
			enum { NOTHING, ART, OVERLAY, PLACEHOLDER } box = NOTHING;
			if (isNet) box = tracks.empty() ? NOTHING : videoArt ? ART : PLACEHOLDER;
			else
			{
				const int pw = topW > 0 ? topW : 960, ph = topH > 0 ? topH : 720;
				int px = (int)std::lround(l.artS * ph / 480.0);
				std::string key = currentPath() + "@" + std::to_string(px);
				if (key != artKey)
				{
					dropSongArt();
					artKey = key;
					auto job = std::make_shared<SongArtJob>();
					job->px = px;
					artJob = job;
					std::string path = currentPath();
					int r = (int)std::lround(SONG_ART_RADIUS * ph / 480.0);
					std::thread([job, path, px, r] {
#ifdef __linux__
						setpriority(PRIO_PROCESS, 0, 10);	// (this thread alone): the song's decoding first
#endif
						auto t0 = std::chrono::steady_clock::now();
						auto px2 = std::make_shared<std::vector<uint8_t>>();
						bool found = songArtPixels(path, px, r, *px2);
						std::lock_guard<std::mutex> g(job->m);
						job->found = found;
						job->bgra = std::move(*px2);
						job->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
						job->done = true;
					}).detach();
				}
				box = artState == ArtState::Shown ? OVERLAY : artState == ArtState::None ? PLACEHOLDER : NOTHING;
				if (box == OVERLAY)
				{
					int x = (int)std::lround(l.artX * pw / 640.0), y = (int)std::lround(l.artY * ph / 480.0);
					if (!artOverlayUp || x != artOverlayX || y != artOverlayY)
					{
						std::shared_ptr<std::vector<uint8_t>> pixels;
						{
							std::lock_guard<std::mutex> g(artJob->m);
							pixels = std::make_shared<std::vector<uint8_t>>(artJob->bgra);
						}
						uint64_t serial = ++artSerial;
						artPixels[serial] = pixels;
						artShownSerial = serial;
						std::string id = std::to_string(ART_OVERLAY), xs = std::to_string(x), ys = std::to_string(y),
							src = "&" + std::to_string((uintptr_t)pixels->data()), ws = std::to_string(px), st = std::to_string(px * 4);
						const char* add[] = {"overlay-add", id.c_str(), xs.c_str(), ys.c_str(), src.c_str(), "0", "bgra", ws.c_str(), ws.c_str(),
							st.c_str(), nullptr};
						mpv_command_async(mpv, ART_REPLY + serial, add);
						artOverlayUp = true;
						artOverlayX = x;
						artOverlayY = y;
					}
				}
			}
			if (box != OVERLAY && artOverlayUp)
			{
				const std::string id = std::to_string(ART_OVERLAY);
				const char* rm[] = {"overlay-remove", id.c_str(), nullptr};
				mpv_command_async(mpv, ART_REPLY + ++artSerial, rm);
				artOverlayUp = false;
				artShownSerial = 0;
				artOverlayX = artOverlayY = -1;
			}
			const int X = u(l.artX), Y = u(l.artY), S = u(l.artS), R = u(SONG_ART_RADIUS), E = u(SONG_ART_EDGE);
			if (show == SHOW_BOTH && l.art)
			{
				const float m = SONG_ART_EDGE + 2;
				groupArtBox = {l.artX - m, l.artY - m, l.artX + l.artS + m, l.artY + l.artS + m, 0};
			}
			if (box == ART || box == OVERLAY)
			{
				// mpv's art has square corners: rounded by the ground laid over them (the overlay's are its
				// own). Each reaches out under the edge too: mpv's scaled picture can bleed a pixel past its box
				if (box == ART)
				{
					int cc = (int)std::lround(R * 0.4477);
					std::string d;
					const int corners[4][4] = {{X, Y, 1, 1}, {X + S, Y, -1, 1}, {X + S, Y + S, -1, -1}, {X, Y + S, 1, -1}};
					for (auto& k : corners)
					{
						const int cx = k[0], cy = k[1], sx = k[2], sy = k[3];
						char p[256];
						snprintf(p, sizeof p, "m %d %d l %d %d l %d %d b %d %d %d %d %d %d l %d %d ", cx - sx * E, cy - sy * E,
							cx + sx * R, cy - sy * E, cx + sx * R, cy, cx + sx * cc, cy, cx, cy + sy * cc, cx, cy + sy * R, cx - sx * E, cy + sy * R);
						d += p;
					}
					ev += "{\\an7\\pos(0,0)\\bord0\\shad0\\1c" + GROUND + "\\p1}" + d + "{\\p0}\n";
				}
				// the edge, 2 units round the box (a ring: nothing drawn over the art)
				ev += "{\\an7\\pos(0,0)\\bord0\\shad0\\1c" + EDGE + "\\p1}" + roundPath(X - E, Y - E, S + 2 * E, S + 2 * E, R + E, false) +
					  roundPath(X, Y, S, S, R, true) + "{\\p0}\n";
			}
			else if (box == PLACEHOLDER)
			{
				// no art: the Art placeholder (artph.h): a record in its sleeve, in the theme's roles, the discs cut to the box
				// and the same 2-unit edge as the art's own. Drawn as vector shapes: no picture to load or scale
				const artph::Shape a = artph::shape(S, artKeyText);
				auto disc = [&](int dia, const std::string& col) {
					const int cx = X + a.cx, cy = Y + a.cy, h = dia / 2, k = (int)std::lround(h * 0.5523);
					char d[320];
					snprintf(d, sizeof d, "m %d %d b %d %d %d %d %d %d b %d %d %d %d %d %d b %d %d %d %d %d %d b %d %d %d %d %d %d ", cx + h, cy,
						cx + h, cy + k, cx + k, cy + h, cx, cy + h, cx - k, cy + h, cx - h, cy + k, cx - h, cy, cx - h, cy - k, cx - k, cy - h, cx, cy - h,
						cx + k, cy - h, cx + h, cy - k, cx + h, cy);
					ev += "{\\an7\\pos(0,0)\\bord0\\shad0\\clip(" + roundPath(X, Y, S, S, R, false) + ")\\1c" + col + "\\p1}" + d + "{\\p0}\n";
				};
				ev += "{\\an7\\pos(0,0)\\bord0\\shad0\\1c" + EDGE + "\\p1}" + roundPath(X - E, Y - E, S + 2 * E, S + 2 * E, R + E, false) + "{\\p0}\n";
				ev += "{\\an7\\pos(0,0)\\bord0\\shad0\\1c" + ass(BUTTON) + "\\p1}" + roundPath(X, Y, S, S, R, false) + "{\\p0}\n";
				disc(a.record, GROUND);
				if (a.band) disc(a.band, ass(SURFACE));
				disc(a.label, MARK);
				disc(a.hole, GROUND);
			}
			why = box == ART ? "mpv's art in the margins" : box == OVERLAY ? "the decoded art"
				: box == PLACEHOLDER ? "the placeholder" : "nothing yet";
			char lg[320];
			const int pw = topW > 0 ? topW : 960, ph = topH > 0 ? topH : 720;
			snprintf(lg, sizeof lg, "art %d at %d,%d (px %ld at %ld,%ld on %dx%d), text %d wide at %d (lines %d, estimated %d)", l.artS, l.artX,
				l.artY, std::lround(l.artS * ph / 480.0), std::lround(l.artX * pw / 640.0), std::lround(l.artY * ph / 480.0), pw, ph, l.textW,
				l.textX, widest, estimated);
			why += std::string(", ") + lg;

			// the lines to the art's right, their block centred on the art's middle, each cut to the column
			if (l.text)
			{
				struct Line { std::string text; int fs; std::string colour; bool bold; int gap; };
				std::vector<Line> lines;
				lines.push_back({title, SONG_TITLE_FS, INK, true, 0});
				if (!chapter.empty()) lines.push_back({chapter, SONG_ARTIST_FS, MARK, false, 6});
				if (!artist.empty()) lines.push_back({artist, SONG_ARTIST_FS, INK2, false, 8});
				if (!album.empty()) lines.push_back({album, SONG_ALBUM_FS, INK3, false, lines.size() > 1 ? 5 : 8});
				double h = 0;
				for (auto& ln : lines) h += ln.gap + ln.fs * 1.2;
				double y = l.midY - h / 2;
				if (show == SHOW_BOTH)
					groupTextBox = {(float)l.textX - 8, (float)(l.midY - h / 2 - 8), (float)(l.textX + std::min(widest, l.textW) + 8),
						(float)(l.midY + h / 2 + 8), 0};
				for (auto& ln : lines)
				{
					y += ln.gap;
					double mid = y + ln.fs * 0.6;
					if (&ln == &lines[0])
					{
						// the title: the Title marquee in the text's column when it doesn't fit
						const std::string shown = titleIn(ln.fs, true, l.textW);
						const int tw = lineW(shown, ln.fs, true);
						if (settings.scrollTitles && tw > l.textW)
						{
							marq = true;
							setTopMarquee(shown, groupEdge + "\\q2\\fs" + num(u(ln.fs)) + "\\b1\\1c" + ln.colour, 4, l.textX, mid, l.textW, ln.fs, tw, true);
							y += ln.fs * 1.2;
							continue;
						}
						ln.text = shown;
					}
					ev += "{\\an4\\pos(" + num(u(l.textX)) + "," + num(u(mid)) + ")" + groupEdge + "\\q2\\fs" + num(u(ln.fs)) +
						  (ln.bold ? "\\b1" : "\\b0") + "\\1c" + ln.colour + "}" + assText(fitLine(ln.text, ln.fs, l.textW, ln.bold)) + "\n";
					y += ln.fs * 1.2;
				}
			}
		}
		else
		{
			if (artOverlayUp || artJob) dropSongArt();
			why = show == SHOW_NONE ? "the title alone" : "the visualizer";
		}

		if (show == SHOW_NONE)
		{
			// the title alone, in the middle: 30 semibold, then "artist · album" 17
			std::string sub = artist + (artist.empty() || tagAlbum.empty() ? "" : " \xC2\xB7 ") + tagAlbum;
			const std::string shown = titleIn(30, true, 560);
			const int tw = lineW(shown, 30, true);
			marq = settings.scrollTitles && tw > 560;
			std::string t;
			if (marq)
			{
				// moving: the title from the box's left edge, the rest under it as before (heights as libass sets them,
				// about 1.2 the size a line)
				double h = 30 * 1.2 + (chapter.empty() ? 0 : 17 * 1.2) + (sub.empty() ? 0 : (8 + 17) * 1.2);
				const double top = 240 - h / 2;
				setTopMarquee(shown, "\\bord0\\shad0\\q2\\fs" + num(u(30)) + "\\b1\\1c" + INK, 4, 40, top + 18, 560, 30, tw, true);
				t = "{\\an8\\pos(480," + num(u(top + 36)) + ")\\bord0\\shad0\\q2}";
				bool any = false;
				if (!chapter.empty()) { t += "{\\b0\\fs" + num(u(17)) + "\\1c" + MARK + "}" + assText(songFitText(chapter, 17, 560)); any = true; }
				if (!sub.empty()) { t += std::string(any ? "\\N" : "") + "{\\b0\\fs" + num(u(8)) + "}\\h\\N{\\fs" + num(u(17)) + "\\1c" + INK3 + "}" + assText(songFitText(sub, 17, 560)); any = true; }
				if (!any) t += "\\h";
			}
			else
			{
				t = "{\\an5\\pos(480,360)\\bord0\\shad0\\q2\\fs" + num(u(30)) + "\\b1\\1c" + INK + "}" + assText(songFitText(shown, 30, 560, true));
				if (!chapter.empty()) t += "\\N{\\b0\\fs" + num(u(17)) + "\\1c" + MARK + "}" + assText(songFitText(chapter, 17, 560));
				if (!sub.empty()) t += "\\N{\\b0\\fs" + num(u(8)) + "}\\h\\N{\\fs" + num(u(17)) + "\\1c" + INK3 + "}" + assText(songFitText(sub, 17, 560));
			}
			// why the visualizer has stopped for now, in the accent's yellow (a blank line first: right under the
			// song's lines it read as part of them)
			if (engineLoading)
				t += "\\N{\\fs" + num(u(14)) + "}\\h\\N{\\c" + MARK + "}" + assText("Loading manga engine\xE2\x80\xA6");
			ev += t + "\n";
		}
		else if (!group)
		{
			// over a visualizer: top left, with a shadow (the native styles: the theme's lines with the ground's
			// outline, the visualizer passing under them)
			std::string look = peaksAlone ? "\\q2" + OUTLINE : "\\q2\\bord2\\blur4\\3c&H000000&\\shad3\\4c&H000000&";
			// a line each, from the top (22, 18): title 26, then 5 down the artist 17, 3 down the album 14
			const int lx = 22, lw = 596;
			double y = 18;
			int linesW = 0;
			auto line = [&](const std::string& s, int fs, bool bold, const std::string& colour, int gap) {
				y += gap;
				if (&s == &title)
				{
					// the title: the Title marquee when it doesn't fit (its box the whole line, lw)
					const std::string shown = titleIn(fs, bold, lw);
					const int tw = lineW(shown, fs, bold);
					if (settings.scrollTitles && tw > lw)
					{
						marq = true;
						setTopMarquee(shown, look + "\\fs" + num(u(fs)) + (bold ? "\\b1" : "\\b0") + "\\1c" + colour, 7, lx, y, lw, fs, tw, peaksAlone);
						linesW = lw;
						y += fs * 1.2;
						return;
					}
					linesW = std::max(linesW, lineW(songFitText(shown, fs, lw, bold), fs, bold));
					ev += "{\\an7\\pos(" + num(u(lx)) + "," + num(u(y)) + ")" + look + "\\fs" + num(u(fs)) + (bold ? "\\b1" : "\\b0") + "\\1c" + colour + "}" +
						  assText(songFitText(shown, fs, lw, bold)) + "\n";
					y += fs * 1.2;
					return;
				}
				linesW = std::max(linesW, lineW(songFitText(s, fs, lw, bold), fs, bold));
				ev += "{\\an7\\pos(" + num(u(lx)) + "," + num(u(y)) + ")" + look + "\\fs" + num(u(fs)) + (bold ? "\\b1" : "\\b0") + "\\1c" + colour + "}" +
					  assText(songFitText(s, fs, lw, bold)) + "\n";
				y += fs * 1.2;
			};
			line(title, 26, true, peaksAlone ? INK : WHITE, 0);
			if (!chapter.empty()) line(chapter, 17, false, peaksAlone ? MARK : ass(TOP_TITLE), 4);
			if (!artist.empty()) line(artist, 17, false, peaksAlone ? INK2 : CREAM, 5);
			if (!album.empty()) line(album, 14, false, peaksAlone ? INK3 : GREY, 3);
			// the lines' box (only kept clear with no layer to compose: nativeKeepClear)
			linesBox = {lx - 8.0f, 18 - 8.0f, lx + (float)linesW + 8, (float)y + 6, 0};
		}
		// the manga engine starting over Album art only (the visualizers rest as None): its note at the foot
		if (engineLoading && show != SHOW_NONE)
			ev += "{\\an2\\pos(480,696)\\bord0\\shad0\\fs" + num(u(14)) + "\\1c" + (themed ? MARK : ass(TOP_TITLE)) + "}" + assText("Loading manga engine\xE2\x80\xA6") + "\n";

		if (!marq && topMarq.on)
		{
			topMarq = TopMarq{};
			const char* none[] = {"osd-overlay", "2", "none", "", nullptr};
			mpv_command_async(mpv, 0, none);
		}
		else if (marq) tickMarquee();
		std::string logLine = std::string(show == SHOW_ART ? "Album art only" : show == SHOW_BOTH ? "Art + visualizer" : show == SHOW_VIS ? "Visualizer only" : "None") +
							  ", ground " + (themed ? groundSet : std::string("the picture's")) + ": " + why;
		if (logLine != layoutLogged)
		{
			layoutLogged = logLine;
			fprintf(stderr, "music: %s: top panel %s\n", baseName(currentPath()).c_str(), logLine.c_str());
		}
		if (ev.empty())
		{
			const char* none[] = {"osd-overlay", "1", "none", "", nullptr};
			mpv_command_async(mpv, 0, none);
			return;
		}
		const char* cmd[] = {"osd-overlay", "1", "ass-events", ev.c_str(), "960", "720", "0", "no", "no", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	std::string Player::get(const char* name) const
	{
		char* v = mpv ? mpv_get_property_string(mpv, name) : nullptr;
		std::string out = v ? v : "";
		mpv_free(v);
		return out;
	}

	std::vector<Player::InfoRow> Player::mediaInfo() const
	{
		std::vector<InfoRow> rows;
		auto header = [&](const std::string& h) { rows.push_back({h, "", true}); };
		auto row = [&](const std::string& label, const std::string& value) {
			if (!value.empty()) rows.push_back({label, value, false});
		};
		auto num = [&](const char* name) { std::string v = get(name); return v.empty() ? -1.0 : atof(v.c_str()); };
		// a track's codec as mpv names it in full ("H.264 / AVC / ..."), or its short name
		auto codec = [&](const char* track) {
			std::string desc = get((std::string("current-tracks/") + track + "/codec-desc").c_str());
			return desc.empty() ? codecName(get((std::string("current-tracks/") + track + "/codec").c_str())) : desc;
		};

		std::string path = get("path");
		size_t slash = path.rfind('/');
		if (isPhoto)
		{
			header("Photo");
			row("Name", slash == std::string::npos ? path : path.substr(slash + 1));
			row("Folder", slash == std::string::npos ? "" : slash == 0 ? "/" : path.substr(0, slash));
			double size = num("file-size");
			if (size >= 0) row("Size", formatBytes((long long)size));
			row("Format", codec("video"));
			// the photo's own size (shown at most 4096 across)
			int w = atoi(get("video-dec-params/dw").c_str()), h = atoi(get("video-dec-params/dh").c_str());
			if (turned()) std::swap(w, h);
			if (w > 0 && h > 0) row("Resolution", std::to_string(w) + " x " + std::to_string(h));
			if (duration > 0.05) row("Animation", formatTime(duration) + ", over and over");
			if (!exif.taken.empty()) row("Taken", exifDate(exif.taken));
			// "Canon" and "Canon EOS R6": the model alone
			std::string camera = exif.model.rfind(exif.make, 0) == 0 ? exif.model
				: exif.make + (exif.make.empty() || exif.model.empty() ? "" : " ") + exif.model;
			row("Camera", camera);
			header("View");
			row("Zoom", std::to_string((int)std::lround(view.zoom * 100)) + "%");
			row("Picture size", view.fill ? "Fill" : "Fit");
			if (view.rotate) row("Turned", std::to_string(view.rotate) + " degrees");
			return rows;
		}
		header("File");
		row("Name", slash == std::string::npos ? path : path.substr(slash + 1));
		row("Folder", slash == std::string::npos ? "" : slash == 0 ? "/" : path.substr(0, slash));
		double size = num("file-size");
		if (size >= 0) row("Size", formatBytes((long long)size));
		row("Container", get("file-format"));
		if (duration > 0) row("Duration", formatTime(duration));
		double left = timeLeft(position, duration, speed);
		if (left >= 0) row("Time left", formatTime(left) + (speed != 1 ? " at " + speedLabel(speed) : ""));
		double nChapters = chapters.empty() ? num("chapters") : (double)chapters.size();
		if (nChapters > 0) row("Chapters", std::to_string((int)nChapters));

		if (!get("current-tracks/video/id").empty())
		{
			// an audio file's picture is its cover
			header(get("current-tracks/video/albumart") == "yes" ? "Cover art" : "Video");
			row("Codec", codec("video"));
			std::string w = get("video-params/w"), h = get("video-params/h");
			if (w.empty()) { w = get("current-tracks/video/demux-w"); h = get("current-tracks/video/demux-h"); }
			if (!w.empty()) row("Resolution", w + " x " + h);
			std::string dw = get("video-params/dw"), dh = get("video-params/dh");
			if (!dw.empty() && (dw != w || dh != h)) row("Shown as", dw + " x " + dh);
			double fps = num("container-fps");
			if (fps > 0)
			{
				char buf[32];
				snprintf(buf, sizeof buf, "%.3g fps", fps);
				row("Frame rate", buf);
			}
			row("Pixel format", get("video-params/pixelformat"));
			std::string prim = get("video-params/primaries"), gamma = get("video-params/gamma");
			if (!prim.empty()) row("Colour", prim + (gamma.empty() ? "" : ", " + gamma));
			double vb = num("video-bitrate");
			if (vb > 0) row("Bitrate", formatBitrate(vb));
			std::string hw = get("hwdec-current");
			row("Decoding", hw.empty() || hw == "no" ? "Software" : "Hardware (" + hw + ")");
			double drops = num("frame-drop-count"), decDrops = num("decoder-frame-drop-count");
			row("Dropped frames", std::to_string((long)std::max(0.0, drops) + (long)std::max(0.0, decDrops)));
		}

		// a selected track's label, numbered within its kind as the menus number it
		auto label = [&](const Track* t) {
			auto list = tracksOf(t->type);
			int n = 1;
			for (size_t i = 0; i < list.size(); i++)
				if (list[i].id == t->id) n = (int)i + 1;
			return trackLabel(*t, n);
		};
		const Track* a = selected(TrackType::Audio);
		header("Audio");
		if (isAudio)
		{
			// what the top panel shows (no graph: all of it the player's)
			std::string vis = visualizerLabel(settings.visualizer);
			const int show = shownMusic();
			row("Top screen", nativeShown() ? (show == SHOW_BOTH ? "Album art + " : "Visualizer: ") + vis + (nativeIdle ? " (idle: no tap for this song)" : " (drawn by the player)")
				: show == SHOW_NONE ? std::string("Title only") : show == SHOW_ART ? (artState == ArtState::None && !isNet ? "Album art (none: the placeholder)" : "Album art")
				: "Title");
		}
		if (a)
		{
			row("Track", label(a));
			row("Codec", codec("audio"));
			double rate = num("audio-params/samplerate");
			if (rate > 0)
			{
				char buf[32];
				snprintf(buf, sizeof buf, "%g kHz", rate / 1000);
				row("Sample rate", buf);
			}
			row("Channels", get("audio-params/hr-channels"));
			double ab = num("audio-bitrate");
			if (ab > 0) row("Bitrate", formatBitrate(ab));
		}
		else row("Track", "Off");

		const Track* s = selected(TrackType::Sub);
		header("Subtitles");
		row("Track", s ? label(s) : "Off");

		header("Tracks");
		const char* names[] = {"Video", "Audio", "Subtitle"};
		TrackType types[] = {TrackType::Video, TrackType::Audio, TrackType::Sub};
		for (int t = 0; t < 3; t++)
		{
			auto list = tracksOf(types[t]);
			for (size_t i = 0; i < list.size(); i++)
			{
				std::string label = types[t] == TrackType::Video ? codecName(list[i].codec) : trackLabel(list[i], (int)i + 1);
				row(std::string(names[t]) + " " + std::to_string(i + 1), (list[i].selected ? "* " : "") + label);
			}
		}

		// the file's own tags: title, encoder, date...
		mpv_node tags;
		if (mpv && mpv_get_property(mpv, "metadata", MPV_FORMAT_NODE, &tags) >= 0)
		{
			if (tags.format == MPV_FORMAT_NODE_MAP && tags.u.list->num > 0)
			{
				header("Tags");
				for (int i = 0; i < tags.u.list->num; i++)
					if (tags.u.list->values[i].format == MPV_FORMAT_STRING) row(tags.u.list->keys[i], tags.u.list->values[i].u.string);
			}
			mpv_free_node_contents(&tags);
		}
		return rows;
	}

	std::vector<Track> Player::tracksOf(TrackType type) const
	{
		std::vector<Track> out;
		for (auto& t : tracks)
			if (t.type == type && !t.albumArt) out.push_back(t);
		return out;
	}

	const Track* Player::selected(TrackType type) const
	{
		for (auto& t : tracks)
			if (t.type == type && t.selected) return &t;
		return nullptr;
	}
}
