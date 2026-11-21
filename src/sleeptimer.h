// The sleep timer's rule (docs/redesign/sleep-timer.md), pure: the time and what plays come in with each call, nothing
// is read or done here. Ui::Sleep (ui_sleep.h) holds one, ticks it from main's loop and does what it says: the fade on
// mpv's own volume, the Pause through the Session, both screens dark, then the sleep asked of BananaOS.
// It is the app's, not the Session's: it plays nothing and moves nothing in the list; at its end it asks for a Pause, as
// a person would. Not saved: a timer doesn't outlive a quit or a sleep.
#pragma once
#include <cstdint>
#include <string>

namespace bp
{
	enum class SleepKind { Off, Minutes, ItemEnd };	// ItemEnd: the end of this song, video or chapter
	// Running, then its last minute (the card, and for Minutes the fade), then paused and dark until the sleep
	enum class SleepPhase { Off, Running, Warning, Dark };
	constexpr uint64_t SLEEP_WARN_MS = 60000;
	// paused and dark this long, then the sleep: a short grace (a key in it calls the sleep off; the pause lands and the
	// place is saved), not a wait the owner sees as "it only turned the screen off"
	constexpr uint64_t SLEEP_DARK_MS = 3000;

	struct SleepTimer
	{
		SleepKind kind = SleepKind::Off;
		SleepPhase phase = SleepPhase::Off;
		int minutes = 0;	// Minutes: the length chosen (the sheet's cursor opens on it)
		uint64_t deadline = 0;	// Minutes: when it pauses (ms, the caller's clock)
		uint64_t darkAt = 0;	// Dark: when it paused
		bool hadItem = false;	// something was loaded at a tick since it was set (its end can be the list's end)
		bool listEnded = false;	// the list ended first: its last minute runs with nothing to fade
	};

	struct SleepIn
	{
		uint64_t now = 0;
		bool loaded = false;	// there is something to play (the Session has an item)
		double itemLeft = -1;	// ItemEnd: seconds to the end of what plays (the song, the video, the chapter); -1 unknown
		bool itemEnded = false;	// ItemEnd: what played reached its end since the last tick
	};

	struct SleepOut
	{
		bool pause = false;	// it ran out: pause now (the phase is Dark from here)
		bool sleep = false;	// the dark's grace is over: the sleep (the timer is Off after)
		double fade = 1;	// the player's volume factor: the Minutes' last minute goes 1 down to 0
	};

	// a length chosen (from now; replaces a running one). minutes <= 0: off. lengthMs: minutes' worth of ms (a test
	// variable shortens it)
	void sleepSet(SleepTimer& t, int minutes, uint64_t lengthMs, uint64_t now);
	void sleepSetItemEnd(SleepTimer& t);
	void sleepOff(SleepTimer& t);
	// `ms` more (the Snooze setting), while it runs; End of this song becomes a length counted from the song's end
	void sleepSnooze(SleepTimer& t, uint64_t ms, const SleepIn& in);
	// once per pass of main's loop
	SleepOut sleepTick(SleepTimer& t, const SleepIn& in);

	inline bool sleepRunning(const SleepTimer& t) { return t.phase == SleepPhase::Running || t.phase == SleepPhase::Warning; }
	// ms until the next moment the rule changes something (the last minute begins, it runs out, the dark's grace ends),
	// so main's loop wakes for it however idle it is; -1: nothing timed ahead (off, or End of this song, which follows
	// what plays)
	int64_t sleepNextMs(const SleepTimer& t, uint64_t now);
	// ms left while it runs; -1: unknown (End of this song before the song's length is known); 0 when it doesn't run
	int64_t sleepLeftMs(const SleepTimer& t, const SleepIn& in);
	// what the key, the badge and the page say: "24 min"; "0:48" in the last minute; itemWord ("Song") for End of this
	// song before that; "" when it doesn't run
	std::string sleepLabel(const SleepTimer& t, const SleepIn& in, const std::string& itemWord);
}
