// The sleep timer on the handheld (ui_sleep.h): the host of the pure rule, its sheet and its last minute's warning.
#include "ui_sleep.h"
#include "format.h"
#include "player.h"
#include "session_host.h"
#include "ui_glyphs.h"
#include "ui_internal.h"
#include "ui_player.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>

namespace bp
{
	static const int SLEEP_LENGTHS[5] = {15, 30, 45, 60, 90};

	static std::string capital(std::string s)
	{
		if (!s.empty()) s[0] = (char)toupper((unsigned char)s[0]);
		return s;
	}

	// ---- what plays, as the rule reads it ----

	bool Ui::Sleep::byChapter() const
	{
		const Player& p = pl();
		return !SessionHost::get().pageIsOutput() && p.playing() && !p.playingPhoto() && p.chapters.size() > 1;
	}

	double Ui::Sleep::itemLeft() const
	{
		SessionHost& sh = SessionHost::get();
		if (sh.pageIsOutput())
		{
			const Session& s = sh.session();
			if (s.dur <= 0) return -1;
			return std::max(0.0, (s.dur - posNow(s, SDL_GetTicks())) / std::max(0.05, s.speed));
		}
		const Player& p = pl();
		if (!p.playing() || p.playingPhoto() || !p.loaded || p.duration <= 0 || p.position < 0) return -1;
		double end = p.duration;
		if (byChapter() && p.chapter >= 0 && p.chapter + 1 < (int)p.chapters.size()) end = p.chapters[p.chapter + 1].time;
		// in the time it takes at the speed it plays
		return std::max(0.0, (end - p.position) / std::max(0.05, p.speed));
	}

	SleepIn Ui::Sleep::now() const
	{
		SleepIn in;
		in.now = SDL_GetTicks();
		const Player& p = pl();
		in.loaded = SessionHost::get().session().index >= 0 || (p.playing() && !p.playingPhoto());
		in.itemLeft = itemLeft();
		return in;
	}

	bool Ui::Sleep::seenEnd(const SleepIn& in)
	{
		// a file's end on the handheld is main's (player.finished: itemEnded); here, a chapter's end and a page's song
		// changing as it ended (its last seconds seen, not a skip)
		const Player& p = pl();
		SessionHost& sh = SessionHost::get();
		bool ended = false;
		if (sh.pageIsOutput())
		{
			uint64_t load = sh.session().load;
			ended = seenLoad && load != seenLoad && seenLeft >= 0 && seenLeft < 3;
			seenLoad = load;
			seenPath.clear();
		}
		else
		{
			seenLoad = 0;
			if (byChapter())
			{
				// the next chapter reached by playing (a jump with L2 / R2 is a new place, its own end what counts), or
				// the chapter's last quarter second
				bool next = p.currentPath() == seenPath && p.chapter == seenChapter + 1 && seenLeft >= 0 && seenLeft < 2;
				ended = next || (!p.paused && in.itemLeft >= 0 && in.itemLeft < 0.25);
			}
			seenPath = p.currentPath();
			seenChapter = p.chapter;
		}
		seenLeft = in.itemLeft;
		return ended;
	}

	uint64_t Ui::Sleep::lengthMs(int minutes) const
	{
		const char* test = getenv("BP_SLEEP_SECONDS");
		if (test && atoi(test) > 0) return (uint64_t)atoi(test) * 1000;
		return (uint64_t)minutes * 60000;
	}

	// ---- what it shows ----

	std::string Ui::Sleep::word() const
	{
		SessionHost& sh = SessionHost::get();
		if (sh.pageIsOutput()) return sh.session().audioOnly ? "song" : "video";
		if (byChapter()) return "chapter";
		return pl().playing() && !pl().playingAudio() ? "video" : "song";
	}

	std::string Ui::Sleep::label() const { return sleepLabel(t, now(), capital(word())); }

	std::string Ui::Sleep::pausesAt() const
	{
		int64_t left = leftMs();
		if (!running() || left < 0) return "";
		time_t at = time(nullptr) + (time_t)((left + 999) / 1000);
		struct tm tm;
		if (!localtime_r(&at, &tm)) return "";
		return formatClock(tm.tm_hour, tm.tm_min, pl().settings.clock == CLOCK_12);
	}

	Ui::Kit::Badge Ui::Sleep::badge() const
	{
		Kit::Badge b;
		b.glyph = &G_CLOCK;
		b.text = label();
		return b;
	}

	std::string Ui::Sleep::signature() const
	{
		return "|sleep" + std::to_string((int)t.phase) + label() + (darkWait ? "d" : "");
	}

