#pragma once
#include <chrono>
#include "aspect.h"
#include "eq.h"
#include "history.h"
#include "levelcard.h"
#include "photo.h"
#include "previewpane.h"
#include "resume.h"
#include "settings.h"
#include "tracks.h"
#include "viztap.h"
#include <algorithm>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace bp
{
	class NetStreams;

	struct PlayerOptions
	{
		std::string stateDir;	// settings.conf and resume.tsv; empty: nothing kept
		std::string lang;		// preferred audio and subtitle languages, mpv's alang/slang ("eng,en")
		std::string fontsDir;	// the subtitle fonts (assets/fonts)
		bool verbose = false;
		NetStreams* net = nullptr;	// bpnet:// files (none: they don't open)
		// the photo viewer's own mpv (main's second Player): no sound output at all, so what plays goes on in the first
		bool photosOnly = false;
	};

	// where what plays comes from: a folder (its videos or songs, in name order), a network folder, or a playlist
	enum class SourceKind { Folder, NetFolder, Playlist };
	struct PlaySource
	{
		SourceKind kind = SourceKind::Folder;
		std::string title;	// the folder's name, or the playlist's
		std::string id;		// the folder, or the playlist's file
		bool shuffled = false;	// the paths came in a shuffled order (Loop all replays the same one)
	};

	// libmpv: demuxing, decoding, A/V sync, tracks and subtitles; the playlist and what is remembered of each file.
	// The window and the buttons are the caller's.
	class Player
	{
	public:
		~Player() { shutdown(); }

		// wakeup is called on mpv's threads when events are waiting: the caller wakes its loop and calls pollEvents
		bool init(const PlayerOptions& o, void (*wakeup)(void*), void* ctx);
		// OpenGL (or GLES) context current on this thread; update is called on mpv's threads when a frame is due
		bool initRender(void* (*getProc)(void*, const char*), void* procCtx, void (*update)(void*), void* ctx);
		void shutdown();

		// plays list[index], then the others in order with next() and prev(). folderNames: a network folder's file
		// names (all of them), where its subtitles and cover are found (mpv can't look in it itself). The source is the
		// folder (Folder, or NetFolder for a bpnet:// one)
		void open(const std::vector<std::string>& list, int index, const std::vector<std::string>& folderNames = {});
		// a playlist's items (local and bpnet:// mixed, in its order: shuffled already for Shuffle play) from paths[index].
		// Photos are dropped. A local item that isn't there is skipped, here and by next/prev/advance. A network item's
		// subtitles and cover need its folder's listing, which the caller fetches: see netFolderWanted
		void openPlaylist(const std::vector<std::string>& paths, int index, const PlaySource& source);
		// the playlist that plays was edited: its new items, and the index of the one playing among them (where it
		// was, if it was removed: it plays on, and Next goes to what followed it). Nothing is reloaded
		void updatePlaylist(const std::vector<std::string>& paths, int current);
		// the playlist that plays (id) was renamed; newId: its file, if that changed too
		void renameSource(const std::string& id, const std::string& title, const std::string& newId = "");
		const PlaySource& source() const { return src; }
		bool next();	// false on the last one
		bool prev();	// false on the first one
		// Previous: RESTART_S or more into a song or video, back to its start (it stays); before that,
		// the one before. A photo always steps. false: the first one, under RESTART_S in (nothing happens)
		static constexpr double RESTART_S = 5;
		bool back();
		// to that file of the list as it plays (the one nearest hint when it's there twice; the Session's Step and
		// Ended): false when the list hasn't it
		bool jumpTo(const std::string& path, int hint);
		// a file out of the list (Up next's Remove from list): the entry nearest hint that isn't the one playing; the
		// one playing leaves as the next takes over (the ghost). False: none
		bool dropEntry(const std::string& path, int hint);
		// at a file's end: the next one, or (Loop all) the first again; false when that's all
		bool advance();
		bool playingAudio() const { return isAudio && !playlist.empty(); }
		// the manga reader is over the top panel: a song's visualizer rests (the bare graph) until it's gone
		void setTopCovered(bool on);
		// the manga engine is starting (its Java process takes ~25 s of the cores): the visualizer rests the same way,
		// and the top panel says why under the song's title
		void setEngineLoading(bool on);
		bool playingPhoto() const { return isPhoto && !playlist.empty(); }
		int index() const { return current; }
		int count() const { return (int)playlist.size() - (ghost >= 0 ? 1 : 0); }

		// A network folder's listing, for its files' subtitles and cover (mpv can't look beside a URL). A folder's
		// open() has it; a playlist's items come from anywhere, so the caller lists their folders: each turn, a folder
		// netFolderWanted() names ("" none) is listed (once: not again while that listing is under way) and the answer
		// given to netFolderListed (ok false: it failed). The item about to open waits for its folder's listing, 1.5 s
		// at most (then it opens without); the next network item's folder is asked for ahead of time
		std::string netFolderWanted() const;
		void netFolderListed(const std::string& dir, const std::vector<std::string>& names, bool ok);
		// the item waits for its folder's listing (loading() is true meanwhile)
		bool netWaiting() const { return !netWait.dir.empty(); }

		// applies what mpv has sent since the last call
		void pollEvents();
		// called now and then: sound that didn't open is opened again
		void checkAudio();
		// called each pass: the song's art, decoded on a thread of its own for Art + visualizer, laid over once it's in
		void checkSongArt();
		// Native visualizers (src/viznative.h: Mirror bars, drawn by the player, not mpv; docs/notes/2026-11-19-viz-native.md).
		// Called each pass: the tap started or stopped with what the top panel shows, mpv's clock told to it, a file
		// it couldn't read put back on the graph style
		void syncNative();
		// the top panel draws the native style now (a local song, its display a visualizer, not resting)
		bool nativeShown() const { return (vizTap.running() && !vizTap.failed()) || nativeIdle; }
		bool nativeBand() const { return nativeOverArt; }	// Art + visualizer: the band along the foot
		// the newest targets (VN_MAX_BARS room), with the style and band count they're for, if new since the last take
		bool nativeTake(float* out, int& vis, int& n, VizHint& hint) { return vizTap.take(out, nativeSeq, vis, n, hint); }
		int nativeStyle() const { return settings.visualizer; }
		// where the shapes mustn't go (640x480 units): mpv's cards (the song's lines and the art are drawn over them:
		// mpv's layer, topviz.cpp)
		std::vector<VizRect> nativeKeepClear() const;
		// mpv's ground clear (0 0 0 0) under a song's native visualizer: the top panel composes mpv's picture (the art,
		// the lines, the cards) over the style's shapes, so the lines have no box round them
		bool groundClear() const { return groundSet == GROUND_CLEAR; }
		// the top panel can't compose (no framebuffers): mpv's ground the theme's again, its picture under the shapes
		void nativeLayerFailed();
		// Art + visualizer: the art's box (Rain check's drops bounce off it); empty otherwise
		VizRect nativeArtBox() const { return shownMusic() == SHOW_BOTH ? groupArtBox : VizRect{0, 0, 0, 0, 0}; }
		void nativeDrew() { nativeFrames++; }	// (the log's frames a second)
		// the cards up now (mpv's image overlays: level, state, note...), in 640x480 units: the bars are drawn round them
		std::vector<VizRect> cardsUp() const;
		// the tap's wake (from its thread): main pushes an SDL event
		void setNativeWake(std::function<void()> w) { nativeWake = std::move(w); }
		// called now and then: a song with a visualizer (or None's picture) at its end. The filter graph's picture
		// never ends, so mpv never ends the file: the clock runs on past the song. Ended here instead (or, Loop one,
		// started again)
		void checkSongEnd();
		// called now and then: a network file whose connection gave up while mpv waits for more of it (a seek, an
		// empty cache) waits for ever (to mpv the stream just ended, mid-seek): stopped, with why
		void checkNet();
		// a file opened and nothing of it shown yet (its first picture still decoding, a slow card); not an error
		bool loading() const;
		// waiting for more of the file: mpv paused it till its cache fills, or a seek that takes a while (to a part
		// not read yet, on a slow network)
		bool buffering() const;
		// called each turn of the loop: the top panel's spinner while loading() lasts (from 0.3 s: a quick file
		// shows none)
		void checkLoading();
		// called now and then: a photo's size, once mpv knows it (the view is set to it), and its thumbnail
		void checkPhoto();
		bool frameDue();
		// mpv's result: < 0 when it couldn't draw (in the log, the first few times: a black top panel's cause)
		// fbo: the target (0 the window's; main's small one for what plays under a photo)
		// skip: take the frame, draw nothing; noWait: never held till the frame's display time (the native visualizer
		// draws at its own rate over mpv's 5 fps ground: a wait there stalled its bars up to 60 ms, five times a second)
		int render(int w, int h, unsigned fbo = 0, bool skip = false, bool noWait = false);
		void reportSwap();

		// a photo is up over what plays (on the photo viewer's own mpv): a video's picture is neither decoded nor drawn
		// meanwhile (vid=no; its sound plays on), and comes back where it has got to (mpv's track switch seeks the
		// new decoder there). A song's graph keeps drawing (main draws it under the photo): its sound comes through it
		void hideVideo(bool hide);
		bool videoHidden() const { return hidingVideo; }

		void togglePause();
		void unpause();
		// paused, however often it's asked (a toggle twice would play): async, as the rest
		void pause();
		// paused before it ever sounds (a page plays it; the handheld shows it): the sound output then opens paused, and
		// on the handheld it stayed silent once played (the picture and the visualizer ran on). The first play after
		// this reopens the output
		void holdPaused();
		bool heldUnplayed = false;
		// from any thread: the lid closed, the handheld slept
		void pauseNow();
		void seekRelative(double seconds);
		void seekFraction(double fraction);	// to the frame
		void setSpeed(double s);
		// the Picture sheet's Adjust page: brightness, contrast, saturation and gamma (mpv's video equalizer, each
		// -100..100; 0 is the film as it is). They apply to every video while the app runs, never to a photo.
		double videoAdjust(int i) const { return videoAdj[std::clamp(i, 0, 3)]; }
		void setVideoAdjust(int i, double v);
		// the Picture sheet's looks (picturelook.h): the one on the video playing, its own ("Only this video", kept in
		// resume.tsv) or every video's (Settings' Picture look), at its strength
		struct LookInUse { int look = LOOK_OFF, strength = 60; bool onlyThis = false; };
		LookInUse lookInUse() const;
		// a look chosen: applied at once (mpv's vf, async; the time it took is in the log), kept for every video or
		// for this one (every video's is then left as it is)
		void setLook(int look, int strength, bool onlyThis);
		// Y held on the Picture sheet: the look's vf entry turned off, on again when let go (its flag, not a new list)
		void compareLook(bool off);
		bool lookCompared() const { return lookBypass; }
		// a choice of the player's: also the language a file opened for the first time starts with
		void selectTrack(TrackType type, int64_t id);
		void setAspect(AspectMode m);
		// applies at once and keeps them in settings.conf (persist false: not yet, as the equalizer's bands move;
		// commitEq writes them). A change of the music display updates the title at once and the filter graph 0.5 s
		// later (checkDisplay), so quick presses rebuild it once
		void setSettings(const Settings& s, bool persist = true);
		// called each turn of the loop: the music display's graph, once its changes have settled
		void checkDisplay();
		void displayNow() { if (displayDueAt) displayDueAt = 1; }	// (tests: the debounced display change at once)
		// called each turn of the loop (and with all=true at shutdown): the saves that waited out their second
		void flushWrites(bool all = false);
		// called each turn of the loop: with BP_CPU_LOG=1, the player's CPU use in the log every 30 s while playing
		void checkCpu();

		// The equalizer (music only; settings.eqOn, eqPreset, eqOrigin, eqGains). Its curve plays as soon as
		// setSettings has it (each band's change sent alone, nothing rebuilt); commitEq makes it the af mpv keeps and
		// writes settings.conf: as the Equalizer page closes (and at saveResume, quitting)
		void commitEq();
		// the built-in presets, then yours (alphabetical); eqPresetSerial changes with the list
		const std::vector<EqPreset>& eqPresets() const { return eqStore.all(); }
		const std::vector<EqPreset>& eqUserPresets() const { return eqStore.user(); }
		int eqPresetSerial() const { return eqSerial; }
		// a preset of yours from these gains, then the one playing (eqPreset = its name, no origin). overwrite false:
		// a name you already have is Taken; true: that one's gains replaced. Built-in names are refused
		EqNameError saveEqPreset(const std::string& name, const EqGains& gains, bool overwrite);
		// one of yours renamed (the settings follow, if it's the one playing or Custom's origin)
		EqNameError renameEqPreset(const std::string& from, const std::string& to);
		// one of yours gone. The one playing becomes Custom with the same gains (nothing heard changes), and Custom
		// made from it has no origin any more
		bool deleteEqPreset(const std::string& name);
		// the card over the top panel: "Equalizer · Rock" and the curve, or "Equalizer off" (1.5 s, see takeCardShown)
		void showEqCard();
		// the note (levelcard.h), bottom centre: an optional glyph (rows of '#' cells), a line in yellow (red: danger), and
		// under it a dim one (the title it was for). 3 s
		void showNoteCard(const char* const* rows, int h, const std::string& line1, const std::string& line2, bool danger = false, int ms = 0);
		void hideNote();	// the note down now (one kept until the first key: the sleep timer's on waking)
		// the sleep timer's last minute (ui_sleep.h): its card where the note goes, kept until dropped (shown again each
		// second with the time left), over every other card; and, while one runs over a song, its quiet mark top right
		void showSleepCard(const char* const* rows, int h, const std::string& line1, const std::string& line2);
		void dropSleepCard();
		// main's: the sleep card over a reader's pages or the Preview pane (mpv's own is under them); null: it goes
		std::function<void(const LevelCardImage*)> sleepCardOnTop;
		// main's: a note over the Preview pane (mpv's overlay would be under it, and the pane would have to go).
		// True: drawn there, no mpv overlay. Null card: the note goes
		std::function<bool(const LevelCardImage*, int)> noteOnTop;
		void showSleepMark(const char* const* rows, int h, const std::string& text);
		void dropSleepMark();
		// a short note, one line (a failure's, "Couldn't ...", in red); ms at least 3 s
		void osd(const std::string& text, int ms = 3000);
		// the level card over the top panel ("Volume", 85; levelcard.h's picture as an mpv image overlay), till
		// hideLevel. The bar's top is the Max volume setting for "Volume", 100 for the rest
		void showLevel(const std::string& label, int percent);
		void hideLevel();

		// Chapters (the file's own markers; mpv's chapter-list and chapter, observed, never read)
		std::vector<Chapter> chapters;
		int chapter = -1;		// mpv's current one, -1 before the first
		int chapterSerial = 0;	// bumped when the list changes
		// the chapter shown as current: a jump made less than a second ago counts at once, till mpv's answer is in,
		// so two quick presses step twice instead of landing on the same start
		int currentChapter() const;
		// to chapter `index` (CHAPTER_FILE_START: to 0), at once, no wait for mpv
		void seekChapter(int index);
		// previous (dir < 0) or next (dir > 0): seeks and shows the card; returns chapterTarget's answer (an index,
		// CHAPTER_FILE_START or CHAPTER_NONE, which also shows the card's first/last note). A file with no chapters
		// says so; a photo, a file not started or an error does nothing
		int stepChapter(int dir, double at = -1);	// at: count from there (a page playing it), not this copy's position
		// the card over the top panel for chapter `index`; edge < 0: "This is the first chapter", > 0: "...last"
		void showChapterCard(int index, int edge = 0);
		// once after a card was shown: how long the caller keeps it up (its levelUntil), then hideLevel(); 0: none
		int takeCardShown() { int ms = cardMs; cardMs = 0; return ms; }
		// the top panel with nothing playing: the icon, the name, where to pick something
		void showIdle(bool on);
		bool idleShown = false;
		// the theme swapped: what stays up on the top panel with its colours in it (the idle title, a song's lines) sent again
		void themeChanged();
		// the top panel's pixels (main sets it): the idle icon is drawn in whole pixels at that size
		void setTopSize(int w, int h) { topW = w; topH = h; }
		int topW = 0, topH = 0;
		// the top panel while a web page plays the music instead: the paused picture dimmed, and why (a paused
		// visualizer looks frozen otherwise). where: the page's name ("Chrome on Mac"); waiting: it was refused the
		// sound without a tap there (Play on from here), and B plays here instead; glyph: drawn over the title (G_LAPTOP)
		// hint: how to play here from the bottom panel as it is (1 hold A, 2 go to the player, 0 none)
		void showElsewhere(bool on, bool playing = true, const std::string& where = "your browser", bool waiting = false, int hint = 1,
			const char* const* glyph = nullptr, int glyphH = 0);
		// the top panel's output made with nothing playing: mpv 0.40 makes force-window's only as it goes idle, and
		// it's idle from the start, before force-window could be set (it needs the render context): black, logo and
		// level cards unseen, till a file had played. An empty load fails at once and sends it through idle again
		void wakeOutput();
		// keeps the file's position and tracks in resume.tsv, as far as the settings say
		void saveResume();
		// back to no file (the browser): the place kept first
		void stop();
		bool playing() const { return !playlist.empty(); }
		// a Loading, state or note card of the player's is up on the top panel (the Preview pane waits for it to go)
		bool topCardUp() const { return shownSerial[OV_NOTE] != 0; }
		// the State card the top panel draws: "Playing in your browser" first, else Loading / Buffering; null: none
		const PaneSpec* topState() const { return topStateElsewhere ? &topStateElsewhereSpec : topStateLoad ? &topStateLoadSpec : nullptr; }
		std::string currentPath() const { return playlist.empty() ? "" : playlist[current]; }
		// the files it plays from, in their order (index(): the one playing)
		const std::vector<std::string>& paths() const { return playlist; }
		const ResumeStore& resumeStore() const { return resume; }
		const PlayHistory& history() const { return played; }
		// changes with the history or a place kept (the web's Recent is told)
		int recentSerial() const { return recentSer; }
		void clearHistory();
		// Home's Recent: one taken out of the play history; one played from its start next time (its tracks kept)
		void removeFromHistory(const std::string& path);
		// several taken out at once (Clear missing, Home's and the dashboard's): how many were there
		int removeFromHistory(const std::vector<std::string>& paths);
		// where a song of Recent played from (main, as the Session's list settles): written only when it changed
		void setRecentContext(const std::string& path, const RecentContext& ctx);
		// a document opened from Files (a TXT, EPUB, PDF, a comic): in Recent as a video is (history off or
		// incognito: not)
		void addRecent(const std::string& path);
		// a file or folder moved (Files' Rename or Move, the dashboard's): Recent and the places kept follow it
		void pathsMoved(const std::string& from, const std::string& to);
		void forgetPosition(const std::string& path);
		// the browser's folder, kept (in resume.tsv) only while Remember last folder is on
		void setLastFolder(const std::string& dir);

		double position = -1, duration = -1, speed = 1;
		bool paused = false, loaded = false;
		bool finished = false;	// the file played to its end (the caller moves on, then clears it)
		bool quit = false;		// mpv itself is going away
		// Incognito mode (the Ui sets it from Quick settings, never saved): what plays stays out of Recent; where
		// a film stopped is still kept
		bool incognito = false;
		AspectMode aspect = AspectMode::Fit;
		double videoAdj[4] = {0, 0, 0, 0};	// the Picture sheet's Adjust: brightness, contrast, saturation, gamma
		Settings settings;
		std::string title, error;
		int loads() const { return loadSerial; }	// how many files it has opened (each load counts)
		bool errorNet = false;
		// the volume over 100%: mpv's own gain (100 = none), up to settings.volumeMax, on top of the handheld's 100%
		// (Ui::setVolumeTotal). Back to 100 at each start: a loud evening isn't the next one's
		int boost = 100;
		void setBoost(int v);
		// the sleep timer's fade (ui_sleep.h): a factor on mpv's own volume, 1 none (the handheld's volume never moves)
		void setFade(double f);
		double fade = 1;
		// the audio diagnostics (Player::logAudio): mpv's audio-params, audio-out-params, current-ao, af, lavfi-complex
		std::string audioIn, audioOut, audioAo, audioAf, audioLavfi;
		void logAudio(const char* what) const;	// the error is the network's (Play tries the file again), not the file's
		bool errorNetFound = true;	// with errorNet: the server answered before (a connection lost), else it couldn't be reached
		std::vector<Track> tracks;
		// a song's artist and album from its tags ("" none)
		const std::string& artist() const { return tagArtist; }
		const std::string& album() const { return tagAlbum; }
		// a song with no cover (the Art placeholder stands in), and what its drawing is keyed by: the album's name, else
		// the title, else the file's name
		bool songArtMissing() const;
		std::string artPlaceholderKey() const;

		// the Media info page: sections (header rows) of label/value rows, read from mpv as it plays
		struct InfoRow
		{
			std::string label, value;
			bool header = false;
		};
		std::vector<InfoRow> mediaInfo() const;
		// one of mpv's properties as text ("mpv-version", "ffmpeg-version"...); "" where mpv has none
		std::string property(const char* name) const { return get(name); }

		std::vector<Track> tracksOf(TrackType type) const;
		const Track* selected(TrackType type) const;

		// A photo's view on the top panel: zoom (a factor of Fit, or of Fill), the place (mpv's video-pan: minus the
		// shown part's centre, as a fraction of the photo from its centre), a turn of the photo's own (L3), Fit or Fill.
		// The zoom, the place in it (as a share of how far it can go) and Fit/Fill pass to the next photo (Keep zoom
		// between photos); the turn doesn't
		struct PhotoView
		{
			double zoom = 1, panX = 0, panY = 0;
			int rotate = 0;
			bool fill = false;
		};
		const PhotoView& photo() const { return view; }
		// the photo's size as shown (turned), 0 till mpv knows it
		int photoWidth() const { return turned() ? photoH : photoW; }
		int photoHeight() const { return turned() ? photoW : photoH; }
		// the photo at its view, in panel widths and heights
		PhotoScale photoScaleNow() const;
		// clamped to the photo's edges, and shown
		void setPhotoView(double zoom, double panX, double panY);
		void rotatePhoto();	// a quarter turn clockwise
		void togglePhotoFill();
		// the photo small, for the bottom panel's pan area (BGRX, thumbW x thumbH, empty till it's taken); thumbTurn:
		// the clockwise turn that shows it as the top panel does; thumbId changes with each new one
		std::vector<unsigned char> thumb;
		int thumbW = 0, thumbH = 0, thumbId = 0;
		int thumbTurn() const;
		// the thumbnail still to come (not after an error, nor after mpv gave none ten times)
		bool thumbPending() const { return isPhoto && !thumbW && error.empty() && thumbTries < 10; }

	private:
		void wakeHeld();	// heldUnplayed: the sound output reopened (mpv's ao-reload) as it starts to play
	public:
		// the sound output opened again (mpv's ao-reload): a speaker amplifier that didn't start gets another start
		// (ampwatch.h), or BananaOS moved the sound (headphones in or out, Bluetooth) and only a device opened after the
		// move follows it; nothing while no sound plays. why: the log line's reason
		void reloadAudio(const char* why = "a speaker's amplifier didn't start");
	private:
		void load();
		// load()'s loadfile, with the network file's options (its folder's sidecars): at once, or once the folder's
		// listing is in (or its wait is over)
		void sendLoad();
		// the index dir steps away from `from` (+1, -1), -1 when there's none. A playlist's local items that are gone
		// are passed over (said on the top panel), and so is the ghost
		int step(int from, int dir);
		void moveTo(int index);	// saveResume, then that index loaded
		PlaySource src;
		// the playing item, removed from the playlist by an edit (updatePlaylist): kept in it at its place till
		// something else plays, so that it plays on and Next goes on from there; -1 none
		int ghost = -1;
		std::string skipNote;	// step's "Skipped a missing file", shown as the file it went to is up
		// a playlist's network item waiting for its folder's listing before its loadfile is sent; dir "" none
		struct NetWait
		{
			std::string dir;
			uint64_t deadline = 0;
		} netWait;
		std::string pendingOpts;	// the loadfile's options, but the network ones (sendLoad adds them)
		const std::vector<std::string>& namesIn(const std::string& dir) const;
		void keepNames(const std::string& dir, const std::vector<std::string>& names);
		std::string netFileOptions(const std::string& path) const;	// loadfile's options for a network file
		// a local video's subtitle files in an old code page, as UTF-8 copies mpv is given instead (mpv's own conversion
		// goes through iconv, which has no code pages on BananaOS: issue #12); "" when every one is UTF-8 already
		std::string localSubOptions(const std::string& path) const;
		void refreshTracks(mpv_node* trackList);
		void clearChapters();
		int cardMs = 0;
		// the level card's pixels by the serial of the command that took them: mpv reads them when the command runs
		// (and again while shown), so they live till its answer, the shown one till it's replaced or removed
		// overlay-add's ids (the idle icon has 0): later ones are drawn over earlier ones
		// 1: the song art (ART_OVERLAY), under every card; 3: the sleep timer's mark (under the level card); 6: its
		// last minute's card, over all
		static constexpr int OV_SLEEP_MARK = 3, OV_LEVEL = 4, OV_NOTE = 5, OV_SLEEP = 6, OV_COUNT = 7;
		std::string sleepMarkText;	// the mark shown ("" none): put again only when it changes
		void sendVolume();
		bool topStateElsewhere = false, topStateLoad = false;
		PaneSpec topStateElsewhereSpec, topStateLoadSpec;
		std::map<uint64_t, std::shared_ptr<std::vector<uint8_t>>> levelPixels;
		uint64_t levelSerial = 0, shownSerial[OV_COUNT] = {};
		VizRect cardAt[OV_COUNT] = {};	// each card's box while it's up, in 640x480 units (the native bars leave it clear)
		uint64_t noteUntil = 0;	// the note's time (ticks), 0: none up
		void levelReplied(uint64_t serial);
		void putImage(int id, const LevelCardImage& card, int x, int y);	// a card as overlay `id`
		void dropImage(int id);	// overlay `id` removed (another card takes its spot, or its time is up)
		void dropLevelCard() { dropImage(OV_LEVEL); }
		void showLevelCard(const LevelCardImage& card, int ms);	// the top-right spot's: level, chapter, Equalizer
		bool pendingOn = false;		// a chapter jump made, mpv's answer not in yet
		int pendingChapter = -1;
		uint64_t pendingAt = 0;
		// the file opened and its track list known: the art's choice, the title, tracks gone missing
		void fileReady();
		bool readyPending = false;	// opened, its track list still to come
		// the playlist entry mpv made of this file's loadfile (its answer), the last one mpv started, and whether
		// the track list is this file's: one that comes before it starts is the last file's, read before it closed
		int64_t loadEntry = -1, startedEntry = -1;
		bool tracksMine = false;
		int wakeLoads = 0;			// wakeOutput's loads still to end (their failure is no file's)
		void prop(const char* name, const std::string& value);
		// an audio file: the visualizer (Audio display) and the title, artist and album over the top panel
		void applyAudioDisplay();
		std::string graphSet;	// lavfi-complex as last set (prop)
		// the freeze's log (watchLog: the play state for 10 s after a graph change; last values seen, never read)
		// the look's shader file as last sent to glsl-shaders ("": none), Y's compare, and when a change was sent (its
		// answer logs the time); the files written this run, and those mpv refused (Off from then on)
		std::string lookShaderSent;
		bool lookBypass = false;
		std::chrono::steady_clock::time_point lookSentAt{};
		int lookInFlight = 0;
		std::set<std::string> lookWritten, lookRefused;
		std::string lookShaderNow();
		void applyLook();
		void lookRejected();
		std::string setValues[12], graphLabel, graphHash, seenPause = "?", seenCoreIdle = "?", seenIdleActive = "?", seenEof = "?";
		uint64_t graphChangeAt = 0;
		void watchLog(const char* name, const char* value) const;
	public:
		void logPlayPressed(const char* sessionState) const;
	private:
		uint64_t graphAskedAt = 0;	// when a song's graph was last changed while it played (the log times the switch)
		bool graphFrameLogged = true;	// the switch's first frame logged (render)
		uint64_t displayDueAt = 0;	// the display changed: its graph applied then (checkDisplay)
		// The equalizer and the sound effect as mpv has them: af = a song's input entry (@bpin), the EQ's (@bpeq),
		// then the effect's (@bpfx), either left out when off. afSet: the af last set (prop; af-command's changes don't show in mpv's af);
		// afFxSet: its effect entry; eqLive: the gains playing. syncEq brings mpv to the settings: a band's change by
		// af-command (heard at once, no gap), unless the chain is about to be rebuilt anyway (rebuilding: a loadfile,
		// a graph or track change, which rebuild it from the af string and lose af-command's gains) or the filters
		// themselves change (EQ in or out, another effect): then af as a whole
		std::string afSet, afFxSet;
		EqGains eqLive{};
		void syncEq(bool rebuilding);
		std::string eqPart() const;	// the EQ's entry for this file ("" off, flat, or not a song)
		std::string fxPart();		// the effect's, its impulse response written first ("" none)
		std::string afWant() { return joinAf(eqPart(), fxPart()); }
		// a song's af: its input entry (@bpin, bpcore's afInputFilter), then the EQ's and the effect's; a video's none
		std::string joinAf(const std::string& eq, const std::string& fx) const
		{
			std::string s = isAudio ? afInputFilter() : "";
			for (const std::string* p : {&eq, &fx})
				if (!p->empty()) s += (s.empty() ? "" : ",") + *p;
			return s;
		}
		std::string irWritten;		// the impulse responses known to be on disk this run (" key" each)
		void sendEqCommand(const char* command, const std::string& arg, const std::string& target);
		int eqInFlight = 0;			// af-commands not answered yet
		bool eqFailed = false;		// one of them failed: af set once they're all in
		bool eqDirty = false;		// settings changed and not written (setSettings with persist false)
		EqPresetStore eqStore;
		int eqSerial = 0;
		void writeEqPresets();
		uint64_t cpuAt = 0;
		double cpuSeconds = 0;
		std::string tagArtist, tagAlbum, tagYear;	// the file's tags, as mpv sent them (the year: the date's first four)
		// whether the last song (in lastArtDir) had art: the next one's guess
		bool lastArt = false;
		std::string lastArtDir;
		int shownMusic() const;	// MusicShow as it comes out now (None while the visualizer rests)
		void updateOverlay();
		// The song's title on the top panel as a Title marquee (docs/redesign/components.md): when it doesn't fit its
		// line's box it is its own ASS overlay (id 2), sent again with a new \pos inside a \clip at each step
		struct TopMarq
		{
			bool on = false;
			std::string key, text, style;	// its text and its look (font, size, colour) as ASS tags
			double x = 0, y = 0, boxW = 0;	// units: the box's left, the line's anchor (an: 4 its middle, 7 its top)
			int textW = 0, fs = 0, an = 4;
			bool fade = true;	// fades in the ground's colour (BG); none over a picture
			uint64_t start = 0;
			int lastStep = -1;
			uint64_t sentAt = 0;	// (ticks) the last place sent while sliding
		};
		TopMarq topMarq;
		void setTopMarquee(const std::string& text, const std::string& style, int an, double x, double y, double boxW, int fs, int textW, bool fade);
	public:
		void tickMarquee();	// each loop pass: the title's next step sent when it is due
		bool marqueeMoving() const;	// (the loop wakes at the steps' rate)
		void marqueeRestart() { topMarq.key.clear(); if (topMarq.on) updateOverlay(); }	// the panel woke
	private:
		// The song's art on the top panel (src/songlayout.h). The art is an image overlay (Album art only and Art + visualizer), decoded here once (a
		// thread of its own) at the panel's pixels, its corners rounded. A network song's art is mpv's own video
		// instead (the decode reads local files only), put in the art's box by the video's margins
		struct SongArtJob
		{
			std::mutex m;
			bool done = false, found = false;
			std::vector<uint8_t> bgra;	// premultiplied, px x px
			int px = 0;
			double ms = 0;
		};
		static constexpr int ART_OVERLAY = 1;	// (overlay-add's id: the idle icon 0; the cards OV_LOAD..OV_NOTE above it)
		std::shared_ptr<SongArtJob> artJob;	// the one decoding or decoded for this song
		std::string artKey;	// its file and size
		enum class ArtState { Unknown, Shown, None };
		ArtState artState = ArtState::Unknown;	// the overlay's art (Art + visualizer, a local song)
		bool artOverlayUp = false;
		int artOverlayX = -1, artOverlayY = -1;
		std::map<uint64_t, std::shared_ptr<std::vector<uint8_t>>> artPixels;	// as levelPixels: kept till answered
		uint64_t artSerial = 0, artShownSerial = 0;
		void artReplied(uint64_t serial);
		void dropSongArt();	// the overlay off and the job forgotten (another file, another mode)
		std::string marginsSet;	// the video margins as last set ("" none)
		// a song's top panel on the theme's ground (Album art only, Art + visualizer, None: docs/redesign, Song layout):
		// mpv's background-color (themed: BG; else black) as last sent. No graph draws any of it (2026-11-19)
		std::string groundSet;
		static constexpr const char* GROUND_CLEAR = "#00000000";
		bool noLayer = false;	// (nativeLayerFailed)
		void setGround(bool themed, const char* why = nullptr, bool clear = false);
		void setSongMargins(const std::string& lrtb);
		std::string layoutLogged;	// the last layout's log line, so each change is logged once
		bool isAudio = false, isPhoto = false;
		bool topCovered = false;	// setTopCovered
		bool engineLoading = false;	// setEngineLoading
		bool vizResting() const { return topCovered || engineLoading; }
		// Native visualizers: the style is drawn by the player (a local song: the tap decodes the file itself; a
		// network one keeps the graph style), unless this song's file couldn't be read by the tap
		bool nativeViz() const;
		bool nativeCanTap() const;	// the tap can read this song (local, not failed); else the style is drawn idle
		bool nativeIdle = false;	// the style shown flat (a network song)
		std::string nativeIdlePath, displayLogged;
		void nativeStop(const char* why);
		VizTap vizTap;
		std::function<void()> nativeWake;
		bool nativeFailed = false, nativeOverArt = false, nativeMoving = false;
		int nativeStyleSent = -1;	// the style the tap measures for
		VizRect linesBox{0, 0, 0, 0, 0};	// the song's lines over a visualizer alone (updateOverlay), in units
		// Art + visualizer: the art's box and its lines' (updateOverlay), kept clear by the styles that take the whole
		// panel there (Sparks, Starfield: vizFullPanel)
		VizRect groupArtBox{0, 0, 0, 0, 0}, groupTextBox{0, 0, 0, 0, 0};
		// the style drawn for this song: the chosen one, or Peaks' graph where the player can't draw it (a network song,
		// a file the second decode can't read)
		int drawnStyle() const;
		std::string nativeFallbackLogged;	// (the log's line, once a song and style)
		uint64_t nativeSeq = 0, nativeFrames = 0, nativeAt = 0, nativeLoggedAt = 0;
		VizClock audioClock;	// mpv's audio-pts and when it came (observed: never read)
		bool hidingVideo = false;	// hideVideo(true): a video's picture off (each video's loadfile says vid=no)
		bool vidOff = false;		// the vid property itself is "no" (a song opened next needs it back first)
		// photos: the size mpv shows (before any turn), the turn mpv makes of the file's own (rotation metadata) and
		// the one made here for an EXIF orientation mpv's FFmpeg leaves (7.1's does)
		PhotoView view;
		int photoW = 0, photoH = 0, fileTurn = 0, exifTurn = 0;
		double panShareX = 0, panShareY = 0;	// the place as a share of how far it can go, for the next photo
		ExifInfo exif;
		bool turned() const { return (fileTurn + exifTurn + view.rotate) % 180 == 90; }
		void applyPhotoView();
		// the thumbnail: asked of mpv (screenshot-raw, answered on its own time) once a frame is up
		uint64_t thumbAt = 0, loadAt = 0, loadedAt = 0;
		int spinnerStep = -1;
		bool started = false;	// the file's playback began (mpv's playback-restart): no longer loading	// the spinner's lit dot as drawn, -1: not up
		int thumbTries = 0, loadSerial = 0;
		bool thumbAsked = false, thumbTurnedByMpv = false;
		void takeThumb(mpv_node* result);
		std::string get(const char* name) const;	// "" where mpv has none
		void writeFile(const std::string& path, const std::string& text);	// durable, at most one sync a second per file
		struct PendingWrite { std::string text; uint64_t dueAt; };
		std::map<std::string, PendingWrite> pendingWrites;	// saved again within WRITE_GAP_MS: the last value waits here
		std::map<std::string, uint64_t> lastWritten;
		static constexpr uint64_t WRITE_GAP_MS = 1000;

		mpv_handle* mpv = nullptr;
		mpv_render_context* rc = nullptr;
		PlayerOptions opts;
		std::vector<std::string> playlist;
		int current = 0;
		// network folders' names (open's folderNames, netFolderListed's), by folder; 32 at most (netOrder: oldest first)
		std::map<std::string, std::vector<std::string>> netNamesByDir;
		std::vector<std::string> netOrder;
		bool isNet = false;
		bool pausedForCache = false;
		uint64_t seekingSince = 0;	// mpv's seeking since then, 0 when it isn't
		ResumeStore resume;
		PlayHistory played;
		int recentSer = 0;
		int64_t wantAid = 0, wantSid = 0;
		std::string alangPref;	// the alang set for this file (the remembered language first)
		// the song's audio track to choose again, by its id: prefer, else the first in alangPref's languages, else the
		// file's default, else the first; "auto" only with no audio track ("auto" set again when it's already the value
		// chooses nothing, and mpv can be left with the cover art alone: a silent song)
		std::string songAid(int64_t prefer) const;
		// the tracks playing, as last seen while the file was open: at its end mpv has dropped its track list
		int64_t seenAid = -1, seenSid = -1;
		uint64_t audioCheckAt = 0;
		int audioChecks = 0;
		static uint64_t ticks();	// asked for at load: checked once the file's tracks are known
	};
}
