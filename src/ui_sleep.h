// The sleep timer on the handheld (docs/redesign/sleep-timer.md): its state, its sheet (sleep-set), its last minute's
// warning (sleep-warn) and what it does at its end (sleep-done). The rule itself is pure and tested (sleeptimer.h,
// bpcore); this holds one, ticks it once per pass of main's loop and does what it says: the fade on mpv's own volume
// (Player::setFade; the handheld's volume never moves), the Pause through the Session (a page that plays it pauses
// too), both screens dark, and the sleep main then asks of BananaOS (the setting When the sleep timer ends; a copy or a
// download still holds it awake). It is the app's, not the Session's: the Session plays, and this only asks it to
// pause, as a person would. Nothing of it is saved: a timer doesn't outlive a quit or a sleep.
// Shown by: the player's Sleep key (ui_player.cpp), More's Sleep timer tile, the header's badge on every page
// (Ui::drawBattery, Shell::drawHeader), a song's top screen (its quiet mark), the web page (api_player.cpp: "sleep").
#pragma once
#include "sleeptimer.h"
#include "ui_kit.h"

namespace bp
{
	struct Ui::Sleep
	{
		explicit Sleep(Ui& u) : ui(u) {}
		Ui& ui;
		Player* player = nullptr;	// the player of what plays (main's, never the photo viewer's): main sets it
		SleepTimer t;

		// ---- what the key, the More tile, the badge, the sheet and the web page show ----
		bool running() const { return sleepRunning(t); }
		bool warning() const { return t.phase == SleepPhase::Warning; }
		std::string label() const;	// "24 min", "0:48", "Song"; "" when off
		std::string word() const;	// what End of this... ends: "song", "video" or "chapter"
		std::string pausesAt() const;	// the clock when it pauses ("22:17"; "" not known)
		int64_t leftMs() const { return sleepLeftMs(t, now()); }
		Kit::Badge badge() const;	// the header's: the clock and what is left

		// ---- what changes it: the sheet, the warning's keys, Power with the lid shut, the web page ----
		void set(int minutes);	// 0: off
		void setItemEnd();
		void snooze();
		void off(bool note = true);
		void openSheet();

		// ---- main's ----
		// once per pass of the loop: true when the sleep is due (the dark's grace is over; main asks BananaOS unless the
		// setting is Pause or something holds it awake)
		bool tick();
		bool endsWithItem() const;	// End of this song or video (its file's end: main's player.finished)
		void itemEnded();	// that end came (the Session has moved on): the pause, now, before the next one sounds
		void stopped();	// Stop: off, with its note
		void woke();	// back from a sleep: off; on waking from its own, the note
		bool dark() const { return darkWait; }	// paused by it, both screens off, until a key or a touch
		bool wake();	// a key or a touch (Ui::press, Ui::pointer): true when it only woke the screens from its dark
		bool animating() const { return rampAt != 0; }
		int waitMs() const;	// how soon the loop must come round (-1: no need)
		std::string signature() const;

	private:
		Player& pl() const { return *player; }
		SleepIn now() const;	// what plays, as the rule reads it (no end seen)
		double itemLeft() const;	// seconds to the end of what plays (a chapter's where End of this chapter counts)
		bool byChapter() const;	// End of this... is the chapter's (what plays here has chapters)
		bool seenEnd(const SleepIn& in);	// End of this...: a chapter's end, or a page's song's, since the last tick
		uint64_t lengthMs(int minutes) const;	// (BP_SLEEP_SECONDS: every length that many seconds, for tests)
		void pauseAll();
		void runOut();
		void showWarning();
		void endWarning(bool ramp);
		void drawCard();
		void note(const std::string& l1, const std::string& l2, int ms = 0);
		void restore();

		// End of this...: what played at the last tick (a chapter's end, a page's song changing at its end)
		std::string seenPath;
		uint64_t seenLoad = 0;
		int seenChapter = -1;
		double seenLeft = -1;
		bool endedNow = false;	// main's player.finished, taken at the next tick
		// the fade: where it began (the place is put back after the pause), the volume back over 1 s after it
		std::string fadePath;
		double fadePos = -1;
		double rampFrom = 1;
		uint32_t rampAt = 0;
		bool restoreOnPause = false;	// the volume back once the pause has landed
		uint32_t lastTickAt = 0;	// (a late pass in the dark is logged)
		bool darkWait = false;	// paused by it and dark: a key or a touch wakes the screens and calls the sleep off
		bool noteKept = false;	// "Paused by the sleep timer" kept up until the first key
		SleepPhase shown = SleepPhase::Off;
		std::string cardText;	// the top screen's card as last put up
	};
}