	int Ui::Sleep::waitMs() const
	{
		// End of this chapter: its last quarter second is looked for (the loop's own wait is 250 ms)
		if (t.kind == SleepKind::ItemEnd && running() && byChapter())
		{
			double left = itemLeft();
			if (left >= 0 && left < 2) return 50;
		}
		// the timer's own next moment (its last minute, its end, the dark's grace over): the loop wakes for it
		int64_t next = sleepNextMs(t, SDL_GetTicks());
		return next < 0 ? -1 : (int)std::min<int64_t>(next + 1, 250);
	}

	void Ui::Sleep::note(const std::string& l1, const std::string& l2, int ms)
	{
		pl().showNoteCard(G_CLOCK.rows, G_CLOCK.h, l1, l2, false, ms);
	}

	// ---- what changes it ----

	void Ui::Sleep::set(int minutes)
	{
		if (minutes <= 0)
		{
			off();
			return;
		}
		sleepSet(t, minutes, lengthMs(minutes), SDL_GetTicks());
		std::string at = pausesAt();
		fprintf(stderr, "sleep timer: %d min (pauses at %s)\n", minutes, at.c_str());
		note("Sleep timer \xC2\xB7 " + std::to_string(minutes) + " min", at.empty() ? "" : "pauses at " + at);
	}

	void Ui::Sleep::setItemEnd()
	{
		sleepSetItemEnd(t);
		fprintf(stderr, "sleep timer: the end of this %s\n", word().c_str());
		note("Sleep timer \xC2\xB7 this " + word(), "");
	}

	void Ui::Sleep::snooze()
	{
		if (!running()) return;
		int m = pl().settings.sleepSnooze;
		sleepSnooze(t, (uint64_t)m * 60000, now());
		fprintf(stderr, "sleep timer: %d more minutes (%s left)\n", m, label().c_str());
		note(std::to_string(m) + " more minutes", pausesAt().empty() ? "" : "pauses at " + pausesAt());
	}

	void Ui::Sleep::off(bool withNote)
	{
		bool was = running() || t.phase == SleepPhase::Dark;
		sleepOff(t);
		if (!was) return;
		fprintf(stderr, "sleep timer: off\n");
		if (withNote) note("Sleep timer off", "");
	}

	void Ui::Sleep::stopped()
	{
		// Stop, anywhere: the timer goes with what played (its dark too: a Stop is someone awake)
		darkWait = false;
		off(true);
	}

	// ---- main's ----

	bool Ui::Sleep::endsWithItem() const
	{
		return t.kind == SleepKind::ItemEnd && running() && !byChapter() && !SessionHost::get().pageIsOutput();
	}

	void Ui::Sleep::itemEnded()
	{
		if (!endsWithItem()) return;
		endedNow = true;
		tick();
	}

	bool Ui::Sleep::tick()
	{
		if (!player) return false;
		Player& p = pl();
		SleepIn in = now();
		// the top's backlight is off only while its dark lasts: whatever ended the dark (a key, a Stop, a wake, the
		// page's Timer off) has it back
		if (!darkWait && !ui.topPanel.on()) ui.topPanel.set(true);
		// (a pass that comes round late in the dark is said: the handheld once never reached the sleep)
		if (t.phase == SleepPhase::Dark && lastTickAt && in.now - lastTickAt > 1000)
			fprintf(stderr, "sleep timer: the loop came round after %llu ms in the dark\n", (unsigned long long)(in.now - lastTickAt));
		lastTickAt = in.now;
		in.itemEnded = seenEnd(in) || endedNow;
		endedNow = false;
		SleepPhase was = shown;
		SleepOut o = sleepTick(t, in);
		shown = t.phase;
		if (o.pause) runOut();
		else if (t.phase == SleepPhase::Warning && was != SleepPhase::Warning) showWarning();
		else if (was == SleepPhase::Warning && t.phase != SleepPhase::Warning) endWarning(true);
		if (t.phase == SleepPhase::Warning)
		{
			// the fade is the handheld's sound only (a page's volume is its own)
			if (!SessionHost::get().pageIsOutput()) p.setFade(o.fade);
			drawCard();
		}
		restore();
		// a song's top screen: the quiet mark while it runs (a video: nothing over the film)
		bool mark = t.phase == SleepPhase::Running && p.playing() && p.playingAudio() && !SessionHost::get().pageIsOutput();
		if (mark) p.showSleepMark(G_CLOCK.rows, G_CLOCK.h, t.kind == SleepKind::ItemEnd ? "This " + word() : label());
		else p.dropSleepMark();
		if (o.sleep) fprintf(stderr, "sleep timer: dark for %d s: the sleep is due\n", (int)(SLEEP_DARK_MS / 1000));
		return o.sleep;
	}

