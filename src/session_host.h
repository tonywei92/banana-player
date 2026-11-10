#pragma once
// The Session's host on the handheld (docs/design/session.md §2), on the main thread only: it holds the Session, is the
// only caller of apply(), and runs the effects on the handheld's mpv (run(), once per pass of main's loop, coalesced).
// Live intents: everything a page sends (/api/intent), the handheld's Open/Step/Ended/Stop/photo/Woke, a page's
// Play/Pause/Seek from the handheld, and the host's Tick/TabGone/TabBack. While the handheld itself is the output, mpv
// is the clock: its play state and place come back as reports (pass()), and the few calls left non-live (play(),
// seek()) only tell the session what the handheld just did.
#include "session.h"
#include "tracks.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bp
{
	class Player;
	class Ui;
	int sourceKindOf(const Player& p);

	class SessionHost
	{
	public:
		static SessionHost& get();

		// the executor's hands (main, once both exist)
		void attach(Player* p, Ui* u) { player = p; ui = u; }

		const Session& session() const { return s; }
		uint64_t boot() const { return s.boot; }
		uint64_t version() const { return s.v; }
		std::string file() const { return s.index >= 0 ? s.queue[s.index] : ""; }
		// a browser tab plays the item (the handheld holds its paused copy): today's browserPlaying, derived
		bool pageIsOutput() const { return s.index >= 0 && s.out.kind == OutKind::Browser; }
		// picks go to a browser tab (the selected output, kept by Stop, the end of a list, a file it can't play...)
		bool prefIsPage() const { return s.pref.kind == OutKind::Browser; }
		std::string prefTab() const { return prefIsPage() ? s.pref.tab : ""; }
		// the v of the last snapshot published to the pages: what an old page's word is taken to have seen (its basis)
		uint64_t publishedV = 0;

		// an intent made here: origin's next seq (the host numbers each origin's words while the old routes carry
		// none), basis the current v (a legacy page: publishedV)
		Intent make(IntentType t, const std::string& origin = "hh", bool legacyBasis = false);
		// live: run its effects on the handheld now (a step-(b) intent); else today's code acts, the session follows
		Result submit(const Intent& i, bool live = false);
		// a page's intent (/api/intent), live. Leaving the handheld, its place and tracks are reported first
		Result fromPage(const Intent& i);

		// ---- step (b), live ----
		// hold A, the handheld's Play while a page is the output, the web's handheld Play, an old page's /api/play of
		// its own file (at: where it was, t >= 0): SetOutput(hh) + Play
		void takeOver(double at = -1);
		// a page's "Play on handheld" of a new pick (/api/play to=handheld): the handheld the selected output before it
		// opens (not run: what plays now is replaced, not moved)
		void chooseOutput(const Output& to, const std::string& origin);
		// the handheld's own "Play on" (START › Playing): the output moved where it says, as a page's switch moves it
		// (SetOutput from "hh", live). Leaving the handheld, its place and tracks are reported first, so the page starts
		// there with them; back here, it plays here if the session plays (a page waiting for a tap: B)
		void sendTo(const Output& to);
		// a browser is the output and waits for a tap there to start the sound (its autoplay rule refused it)
		bool pageWaits() const { return pageIsOutput() && s.blocked; }
		// the page sent a pick never answered it, or one it said it can't play: CantPlay for it (the handheld plays)
		void pageCant(const std::string& reason);
		// a track chosen on the handheld (its Audio & Subtitles page, the web's /api/track): the session's pick
		void handheldPicked(TrackType type, int64_t id);

		// ---- what the handheld did itself, or picks it made (the session follows: nothing more runs) ----
		// can that browser tab play this file (the web serves it, a format a browser plays, the tab is there; tab "":
		// the tab's own word, there by definition): the context Open and Step need (main sets it; none: no)
		std::function<bool(const std::string& path, const std::string& tab)> browserCan;
		// a pick (handheld or web) as Open; sourceKind 0 folder, 1 network, 2 playlist. out: given when the picker says
		// where (the page's Play here / Play in this browser); else the session's pref decides
		void open(const std::vector<std::string>& paths, int index, int sourceKind, const std::string& title,
			const std::string& origin, const Output* out, double t, bool live = false, const std::string& sourceId = "");
		// Shuffle on or off (the handheld's mode key, a page's Shuffle, a Shuffle play): the setting (it lasts, as
		// Loop: every list from now) and the session's (what plays plays on; Next follows a hidden random order)
		void setShuffle(bool on, const std::string& origin = "hh");
		// Shuffle is on (the setting)
		bool shuffleMode() const;
		// Next / Previous (n: how many; Previous RESTART_S in restarts first), live: the handheld follows in its own list
		Result step(int n, const std::string& origin = "hh");
		// a Play / Pause or a seek of what a page plays (the handheld's A, its bar, a remote tab): live, the page follows
		void playLive(bool on, const std::string& origin = "hh");
		void seekLive(double t, bool relative, const std::string& origin = "hh");
		// Next / Previous on the handheld go through the session: it has the item playing here, or a page plays it
		bool stepsHere(const Player& p) const;
		// the handheld's file played to its end (it is the output): the next, a wrap, or the end (index -1)
		Result ended();
		void toggle(const std::string& origin = "hh");	// the opposite of the session's playing
		void play(bool on, const std::string& origin = "hh");
		// Stop (the handheld's, a remote's): live, everywhere; the handheld's own stop follows (takeUnload)
		Result stop(const std::string& origin);
		// the executor's word to main that what plays here stops (HhUnload): 0 no, 1 a Stop, 2 the end of the list
		int takeUnload() { int u = unload; unload = 0; return u; }
		bool fileGone(const std::string& path);	// true: it was the session's item (stopped)
		// the playlist (its file) edited: paths, its items now; remap, each old place's new one (-1 gone). The
		// session's list follows when that playlist plays (Next reaches what was added)
		void playlistEdited(const std::string& id, const std::vector<std::string>& paths, const std::vector<int>& remap);
		void seek(double t, bool relative, const std::string& origin);
		void woke();
		// each pass of main's loop: the handheld's reports (as the output), its picks once it can take them, Tick, the
		// tabs, invariant I2, the effects. tabUp: is that tab's event stream connected
		void pass(Player& p, Ui& u, const std::function<bool(const std::string&)>& tabUp);

	private:
		SessionHost();
		Session s;
		Player* player = nullptr;
		Ui* ui = nullptr;
		std::map<std::string, uint64_t> seqs;
		uint32_t tickAt = 0, posAt = 0, tabsAt = 0, mismatchSince = 0, soundingSince = 0;
		std::map<std::string, bool> tabState;
		uint64_t erroredLoad = 0;
		bool picksWanted = false;	// the session's picks to put on the handheld's copy once its tracks are known
		bool showHere = false;	// the next run that brings the item here shows the player
		int unload = 0;
		bool endedFx = false;	// the effects waiting came from the handheld's Ended (its unload is the end of the list)
		std::vector<Effect> pendingFx;	// live effects, run once per pass of main's loop (coalesce: a burst loads one file)
		void run(const std::vector<Effect>& fx);
		bool holdsItem() const;	// the handheld's mpv has the session's item
		void bringHere(bool show = true);	// opens the session's item on the handheld (it holds another, or none)
		void seekHere(double t);
		void applyPicks();
		void reportHere();	// before the handheld stops being the output: its place and its tracks
	};
	// the pick a renderer list's chosen track makes, and the list as matchTrack reads it (the labels as trackLabel's)
	TrackPick pickOf(const std::vector<Track>& list, int64_t selectedId);
	std::vector<TrackInfo> infosOf(const std::vector<Track>& list);
}
