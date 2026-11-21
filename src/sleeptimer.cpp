#include "sleeptimer.h"
#include <algorithm>

namespace bp
{
	void sleepSet(SleepTimer& t, int minutes, uint64_t lengthMs, uint64_t now)
	{
		if (minutes <= 0)
		{
			sleepOff(t);
			return;
		}
		t = SleepTimer{};
		t.kind = SleepKind::Minutes;
		t.phase = SleepPhase::Running;
		t.minutes = minutes;
		t.deadline = now + lengthMs;
	}

	void sleepSetItemEnd(SleepTimer& t)
	{
		t = SleepTimer{};
		t.kind = SleepKind::ItemEnd;
		t.phase = SleepPhase::Running;
	}

	void sleepOff(SleepTimer& t) { t = SleepTimer{}; }

	void sleepSnooze(SleepTimer& t, uint64_t ms, const SleepIn& in)
	{
		if (!sleepRunning(t)) return;
		uint64_t base = t.kind == SleepKind::ItemEnd ? in.now + (uint64_t)std::max(0.0, in.itemLeft * 1000)
			: std::max(t.deadline, in.now);
		t.kind = SleepKind::Minutes;
		t.deadline = base + ms;
		t.phase = t.deadline - in.now <= SLEEP_WARN_MS ? SleepPhase::Warning : SleepPhase::Running;
	}

	SleepOut sleepTick(SleepTimer& t, const SleepIn& in)
	{
		SleepOut o;
		if (t.phase == SleepPhase::Off) return o;
		if (t.phase == SleepPhase::Dark)
		{
			if (in.now >= t.darkAt + SLEEP_DARK_MS)
			{
				o.sleep = true;
				sleepOff(t);
			}
			return o;
		}
		auto runOut = [&] {
			o.pause = true;
			t.phase = SleepPhase::Dark;
			t.darkAt = in.now;
		};
		// the list ended (nothing loaded after something was): a length goes to its last minute now, with nothing
		// to fade; End of this song ends now. Something loaded again: it is a list once more
		bool listGone = !in.loaded && t.hadItem;
		if (in.loaded)
		{
			t.hadItem = true;
			t.listEnded = false;
		}
		if (t.kind == SleepKind::ItemEnd)
		{
			if (in.itemEnded || listGone) runOut();
			else t.phase = in.itemLeft >= 0 && in.itemLeft * 1000 <= SLEEP_WARN_MS ? SleepPhase::Warning : SleepPhase::Running;
			return o;
		}
		if (listGone && !t.listEnded)
		{
			t.listEnded = true;
			t.deadline = std::min(t.deadline, in.now + SLEEP_WARN_MS);
		}
		if (in.now >= t.deadline)
		{
			runOut();
			o.fade = t.listEnded ? 1 : 0;
			return o;
		}
		uint64_t left = t.deadline - in.now;
		t.phase = left <= SLEEP_WARN_MS ? SleepPhase::Warning : SleepPhase::Running;
		if (t.phase == SleepPhase::Warning && !t.listEnded) o.fade = (double)left / SLEEP_WARN_MS;
		return o;
	}

	int64_t sleepLeftMs(const SleepTimer& t, const SleepIn& in)
	{
		if (!sleepRunning(t)) return 0;
		if (t.kind == SleepKind::ItemEnd) return in.itemLeft < 0 ? -1 : (int64_t)(in.itemLeft * 1000);
		return t.deadline > in.now ? (int64_t)(t.deadline - in.now) : 0;
	}

	std::string sleepLabel(const SleepTimer& t, const SleepIn& in, const std::string& itemWord)
	{
		if (!sleepRunning(t)) return "";
		int64_t left = sleepLeftMs(t, in);
		if (left >= 0 && left <= (int64_t)SLEEP_WARN_MS)
		{
			// seconds rounded up: "1:00" as the minute begins, "0:01" in its last second
			int s = (int)((left + 999) / 1000);
			return std::to_string(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + std::to_string(s % 60);
		}
		if (t.kind == SleepKind::ItemEnd) return itemWord;
		return std::to_string((left + 59999) / 60000) + " min";
	}
}