	void Ui::Sleep::restore()
	{
		Player& p = pl();
		// after the pause has landed (mpv answers a moment later): the volume back at once, nothing to hear
		if (restoreOnPause && (p.paused || !p.playing()))
		{
			restoreOnPause = false;
			rampAt = 0;
			p.setFade(1);
		}
		// a snooze or Timer off in the last minute: back over a second
		if (rampAt && t.phase != SleepPhase::Warning)
		{
			double f = std::min(1.0, (SDL_GetTicks() - rampAt) / 1000.0);
			p.setFade(rampFrom + (1 - rampFrom) * f);
			if (f >= 1) rampAt = 0;
		}
	}

	void Ui::Sleep::pauseAll()
	{
		Player& p = pl();
		SessionHost& sh = SessionHost::get();
		if (sh.pageIsOutput())
		{
			// a page plays it: the Session's Pause, which the page follows (no fade there: its volume is its own)
			if (sh.session().playing) sh.playLive(false);
		}
		else if (p.playing() && !p.playingPhoto())
		{
			sh.play(false);
			p.pause();
		}
	}

	void Ui::Sleep::runOut()
	{
		Player& p = pl();
		fprintf(stderr, "sleep timer: ran out: paused, both screens off\n");
		pauseAll();
		// the place put back where the fade began: nothing heard half asleep at a whisper is missed
		if (!fadePath.empty() && fadePath == p.currentPath() && fadePos >= 0 && p.duration > 0)
		{
			fprintf(stderr, "sleep timer: back to %.1f s, where the fade began\n", fadePos);
			p.seekFraction(std::clamp(fadePos / p.duration, 0.0, 1.0));
		}
		fadePath.clear();
		fadePos = -1;
		endWarning(false);
		restoreOnPause = true;
		darkWait = true;
		ui.undim();
		ui.panel.set(false);
		ui.topPanel.set(false);	// off, not only drawn black (the top's picture is black too where there's no backlight)
	}

	void Ui::Sleep::endWarning(bool ramp)
	{
		Player& p = pl();
		if (Overlay* o = ui.findOverlay("sleepwarn")) o->close();
		p.dropSleepCard();
		cardText.clear();
		fadePath.clear();
		fadePos = -1;
		if (ramp && p.fade < 1)
		{
			rampFrom = p.fade;
			rampAt = SDL_GetTicks();
		}
	}

	void Ui::Sleep::drawCard()
	{
		// put up again only when its words change (once a second)
		std::string line1 = t.kind == SleepKind::ItemEnd ? "Sleeping after this " + word() + " \xC2\xB7 " + label() : "Sleeping in " + label();
		if (line1 == cardText) return;
		cardText = line1;
		std::string snooze = std::to_string(pl().settings.sleepSnooze) + " more min";
		pl().showSleepCard(G_CLOCK.rows, G_CLOCK.h, line1, "A  " + snooze + "     B  Timer off");
	}

	bool Ui::Sleep::wake()
	{
		if (noteKept && !darkWait)
		{
			// the first key after waking takes the note down, and does what it does
			noteKept = false;
			if (player) pl().hideNote();
			return false;
		}
		if (!darkWait) return false;
		darkWait = false;
		bool due = t.phase == SleepPhase::Dark;
		sleepOff(t);
		shown = SleepPhase::Off;
		ui.panel.set(true);
		ui.topPanel.set(true);
		fprintf(stderr, "sleep timer: a key in its dark: the screens back%s; it stays paused\n", due ? ", the sleep called off" : "");
		note("Paused by the sleep timer", "A plays", 24 * 3600 * 1000);
		noteKept = true;
		return true;
	}

	void Ui::Sleep::woke()
	{
		if (!player) return;
		bool mine = darkWait;
		if (running()) fprintf(stderr, "sleep timer: off (the handheld slept)\n");
		sleepOff(t);
		shown = SleepPhase::Off;
		endWarning(false);
		rampAt = 0;
		restoreOnPause = false;
		pl().setFade(1);
		darkWait = false;
		if (!mine) return;
		ui.panel.set(true);
		ui.topPanel.set(true);
		note("Paused by the sleep timer", "A plays", 24 * 3600 * 1000);
		noteKept = true;
	}

	// ---- the sheet (sleep-set): seven choice tiles, A sets and closes, X snoozes a running one ----

	struct Ui::SleepSheet : Ui::Overlay
	{
		int cursor = 1, pressed = -1;

