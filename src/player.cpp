#include "player.h"
#include "format.h"
#include "logo.h"
#include "pagedecode.h"
#include "netpath.h"
#include "netstore.h"
#include "netstream.h"
#include "sidecar.h"
#include "soundfx.h"
#include "playlist.h"
#include "theme.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	enum Observed : uint64_t { TIME_POS = 1, DURATION, PAUSE, TRACK_LIST, MEDIA_TITLE, AID, SID, SPEED, METADATA, PAUSED_FOR_CACHE, SEEKING, CHAPTER_LIST, CHAPTER };
	// screenshot-raw's answers for the photo's thumbnail: this plus the load they were asked in
	static const uint64_t THUMB_REPLY = 1000;
	// af-command's answers (the equalizer's bands): far above THUMB_REPLY's, which grow with each file
	static const uint64_t EQ_REPLY = 1ull << 40;
	// loadfile's answers (the playlist entry it made), plus the load's serial
	static const uint64_t LOAD_REPLY = 1ull << 41;
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
		opt("input-default-bindings", "no");
		opt("input-vo-keyboard", "no");
		opt("osc", "no");
		// none of mpv's own scripts (they load even with config=no): each file waits on their hooks as it opens
		// (ytdl_hook, auto_profiles...), a stall at every Next. Options this mpv doesn't have are refused, harmlessly
		for (const char* o : {"ytdl", "load-scripts", "load-stats-overlay", "load-osd-console", "load-auto-profiles",
				 "load-select", "load-commands", "load-context-menu", "load-positioning"})
			opt(o, "no");
		opt("sub-auto", "fuzzy");			// movie.srt, movie.en.srt... next to movie.mkv
		// the style from settings.conf is for plain subtitles; ASS/SSA keep their own (only sub-scale reaches
		// them, and we leave it)
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
		if (o.net && !o.net->attach(mpv)) fprintf(stderr, "net: mpv has no stream callbacks: network files won't open\n");
		mpv_set_wakeup_callback(mpv, wakeup, ctx);
		setAspect(aspect);
		for (auto& p : subStyleProperties(settings.style)) prop(p.first.c_str(), p.second);
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
		mpv_set_property_async(mpv, 0, name, MPV_FORMAT_STRING, &v);
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
		title.clear();
		tracks.clear();
		loadEntry = -1;
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

		showIdle(false);
		// A song's picture is chosen before it opens, as far as it can be known: a filter graph changed (or taken off
		// and put back) between songs closes and opens the sound output again, which takes the handheld a second or
		// more. The art's graph ([vid1]) only where the art is sure (a cover file: a song without the track it names
		// doesn't open at all); else the visualizer alone, or (Album art) a guess from the folder's last song, put right
		// at FILE_LOADED (applyAudioDisplay)
		std::string graph;
		if (isAudio)
		{
			size_t slash = path.rfind('/');
			std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
			bool cover = isNet ? !coverFileFor(namesIn(netParent(path))).empty() : folderHasCover(path), guessArt = cover || (dir == lastArtDir && lastArt);
			if (topCovered && (settings.musicShow == SHOW_VIS || settings.musicShow == SHOW_BOTH)) graph = blankGraph();
			else if (settings.musicShow == SHOW_VIS) graph = graphFor(settings.visualizer, false);
			else if (settings.musicShow == SHOW_NONE) graph = blankGraph();
			else if (settings.musicShow == SHOW_BOTH) graph = graphFor(settings.visualizer, cover);
			else graph = guessArt ? "" : blankGraph();
		}
		// the same graph as the last song's: kept. Else the last file's off first: under a graph mpv's tracks can't
		// be chosen (aid, sid below)
		bool keepGraph = !graph.empty() && graphSet == graph;
		// the equalizer and effect for this file (none on a video), before the loadfile rebuilds the chain from af: a
		// song after a song with the same ones, nothing sent
		syncEq(true);
		if (!keepGraph) prop("lavfi-complex", "");

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

		if (!graph.empty() && !keepGraph) prop("lavfi-complex", graph);
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
		const char* cmd[] = {"stop", nullptr};	// (its END_FILE is a stop: the error stays)
		mpv_command_async(mpv, 0, cmd);
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
				switch (ev->reply_userdata)
				{
				case TIME_POS: position = p->format == MPV_FORMAT_DOUBLE ? *(double*)p->data : -1; break;
				case DURATION: duration = p->format == MPV_FORMAT_DOUBLE ? *(double*)p->data : -1; break;
				case PAUSE: paused = p->format == MPV_FORMAT_FLAG && *(int*)p->data; break;
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
					auto* n = p->format == MPV_FORMAT_NODE ? (mpv_node*)p->data : nullptr;
					if (n && n->format == MPV_FORMAT_NODE_MAP)
						for (int i = 0; i < n->u.list->num; i++)
						{
							std::string k = n->u.list->keys[i];
							for (char& c : k) c = (char)tolower((unsigned char)c);
							if (n->u.list->values[i].format != MPV_FORMAT_STRING) continue;
							if (k == "artist") tagArtist = n->u.list->values[i].u.string;
							else if (k == "album") tagAlbum = n->u.list->values[i].u.string;
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
				// the file before one waiting for its folder's listing, stopped (or ended): not the one to come
				if (!netWait.dir.empty()) break;
				auto* e = (mpv_event_end_file*)ev->data;
				// a network file: a read that failed is an end of file to mpv; the connection's error, kept by the
				// stream, says it wasn't
				NetResult netFail = isNet && opts.net && !playlist.empty() ? opts.net->takeFailure(currentPath()) : NetResult();
				if (e->reason == MPV_END_FILE_REASON_ERROR || (e->reason == MPV_END_FILE_REASON_EOF && !netFail.ok()))
					error = netFail.ok() ? mpv_error_string(e->error) : netPlayError(netFail.error);
				else if (e->reason == MPV_END_FILE_REASON_EOF) finished = true;
				break;
			}
			case MPV_EVENT_PLAYBACK_RESTART:
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

	int Player::render(int w, int h, unsigned target, bool skip)
	{
		mpv_opengl_fbo fbo{(int)target, w, h, 0};
		int flip = 1;
		// skip: the frame is taken (mpv's queue moves on, its timing kept) but nothing is drawn: no scaling, no
		// shaders, no GL work stealing time from the bottom panel's drawing
		int skipping = skip ? 1 : 0;
		mpv_render_param params[] = {
			{MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
			{MPV_RENDER_PARAM_FLIP_Y, &flip},
			{MPV_RENDER_PARAM_SKIP_RENDERING, &skipping},
			{MPV_RENDER_PARAM_INVALID, nullptr},
		};
		return mpv_render_context_render(rc, params);
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
		// a song's graph that mpv couldn't put up (its art's [vid1] on a song with none: "Pad vid1 is not connected"):
		// mpv then drops every track, and the song sits silent at its first second. Drawn without the art, else none
		if (!selected(TrackType::Audio) && !tracksOf(TrackType::Audio).empty() && !graphSet.empty())
		{
			if (audioChecks >= 3) return;
			audioChecks++;
			bool art = graphSet.find("[vid1]") != std::string::npos;
			fprintf(stderr, "banana-player: %s: the music's graph failed (%s); %s (try %d)\n", baseName(currentPath()).c_str(),
				art ? "with the art" : "without art", art ? "the visualizer alone" : "no graph", audioChecks);
			syncEq(true);
			if (art) prop("lavfi-complex", graphFor(settings.visualizer, false));
			else
			{
				prop("lavfi-complex", "");
				prop("aid", songAid(0));
			}
			audioCheckAt = ticks() + 1500;
			return;
		}
		// a file with audio tracks and none chosen (not under a graph, which chooses its own): mpv dropped the track
		// when the sound device wouldn't open (its error_on_track), and ao-reload does nothing with no device. The
		// track chosen again opens the device anew. (The Audio page has no Off: this is never the person's choice)
		if (!selected(TrackType::Audio) && !tracksOf(TrackType::Audio).empty() && graphSet.empty())
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
		if (graphSet.empty()) return;	// no graph: mpv ends the file itself
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
		// (buffering: the network file waits, as a file opening does, but with a word of its own)
		bool show = (loading() && ticks() - loadAt >= 300) || buffering();
		if (!show)
		{
			if (spinnerStep >= 0)
			{
				const char* cmd[] = {"osd-overlay", "4", "none", "", nullptr};
				mpv_command_async(mpv, 0, cmd);
				spinnerStep = -1;
			}
			return;
		}
		int step = (int)(ticks() / 100 % 8) + (loading() ? 0 : 8);	// the word changes with the next step
		if (step == spinnerStep) return;
		spinnerStep = step;
		// eight dots round the middle of a 960x720 canvas, one lit going round, the word under them, on a dark card
		// (the last file's picture can still be up)
		std::string ev = "{\\an7\\pos(390,262)\\bord0\\shad0\\1c" + ass(TOP_CARD) + "\\1a&H08&\\p1}" + roundBox(180, 180, R_TOP_CARD) + "{\\p0}\n";
		for (int i = 0; i < 8; i++)
		{
			double a = i * M_PI / 4;
			int x = 480 + (int)std::lround(std::sin(a) * 36), y = 330 - (int)std::lround(std::cos(a) * 36);
			int age = (step % 8 - i + 8) % 8;	// the lit one, then fading behind it
			const char* alpha = age == 0 ? "00" : age == 1 ? "40" : age == 2 ? "80" : "B0";
			char d[160];
			snprintf(d, sizeof d, "{\\an7\\pos(%d,%d)\\bord0\\shad0\\1c%s\\1a&H%s&\\p1}m 0 0 l 12 0 12 12 0 12{\\p0}\n", x - 6, y - 6,
				ass(TOP_TITLE).c_str(), alpha);
			ev += d;
		}
		ev += "{\\an8\\pos(480,390)\\bord2\\shad0\\fnInter\\fs30\\1c" + ass(TEXT) + "}";
		ev += loading() ? "Loading" : "Buffering";
		const char* cmd[] = {"osd-overlay", "4", "ass-events", ev.c_str(), "960", "720", "20", "no", "no", nullptr};
		mpv_command_async(mpv, 0, cmd);
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

	void Player::writeFile(const std::string& path, const std::string& text)
	{
		// written beside and renamed over: a power cut never leaves half a file
		std::string tmp = path + ".tmp";
		{
			std::ofstream out(tmp, std::ios::trunc);
			out << text;
			if (!out.flush()) return;
		}
		rename(tmp.c_str(), path.c_str());
	}

	void Player::setSettings(const Settings& s, bool persist)
	{
		for (auto& p : subStyleProperties(s.style)) prop(p.first.c_str(), p.second);
		bool displayChanged = s.musicShow != settings.musicShow || s.visualizer != settings.visualizer;
		bool loopChanged = s.loop != settings.loop;
		bool soundChanged = s.eqOn != settings.eqOn || s.eqGains != settings.eqGains || s.soundFx != settings.soundFx;
		settings = s;
		if (displayChanged && isAudio && loaded)
		{
			// the title's place at once; the graph once the presses stop (each change of it reopens the sound output,
			// a second on the handheld)
			updateOverlay();
			displayDueAt = ticks() + 500;
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
			writeFile(path, wav);
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

	void Player::showLevel(const std::string& label, int percent)
	{
		// an ASS card in the middle of a 960x720 canvas (the panel's 4:3; the title is at the top, subtitles at the
		// bottom): a dark box, the label and percent, a bar in yellow on teal.
		// ASS colours are &HBBGGRR&
		percent = std::max(0, std::min(100, percent));
		int fill = 260 * percent / 100;
		// with nothing playing, under the icon and the name rather than over them
		int dy = playing() ? 0 : 250;
		char ev[2048];
		snprintf(ev, sizeof ev,
			"{\\an7\\pos(330,%d)\\bord0\\shad0\\1c%s\\1a&H20&\\p1}%s{\\p0}\n"
			"{\\an7\\pos(350,%d)\\bord0\\shad0\\fs30\\1c%s}%s  %d%%\n"
			"{\\an7\\pos(350,%d)\\bord0\\shad0\\1c%s\\p1}%s{\\p0}\n"
			"{\\an7\\pos(350,%d)\\bord0\\shad0\\1c%s\\p1}%s{\\p0}",
			318 + dy, ass(TOP_CARD).c_str(), roundBox(300, 84, R_TOP_CARD).c_str(), 328 + dy, ass(TEXT).c_str(), label.c_str(), percent,
			372 + dy, ass(TOP_TRACK).c_str(), roundBox(260, 12, R_TOP_BAR).c_str(), 372 + dy, ass(TOP_TITLE).c_str(),
			roundBox(std::max(fill, 1), 12, R_TOP_BAR).c_str());
		const char* cmd[] = {"osd-overlay", "2", "ass-events", ev, "960", "720", "10", "no", "no", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	void Player::showIdle(bool on)
	{
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
			const struct { char c; std::string colour; } inks[] = {{'.', ass(TOP_SQUARE)}, {'Y', ass(TOP_TITLE)}, {'W', ass(TEXT)}};
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
		ev += "{\\an8\\pos(480,330)\\bord0\\shad0\\fnPixelify Sans\\fs72\\1c" + ass(TOP_TITLE) + "}Banana Player\n";
		ev += "{\\an8\\pos(480,420)\\bord0\\shad0\\fnInter\\fs30\\1c" + ass(TOP_SUB) + "}Pick a video or a song on the bottom screen";
		const char* cmd[] = {"osd-overlay", "3", "ass-events", ev.c_str(), "960", "720", "0", "no", "no", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	static std::string assText(std::string s);

	void Player::showElsewhere(bool on, bool playing, const std::string& where, bool waiting, int hint, const char* const* glyph, int glyphH)
	{
		if (!on)
		{
			const char* cmd[] = {"osd-overlay", "6", "none", "", nullptr};
			mpv_command_async(mpv, 0, cmd);
			return;
		}
		std::string ev = "{\\an7\\pos(0,0)\\bord0\\shad0\\1c" + ass(TOP_CARD) + "\\1a&H00&\\p1}m 0 0 l 960 0 960 720 0 720{\\p0}\n";
		// the page's mark over the title (its cells 12 units square, centred on 480, 236)
		if (glyph && glyphH > 0)
		{
			int w = 0;
			std::string path;
			for (int y = 0; y < glyphH; y++)
			{
				w = std::max(w, (int)strlen(glyph[y]));
				for (int x = 0; glyph[y][x];)
				{
					if (glyph[y][x] != '#') { x++; continue; }
					int e = x;
					while (glyph[y][e] == '#') e++;
					char r[96];
					snprintf(r, sizeof r, "m %d %d l %d %d %d %d %d %d ", x * 12, y * 12, e * 12, y * 12, e * 12, (y + 1) * 12, x * 12, (y + 1) * 12);
					path += r;
					x = e;
				}
			}
			char head[128];
			snprintf(head, sizeof head, "{\\an7\\pos(%d,%d)\\bord0\\shad0\\1c%s\\p1}", 480 - w * 6, 236 - glyphH * 6, ass(TOP_TITLE).c_str());
			ev += head + path + "{\\p0}\n";
		}
		// (a long name: the title smaller, kept on one line)
		std::string title = std::string(waiting ? "Waiting for " : playing ? "Playing in " : "Paused in ") + assText(where);
		int fs = title.size() > 34 ? 40 : title.size() > 26 ? 48 : 56;
		ev += "{\\an5\\pos(480,340)\\bord0\\shad0\\q2\\fnPixelify Sans\\fs" + std::to_string(fs) + "\\1c" + ass(TOP_TITLE) + "}" + title + "\n";
		if (waiting)
		{
			ev += "{\\an5\\pos(480,412)\\bord0\\shad0\\fnInter\\fs30\\1c" + ass(TEXT) + "}Tap Play here on that page to start the sound\n";
			ev += "{\\an5\\pos(480,464)\\bord0\\shad0\\fnInter\\fs28\\1c" + ass(TOP_SUB) + "}B: play on the handheld instead";
		}
		// (only what works on the page shown below: A is the player's; elsewhere, where to go for it)
		else if (hint == 1) ev += "{\\an5\\pos(480,412)\\bord0\\shad0\\fnInter\\fs30\\1c" + ass(TOP_SUB) + "}Hold A to play here";
		else if (hint == 2)
			ev += "{\\an5\\pos(480,412)\\bord0\\shad0\\fnInter\\fs30\\1c" + ass(TOP_SUB) + "}Go to the player to play it here";
		// a song with the equalizer or an effect on: the browser plays it without them (they're the handheld's)
		if (isAudio && (settings.eqOn || settings.soundFx != SoundFx::None))
			ev += "\n{\\an5\\pos(480,600)\\bord0\\shad0\\fnInter\\fs26\\1c" + ass(TOP_SUB) + "}Your equalizer and effects don't apply in the browser";
		const char* cmd[] = {"osd-overlay", "6", "ass-events", ev.c_str(), "960", "720", "5", "no", "no", nullptr};
		mpv_command_async(mpv, 0, cmd);
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
		bool untitled = chapterUntitled(chapters[index].title);
		std::string ev =
			"{\\an7\\pos(180,296)\\bord0\\shad0\\1c" + ass(TOP_CARD) + "\\1a&H20&\\p1}" + roundBox(600, 128, R_TOP_CARD) + "{\\p0}\n";
		// the line 1 note over the first or last chapter, in yellow after the count
		std::string note = edge < 0 ? "  \xc2\xb7  This is the first chapter" : edge > 0 ? "  \xc2\xb7  This is the last chapter" : "";
		auto line1 = [&](const std::string& text) {
			ev += "{\\an7\\pos(208,310)\\bord0\\shad0\\fnInter\\fs24\\1c" + ass(TOP_SUB) + "}" + text;
			if (!note.empty()) ev += "{\\1c" + ass(TOP_TITLE) + "}" + note;
			ev += "\n";
		};
		if (untitled)
		{
			// the count is the title line; a note has line 1 to itself (no empty title line under it)
			if (!note.empty())
				ev += "{\\an7\\pos(208,310)\\bord0\\shad0\\fnInter\\fs24\\1c" + ass(TOP_TITLE) + "}" + note.substr(6) + "\n";
			ev += "{\\an7\\pos(208,340)\\bord0\\shad0\\fnInter\\fs32\\1c" + ass(TEXT) + "}" + of + "\n";
		}
		else
		{
			line1(of);
			if (!untitled)
				ev += "{\\an7\\pos(208,340)\\bord0\\shad0\\fnInter\\fs32\\q2\\clip(180,296,780,424)\\1c" + ass(TEXT) + "}" +
					cardText(chapters[index].title, 32) + "\n";
		}
		// the bar: a segment a chapter by its start, a gap at each boundary (none in a segment under 6 units)
		if (duration > 0)
		{
			std::string teal, yellow;
			for (int i = 0; i < n; i++)
			{
				int x0 = (int)std::lround(std::min(1.0, std::max(0.0, chapters[i].time / duration)) * 544);
				int x1 = i + 1 < n ? (int)std::lround(std::min(1.0, std::max(0.0, chapters[i + 1].time / duration)) * 544) : 544;
				if (x1 - x0 >= 6 && i + 1 < n) x1 -= 2;
				x1 = std::max(x1, x0 + 1);
				char r[96];
				snprintf(r, sizeof r, "m %d 0 l %d 0 %d 10 %d 10 ", x0, x1, x1, x0);
				(i == index ? yellow : teal) += r;
			}
			ev += "{\\an7\\pos(208,398)\\bord0\\shad0\\1c" + ass(TOP_TRACK) + "\\p1}" + teal + "{\\p0}\n";
			ev += "{\\an7\\pos(208,398)\\bord0\\shad0\\1c" + ass(TOP_TITLE) + "\\p1}" + yellow + "{\\p0}";
		}
		const char* cmd[] = {"osd-overlay", "2", "ass-events", ev.c_str(), "960", "720", "10", "no", "no", nullptr};
		mpv_command_async(mpv, 0, cmd);
		cardMs = 2000;
	}

	void Player::showEqCard()
	{
		// the volume card's colours, in the same overlay (one replaces the other); with nothing playing, under the
		// icon and the name. The curve (centred, 334 to 626) under the text
		int dy = playing() ? 0 : 250;
		bool on = settings.eqOn, fx = settings.soundFx != SoundFx::None;
		std::string preset = settings.eqPreset.empty() ? "Custom" : settings.eqPreset, effect = soundFxName(settings.soundFx);
		// "Equalizer · Rock", "Rock · Concert hall" (the preset cut short for the effect's room), "EQ off · Cave",
		// "Equalizer off"
		std::string text = !fx ? (on ? "Equalizer \xc2\xb7 " + cardText(preset, 24) : "Equalizer off")
							   : (on ? cardText(preset, 16) : "EQ off") + " \xc2\xb7 " + effect;
		char e[1024];
		snprintf(e, sizeof e,
			"{\\an7\\pos(280,%d)\\bord0\\shad0\\1c%s\\1a&H20&\\p1}%s{\\p0}\n"
			"{\\an7\\pos(304,%d)\\bord0\\shad0\\fnInter\\fs28\\q2\\clip(280,%d,680,%d)\\1c%s}",
			300 + dy, ass(TOP_CARD).c_str(), roundBox(400, 124, R_TOP_CARD).c_str(), 312 + dy, 300 + dy, 424 + dy,
			ass(on || fx ? TEXT : TOP_SUB).c_str());
		std::string ev = e + text + "\n";
		snprintf(e, sizeof e, "{\\an7\\pos(334,%d)\\bord0\\shad0\\1c%s\\p1}m 0 0 l 292 0 292 2 0 2{\\p0}", 392 + dy, ass(TOP_TRACK).c_str());
		ev += e;
		// the curve: a bar a band, up or down from the teal line by its gain. One event each, its drawing from 0 0:
		// libass places a drawing by its own box, so a shared one would move with its highest bar
		for (int i = 0; i < EQ_BANDS; i++)
		{
			double g = settings.eqGains[i];
			int h = (int)std::lround(std::fabs(g) * 2.5);
			if (!h) continue;
			snprintf(e, sizeof e, "\n{\\an7\\pos(%d,%d)\\bord0\\shad0\\1c%s\\p1}m 0 0 l 22 0 22 %d 0 %d{\\p0}",
				334 + 30 * i, g > 0 ? 392 + dy - h : 394 + dy, ass(on ? TOP_TITLE : TOP_SUB).c_str(), h, h);
			ev += e;
		}
		const char* cmd[] = {"osd-overlay", "2", "ass-events", ev.c_str(), "960", "720", "10", "no", "no", nullptr};
		mpv_command_async(mpv, 0, cmd);
		cardMs = 1500;
	}

	void Player::showNoteCard(const char* const* rows, int h, const char* ink, const std::string& line1, const std::string& line2)
	{
		// the equalizer card's box and colours; the glyph's cells 4.5 units (\p2: drawn at twice the scale) on the
		// left, centred on (332, 362), the two lines right of it
		int dy = playing() ? 0 : 250, w = 0;
		for (int y = 0; y < h; y++) w = std::max(w, (int)strlen(rows[y]));
		std::string path;
		for (int y = 0; y < h; y++)
			for (int x = 0; rows[y][x];)
			{
				if (rows[y][x] != '#') { x++; continue; }
				int e = x;
				while (rows[y][e] == '#') e++;
				char r[96];
				snprintf(r, sizeof r, "m %d %d l %d %d %d %d %d %d ", x * 9, y * 9, e * 9, y * 9, e * 9, (y + 1) * 9, x * 9, (y + 1) * 9);
				path += r;
				x = e;
			}
		char e[1024];
		snprintf(e, sizeof e,
			"{\\an7\\pos(280,%d)\\bord0\\shad0\\1c%s\\1a&H20&\\p1}%s{\\p0}\n"
			"{\\an7\\pos(%d,%d)\\bord0\\shad0\\1c%s\\p2}",
			300 + dy, ass(TOP_CARD).c_str(), roundBox(400, 124, R_TOP_CARD).c_str(), (int)std::lround(332 - w * 2.25), (int)std::lround(362 + dy - h * 2.25), ink);
		std::string ev = e + path + "{\\p0}\n";
		snprintf(e, sizeof e, "{\\an4\\pos(382,%d)\\bord0\\shad0\\fnInter\\fs28\\q2\\clip(280,%d,668,%d)\\1c%s}", 345 + dy, 300 + dy, 424 + dy, ass(TEXT).c_str());
		ev += e + cardText(line1, 30) + "\n";
		snprintf(e, sizeof e, "{\\an4\\pos(382,%d)\\bord0\\shad0\\fnInter\\fs22\\q2\\clip(280,%d,668,%d)\\1c%s}", 384 + dy, 300 + dy, 424 + dy, ass(TOP_SUB).c_str());
		ev += e + cardText(line2, 30);
		const char* cmd[] = {"osd-overlay", "2", "ass-events", ev.c_str(), "960", "720", "10", "no", "no", nullptr};
		mpv_command_async(mpv, 0, cmd);
		cardMs = 1500;
	}

	void Player::hideLevel()
	{
		const char* cmd[] = {"osd-overlay", "2", "none", "", nullptr};
		mpv_command_async(mpv, 0, cmd);
	}

	void Player::osd(const std::string& text, int ms)
	{
		std::string d = std::to_string(ms);
		const char* cmd[] = {"show-text", text.c_str(), d.c_str(), nullptr};
		mpv_command_async(mpv, 0, cmd);
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
		prop("lavfi-complex", "");
	}

	void Player::clearHistory()
	{
		played.clear();
		recentSer++;
		if (!opts.stateDir.empty()) unlink((opts.stateDir + "/history.tsv").c_str());
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
		// the art's choices need art: a song with none shows its title alone (Album art: no visualizer the person
		// didn't pick) or the visualizer (Art + visualizer); None is none either way
		int show = settings.musicShow == SHOW_NONE || hasCover(tracks) ? settings.musicShow
			: settings.musicShow == SHOW_ART ? SHOW_NONE : SHOW_VIS;
		// the manga reader over the top panel: a visualizer nobody sees would still make its 30 frames a second
		// (mpv's sound waits on the graph) on the cores the reader decodes on; the bare graph meanwhile
		if (topCovered && (show == SHOW_VIS || show == SHOW_BOTH)) return SHOW_NONE;
		return show;
	}

	void Player::setTopCovered(bool on)
	{
		if (on == topCovered) return;
		topCovered = on;
		fprintf(stderr, "music: top panel %s\n", on ? "covered: the visualizer rests" : "shown again: the visualizer back");
		if (isAudio && loaded) applyAudioDisplay();
	}

	std::string Player::graphFor(int vis, bool overCover)
	{
		// Circle's tables: made the first time it's wanted, beside the settings, else (none kept, or a folder a
		// filter graph can't name) in /tmp. Tried again at the next song when they couldn't be written (a full card)
		// (and the newer styles' stills, the same once: src/viz_more.h)
		if (visualizerNeedsMaps(vis) && circleMaps.empty())
		{
			std::string dir = lavfiSafePath(opts.stateDir) ? opts.stateDir : "/tmp/banana-player";
			if (writeVisualizerMaps(dir)) circleMaps = dir;
			else fprintf(stderr, "banana-player: the visualizers' stills couldn't be written in %s: drawn as Bars or Peaks\n", dir.c_str());
		}
		return visualizerGraph(vis, overCover, circleMaps);
	}

	void Player::applyAudioDisplay()
	{
		// for the next song's guess (load): an album's songs mostly all have art, or none
		size_t slash = currentPath().rfind('/');
		lastArtDir = slash == std::string::npos ? "." : currentPath().substr(0, slash);
		lastArt = hasCover(tracks);
		int show = shownMusic();
		std::string graph = show == SHOW_ART ? "" : show == SHOW_NONE ? blankGraph() : graphFor(settings.visualizer, show == SHOW_BOTH);
		bool had = !graphSet.empty();
		if (graphSet != graph)
		{
			// (in the log: a graph changed as a song starts, by the art its track list says it has)
			fprintf(stderr, "music: %s: %s art, %d tracks; graph %s\n", baseName(currentPath()).c_str(),
				hasCover(tracks) ? "has" : "no", (int)tracks.size(), graph.empty() ? "off" : graph.find("[vid1]") != std::string::npos ? "with the art" : "without art");
			syncEq(true);	// the graph's change rebuilds the chain from af
			prop("lavfi-complex", graph);
		}
		if (graph.empty() && had)
		{
			// a graph taken off leaves no track chosen: the song's sound and its art back
			prop("aid", songAid(seenAid));
			// by its id: "auto" while playing doesn't pick the art again
			int64_t art = 0;
			for (auto& t : tracks)
				if (t.type == TrackType::Video && !art) art = t.id;
			prop("vid", art ? std::to_string(art) : "auto");
		}
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

	void Player::updateOverlay()
	{
		if (!isAudio)
		{
			const char* cmd[] = {"osd-overlay", "1", "none", "", nullptr};
			mpv_command_async(mpv, 0, cmd);
			return;
		}
		std::string title = this->title, artist = tagArtist, album = tagAlbum;
		// over the art alone: at the bottom; with a visualizer (along the bottom): at the top; alone: in the middle, bigger
		int show = shownMusic();
		const char* at = show == SHOW_ART ? "{\\an2}" : show == SHOW_NONE ? "{\\an5}" : "{\\an8}";
		int big = show == SHOW_NONE ? 56 : 40;
		// Pocket console alone: the screen's own ink, dark green in the pixel font, no outline (the rest of the text
		// takes it on; the album line its mid green)
		bool pocket = show == SHOW_VIS && settings.visualizer == VIS_POCKET;
		std::string text = std::string(at) + (pocket ? "{\\fnPixelify Sans}{\\bord0}{\\shad0}{\\1c&H0F380F&}" : "{\\bord3}") +
			"{\\fs" + std::to_string(big) + "}{\\b1}" + assText(title) + "{\\b0}";
		// an audiobook's (or a song's) chapter, under the title in yellow: where in the book this is
		int c = currentChapter();
		if (c >= 0 && c < (int)chapters.size())
		{
			std::string label = chapterUntitled(chapters[c].title)
				? "Chapter " + std::to_string(c + 1) + " of " + std::to_string(chapters.size()) : cardText(chapters[c].title, 36);
			// (no \q2: libass takes an event's last wrap style for all of it, and the title would stop wrapping; the
			// label's cut keeps it to one line)
			text += "\\N{\\b0}{\\fs" + std::to_string(big * 3 / 4) + "}{\\c" + (pocket ? std::string("&H306230&") : ass(TOP_TITLE)) + "}" +
				assText(label) + (pocket ? "{\\c&H0F380F&}" : "{\\c&HFFFFFF&}");
		}
		if (!artist.empty()) text += "\\N{\\fs" + std::to_string(big * 3 / 4) + "}" + assText(artist);
		if (!album.empty()) text += "\\N{\\fs" + std::to_string(big * 2 / 3) + "}{\\c" + (pocket ? "&H306230&" : "&HB4B4B4&") + "}" + assText(album);
		const char* cmd[] = {"osd-overlay", "1", "ass-events", text.c_str(), "0", "720", "0", "no", "no", nullptr};
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
			// what the top panel shows, as mpv has it now
			std::string graph = get("lavfi-complex");
			std::string vis = graph.find("showwaves") != std::string::npos ? "Waves"
							  : graph.find("showspectrum") != std::string::npos ? "Spectrum"
							  : graph.find("] remap [") != std::string::npos ? "Circle"
							  : graph.find("lagfun") != std::string::npos ? "Peaks" : "Bars";	// the colours aren't told apart
			row("Top screen", graph == blankGraph() ? "Title only"
				: graph.find("[vid1]") != std::string::npos ? "Album art + " + vis
				: !graph.empty() ? "Visualizer: " + vis
				: shownMusic() == SHOW_NONE ? "Title only"
				: hasCover(tracks) ? "Album art" : "Title");
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
