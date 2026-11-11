#pragma once
#include "aspect.h"
#include "eq.h"
#include "history.h"
#include "photo.h"
#include "resume.h"
#include "settings.h"
#include "tracks.h"
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <map>
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
		// at a file's end: the next one, or (Loop all) the first again; false when that's all
		bool advance();
		bool playingAudio() const { return isAudio && !playlist.empty(); }
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
		int render(int w, int h, unsigned fbo = 0, bool skip = false);	// skip: take the frame, draw nothing
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
		void scrub(double fraction);		// fast (to a keyframe): while a finger drags the bar
		void seekFraction(double fraction);	// to the frame
		void setSpeed(double s);
		// a choice of the player's: also the language a file opened for the first time starts with
		void selectTrack(TrackType type, int64_t id);
		void setAspect(AspectMode m);
		// applies at once and keeps them in settings.conf (persist false: not yet, as the equalizer's bands move;
		// commitEq writes them). A change of the music display updates the title at once and the filter graph 0.5 s
		// later (checkDisplay), so quick presses rebuild it once
		void setSettings(const Settings& s, bool persist = true);
		// called each turn of the loop: the music display's graph, once its changes have settled
		void checkDisplay();
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
		// a card in the same place (Like, Add to playlist): a glyph (rows of '#' cells, ink as &HBBGGRR&), a line, and
		// under it a dim one (the title it was for). 1.5 s, see takeCardShown
		void showNoteCard(const char* const* rows, int h, const char* ink, const std::string& line1, const std::string& line2);
		void osd(const std::string& text, int ms = 1500);
		// the volume/brightness card over the top panel ("Volume", 85), till hideLevel
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
		// the top panel's pixels (main sets it): the idle icon is drawn in whole pixels at that size
		void setTopSize(int w, int h) { topW = w; topH = h; }
		int topW = 0, topH = 0;
		int idleIconPx = 0, idleIconH = 0;	// the idle picture decoded at this width, written to idleIconFile
		std::string idleIconFile;
		// the top panel while a web page plays the music instead: the paused picture dimmed, and why (a paused
		// visualizer looks frozen otherwise). where: the page's name ("Chrome on Mac"); waiting: it was refused the
		// sound without a tap there (Play on from here), and B plays here instead; glyph: drawn over the title (G_LAPTOP)
		void showElsewhere(bool on, bool playing = true, const std::string& where = "your browser", bool waiting = false,
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
		void forgetPosition(const std::string& path);
		// the browser's folder, kept (in resume.tsv) only while Remember last folder is on
		void setLastFolder(const std::string& dir);

		double position = -1, duration = -1, speed = 1;
		bool paused = false, loaded = false;
		bool finished = false;	// the file played to its end (the caller moves on, then clears it)
		bool quit = false;		// mpv itself is going away
		AspectMode aspect = AspectMode::Fit;
		Settings settings;
		std::string title, error;
		std::vector<Track> tracks;
		// a song's artist and album from its tags ("" none)
		const std::string& artist() const { return tagArtist; }
		const std::string& album() const { return tagAlbum; }

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
		void refreshTracks(mpv_node* trackList);
		void clearChapters();
		int cardMs = 0;
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
		std::string graphFor(int vis, bool overCover);	// visualizerGraph, with Circle's tables made first
		std::string circleMaps;	// where they are, once made
		std::string graphSet;	// lavfi-complex as last set (prop)
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
		std::string tagArtist, tagAlbum;	// the file's tags, as mpv sent them
		// whether the last song (in lastArtDir) had art: the next one's guess
		bool lastArt = false;
		std::string lastArtDir;
		int shownMusic() const;	// MusicShow as it comes out for this file (its art or none)
		void updateOverlay();
		bool isAudio = false, isPhoto = false;
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
		void writeFile(const std::string& path, const std::string& text);

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