		explicit SleepSheet(Ui& u) : Overlay(u)
		{
			// on the choice in use, else 30 min
			const SleepTimer& t = ui.sleep->t;
			if (t.kind == SleepKind::ItemEnd && ui.sleep->running()) cursor = 5;
			else if (t.kind == SleepKind::Minutes && ui.sleep->running())
				for (int i = 0; i < 5; i++)
					if (SLEEP_LENGTHS[i] == t.minutes) cursor = i;
		}
		const char* name() const override { return "sleep"; }

		int inUse() const
		{
			const SleepTimer& t = ui.sleep->t;
			if (!ui.sleep->running()) return 6;
			if (t.kind == SleepKind::ItemEnd) return 5;
			for (int i = 0; i < 5; i++)
				if (SLEEP_LENGTHS[i] == t.minutes) return i;
			return -1;	// (a snooze made it a length of no tile's)
		}
		void choose(int i)
		{
			close();
			if (i < 5) ui.sleep->set(SLEEP_LENGTHS[i]);
			else if (i == 5) ui.sleep->setItemEnd();
			else ui.sleep->off();
		}
		bool key(Button b, Player&) override
		{
			switch (b)
			{
			case Button::Left: cursor = lay::sleepMove(cursor, lay::Dir::Left); break;
			case Button::Right: cursor = lay::sleepMove(cursor, lay::Dir::Right); break;
			case Button::Up: cursor = lay::sleepMove(cursor, lay::Dir::Up); break;
			case Button::Down: cursor = lay::sleepMove(cursor, lay::Dir::Down); break;
			case Button::A: choose(cursor); break;
			case Button::X:
				if (ui.sleep->running())
				{
					close();
					ui.sleep->snooze();
				}
				break;
			case Button::B: close(); break;
			default: break;
			}
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player&) override
		{
			if (ui.touchIsLocked()) return false;
			lay::SleepSheet l = lay::sleepSheet();
			if (pt.kind == Pointer::Down)
			{
				pressed = -1;
				// the veil: closed, nothing changes
				if (!l.sheet.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
				for (int i = 0; i < lay::SLEEP_TILES; i++)
					if (l.tile[i].hit(x, y)) pressed = i;
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int i = pressed;
			pressed = -1;
			if (i >= 0 && l.tile[i].hit(x, y)) choose(i);
			return false;
		}
		void draw(const Player&) override
		{
			lay::SleepSheet l = lay::sleepSheet();
			Kit& k = *ui.kit;
			Sleep& s = *ui.sleep;
			std::string note = !s.running() ? "Off" : s.t.kind == SleepKind::ItemEnd ? "This " + s.word() : s.label() + " left";
			k.sheet(l.sheet, {&G_CLOCK, "Sleep timer", note});
			ui.drawTextMid("Pause after", F_I12, DIM_ON, l.label.x, l.label.y + l.label.h / 2);
			int on = inUse();
			for (int i = 0; i < lay::SLEEP_TILES; i++)
			{
				Kit::Choice c;
				c.name = i < 5 ? std::to_string(SLEEP_LENGTHS[i]) + " min" : i == 5 ? "End of this " + s.word() : "Off";
				c.on = i == on;
				c.focused = i == cursor;
				c.pressed = i == pressed;
				c.font = F_P14;
				k.choiceTile(l.tile[i], c);
			}
			ui.drawTextMid("The last minute fades the volume. Then it pauses, and the handheld sleeps.", F_I12, DIM, l.foot.x,
				l.foot.y + l.foot.h / 2, l.foot.w);
			std::vector<Hint> hints{{"A", "Set"}};
			if (s.running()) hints.push_back({"X", std::to_string(ui.sleep->player->settings.sleepSnooze) + " more min"});
			hints.push_back({"B", "Back"});
			k.hints(hints);
		}
		std::string signature(const Player&) const override
		{
			return std::to_string(cursor) + "," + std::to_string(pressed) + ui.sleep->signature() + ui.sleep->word();
		}
	};

	// ---- the last minute (sleep-warn): A snoozes, B turns it off; UP / DOWN the volume; nothing else acts ----

	struct Ui::SleepWarning : Ui::Overlay
	{
		int pressed = -1;	// 0 the snooze, 1 Timer off
		explicit SleepWarning(Ui& u) : Overlay(u) {}
		const char* name() const override { return "sleepwarn"; }

		bool key(Button b, Player& p) override
		{
			// A and B are its own even with both screens dark: the usual case in bed
			if (b == Button::A) ui.sleep->snooze();
			else if (b == Button::B) ui.sleep->off();
			else if (b == Button::Up || b == Button::Down) ui.playerPage->volumeKey(b, p);
			else if (!ui.panel.on()) ui.panel.set(true);	// any other key: a dark screen wakes, as always; nothing else
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player&) override
		{
			// a dark bottom screen's first touch only wakes it, to the card; touch locked: A and B still work
			if (!ui.panel.on())
			{
				if (pt.kind == Pointer::Down) { ui.panel.set(true); ui.swallowTouch = true; }
				return false;
			}
			if (ui.touchIsLocked()) return false;
			lay::SleepWarn l = lay::sleepWarn();
			if (pt.kind == Pointer::Down)
			{
				pressed = l.snooze.hit(x, y) ? 0 : l.off.hit(x, y) ? 1 : -1;
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int i = pressed;
			pressed = -1;
			if (i == 0 && l.snooze.hit(x, y)) ui.sleep->snooze();
			else if (i == 1 && l.off.hit(x, y)) ui.sleep->off();
			return false;
		}
		void draw(const Player&) override
		{
			Sleep& s = *ui.sleep;
			if (!s.warning()) { close(); return; }
			Kit& k = *ui.kit;
			// the Screen-off warning's veil and an ACCENT-edged card: never red, never a sound
			ui.fill({0, 0, W, H}, {0, 0, 0, (Uint8)(255 * lay::WARN_VEIL / 100)});
			lay::SleepWarn l = lay::sleepWarn();
			k.flat(l.card, SURFACE, R_BUTTON, ACCENT);
			ui.drawGlyphMid(G_CLOCK, l.icon.x + l.icon.w / 2, l.icon.y + l.icon.h / 2, ACCENT, 2);
			int cy = l.line.y + l.line.h / 2;
			std::string words = s.t.kind == SleepKind::ItemEnd ? "Sleeping after this " + s.word() + " \xC2\xB7 " + s.label()
				: "Sleeping in " + s.label();
			ui.drawTextMid(words, F_P16, ACCENT, l.line.x, cy, l.line.w);
			// the note on the right, where it fits: what the minute is doing
			std::string why = s.t.listEnded ? "the list has ended" : s.t.kind == SleepKind::Minutes && !SessionHost::get().pageIsOutput()
				? "the volume is fading" : "";
			int room = l.line.w - k.textW(words, F_P16) - 16;
			if (!why.empty() && k.textW(why, F_I12) <= room) ui.drawTextMid(why, F_I12, DIM_ON, l.line.x + l.line.w, cy, room, 1);
			Kit::Key snooze;
			snooze.glyph = &G_PLUS;
			snooze.label = std::to_string(s.player->settings.sleepSnooze) + " more min";
			snooze.key = "A";
			snooze.tone = Kit::Tone::Primary;
			snooze.pressed = pressed == 0;
			k.key(l.snooze, snooze);
			Kit::Key off;
			off.glyph = &G_CROSS;
			off.label = "Timer off";
			off.key = "B";
			off.pressed = pressed == 1;
			k.key(l.off, off);
			// its own hint bar, over the page's
			ui.fill({0, H - lay::HINTS_H, W, lay::HINTS_H}, BG);
			k.hints({{"A", std::to_string(s.player->settings.sleepSnooze) + " more min"}, {"B", "Timer off"}, {"UP/DOWN", "Volume"}});
		}
		std::string signature(const Player&) const override { return std::to_string(pressed) + ui.sleep->signature(); }
	};

	// ---- opening them (after both are whole) ----

	void Ui::Sleep::openSheet()
	{
		// (its last minute has its own card: the sheet waits till that's answered)
		if (warning()) return;
		ui.pushOverlay(std::make_unique<SleepSheet>(ui));
	}

	void Ui::Sleep::showWarning()
	{
		Player& p = pl();
		// where the fade begins (a length's, on the handheld): the pause comes back here
		fadePath.clear();
		fadePos = -1;
		if (t.kind == SleepKind::Minutes && !t.listEnded && !SessionHost::get().pageIsOutput() && p.playing() && p.loaded && p.position >= 0)
		{
			fadePath = p.currentPath();
			fadePos = p.position;
		}
		fprintf(stderr, "sleep timer: the last minute (%s)%s\n", label().c_str(), fadePath.empty() ? "" : ": the volume fades");
		// the sheet closes under it; the card is an overlay over any page, and the top screen's card goes up
		if (Overlay* o = ui.findOverlay("sleep")) o->close();
		if (!ui.findOverlay("sleepwarn")) ui.pushOverlay(std::make_unique<SleepWarning>(ui));
		cardText.clear();
		drawCard();
	}
}
