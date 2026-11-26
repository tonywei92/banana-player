// The player screen (a video, a song), rebuilt on the redesign's component kit (ui_player.h says what it is and what
// stayed Ui's; docs/redesign/src/screens.js "video", "music", "video-chips", "video-more", "play-on", "lock").
#include "ui_player.h"
#include "ui_sleep.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_settings.h"
#include "ui_sound.h"
#include "ui_smoothlist.h"
#include "session_host.h"
#include "volumestep.h"
#ifdef BP_WEB
#include "web/server.h"
#endif
#include <cmath>

namespace bp
{
	// a switch's touch: well past its drawn 40 x 22 (a finger's size each way), so a press that lands or lifts a little
	// off the switch still flips it
	static SDL_Rect switchArea(int x, int cy) { return {x - 14, cy - 24, 40 + 28, 48}; }
	static bool switchHit(int x, int cy, int px, int py) { SDL_Rect r = switchArea(x, cy); return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; }

	namespace
	{
		const char* const LOCK_NOTE = "Touch locked: hold the lock 1 s to unlock";
		// its key only where there's an L3 (the RG DS Plus, the 1024x768 one)
		bool hasL3() { return sx > 1.2f; }
	}

	bool Ui::touchIsLocked() const { return playerPage->touchLocked; }

	// ---- what plays next, and the lines ----

	bool Ui::PlayerPage::chapterLineShown(const Player& p) const
	{
		// before the first chapter starts there's none to name (and nothing to tap)
		return p.playing() && !p.playingPhoto() && !p.loading() && p.error.empty() && !p.chapters.empty() &&
			ui.shownChapter(p) >= 0 && ui.shownChapter(p) < (int)p.chapters.size();
	}

	Ui::PlayerPage::NextUp Ui::PlayerPage::nextUp(const Player& p) const
	{
		NextUp out;
		if (!p.playing() || p.playingPhoto()) return out;
		// the Session's list when it has this file (its Next is what the keys do), else the player's own, in order
		const Session& s = SessionHost::get().session();
		if (s.index >= 0 && s.index < (int)s.queue.size() && s.queue[s.index] == p.currentPath())
		{
			int to = nextPlace(s);
			if (to == NEXT_ROUND)
			{
				out.any = out.round = true;
				return out;
			}
			if (to < 0) return out;
			out.any = true;
			out.path = s.queue[to];
			// the mock-up shows what comes next's length when the lists have probed it; else its place in the list
			double d = -1;
			if (!isNetPath(out.path)) cachedDuration(out.path, d);
			out.note = d > 0 ? formatTime(d) : std::to_string(to + 1) + " of " + std::to_string(s.queue.size()) + (s.shuffle ? " \xC2\xB7 shuffled" : "");
			return out;
		}
		if (p.index() + 1 >= p.count()) return out;
		out.any = true;
		out.path = p.paths()[p.index() + 1];
		double d = -1;
		if (!isNetPath(out.path)) cachedDuration(out.path, d);
		out.note = d > 0 ? formatTime(d) : std::to_string(p.index() + 2) + " of " + std::to_string(p.count());
		return out;
	}

	bool Ui::PlayerPage::repeatMarkShown(const Player& p) const
	{
		return p.playing() && p.playingAudio();
	}

	void Ui::PlayerPage::stepRepeat(Player& p)
	{
		// Off -> All -> One -> Off: the List sheet's own setting (the Session follows the settings' loop)
		Settings st = p.settings;
		st.loop = (st.loop + 1) % LOOPS;
		p.setSettings(st);
	}

	bool Ui::PlayerPage::listAct(Player& p)
	{
		// List: a playlist's song opens that playlist's own page (its edits, its order), what plays going on behind;
		// a folder, Recent or a selection: the List sheet, on the tab it was left on
		if (p.source().kind == SourceKind::Playlist && !p.source().id.empty() && !p.playingPhoto())
		{
			ui.showSourceAfterLeave = true;
			return true;
		}
		openList(listTab, p);
		return false;
	}

	std::string Ui::PlayerPage::crumb(const Player& p, const Glyph*& mark) const
	{
		mark = nullptr;
		const PlaySource& src = p.source();
		if (src.id.empty()) return "";
		if (src.kind == SourceKind::Playlist)
		{
			// the playlist (Liked: its heart); an item on a server says which
			mark = ui.plIsLiked(src.id) ? &G_HEART : &G_PLAYLIST;
			std::string out = "Playlist \xC2\xB7 " + src.title;
			NetPath np;
			if (isNetPath(p.currentPath()))
			{
				const NetServer* sv = ui.netStore && parseNetPath(p.currentPath(), np) ? ui.netStore->find(np.server) : nullptr;
				out += std::string(" \xC2\xB7 ") + (sv ? serverName(*sv) : "Network");
			}
			return out;
		}
		// the folder, as the browser says it
		mark = src.kind == SourceKind::NetFolder ? &G_NETWORK : &G_CARD;
		return ui.whereOf(src.id);
	}

	// ---- the chips ----

	std::vector<Ui::PlayerPage::ChipDef> Ui::PlayerPage::chips(const Player& p) const
	{
		std::vector<ChipDef> out;
		auto add = [&](ChipKind kind, const Glyph* g, const char* label, const std::string& value, bool away) {
			ChipDef d;
			d.kind = kind;
			d.look.glyph = g;
			d.look.label = label;
			d.look.value = value;
			d.look.away = away;
			d.look.off = !p.error.empty();	// a file that didn't open: nothing to choose or speed up
			out.push_back(d);
		};
		auto track = [&](TrackType type) {
			const Track* t = p.selected(type);
			if (!t) return std::string("Off");
			auto list = p.tracksOf(type);
			int n = 1;
			for (size_t i = 0; i < list.size(); i++)
				if (list[i].id == t->id) n = (int)i + 1;
			return lay::chipTrack(*t, n, list);
		};
		const Settings& st = p.settings;
		if (!p.playingAudio())
		{
			add(C_SUBS, &G_CC, "Subtitles", track(TrackType::Sub), false);
			add(C_AUDIO, &G_SPEAKER, "Audio", track(TrackType::Audio), false);
			add(C_PICTURE, &G_PICTURE, "Picture", aspectName(p.aspect), p.aspect != AspectMode::Fit);
		}
		else
		{
			// two chips, one per choice (the preset and the effect were one value before): what changes the sound is
			// yellow, so an EQ or effect left on can't go unnoticed
			std::string preset = st.eqPreset.empty() ? CUSTOM_NAME : st.eqPreset;
			add(C_SOUND, &G_EQ, "Equalizer", st.eqOn ? preset : "Off", st.eqOn);
			add(C_FX, &G_SPEAKER, "Sound FX", FX_SHORT[std::clamp((int)st.soundFx, 0, SOUNDFX_COUNT - 1)],
				st.soundFx != SoundFx::None);
			// the Show mode in its short forms on the chip only (the style is named on View › Display and in
			// Settings); a choice, not a state away from normal: never yellow
			int show = std::clamp(st.musicShow, 0, SHOWS - 1);
			add(C_TOP, showGlyph(show, st.visualizer), "Display", SHOW_SHORT[show], false);
		}
		add(C_SPEED, &G_SPEED, "Speed", speedLabel(p.speed), std::fabs(p.speed - 1) > 1e-6);
		// Repeat is its own state (Off / All / One): the chip steps it in place; a song's fourth chip is Shuffle
		if (p.playingAudio()) add(C_SHUFFLE, &G_SHUFFLE, "Shuffle", st.shuffle ? "On" : "Off", st.shuffle);
		else add(C_MODE, st.loop == LOOP_ONE ? &G_LOOP_ONE : &G_LOOP, "Repeat", LOOP_SHORT[std::clamp(st.loop, 0, LOOPS - 1)],
			st.loop != LOOP_OFF);
		// a browser page plays it: these are the handheld's (their graphs, its screen), so they say so and are dim;
		// the buttons still answer (chipAct says why nothing changed)
		if (ui.predicting(p))
			for (auto& d : out)
				if (d.kind == C_SOUND || d.kind == C_FX || d.kind == C_TOP || d.kind == C_PICTURE) { d.look.value = "Handheld"; d.look.off = true; }
		return out;
	}

	void Ui::PlayerPage::chipAct(ChipKind kind, Player& p)
	{
		if (!p.error.empty()) return;
		// a page plays it: the sound, the top screen and the picture are the handheld's alone
		if (ui.predicting(p) && (kind == C_SOUND || kind == C_FX || kind == C_TOP || kind == C_PICTURE))
		{
			p.osd("Plays on the handheld only");
			return;
		}
		switch (kind)
		{
		// a video's: its own sheet (the tracks, the picture, the speed), over the controls
		case C_SUBS: openView(0, p); break;
		case C_AUDIO: openView(1, p); break;
		case C_PICTURE: openView(2, p); break;
		// a song's: Equalizer, Effects and Display (the preset, the effect, the Show mode)
		case C_SOUND: openView(1, p); break;
		case C_FX: openView(2, p); break;
		case C_TOP: openView(0, p); break;
		// Speed: its own sheet for both (a video's fourth, a song's fourth)
		case C_SPEED: openView(3, p); break;
		// Repeat steps Off -> All -> One -> Off in place (the List sheet's own setting, through the Session); a song's
		// Shuffle turns on and off in place. No sheet and no note: the chip's word and its yellow say it
		case C_MODE: stepRepeat(p); break;
		case C_SHUFFLE:
			SessionHost::get().setShuffle(!p.settings.shuffle);
			break;
		}
	}

	// ---- touch ----

	Ui::Rect Ui::PlayerPage::rect(int id, const lay::PlayerLayout& l) const
	{
		if (id == T_BACK) return l.backHit;
		if (id == T_MORE) return l.moreHit;
		if (id == T_LINE) return l.lineHit;
		if (id >= T_PREV && id <= T_NEXT) return l.transport[id - T_PREV];
		if (id >= T_LIKE && id <= T_STOP) return l.keys[id - T_LIKE];
		if (id >= T_CHIP0 && id < T_CHIP0 + l.chips) return l.chip[id - T_CHIP0];
		if (id == T_NEXTLINE) return l.next;
		if (id == T_REPEAT) return l.repeat;
		return {0, 0, 0, 0};
	}

	bool Ui::PlayerPage::usable(int id, const Player& p) const
	{
		bool err = !p.error.empty();
		switch (id)
		{
		case T_LINE: return chapterLineShown(p);
		// a file that didn't open: only what leads elsewhere (the next file, the list, More), and Like / Add
		case T_REW: case T_FF: return !err && p.duration > 0;
		case T_REPEAT: return !err;
		case T_PLAY: return !err || p.errorNet;	// (the network dropped it: Retry)
		// Like and Add work on a file that didn't open too (it's still a file)
		case T_LIKE: case T_ADD: return ui.headerActions(p);
		case T_NEXT:
		{
			// (only In order has no Next at the last one: Shuffle starts a new round, a Loop goes back to the first)
			const Session& ss = SessionHost::get().session();
			return ss.index >= 0 && p.currentPath() == ss.queue[ss.index] ? hasNext(ss)
				: p.settings.shuffle || p.settings.loop != LOOP_OFF || p.index() + 1 < p.count();
		}
		default: return id >= T_CHIP0 && id < T_CHIP_END ? !err : true;
		}
	}

	bool Ui::PlayerPage::act(int id, Player& p)
	{
		switch (id)
		{
		// "<" over what plays leaves at once (it plays on behind the browser)
		case T_BACK: return true;
		case T_MORE: openMoreSheet(); return false;
		case T_LINE: openList(1, p); return false;
		// a key on the screen: what its button does, whether the buttons themselves are on or not
		case T_PREV: return ui.pressAnyway(Button::L, p);
		case T_REW: return ui.pressAnyway(Button::Left, p);
		case T_PLAY: return ui.pressAnyway(Button::A, p);
		case T_FF: return ui.pressAnyway(Button::Right, p);
		case T_NEXT: return ui.pressAnyway(Button::R, p);
		case T_LIKE: ui.likeNow(p); return false;
		case T_ADD:
			if (p.playing()) ui.browse->playlists->openAddOne(p.currentPath(), p.title, p.loaded ? p.duration : -1, p);
			return false;
		case T_SLEEP: ui.sleep->openSheet(); return false;
		case T_NEXTLINE: openList(0, p); return false;
		// the mark has no word of its own: the top screen names what it is now ("Loop all", with Shuffle's)
		case T_REPEAT:
			stepRepeat(p);
			p.osd(ui.modeLabel(p));
			return false;
		// Stop, the same flag the More sheet's Stop tile sets: the Session stops it everywhere, the place kept
		case T_STOP: ui.stopPressed = true; return false;
		default: break;
		}
		if (id >= T_CHIP0 && id < T_CHIP_END)
		{
			auto list = chips(p);
			if (id - T_CHIP0 < (int)list.size()) chipAct(list[id - T_CHIP0].kind, p);
		}
		return false;
	}

	bool Ui::PlayerPage::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (p.playingPhoto())
		{
			// the photo viewer's (Ui's: its moves and lifts are taken before this, by its pan and its buttons)
			if (pt.kind != Pointer::Down) return false;
			if (ui.buttonRect(BACK_ICON).hit(x, y)) { ui.pressedBtn = BACK_ICON; return false; }
			return ui.photoDown(x, y, p);
		}
		lay::PlayerLayout l = lay::playerLayout((int)chips(p).size(), repeatMarkShown(p));
		if (pt.kind == Pointer::Down)
		{
			pressed = T_NONE;
			// the header's keys, then the chapter line (its touch area reaches up over the title), then the bar
			for (int id : {T_BACK, T_MORE, T_LINE})
				if (pressed == T_NONE && rect(id, l).hit(x, y) && usable(id, p)) pressed = id;
			if (pressed != T_NONE) return false;
			if (l.barHit.hit(x, y) && p.duration > 0 && p.error.empty())
			{
				// a trackpad's scrub: a slide moves the place on by the finger's travel, a tap jumps there as it lifts
				// (drawBar shows slide.value and its time while it's held). Nothing seeks until the finger lifts (the
				// owner): then the exact seek to where it was left, and the bar held there until playback arrives
				// (seekHeld)
				double pos = ui.shownPosition(p);
				ui.slideGrab(SL_SEEK, 0, {l.bar, {0, 1, 0}, false, SCRUB_MS, seekHeld >= 0 ? seekHeld : pos >= 0 ? std::clamp(pos / p.duration, 0.0, 1.0) : 0}, x, y, p,
					[this, scrubs = 0](double f, bool last, Player& p) mutable {
						if (!last)
						{
							scrubs++;
							return;
						}
						fprintf(stderr, "seek bar: let go at %.4f after %d moves\n", f, scrubs);
						p.seekFraction(f);
						ui.noteSeek(p, f * p.duration);
						seekHeld = f;
						seekHeldAt = SDL_GetTicks();
					});
				return false;
			}
			// (a dimmed Next still answers: its press says "This is the last one", as the R button's does)
			for (int id = T_PREV; id < T_COUNT; id++)
				if (rect(id, l).hit(x, y) && (usable(id, p) || id == T_NEXT)) pressed = id;
			// Play while a page plays it: held, it plays here (Ui::tick times it); let go sooner, the page's play / pause
			if (pressed == T_PLAY && p.error.empty() && ui.pageDrives(p))
			{
				ui.playHoldAt = SDL_GetTicks();
				ui.playHoldTouch = true;
			}
			return false;
		}
		if (pt.kind != Pointer::Up) return false;
		int id = pressed;
		pressed = T_NONE;
		if (id == T_NONE) return false;
		if (id == T_PLAY && ((ui.playHoldTouch && ui.playHoldAt) || ui.pageDrives(p)))
		{
			ui.playHoldAt = 0;
			if (rect(id, l).hit(x, y) && ui.pageDrives(p)) ui.toggleBrowser(p);
			return false;
		}
		// let go off the key: nothing (a finger that slid away changed its mind)
		return rect(id, l).hit(x, y) && (usable(id, p) || id == T_NEXT) && act(id, p);
	}

	// ---- Lock ----

	void Ui::PlayerPage::lock(Player& p)
	{
		touchLocked = true;
		lockHoldAt = 0;
		p.osd(LOCK_NOTE);
	}

	void Ui::PlayerPage::unlock()
	{
		touchLocked = false;
		lockHoldAt = 0;
	}

	bool Ui::PlayerPage::lockPointer(const Pointer& pt, int x, int y, Player& p)
	{
		// a hold on the lock (lockTimer times it) releases it; nothing else answers a finger
		Rect key = lay::playerLayout(0).lock;
		if (pt.kind == Pointer::Down && key.hit(x, y)) lockHoldAt = SDL_GetTicks();
		else if (pt.kind == Pointer::Up) lockHoldAt = 0;	// let go too soon: still locked
		else if (pt.kind == Pointer::Down && SDL_GetTicks() - lockNoticeAt > 3000)
		{
			lockNoticeAt = SDL_GetTicks();
			p.osd(LOCK_NOTE);
		}
		return false;
	}

	void Ui::PlayerPage::lockTimer()
	{
		// the lock held long enough: released (the finger still down presses nothing)
		if (touchLocked && lockHoldAt && SDL_GetTicks() - lockHoldAt >= UNLOCK_HOLD_MS)
		{
			unlock();
			ui.swallowTouch = true;
		}
		if (!touchLocked) lockHoldAt = 0;
	}

	// ---- keys ----

	void Ui::PlayerPage::release(Button b, Player& p)
	{
		(void)p;
		if (b == volHold) volHold = Button::None;
	}

	void Ui::PlayerPage::volumeKey(Button b, Player& p)
	{
		volStepAt = SDL_GetTicks();	// (a hold at 100% steps nowhere: the repeats still say it is held)
		bool up = b == Button::Up, hold = volHold == b;
		if (!hold) { volHold = b; volHoldLogged = false; }
#ifdef BP_WEB
		// a browser tab plays the sound: UP / DOWN move its volume, five a press, as the handheld's keys do (the
		// handheld's own is left as it was); the card says whose volume it is
		const Session& s = SessionHost::get().session();
		if (ui.web && s.out.kind == OutKind::Browser && s.index >= 0 && !s.out.tab.empty())
		{
			int bv = std::clamp((ui.browserVol >= 0 ? ui.browserVol : 100) + (up ? 5 : -5), 0, 100);
			ui.browserVol = bv;
			ui.web->notify("volume", "{\"tab\":\"" + s.out.tab + "\",\"v\":" + std::to_string(bv) + "}");
			ui.levelLabel = "Browser volume";
			ui.levelPct = bv;
			ui.levelMs = 1500;
			fprintf(stderr, "volume: UP/DOWN: the browser's volume %d\n", bv);
			return;
		}
#endif
		int now = ui.volumeTotal(p);
		if (now < 0) now = p.boost;	// (no level read: the player's own gain is what it steps from)
		VolumeStep st = volumeStep(now, p.settings.volumeMax, up, hold);
		if (st.stopped && !volHoldLogged && p.settings.volumeMax > 100)
		{
			volHoldLogged = true;
			fprintf(stderr, "volume: a hold stops at 100%% (a fresh press boosts)\n");
		}
		if (st.v == now) return;
		ui.setVolumeTotal(p, st.v, !up);
		fprintf(stderr, "volume: UP/DOWN %s: %d%%%s\n", up ? "up" : "down", st.v, hold ? " (held)" : "");
	}

	bool Ui::PlayerPage::key(Button b, Player& p)
	{
		if (p.playingPhoto())
		{
			// SELECT or X: the photo's More (the four chips, then its file's rows)
			if (b == Button::X || b == Button::Select) { openPhotoMore(); return false; }
			if (ui.photoKey(b, p)) return false;
		}
		bool err = !p.error.empty();
		// the network dropped it: A tries again, from where it stopped (the session reloads it there)
		if (err && p.errorNet && b == Button::A)
		{
			fprintf(stderr, "net: trying %s again\n", p.currentPath().c_str());
			SessionHost::get().playLive(true);	// (live: its reload runs now, as Next's does)
			return false;
		}
		// a file that didn't open: nothing to pause or choose (Prev, Next, the list, More, the menu and leaving still go)
		if (err && b == Button::A) return false;
		// UP / DOWN are the volume, as the handheld's keys are (five a step, past 100% the player's gain; the browser's
		// volume while a browser plays); after the photo branch, where they zoom
		if (b == Button::Up || b == Button::Down) { volumeKey(b, p); return false; }
		switch (b)
		{
		case Button::A:
			// a page plays it: the page's play/pause as A lets go (Ui::release), here only once held (Ui::tick)
			if (ui.pageDrives(p)) { if (!ui.playHoldAt) { ui.playHoldAt = SDL_GetTicks(); ui.playHoldTouch = false; } break; }
			// the page is the output but can't hear (its tab gone a moment): play here
			if (ui.pageIsOut()) { ui.takeOver(p); break; }
			SessionHost::get().play(p.paused);	// (mpv answers the flip later: paused is still the old state)
			p.togglePause();
			break;
		// out of the player at once: what plays goes on behind the browser (nothing is lost, so a pocket's press costs
		// nothing); a photo closes
		case Button::B: return true;
		// More: Lock, Screen off, Info, Stop; Like, Add to playlist, Show in folder, Play on
		case Button::X: openMoreSheet(); break;
		case Button::Y: return listAct(p);
		// the menu opens on what plays (Like, Add to playlist...): Settings is a tab along
		case Button::Start: ui.openSettings(p, true); break;
		// the stick pressed in (the RG DS Plus): Like, at once (over a photo it turns it, above)
		case Button::L3: ui.likeNow(p); break;
		// More, as X: its first row is the player's five chips, then Lock, Screen off, Info, Stop, Like, Add,
		// Show in folder, Play on
		case Button::Select: openMoreSheet(); break;
		// (a page playing it: from where it should be by now; the jump is passed on to the page)
		case Button::Left:
		case Button::Right:
			if (err) break;
			if (ui.predicting(p))
			{
				double t = std::clamp(ui.shownPosition(p) + (b == Button::Left ? -BACK_S : FORWARD_S), 0.0, p.duration);
				p.seekFraction(t / p.duration);
				ui.noteSeek(p, t);
			}
			else p.seekRelative(b == Button::Left ? -BACK_S : FORWARD_S);
			break;
		case Button::L:
		case Button::R:
			// the session's Step (Previous RESTART_S in restarts; a burst counts every press; a page playing it moves on
			// there, the copy here following held). What the session doesn't have (it disagrees: logged), as before
			if (SessionHost::get().stepsHere(p)) { SessionHost::get().step(b == Button::R ? 1 : -1); break; }
			if (b == Button::L ? !p.back() : !p.next()) p.osd(b == Button::L ? "This is the first one" : "This is the last one");
			break;
		default: break;
		}
		return false;
	}

	void Ui::PlayerPage::tick(Player& p)
	{
		// a hold whose key-up was missed (the page left under it, the focus lost) ends a second after its last step
		if (volHold != Button::None && SDL_GetTicks() - volStepAt > 1000) volHold = Button::None;
		// the bar held at a released drag: let go once playback is there, or after 6 s whatever happens (a network file
		// buffers a while)
		if (seekHeld >= 0 && !ui.slideHeld(SL_SEEK) &&
			(p.duration <= 0 || std::fabs(ui.shownPosition(p) - seekHeld * p.duration) < 1.0 || SDL_GetTicks() - seekHeldAt > 6000))
			seekHeld = -1;
	}

	// ---- X: More, and Play on ----

	Ui::MoreSpec Ui::PlayerPage::moreSpec(const Player& p) const
	{
		MoreSpec m;
		// (nothing plays any more, or the player was left under the sheet: an empty list, and the sheet closes)
		if (!p.playing() || p.playingPhoto() || ui.page != Page::Main) return m;
		Ui* u = &ui;
		PlayerPage* self = const_cast<PlayerPage*>(this);
		std::string path = p.currentPath();
		bool audio = p.playingAudio();
		m.glyph = audio ? &G_NOTE : &G_FILM;
		m.title = !p.title.empty() ? p.title : entryTitle(path);
		m.note = audio && !p.artist().empty() ? p.artist() + (p.album().empty() ? "" : " \xC2\xB7 " + p.album())
			: ui.whereOf(isNetPath(path) ? netParent(path) : parentFolder(path));
		auto item = [](const Glyph* g, const std::string& label, std::function<bool(Player&)> act) {
			MoreItem it;
			it.glyph = g;
			it.label = label;
			it.act = std::move(act);
			return it;
		};
		// only what the player's page doesn't already show (its chips, Like, Playlist and Stop are on it)
		m.tiles.push_back(item(&G_LOCK, touchLocked ? "Unlock touch" : "Lock touch", [self](Player& p) {
			if (self->touchLocked) self->unlock();
			else self->lock(p);
			return false;
		}));
		m.tiles.push_back(item(&G_MOON, "Screen off", [u](Player&) { u->panel.set(false); return false; }));
		// the sleep timer: its sheet (what is left, in yellow, while one runs)
		m.tiles.push_back(item(&G_CLOCK, ui.sleep->running() ? ui.sleep->label() : "Sleep timer", [u](Player&) { u->sleep->openSheet(); return false; }));
		m.tiles.back().lit = ui.sleep->running();
		m.tiles.push_back(item(&G_INFO, "Info", [u](Player&) {
			u->page = Page::Info;
			u->cursor = u->scroll = 0;
			u->infoAt = 0;	// read at once
			return false;
		}));
		// its folder: the player left first (it plays on behind the browser), then the folder (showBrowser)
		m.rows.push_back(item(&G_FOLDER, "Show in folder", [u](Player& p) { u->showAfterLeave = p.currentPath(); return true; }));
		// a song: Keep playing (Music rail lists play on past their last song), switched here in place
		if (audio)
		{
			m.rows.push_back(item(&G_SHUFFLE, "Autoplay", [](Player& p) {
				Settings st = p.settings;
				st.keepPlaying = !st.keepPlaying;
				p.setSettings(st);
				fprintf(stderr, "ui: keep playing %s (More)\n", st.keepPlaying ? "on" : "off");
				return false;
			}));
			m.rows.back().note = p.settings.keepPlaying ? "On" : "Off";
			m.rows.back().lit = p.settings.keepPlaying;
			m.rows.back().stay = true;
		}
		// while a dashboard page that can play is open, or one plays it
		if (ui.playOnShown())
		{
			m.rows.push_back(item(&G_QR, "Play on...", [self](Player&) { self->openPlayOn(); return false; }));
			m.rows.back().note = ui.outputName();
		}
		m.cols = 2;
		return m;
	}

	void Ui::PlayerPage::openMoreSheet()
	{
		pressed = T_NONE;
		ui.openMore([this](const Player& p) { return moreSpec(p); });
	}

	// A photo's More (SELECT or X): the four chips that shape how photos are shown, changed in place, then the file's
	// own rows (Info, its folder, Photo settings). The zoom and the turn have their keys, so they need no chip.
	Ui::MoreSpec Ui::PlayerPage::photoMoreSpec(const Player& p) const
	{
		MoreSpec m;
		if (!p.playingPhoto() || ui.page != Page::Main) return m;
		Ui* u = &ui;
		m.glyph = &G_PICTURE;
		m.title = !p.title.empty() ? p.title : entryTitle(p.currentPath());
		m.note = p.photoWidth() > 0 ? std::to_string(p.photoWidth()) + " x " + std::to_string(p.photoHeight()) : "";
		auto chip = [](const Glyph* g, const char* label, const std::string& value, bool away, std::function<void(Player&)> act) {
			MoreItem it;
			it.glyph = g;
			it.label = label;
			it.value = value;
			it.away = away;
			it.stay = true;	// (changed in place: the chip says what it is now)
			it.act = [act = std::move(act)](Player& p) { act(p); return false; };
			return it;
		};
		m.chips.push_back(chip(&G_PICTURE, "Shown", p.photo().fill ? "Fill" : "Fit", p.photo().fill, [](Player& p) { p.togglePhotoFill(); }));
		m.chips.push_back(chip(&G_REFRESH, "Keep zoom", p.settings.keepPhotoZoom ? "On" : "Off", !p.settings.keepPhotoZoom,
			[](Player& p) { Settings s = p.settings; s.keepPhotoZoom = !s.keepPhotoZoom; p.setSettings(s); }));
		m.chips.push_back(chip(&G_MOVE, "Pan area", p.settings.panRight ? "Right" : "Left", p.settings.panRight,
			[](Player& p) { Settings s = p.settings; s.panRight = !s.panRight; p.setSettings(s); }));
		m.chips.push_back(chip(&G_MOVE, "A drag moves", p.settings.dragBox ? "The box" : "The photo", !p.settings.dragBox,
			[](Player& p) { Settings s = p.settings; s.dragBox = !s.dragBox; p.setSettings(s); }));
		auto row = [](const Glyph* g, const char* label, std::function<bool(Player&)> act) {
			MoreItem it;
			it.glyph = g;
			it.label = label;
			it.act = std::move(act);
			return it;
		};
		if (p.count() > 1)
		{
			// the next photo every few seconds; L / R still step by hand, and it ends with the last photo
			MoreItem it = row(&G_PLAY, u->slideshow ? "Stop slideshow" : "Slideshow", [u](Player& pl) {
				u->slideshow = !u->slideshow;
				u->slideAt = SDL_GetTicks();
				pl.osd(u->slideshow ? "Slideshow: a photo every 5 s" : "Slideshow stopped", 1800);
				return false;
			});
			it.note = u->slideshow ? "" : "A photo every 5 s";
			it.lit = u->slideshow;
			m.rows.push_back(it);
		}
		m.rows.push_back(row(&G_INFO, "Info", [u](Player&) {
			u->page = Page::Info;
			u->cursor = u->scroll = 0;
			u->infoAt = 0;	// read at once
			return false;
		}));
		m.rows.push_back(row(&G_FOLDER, "Show in folder", [u](Player& p) { u->showAfterLeave = p.currentPath(); return true; }));
		if (!isNetPath(p.currentPath()))
		{
			// the file's own rows, as Files' More has them (the clipboard's flow is Files': the viewer leaves for the
			// photo's folder; Delete asks first, then the viewer closes)
			m.rows.push_back(row(&G_PAGE, "Copy\xE2\x80\xA6", [u](Player& pl) { u->browse->photoFileOp(CTX_F_COPY, pl.currentPath(), pl); return true; }));
			m.rows.push_back(row(&G_MOVE, "Move\xE2\x80\xA6", [u](Player& pl) { u->browse->photoFileOp(CTX_F_MOVE, pl.currentPath(), pl); return true; }));
			MoreItem del = row(&G_TRASH, "Delete", [u](Player& pl) { u->browse->photoFileOp(CTX_F_DELETE, pl.currentPath(), pl); return false; });
			del.danger = true;
			m.rows.push_back(del);
		}
		m.rows.push_back(row(&G_GEAR, "Photo settings", [u](Player& p) {
			u->openSettings(p, true);	// (B or START comes back to the photo)
			u->settingsPage->openGroup(GROUP_PHOTOS, &p);
			return false;
		}));
		return m;
	}

	void Ui::PlayerPage::openPhotoMore()
	{
		ui.openMore([this](const Player& p) { return photoMoreSpec(p); });
	}

	// A sheet's rows (List, View, a song's sheets) on a smooth list (ui_smoothlist.h): the rows' window under the sheet's
	// header, the finger (a drag scrolls, a tap chooses, a touch on a glide only stops it), the cursor kept in view, and
	// the list put on its cursor at once when it shows other rows (a tab, a dialog) or comes back from under a page.
	struct Ui::SheetRows
	{
		explicit SheetRows(Ui& u) : list(u) {}
		SmoothList list;
		bool onList = false, caught = false, moved = false;
		std::string shownFor = "\x01";	// what the rows were (a tab, a dialog): another one is put on its cursor

		static Rect view(const lay::ListSheet& s, int shown)
		{
			lay::Box r0 = s.row(0);
			return {r0.x, s.rowsY, r0.w, shown * lay::LIST_ROW};
		}
		// the cursor kept in view by a short glide (a key moved it)
		void reveal(int cursor, Rect v, int n) { if (n > 0) list.reveal(cursor, v, lay::LIST_ROW, n); }
		// before the rows are drawn: rows of another kind (`what`) are put with `above` rows over the cursor where the
		// list allows; the same rows come back (from under a page): the least move that shows the cursor
		void settleOn(const std::string& what, int cursor, Rect v, int n, int above)
		{
			bool fresh = list.takeFresh();
			if (what != shownFor)
			{
				shownFor = what;
				list.sc.setExtent((double)n * lay::LIST_ROW, v.h);
				list.sc.jump((double)std::max(0, cursor - above) * lay::LIST_ROW);
				fresh = true;
			}
			if (fresh && n > 0) list.reveal(cursor, v, lay::LIST_ROW, n, false);
		}
		// a finger down: the row under it (-1: none, outside the rows, or it only stopped a glide)
		int down(const Pointer& pt, int x, int y, Rect v)
		{
			moved = caught = false;
			onList = v.hit(x, y);
			if (!onList) return -1;
			caught = list.press(pt.y * H);
			return caught ? -1 : list.rowAt(x, y);
		}
		void move(const Pointer& pt) { if (onList && list.move(pt.y * H)) moved = true; }
		// the lift: the glide (or the band's spring); true when it was a tap on the rows
		bool up()
		{
			bool tap = onList && !moved && !caught;
			if (onList) list.lift();
			onList = false;
			return tap;
		}
		// the first row shown, for the scroll bar
		int first() const { return (int)std::lround(list.sc.offset() / lay::LIST_ROW); }
		// a Kit row's look as its key ("": a focused one, whose name's marquee moves by itself)
		static std::string key(const Kit::Row& r)
		{
			if (r.focused && !r.off) return "";
			if (r.heading) return "head|" + std::to_string(r.pressed) + "|" + r.name + "|" + r.key;
			char b[96];
			snprintf(b, sizeof b, "%p,%d,%d,%d,%d,%d,%d,%d,%d|", (const void*)r.glyph, (int)r.tone, (int)r.trail, r.pressed, r.off,
				r.radio, r.radioOn, r.font, (int)r.under.r);
			return b + r.lead + "|" + r.name + "|" + r.note + "|" + r.pill;
		}
		// the rows drawn: row(i) gives each one's look (it may outlive this draw: what it needs, by value)
		void draw(Ui& ui, Rect v, int n, std::function<Kit::Row(int)> row, std::function<bool(int)> heading = nullptr)
		{
			SmoothList::Spec spec;
			spec.view = v;
			spec.rowH = lay::LIST_ROW;
			spec.count = n;
			spec.heading = std::move(heading);
			spec.key = [row](int i) { return key(row(i)); };
			spec.draw = [&ui, row](int i, Rect box) { ui.kit->row(box, row(i)); };
			list.draw(std::move(spec));
		}
	};

	// List (Y): what is in this file and what comes after it, side by side (docs/redesign, "video-list" and
	// "video-chapters-long"). Two tabs, by lay::tabs: Up next (ui.upNextList, A = ui.playUpNext) and Chapters
	// (p.chapters, A as the Chapters page chooses). Shuffle and Repeat sit in its header, above the list they change.
	// L / R step the tabs, LEFT / RIGHT page the rows, L2 / R2 jump a chapter with the sheet open, Y / B close.
	struct Ui::ListSheet : Ui::Overlay
	{
		enum Press { P_NONE, P_ROW, P_TAB, P_SHUFFLE, P_REPEAT, P_HEADING };
		int tab = 0, cursor = 0, press = P_NONE, pressAt = -1;
		Uint32 pressTicks = 0;	// when the finger came down on a row (held ROW_HOLD_MS and let go: the row's menu)
		static constexpr Uint32 ROW_HOLD_MS = 500;
		// the rows: a finger's drag scrolls them, a tap (no drag) still chooses
		SheetRows rows{ui};
		SmoothList* smoothList() override { return &rows.list; }

		ListSheet(Ui& u, int at, const Player& p) : Overlay(u)
		{
			tab = std::clamp(at, 0, tabCount(p) - 1);
			// opened on what plays, three rows above it showing where the list allows (as the old page did)
			int n = count(p), cur = current(p);
			cursor = std::clamp(cur, 0, std::max(0, n - 1));
			ui.playerPage->listTab = tab;
		}
		const char* name() const override { return "list"; }

		bool hasChapters(const Player& p) const { return !p.chapters.empty(); }
		int tabCount(const Player& p) const { return hasChapters(p) ? 2 : 1; }
		std::vector<std::string> labels(const Player& p) const
		{
			std::vector<std::string> out{"Up next"};
			if (hasChapters(p)) out.push_back("Chapters " + std::to_string(p.chapters.size()));
			return out;
		}
		int count(const Player& p) const
		{
			if (tab == 1) return (int)p.chapters.size();
			int cur = -1;
			return (int)ui.upNextList(p, cur).size();
		}
		// the rows: the items (the cursor's), with Up next's heading before what Keep playing added
		lay::Sections sections(const Player& p) const
		{
			lay::Sections sc;
			sc.items = count(p);
			if (tab == 0)
				if (int from = ui.upNextAddedFrom(p); from >= 0) sc.before = {from};
			return sc;
		}
		int rowCount(const Player& p) const { return sections(p).rows(); }
		int shown(const Player& p) const { return std::min(std::max(1, rowCount(p)), lay::LIST_ROWS); }
		static constexpr const char* HEADING_ON = "Autoplay \xC2\xB7 songs picked for you", *HEADING_OFF = "Songs picked for you";
		static std::string headingAction(const Player& p) { return p.settings.keepPlaying ? "Turn off" : "Turn on"; }
		// Keep playing on or off (the heading's chip, X): the setting; what it added stays in the list
		void toggleKeepPlaying(Player& p)
		{
			Settings st = p.settings;
			st.keepPlaying = !st.keepPlaying;
			p.setSettings(st);
			p.osd(st.keepPlaying ? "Autoplay on" : "Autoplay off");
			fprintf(stderr, "ui: keep playing %s (Up next)\n", st.keepPlaying ? "on" : "off");
		}
		lay::ListSheet layout(const Player& p) const { return lay::listSheet(shown(p)); }
		Rect rowsView(const Player& p) const { return SheetRows::view(layout(p), shown(p)); }

		// what plays in the tab shown: Up next's row, or the chapter playing (none before the first)
		int current(const Player& p) const
		{
			if (tab == 1) return ui.shownChapter(p);
			int cur = -1;
			ui.upNextList(p, cur);
			return cur;
		}
		void settle(const Player& p)
		{
			int n = count(p);
			cursor = n <= 0 ? 0 : std::clamp(cursor, 0, n - 1);
		}
		void openTab(int t, const Player& p)
		{
			tab = std::clamp(t, 0, tabCount(p) - 1);
			int n = count(p), cur = current(p);
			cursor = std::clamp(cur, 0, std::max(0, n - 1));
			ui.playerPage->listTab = tab;
		}

		struct Head
		{
			Rect tabs, shuffleHit, shuffleLabel, shuffleGlyph, repeatGlyph, repeatArea;
			int toggleX = 0;
		};
		// the header's right side: Shuffle's switch and Repeat's three chips, right-aligned; the tabs take the rest
		Head head(const lay::ListSheet& s) const
		{
			Head h;
			int cy = s.head.y + s.head.h / 2, right = s.head.x + s.head.w;
			int segW = 3 * 42 + 2 * 6, repW = 16 + 6 + segW;
			right -= repW;
			h.repeatGlyph = {right, cy - 8, 16, 16};
			h.repeatArea = {right + 22, cy - 20, segW, 40};
			right -= 12;
			int labelW = ui.kit->textW("Shuffle", F_I12);
			int swW = 16 + 6 + labelW + 7 + 40;
			right -= swW;
			h.shuffleGlyph = {right, cy - 8, 16, 16};
			h.shuffleLabel = {right + 22, cy - 7, labelW, 14};
			h.toggleX = right + 22 + labelW + 7;
			h.shuffleHit = {right, cy - 16, swW, 32};
			h.tabs = {s.head.x, s.head.y, std::max(0, right - 12 - s.head.x), s.head.h};
			return h;
		}
		std::vector<Rect> tabRects(const Player& p, const Head& h) const { return ui.kit->tabRects(h.tabs, labels(p), hasChapters(p)); }
		std::vector<Rect> repeatRects(const Head& h) const
		{
			std::vector<Rect> out;
			for (int i = 0; i < 3; i++) out.push_back(lay::cell(h.repeatArea, 3, 6, i));
			return out;
		}

		void setShuffle(Player& p)
		{
			SessionHost::get().setShuffle(!p.settings.shuffle);
			p.osd(ui.modeLabel(p));
		}
		void setLoop(int loop, Player& p)
		{
			Settings st = p.settings;
			st.loop = std::clamp(loop, 0, LOOPS - 1);
			p.setSettings(st);
			p.osd(ui.modeLabel(p));
		}

		// Up next's row menu (X, or a row held): Play from here (not on the one playing), Remove from list, Keep playing on / off. Remove goes
		// through the Session (Remove): the list that plays loses the row; a playlist's own file is untouched
		void openRowMenu(int i, const Player& p)
		{
			int cur = -1;
			auto list = ui.upNextList(p, cur);
			if (tab != 0 || i < 0 || i >= (int)list.size()) return;
			std::string path = list[i];
			const Session& s = SessionHost::get().session();
			bool fromPlaylist = s.source.kind == 2;
			bool added = std::find(s.added.begin(), s.added.end(), path) != s.added.end();
			bool now = i == cur, hasSection = !sections(p).before.empty();
			ui.openMore([this, i, path, fromPlaylist, added, now, hasSection](const Player& pl) {
				MoreSpec m;
				m.glyph = &G_NOTE;
				m.title = entryTitle(path);
				m.note = added ? "Picked by Autoplay" : "Row " + std::to_string(i + 1) + " of Up next";
				if (!now)
				{
					MoreItem play;
					play.glyph = &G_PLAY;
					play.label = "Play from here";
					play.act = [this, i](Player& p) { choose(i, p); return false; };
					m.rows.push_back(play);
				}
				MoreItem rm;
				rm.glyph = &G_MINUS;
				rm.label = "Remove from list";
				rm.note = now ? "The next song plays" : fromPlaylist ? "The playlist itself is unchanged" : "";
				rm.act = [i, path](Player&) {
					SessionHost::get().removeFromList(i, path);
					return false;
				};
				m.rows.push_back(rm);
				if (hasSection)
				{
					MoreItem kp;
					kp.glyph = &G_SHUFFLE;
					kp.label = pl.settings.keepPlaying ? "Turn Autoplay off" : "Turn Autoplay on";
					kp.note = "Songs it added stay in the list";
					kp.act = [this](Player& p) { toggleKeepPlaying(p); return false; };
					m.rows.push_back(kp);
				}
				return m;
			}, "upnext-row");
			fprintf(stderr, "ui: up next, row %d's menu\n", i + 1);
		}

		void choose(int i, Player& p)
		{
			if (i < 0) return;	// (a heading)
			if (tab == 0)
			{
				int cur = -1;
				if (i < 0 || i >= (int)ui.upNextList(p, cur).size()) return;
				close();
				ui.playUpNext(i, p);
				return;
			}
			if (i < 0 || i >= (int)p.chapters.size()) return;
			// as the Chapters page chose: playing, the card says where, back to the player (this sheet closes)
			close();
			p.seekChapter(i);
			if (!ui.pageIsOut()) p.unpause();
			else ui.noteSeek(p, p.chapters[i].time);	// (a page playing it: the session's Seek, which the page follows)
			p.showChapterCard(i);
		}

		bool key(Button b, Player& p) override
		{
			if (!p.playing() || p.playingPhoto() || ui.page != Page::Main) { close(); return false; }
			int n = count(p);
			if (n <= 0) { close(); return false; }
			settle(p);
			const int was = cursor;
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			// a page at a time, as every list's LEFT / RIGHT (the mock-up's seven rows)
			case Button::Left: cursor = std::max(0, cursor - shown(p)); break;
			case Button::Right: cursor = std::min(n - 1, cursor + shown(p)); break;
			// the tabs: L back, R on, no wrap
			case Button::L: if (tab > 0) openTab(tab - 1, p); break;
			case Button::R: if (tab + 1 < tabCount(p)) openTab(tab + 1, p); break;
			case Button::A: choose(cursor, p); break;
			case Button::X: if (tab == 0) openRowMenu(cursor, p); break;
			// the chapter step over the sheet (Button hotkeys says whether they work at all)
			case Button::L2:
			case Button::R2:
				if (p.settings.buttonHotkeys && p.error.empty())
				{
					ui.chapterStep(b == Button::L2 ? -1 : 1, p);
					if (tab == 1) cursor = std::clamp(ui.shownChapter(p), 0, n - 1);
				}
				break;
			case Button::Y:
			case Button::B: close(); break;
			default: break;
			}
			settle(p);
			if (cursor != was && !done) rows.reveal(sections(p).row(cursor), rowsView(p), rowCount(p));
			return false;
		}

		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			// touch locked (the player's lock): the sheet is the buttons' until it is unlocked
			if (ui.touchIsLocked()) return false;
			if (!p.playing() || p.playingPhoto() || ui.page != Page::Main) { close(); return false; }
			int n = count(p);
			if (n <= 0) { close(); return false; }
			settle(p);
			lay::ListSheet s = layout(p);
			Head h = head(s);
			if (pt.kind == Pointer::Down)
			{
				press = P_NONE;
				pressAt = -1;
				// the veil: closed (the lift that follows presses nothing)
				if (!s.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
				auto tabs = tabRects(p, h);
				for (int i = 0; i < (int)tabs.size(); i++)
					if (tabs[i].hit(x, y)) { press = P_TAB; pressAt = i; return false; }
				if (h.shuffleHit.hit(x, y)) { press = P_SHUFFLE; pressAt = 0; return false; }
				auto rep = repeatRects(h);
				for (int i = 0; i < 3; i++)
					if (rep[i].hit(x, y)) { press = P_REPEAT; pressAt = i; return false; }
				lay::Sections sc = sections(p);
				if (!sc.before.empty() && rowsView(p).hit(x, y))
				{
					// the heading's chip (Keep playing on / off); the rest of a heading is no row
					Rect hb = rows.list.rowBox(sc.headRow(0));
					if (ui.kit->headingAction(hb, headingAction(p)).hit(x, y)) { rows.down(pt, x, y, rowsView(p)); press = P_HEADING; pressAt = 0; return false; }
				}
				int r = sc.item(rows.down(pt, x, y, rowsView(p)));
				if (r >= 0 && r < n) { press = P_ROW; pressAt = r; cursor = r; pressTicks = SDL_GetTicks(); }
				return false;
			}
			if (pt.kind == Pointer::Move)
			{
				rows.move(pt);	// (the rows follow the finger past the slop; a tap still chooses)
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int kind = press, at = pressAt;
			press = P_NONE;
			pressAt = -1;
			bool tap = rows.up();
			// a drag or a caught glide chooses nothing; let go off its row: nothing (the finger changed its mind)
			if (kind == P_ROW)
			{
				if (tap && sections(p).item(rows.list.rowAt(x, y)) == at)
				{
					// held, then let go: the row's menu (Remove from list); a tap plays from it
					if (tab == 0 && SDL_GetTicks() - pressTicks >= ROW_HOLD_MS) openRowMenu(at, p);
					else choose(at, p);
				}
			}
			else if (kind == P_HEADING)
			{
				lay::Sections sc = sections(p);
				if (tap && !sc.before.empty() && ui.kit->headingAction(rows.list.rowBox(sc.headRow(0)), headingAction(p)).hit(x, y))
					toggleKeepPlaying(p);
			}
			else if (kind == P_TAB)
			{
				auto tabs = tabRects(p, h);
				if (at >= 0 && at < (int)tabs.size() && tabs[at].hit(x, y)) openTab(at, p);
			}
			else if (kind == P_SHUFFLE && h.shuffleHit.hit(x, y)) setShuffle(p);
			else if (kind == P_REPEAT)
			{
				auto rep = repeatRects(h);
				if (at >= 0 && at < 3 && rep[at].hit(x, y)) setLoop(at, p);
			}
			return false;
		}

		void draw(const Player& p) override
		{
			// (what it was about has gone: the last file ended under it, the player was left): closed
			if (!p.playing() || p.playingPhoto() || ui.page != Page::Main) { close(); return; }
			int n = count(p);
			if (n <= 0) { close(); return; }
			settle(p);
			lay::ListSheet s = layout(p);
			Head h = head(s);
			Kit& k = *ui.kit;
			k.sheetBack(s.box, s.grab);
			k.tabs(h.tabs, labels(p), tab, hasChapters(p) ? "L/R" : "");

			// the header's controls: Shuffle's switch, Repeat's three chips (the states they set)
			const Settings& st = p.settings;
			int cy = s.head.y + s.head.h / 2;
			ui.drawGlyphMid(G_SHUFFLE, h.shuffleGlyph.x + 8, cy, DIM_ON);
			ui.drawTextMid("Shuffle", F_I12, DIM_ON, h.shuffleLabel.x, cy, h.shuffleLabel.w);
			k.toggle(h.toggleX, cy, st.shuffle);
			ui.drawGlyphMid(G_LOOP, h.repeatGlyph.x + 8, cy, DIM_ON);
			k.segment(h.repeatArea, {"Off", "All", "One"}, std::clamp(st.loop, 0, LOOPS - 1));

			// the rows: Up next (its own order under Shuffle) or every chapter with its start, each row's look made
			// from a copy of what it shows (the list may draw a row after this draw)
			auto paths = std::make_shared<std::vector<std::string>>();
			int cur = current(p);
			if (tab == 0) *paths = ui.upNextList(p, cur);
			auto chapters = std::make_shared<std::vector<Chapter>>(tab == 1 ? p.chapters : std::vector<Chapter>{});
			const bool drives = ui.pageDrives(p), paused = p.paused;
			const double nowDur = p.duration;
			const int focus = cursor, pressed = press == P_ROW && !rows.moved ? pressAt : -1, t = tab;
			const lay::Sections sc = sections(p);
			const bool headPressed = press == P_HEADING && !rows.moved, keepOn = p.settings.keepPlaying;
			const std::string action = headingAction(p);
			auto rowOf = [paths, chapters, cur, drives, paused, nowDur, focus, pressed, t, sc, headPressed, keepOn, action](int r) {
				Kit::Row row;
				int i = sc.item(r);
				if (i < 0)
				{
					// Keep playing's heading: what follows was picked for you; its chip turns it off (or on)
					row.heading = true;
					row.name = keepOn ? HEADING_ON : HEADING_OFF;
					row.key = action;
					row.pressed = headPressed;
					return row;
				}
				row.font = F_P16;
				row.focused = i == focus;
				row.pressed = i == pressed;
				bool now = i == cur;
				if (t == 0)
				{
					const std::string& path = (*paths)[i];
					row.name = entryTitle(path);
					double d = now && nowDur > 0 ? nowDur : -1;
					if (d <= 0 && !isNetPath(path)) cachedDuration(path, d);
					if (d > 0) row.note = formatTime(d);
				}
				else
				{
					const Chapter& c = (*chapters)[i];
					row.name = chapterLabel(c, i + 1);
					row.note = formatTime(c.time);
				}
				// the one playing: the play mark and its pill, as the old lists had it; the others their number
				if (now)
				{
					row.glyph = drives ? &G_QR : paused ? &G_PAUSE : &G_PLAY;
					row.pill = drives ? "Browser" : paused ? "Paused" : "Now";
					row.tone = drives || paused ? Kit::Pill::Fact : Kit::Pill::Live;
				}
				else row.lead = std::to_string(i + 1);
				return row;
			};
			Rect v = SheetRows::view(s, shown(p));
			int nr = sc.rows();
			rows.settleOn(std::to_string(tab), sc.row(cursor), v, nr, 3);
			rows.draw(ui, v, nr, rowOf, [sc](int r) { return sc.isHead(r); });
			ui.drawScrollBar(s.rowsY, shown(p) * lay::LIST_ROW, rows.first(), shown(p), nr);

			std::vector<Hint> hints{{"UP/DOWN", "Move"}, {"LEFT/RIGHT", "Page"}, {"A", "Play from here"}};
			if (tab == 0) hints.push_back({"X", "More"});
			if (hasChapters(p)) hints.push_back({"L/R", "Tab"});
			if (tab == 1) hints.push_back({"L2/R2", "Chapter"});
			hints.push_back({"Y/B", "Close"});
			k.hints(hints);
		}

		std::string signature(const Player& p) const override
		{
			// the tab, the cursor and the finger here; the lists, what plays and the two settings (Settings' own are
			// Ui::signature's, but the Session's queue is not)
			int n = count(p), cur = current(p);
			std::string sig = std::to_string(tab) + "," + std::to_string(cursor) + "," + std::to_string(rows.moved) + "," +
				std::to_string(press) + "," + std::to_string(pressAt) + "," + std::to_string(n) + "," + std::to_string(cur) + "," +
				std::to_string(SessionHost::get().version()) + "," + std::to_string(p.paused) + std::to_string(ui.pageDrives(p)) +
				std::to_string(p.settings.keepPlaying);
			if (tab == 0)
			{
				int c = -1;
				for (auto& path : ui.upNextList(p, c)) sig += "|" + path;
			}
			else
				for (auto& c : p.chapters) sig += "|" + c.title + ":" + std::to_string((int)c.time);
			return sig;
		}
	};

	void Ui::PlayerPage::openList(int tab, Player& p)
	{
		pressed = T_NONE;
		ui.pushOverlay(std::make_unique<ListSheet>(ui, tab, p));
	}

	// Adjust: a page of the Picture sheet, behind its last row (docs/redesign, "video-adjust"). Four two-way sliders
	// (Brightness, Contrast, Saturation, Gamma) change the film on the top screen as they move; A puts the focused one
	// back to 0, X all four, and a slide anywhere on a row moves it from where it is (a tap jumps). B (or its <) goes
	// back to Picture; SELECT closes the sheet too.
	struct Ui::AdjustSheet : Ui::Overlay
	{
		std::function<void()> closeParent;	// SELECT closes the whole sheet (Picture under this page)
		int cursor = 0, slideRow = -1;
		static constexpr const char* NAMES[4] = {"Brightness", "Contrast", "Saturation", "Gamma"};
		AdjustSheet(Ui& u, std::function<void()> parent) : Overlay(u), closeParent(std::move(parent)) {}
		const char* name() const override { return "adjust"; }
		static std::string valueText(const Player& p, int i)
		{
			int v = (int)std::lround(p.videoAdjust(i));
			return v > 0 ? "+" + std::to_string(v) : std::to_string(v);
		}
		static lay::ListSheet layout() { return lay::listSheet(4); }
		static Rect backRect(const lay::ListSheet& s) { return {14, s.head.y + 5, 44, 44}; }
		static Rect rowRect(const lay::ListSheet& s, int i) { return s.row(i); }
		static int rowAt(const lay::ListSheet& s, int x, int y)
		{
			for (int i = 0; i < 4; i++)
				if (rowRect(s, i).hit(x, y)) return i;
			return -1;
		}
		static void setFromX(const lay::ListSheet& s, int i, int x, Player& p)
		{
			Rect r = rowRect(s, i);
			double f = std::clamp((x - (r.x + 12)) / (double)std::max(1, r.w - 24), 0.0, 1.0);
			p.setVideoAdjust(i, std::lround((f * 200 - 100) / 5) * 5);	// (steps of 5)
		}
		bool key(Button b, Player& p) override
		{
			if (!p.playing() || p.playingPhoto() || p.playingAudio() || ui.page != Page::Main) { close(); return false; }
			switch (b)
			{
			case Button::Up: cursor = (cursor + 3) % 4; break;
			case Button::Down: cursor = (cursor + 1) % 4; break;
			case Button::Left: case Button::Right:
				p.setVideoAdjust(cursor, std::clamp(p.videoAdjust(cursor) + (b == Button::Right ? 5 : -5), -100.0, 100.0));
				break;
			case Button::A: p.setVideoAdjust(cursor, 0); break;
			case Button::X:
				for (int i = 0; i < 4; i++) p.setVideoAdjust(i, 0);
				break;
			case Button::B: close(); break;
			case Button::Select: close(); if (closeParent) closeParent(); break;
			default: break;
			}
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			if (ui.touchIsLocked()) return false;
			if (!p.playing() || p.playingPhoto() || p.playingAudio() || ui.page != Page::Main) { close(); return false; }
			lay::ListSheet s = layout();
			if (pt.kind == Pointer::Down)
			{
				if (backRect(s).hit(x, y)) { close(); return false; }
				if (!s.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
				int at = rowAt(s, x, y);
				if (at >= 0) { cursor = slideRow = at; setFromX(s, at, x, p); }
				return false;
			}
			if (pt.kind == Pointer::Move)
			{
				if (slideRow >= 0) setFromX(s, slideRow, x, p);
				return false;
			}
			if (pt.kind == Pointer::Up) slideRow = -1;
			return false;
		}
		void draw(const Player& p) override
		{
			if (!p.playing() || p.playingPhoto() || p.playingAudio() || ui.page != Page::Main) { close(); return; }
			Kit& k = *ui.kit;
			lay::ListSheet s = layout();
			k.sheetBack(s.box, s.grab);
			int cy = s.head.y + s.head.h / 2;
			ui.drawGlyphMid(G_LEFT, backRect(s).x + 22, cy, ACCENT);
			ui.drawTextMid("Adjust", F_P20, ACCENT, backRect(s).x + 52, cy, 260);
			ui.drawTextMid("Picture", F_I12, DIM, s.head.x + s.head.w, cy, 0, 1);
			for (int i = 0; i < 4; i++)
			{
				Rect r = rowRect(s, i);
				Kit::Row row;
				row.font = F_P16;
				row.name = NAMES[i];
				row.note = valueText(p, i);
				row.focused = i == cursor;
				row.off = false;
				k.row(r, row);
				// the two-way track: the middle is 0, the fill runs from it to the value
				Rect track{r.x + 12, r.y + r.h - 9, r.w - 24, 6};
				ui.fillRound(track, TRACK, 3);
				int mid = track.x + track.w / 2;
				double share = std::clamp(p.videoAdjust(i) / 100.0, -1.0, 1.0);
				int ex = mid + (int)std::lround(share * track.w / 2);
				if (ex != mid)
					ui.fillRound({std::min(mid, ex), track.y, std::abs(ex - mid), track.h}, i == cursor ? ACCENT : PRESSED, 3);
				ui.fill({mid, track.y - 3, 1, track.h + 6}, DIM_ON);
			}
			k.hints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Adjust"}, {"A", "To 0"}, {"X", "Reset all"}, {"B", "Picture"}});
		}
		std::string signature(const Player& p) const override
		{
			std::string sig = std::to_string(cursor) + "," + std::to_string(slideRow) + "," + std::to_string(ui.touchIsLocked());
			for (int i = 0; i < 4; i++) sig += "|" + valueText(p, i);
			return sig;
		}
	};

	// One of the video's own sheets (docs/redesign, "video-view", "video-picture", "video-speed"): Subtitles, Audio,
	// Picture or Speed, opened straight from its chip, titled with the chip's name and with no tab strip. The rows are
	// Ui::rows' rows, the ones the old Tracks page listed, and Ui::choose does each one's action (a track by its id, a
	// picture size); Speed is format.h's five speeds, and A sets it at once. Picture's last row opens Adjust (a page of
	// this sheet). It stays open on a choice (several are made here), unless the row leaves for Settings (the subtitle
	// style) or for Adjust. UP / DOWN move, LEFT / RIGHT page, A chooses, SELECT / B close; a tap on a row acts on the
	// lift, outside closes. L and R do nothing: there are no tabs.
	struct Ui::ViewSheet : Ui::Overlay
	{
		enum Tab { TAB_SUBS, TAB_AUDIO, TAB_PICTURE, TAB_SPEED, TABS };
		enum Press { P_NONE, P_ROW };
		static constexpr int64_t ADJUST_ROW = -3;	// Picture's last row: the Adjust page
		int tab = 0, cursor = 0, press = P_NONE, pressAt = -1;
		// the rows: a finger's drag scrolls them, a tap (no drag) still chooses
		SheetRows rows{ui};
		SmoothList* smoothList() override { return &rows.list; }

		ViewSheet(Ui& u, int at, const Player& p) : Overlay(u)
		{
			tab = std::clamp(at, 0, TABS - 1);
			// opened on what is in use: the chosen track (Off among the subtitles too), the size, the speed
			int n = count(p), cur = current(p);
			cursor = std::clamp(cur, 0, std::max(0, n - 1));
		}
		const char* name() const override { return "view"; }

		static std::vector<std::string> labels() { return {"Subtitles", "Audio", "Picture", "Speed"}; }
		static const Glyph* glyphFor(int t)
		{
			static const Glyph* G[TABS] = {&G_CC, &G_SPEAKER, &G_PICTURE, &G_SPEED};
			return G[std::clamp(t, 0, TABS - 1)];
		}
		static int tracksTabFor(int t) { return t == TAB_AUDIO ? TRACKS_AUDIO : t == TAB_PICTURE ? TRACKS_PICTURE : TRACKS_SUBS; }
		// Ui::rows reads the tab it is on: point it at this one while asking, and put it back (asking is not choosing)
		std::vector<Ui::Row> rowsFor(int t, const Player& p) const
		{
			if (t == TAB_SPEED)
			{
				std::vector<Row> out;
				for (double s : speedChoices())
					out.push_back({(int64_t)std::lround(s * 100), speedLabel(s), std::fabs(p.speed - s) < 1e-6,
						std::fabs(s - 1) < 1e-6 ? "Normal" : ""});
				return out;
			}
			int keep = ui.tracksTab;
			ui.tracksTab = tracksTabFor(t);
			auto out = ui.rows(p);
			ui.tracksTab = keep;
			// the picture's last row: the Adjust page (Brightness, Contrast, Saturation, Gamma)
			if (t == TAB_PICTURE) out.push_back({ADJUST_ROW, "Adjust\xE2\x80\xA6", false, "Brightness, Contrast, Saturation, Gamma"});
			return out;
		}
		int count(const Player& p) const { return (int)rowsFor(tab, p).size(); }
		int current(const Player& p) const
		{
			auto r = rowsFor(tab, p);
			for (size_t i = 0; i < r.size(); i++)
				if (r[i].selected) return (int)i;
			return 0;
		}
		int shown(const Player& p) const { return std::min(std::max(1, count(p)), lay::LIST_ROWS); }
		lay::ListSheet layout(const Player& p) const { return lay::listSheet(shown(p)); }
		Rect rowsView(const Player& p) const { return SheetRows::view(layout(p), shown(p)); }
		void settle(const Player& p)
		{
			int n = count(p);
			cursor = n <= 0 ? 0 : std::clamp(cursor, 0, n - 1);
		}
		// what the header says under its title (on the right): how many there are to choose
		std::string headNote(const Player& p) const
		{
			if (tab == TAB_SPEED) return "";
			if (tab == TAB_PICTURE) return "3 sizes";
			int n = (int)p.tracksOf(tab == TAB_AUDIO ? TrackType::Audio : TrackType::Sub).size();
			return n == 0 ? "None in this file" : n == 1 ? "1 track" : std::to_string(n) + " tracks";
		}

		void choose(int i, Player& p)
		{
			auto r = rowsFor(tab, p);
			if (i < 0 || i >= (int)r.size()) return;
			cursor = i;
			if (tab == TAB_SPEED)
			{
				// (the radio moves at once; p.speed answers from mpv a moment later, and the OSD says it meanwhile)
				double s = speedChoices()[i];
				p.setSpeed(s);
				p.osd("Speed " + speedLabel(s));
				return;
			}
			if (tab == TAB_PICTURE && r[i].id == ADJUST_ROW)
			{
				// a page of this sheet: it stays under Adjust, and B (or its <) comes back here
				ui.pushOverlay(std::make_unique<AdjustSheet>(ui, [this] { close(); }));
				return;
			}
			// the style row leaves for Settings (its page draws there): the sheet closes first. A track or a size
			// changes under it, as the old page did, and it stays for the next choice
			int keep = ui.tracksTab;
			ui.tracksTab = tracksTabFor(tab);
			bool style = r[i].id == STYLE_ROW;
			if (style) close();
			ui.choose(i, p);
			ui.tracksTab = keep;
		}

		bool key(Button b, Player& p) override
		{
			// (nothing plays any more, it became a song, or the player was left under the sheet: closed)
			if (!p.playing() || p.playingPhoto() || p.playingAudio() || ui.page != Page::Main) { close(); return false; }
			int n = count(p);
			if (n <= 0) { close(); return false; }
			settle(p);
			const int was = cursor;
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			// a page at a time when there are more rows than show (the List sheet's LEFT / RIGHT)
			case Button::Left: cursor = std::max(0, cursor - shown(p)); break;
			case Button::Right: cursor = std::min(n - 1, cursor + shown(p)); break;
			case Button::A: choose(cursor, p); break;
			case Button::Select:
			case Button::B:
			case Button::Y: close(); break;	// (Y opens the List elsewhere: here the sheet closes first)
			default: break;
			}
			settle(p);
			if (cursor != was && !done) rows.reveal(cursor, rowsView(p), count(p));
			return false;
		}

		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			// touch locked (the player's lock): the sheet is the buttons' until it is unlocked
			if (ui.touchIsLocked()) return false;
			if (!p.playing() || p.playingPhoto() || p.playingAudio() || ui.page != Page::Main) { close(); return false; }
			int n = count(p);
			if (n <= 0) { close(); return false; }
			settle(p);
			lay::ListSheet s = layout(p);
			if (pt.kind == Pointer::Down)
			{
				press = P_NONE;
				pressAt = -1;
				// the veil: closed (the lift that follows presses nothing)
				if (!s.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
				int r = rows.down(pt, x, y, rowsView(p));
				if (r >= 0 && r < n) { press = P_ROW; pressAt = r; cursor = r; }
				return false;
			}
			if (pt.kind == Pointer::Move)
			{
				rows.move(pt);	// (the rows follow the finger past the slop; a tap still chooses)
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int kind = press, at = pressAt;
			press = P_NONE;
			pressAt = -1;
			bool tap = rows.up();
			// a drag or a caught glide chooses nothing; let go off its row: nothing (the finger changed its mind)
			if (kind == P_ROW && tap && rows.list.rowAt(x, y) == at) choose(at, p);
			return false;
		}

		void draw(const Player& p) override
		{
			// (what it was about has gone: closed)
			if (!p.playing() || p.playingPhoto() || p.playingAudio() || ui.page != Page::Main) { close(); return; }
			int n = count(p);
			if (n <= 0) { close(); return; }
			settle(p);
			lay::ListSheet s = layout(p);
			Kit& k = *ui.kit;
			k.sheet(s, {glyphFor(tab), labels()[tab], headNote(p), false});

			// a choice a row: its radio (the track in use, the size, the speed) and its note (made from a copy: the list
			// may draw a row after this draw)
			auto list = std::make_shared<std::vector<Row>>(rowsFor(tab, p));
			const int focus = cursor, pressed = press == P_ROW && !rows.moved ? pressAt : -1;
			auto rowOf = [list, focus, pressed](int i) {
				const Row& r = (*list)[i];
				Kit::Row row;
				row.font = F_P16;
				row.name = r.label;
				row.note = r.note;
				row.focused = i == focus;
				row.pressed = i == pressed;
				// (a message row — "No audio in this file" — is no choice: no radio, and dim)
				row.radio = r.id >= 0;
				row.radioOn = row.radio && r.selected;
				row.trail = r.id == STYLE_ROW || r.id == ADJUST_ROW ? Kit::Trail::Chevron : Kit::Trail::None;
				row.off = r.id < 0 && r.id != STYLE_ROW && r.id != ADJUST_ROW;
				return row;
			};
			Rect v = SheetRows::view(s, shown(p));
			rows.settleOn(std::to_string(tab), cursor, v, n, 3);
			rows.draw(ui, v, n, rowOf);
			ui.drawScrollBar(s.rowsY, shown(p) * lay::LIST_ROW, rows.first(), shown(p), n);

			std::vector<Hint> hints{{"UP/DOWN", "Move"}};
			if (n > shown(p)) hints.push_back({"LEFT/RIGHT", "Page"});
			hints.push_back({"A", tab == TAB_SPEED ? "Set" : "Choose"});
			hints.push_back({"SELECT/B", "Close"});
			k.hints(hints);
		}

		std::string signature(const Player& p) const override
		{
			// the tab, the cursor and the finger here; the rows (their names and notes arrive with the file's
			// tracks), what is in use and the lock. Settings' own, the speed and the chosen ids are Ui::signature's
			std::string sig = std::to_string(tab) + "," + std::to_string(cursor) + "," + std::to_string(rows.moved) + "," +
				std::to_string(press) + "," + std::to_string(pressAt) + "," + std::to_string(ui.touchIsLocked()) + "," +
				std::to_string(p.playingAudio());
			for (auto& r : rowsFor(tab, p))
				sig += "|" + r.label + ":" + r.note + (r.selected ? "s" : "") + std::to_string(r.id);
			return sig;
		}
	};

	// One of a song's own sheets (docs/redesign, "music-view", "music-viz-pick", "music-eq"): Display, Equalizer,
	// Effects or Speed, opened straight from its chip, titled with the chip's name and with no tab strip. Display is one
	// page for both choices (lay::displayPage): the four Show modes in a row of choice tiles, the fifteen visualizer
	// styles in a 5 x 4 grid under them; the cursor moves over both and changes nothing, A sets what is under it (the
	// top screen follows at once). The equalizer is the Sound page's bands in the sheet's body, its preset options as
	// rows. B closes (on Display SELECT steps the Show mode as the popup did; elsewhere SELECT closes too). L and R do
	// nothing: there are no tabs.
	struct Ui::SongViewSheet : Ui::Overlay
	{
		enum Tab { TAB_SHOW, TAB_EQ, TAB_FX, TAB_SPEED, TABS };
		enum Press { P_NONE, P_ROW, P_PREV, P_NEXT, P_NAME, P_ON, P_SAVE, P_TILE };
		enum EqMode { EQ_MAIN, EQ_OPTIONS, EQ_LIST, EQ_DELETE };
		int tab = 0, cursor = 0, press = P_NONE, pressAt = -1;
		int eqMode = EQ_MAIN;	// (its lists are the sheet's rows: the cursor, and the rows' smooth list)
		SheetRows rows{ui};
		SmoothList* smoothList() override { return tab != TAB_SHOW && isList() ? &rows.list : nullptr; }
		std::string rowsKind() const { return std::to_string(tab) + "," + std::to_string(eqMode); }

		bool fromSettings = false;	// opened over Settings (Equalizer, Sound effect): nothing need play
		bool gone(const Player& p) const
		{
			if (fromSettings) return ui.page != Page::Settings;
			return !p.playing() || p.playingPhoto() || !p.playingAudio() || ui.page != Page::Main;
		}

		SongViewSheet(Ui& u, int at, const Player& p, bool fromSettings = false) : Overlay(u), fromSettings(fromSettings)
		{
			tab = std::clamp(at, 0, TABS - 1);
			cursor = currentRow(p);
		}
		const char* name() const override { return "songview"; }

		static std::vector<std::string> labels() { return {"Display", "Equalizer", "Effects", "Speed"}; }
		static const Glyph* glyphFor(int t, const Player& p)
		{
			switch (t)
			{
			case TAB_EQ: return &G_EQ;
			case TAB_FX: return &G_SPEAKER;
			case TAB_SPEED: return &G_SPEED;
			default: return showGlyph(std::clamp(p.settings.musicShow, 0, SHOWS - 1), p.settings.visualizer);
			}
		}
		static bool showHidesVisualizer(const Player& p)
		{
			int show = std::clamp(p.settings.musicShow, 0, SHOWS - 1);
			return show == SHOW_ART || show == SHOW_NONE;
		}
		// Display's tiles: the four modes, then the styles (VIS_SHOWN's order); the cursor is an index over both,
		// and one past them all is the sheet's foot row (the Match album art switch)
		static constexpr int DISPLAY_N = SHOWS + VIS_SHOWN_COUNT;
		static constexpr int DISPLAY_FOOT = DISPLAY_N;	// the foot row's index in the cursor's range
		static lay::DisplayPage displayLayout() { return lay::displayPage(VIS_SHOWN_COUNT); }
		static int displayInUse(const Player& p, bool style)
		{
			return style ? SHOWS + visualizerSlot(p.settings.visualizer) : std::clamp((int)p.settings.musicShow, 0, SHOWS - 1);
		}
		// A on a tile (or a tap): the mode or the style set, and the top screen follows; a style is kept when the
		// mode shows none ("not shown now"); the foot row is the switch, LEFT / RIGHT stepping it as A does
		static void setTile(int i, Player& p)
		{
			if (i == DISPLAY_FOOT)
			{
				Settings s = p.settings;
				s.matchAlbumArt = !s.matchAlbumArt;
				p.setSettings(s);
				return;
			}
			if (i < 0 || i >= DISPLAY_N) return;
			if (i < SHOWS)
			{
				if (p.settings.musicShow == i) return;
				Settings s = p.settings;
				s.musicShow = i;
				p.setSettings(s);
				return;
			}
			applyStyle(i - SHOWS, p);
		}
		static void applyStyle(int slot, Player& p)
		{
			int v = VIS_SHOWN[std::clamp(slot, 0, VIS_SHOWN_COUNT - 1)];
			if (p.settings.visualizer == v) return;
			Settings s = p.settings;
			s.visualizer = v;
			p.setSettings(s);
		}
		// the list of a tab: Display's tiles, Effects' choices, Speed's five
		int count(const Player& p) const
		{
			if (tab == TAB_SHOW) return DISPLAY_N;
			if (tab == TAB_FX) return SOUNDFX_COUNT;
			if (tab == TAB_SPEED) return (int)speedChoices().size();
			return 0;
		}
		int shown(const Player& p) const { return std::min(std::max(1, isList() ? listCount(p) : count(p)), lay::LIST_ROWS); }
		lay::ListSheet layout(const Player& p) const
		{
			// the Equalizer's main tab has no rows: its bands take the full height a sheet may have (about 470, the
			// bands about 300 tall), not the one-row box an empty list would leave (docs/redesign "music-eq")
			if (tab == TAB_EQ && eqMode == EQ_MAIN)
				return lay::listSheetBody(lay::SHEET_MAX - 11 - lay::LIST_HEAD - 8 - lay::HINTS_H);
			return lay::listSheet(shown(p));
		}
		Rect rowsView(const Player& p) const { return SheetRows::view(layout(p), shown(p)); }
		bool isList() const { return (tab != TAB_EQ && tab != TAB_SHOW) || eqMode == EQ_OPTIONS || eqMode == EQ_LIST || eqMode == EQ_DELETE; }
		void settle(const Player& p)
		{
			if (tab == TAB_SHOW) { cursor = std::clamp(cursor, 0, DISPLAY_FOOT); return; }
			int n = isList() ? listCount(p) : count(p);
			cursor = n <= 0 ? 0 : std::clamp(cursor, 0, n - 1);
		}

		// ---- Display and the two lists into rows ----
		struct Line { std::string name, note; bool on = false, off = false; };
		int listCount(const Player& p) const
		{
			if (tab == TAB_EQ)
			{
				if (eqMode == EQ_OPTIONS) return (int)ui.sound->eqOptions(p).size();
				if (eqMode == EQ_LIST) return (int)ui.sound->eqEntries(p).size();
				if (eqMode == EQ_DELETE) return 2;
			}
			return count(p);
		}
		Line lineAt(int i, const Player& p) const
		{
			Line l;
			if (tab == TAB_FX)
			{
				l.name = soundFxName((SoundFx)i);
				l.note = FX_ABOUT[std::clamp(i, 0, SOUNDFX_COUNT - 1)];
				l.on = (int)p.settings.soundFx == i;
			}
			else if (tab == TAB_SPEED)
			{
				double s = speedChoices()[i];
				l.name = speedLabel(s);
				l.note = std::fabs(s - 1) < 1e-6 ? "Normal" : "";
				l.on = std::fabs(p.speed - s) < 1e-6;
			}
			else if (eqMode == EQ_OPTIONS)
			{
				auto o = ui.sound->eqOptions(p);
				int what = i >= 0 && i < (int)o.size() ? o[i] : Sound::EQO_CANCEL;
				l.name = what == Sound::EQO_LIST ? "All presets..." : what == Sound::EQO_SAVE_TO ? "Save to " + quoted(p.settings.eqOrigin)
					: what == Sound::EQO_SAVE_AS ? "Save as new preset..." : what == Sound::EQO_RENAME ? "Rename..." : what == Sound::EQO_DELETE ? "Delete..."
					: what == Sound::EQO_RESET ? "Reset to " + quoted(p.settings.eqOrigin) : "Cancel";
				l.off = what == Sound::EQO_CANCEL;
			}
			else if (eqMode == EQ_LIST)
			{
				auto e = ui.sound->eqEntries(p);
				if (i >= 0 && i < (int)e.size())
				{
					l.name = e[i].name;
					// a section's name is no choice (dim, no radio), as the Sound page's list had it
					l.off = e[i].kind == 0;
					l.on = e[i].kind == 2 ? ui.sound->eqCustom(p) : e[i].kind == 1 && !ui.sound->eqCustom(p) && e[i].name == p.settings.eqPreset;
				}
			}
			else if (eqMode == EQ_DELETE)
			{
				l.name = i == 0 ? "Delete" : "Cancel";
				l.off = i != 0;
			}
			return l;
		}
		int currentRow(const Player& p) const
		{
			if (tab == TAB_SHOW) return displayInUse(p, false);
			if (tab == TAB_FX) return (int)p.settings.soundFx;
			if (tab == TAB_SPEED)
				for (size_t i = 0; i < speedChoices().size(); i++)
					if (std::fabs(p.speed - speedChoices()[i]) < 1e-6) return (int)i;
			if (eqMode == EQ_OPTIONS) return 0;
			if (eqMode == EQ_LIST)
			{
				auto e = ui.sound->eqEntries(p);
				for (int i = 0; i < (int)e.size(); i++)
					if ((e[i].kind == 2 && ui.sound->eqCustom(p)) || (e[i].kind == 1 && !ui.sound->eqCustom(p) && e[i].name == p.settings.eqPreset)) return i;
			}
			return 0;
		}

		// ---- what choosing one does (the popup's, the Sound page's and the Speed tab's own actions) ----
		void choose(int i, Player& p)
		{
			cursor = i;
			if (tab == TAB_SHOW) { setTile(i, p); return; }
			if (tab == TAB_FX) { ui.sound->fxChoose(i, p); return; }
			if (tab == TAB_SPEED)
			{
				double s = speedChoices()[std::clamp(i, 0, (int)speedChoices().size() - 1)];
				p.setSpeed(s);
				p.osd("Speed " + speedLabel(s));
				return;
			}
			// the equalizer's rows: its options, its presets, Delete's question
			if (eqMode == EQ_OPTIONS)
			{
				auto o = ui.sound->eqOptions(p);
				int what = i >= 0 && i < (int)o.size() ? o[i] : Sound::EQO_CANCEL;
				eqMode = EQ_MAIN;
				switch (what)
				{
				case Sound::EQO_LIST:
					eqMode = EQ_LIST;
					{
						auto e = ui.sound->eqEntries(p);
						cursor = std::max(0, currentRow(p));
						if (cursor < (int)e.size() && e[cursor].kind == 0) cursor++;	// (never on a section's name)
					}
					break;
				case Sound::EQO_SAVE_TO: ui.sound->eqSave(p); break;
				case Sound::EQO_SAVE_AS: ui.sound->eqNameOpen(false, p); break;
				case Sound::EQO_RENAME: ui.sound->eqNameOpen(true, p); break;
				case Sound::EQO_DELETE: eqMode = EQ_DELETE; cursor = 0; break;
				case Sound::EQO_RESET: ui.sound->eqReset(p); break;
				default: break;
				}
				return;
			}
			if (eqMode == EQ_LIST)
			{
				auto e = ui.sound->eqEntries(p);
				if (i >= 0 && i < (int)e.size() && e[i].kind)
				{
					eqMode = EQ_MAIN;
					ui.sound->eqPick(e[i].name, p);
				}
				return;
			}
			if (eqMode == EQ_DELETE)
			{
				eqMode = EQ_MAIN;
				if (i == 0)
				{
					// as the Sound page deletes: the one playing, Custom now (nothing heard changes)
					std::string n = p.settings.eqPreset;
					if (p.deleteEqPreset(n))
					{
						if (ui.sound->stashOrigin == n) ui.sound->stashOrigin.clear();
						p.showEqCard();
						p.osd("Deleted " + quoted(n));
					}
				}
			}
		}

		// ---- the equalizer's body: the preset row and the ten bands ----
		struct EqLayout
		{
			Rect body, save, toggle, prevCap, nextCap, name, prevArrow, nextArrow;
			int cy = 0, toggleX = 0, onCapX = 0, onLabelX = 0;
			Rect band[EQ_BANDS], track[EQ_BANDS];
			int gainY = 0, freqY = 0, trackY = 0, trackH = 0;
		};
		EqLayout eqLayout(const lay::ListSheet& s) const
		{
			EqLayout e;
			e.body = {16, s.head.y + s.head.h + 4, 608, s.hints.y - 8 - (s.head.y + s.head.h + 4)};
			e.cy = e.body.y + 22;
			e.save = {e.body.x + e.body.w - 140, e.body.y, 140, 44};
			e.toggleX = e.save.x - 16 - 40;
			e.onCapX = e.toggleX - 8 - ui.kit->capsW("Y");
			e.onLabelX = e.onCapX - 8 - 24;
			e.prevCap = {e.body.x + 52, e.cy - 10, 19, 19};
			e.name = {e.prevCap.x + 19 + 8, e.cy - 14, 160, 28};
			e.nextCap = {e.name.x + e.name.w + 6, e.cy - 10, 19, 19};
			e.prevArrow = {e.name.x, e.cy - 14, 18, 28};
			e.nextArrow = {e.name.x + e.name.w - 18, e.cy - 14, 18, 28};
			int top = e.body.y + 52;
			e.gainY = top + 8;
			e.freqY = e.body.y + e.body.h - 8;
			e.trackY = top + 18;
			e.trackH = std::max(20, e.freqY - 14 - e.trackY);
			Rect area{e.body.x, top, e.body.w, e.body.h - 52};
			for (int i = 0; i < EQ_BANDS; i++)
			{
				Rect c = lay::cell(area, EQ_BANDS, 0, i);
				int cx = c.x + c.w / 2;
				e.band[i] = {cx - 20, top, 40, e.body.h - 52};
				e.track[i] = {cx - 2, e.trackY, 4, e.trackH};
			}
			return e;
		}
		int bandGainY(const EqLayout& e, double g) const
		{
			return e.trackY + (int)std::lround((12 - std::clamp(g, -12.0, 12.0)) / 24.0 * e.trackH);
		}
		void bandGrab(int i, int x, int y, Player& p)
		{
			auto l = eqLayout(layout(p));
			ui.sound->eqBand = i;
			ui.slideGrab(SL_EQ, i, {l.track[i], {-12, 12, 1}, true, 0, p.settings.eqGains[i]}, x, y, p,
				[this, i](double g, bool last, Player& p) {
					Uint32 now = SDL_GetTicks();
					if (!last) { ui.sound->eqSetBand(i, g, p); return; }
					// two taps on a band, no drag: back to 0 dB (the Sound page's)
					if (!ui.slide.moved && ui.sound->eqTapAt && now - ui.sound->eqTapAt < 350 && ui.sound->eqTapBand == i)
					{
						ui.sound->eqSetBand(i, 0, p);
						ui.sound->eqTapAt = 0;
						return;
					}
					ui.sound->eqSetBand(i, g, p);
					ui.sound->eqTapAt = ui.slide.moved ? 0 : now;
					ui.sound->eqTapBand = i;
				}, this);	// (the sheet is the overlay: once it isn't on top, the finger lets go)
		}

		// ---- keys ----
		bool eqMainKey(Button b, Player& p)
		{
			switch (b)
			{
			case Button::Left: ui.sound->eqBand = std::max(0, ui.sound->eqBand - 1); break;
			case Button::Right: ui.sound->eqBand = std::min(EQ_BANDS - 1, ui.sound->eqBand + 1); break;
			case Button::Up: case Button::Down:
				ui.sound->eqSetBand(ui.sound->eqBand, p.settings.eqGains[ui.sound->eqBand] + (b == Button::Up ? 1 : -1), p);
				break;
			// presets on the shoulders, as the design has it (they were Y / X on the old page)
			case Button::L2: ui.sound->eqStep(-1, p); break;
			case Button::R2: ui.sound->eqStep(1, p); break;
			case Button::Y: ui.sound->eqToggle(p); break;
			case Button::A: ui.sound->eqSave(p); break;
			// the preset options (they were START on the old page)
			case Button::X: eqMode = EQ_OPTIONS; cursor = 0; break;
			default: return false;
			}
			return true;
		}
		bool listKey(Button b, Player& p)
		{
			int n = listCount(p);
			if (n <= 0) return false;
			auto move = [&](int dir) {
				int c = cursor;
				for (int k = 0; k < n; k++)
				{
					c = (c + dir + n) % n;
					// a section's name is stepped over (Display has none)
					if (eqMode != EQ_LIST || lineAt(c, p).off == false) { cursor = c; break; }
				}
			};
			switch (b)
			{
			case Button::Up: move(-1); break;
			case Button::Down: move(1); break;
			case Button::Left: cursor = std::max(0, cursor - shown(p)); break;
			case Button::Right: cursor = std::min(n - 1, cursor + shown(p)); break;
			case Button::A: choose(cursor, p); break;
			default: return false;
			}
			return true;
		}
		// Display: the cursor moves over the modes and the styles (lay::displayMove) and changes nothing; the foot
		// row is reached from the grid's last row and back (its middle), A sets what is under it
		bool displayKey(Button b, Player& p)
		{
			lay::DisplayPage d = displayLayout();
			switch (b)
			{
			case Button::Up:
				cursor = cursor >= DISPLAY_N ? DISPLAY_N - lay::DISPLAY_COLS + lay::DISPLAY_COLS / 2 : lay::displayMove(d, cursor, lay::Dir::Up);
				break;
			case Button::Down:
				if (cursor >= DISPLAY_N - lay::DISPLAY_COLS && cursor < DISPLAY_N) cursor = DISPLAY_FOOT;
				else if (cursor < DISPLAY_N) cursor = lay::displayMove(d, cursor, lay::Dir::Down);
				break;
			case Button::Left: case Button::Right:
				if (cursor == DISPLAY_FOOT) setTile(DISPLAY_FOOT, p);
				else cursor = lay::displayMove(d, cursor, b == Button::Left ? lay::Dir::Left : lay::Dir::Right);
				break;
			case Button::A: setTile(cursor, p); break;
			default: return false;
			}
			return true;
		}
		bool key(Button b, Player& p) override
		{
			const int was = cursor;
			const std::string kind = rowsKind();
			bool r = keyImpl(b, p);
			// the cursor moved in the same rows: kept in view by a glide (other rows are put on it as they draw)
			if (!done && tab != TAB_SHOW && isList() && cursor != was && rowsKind() == kind)
				rows.reveal(cursor, rowsView(p), listCount(p));
			if (done && tab == TAB_EQ) p.commitEq();	// (the curve as it stands: what mpv keeps, and settings.conf)
			return r;
		}
		bool keyImpl(Button b, Player& p)
		{
			// (the song has gone, it became a video, or the player was left under the sheet: closed)
			if (gone(p)) { close(); return false; }
			settle(p);
			// its own dialogs: B goes back a step, not out of the sheet; SELECT closes the sheet
			if (tab == TAB_EQ && eqMode != EQ_MAIN)
			{
				if (b == Button::Select) { close(); return false; }
				if (b == Button::B || b == Button::Start) { eqMode = EQ_MAIN; return false; }
				if (listKey(b, p)) return false;
				settle(p);
				return false;
			}
			// Display: SELECT steps the Show mode (as the popup did), B (and Y: List opens there) close
			if (tab == TAB_SHOW)
			{
				switch (b)
				{
				case Button::B: case Button::Y: close(); break;
				case Button::Select:
					{
						Settings s = p.settings;
						s.musicShow = (s.musicShow + 1) % SHOWS;
						p.setSettings(s);
					}
					break;
				default: displayKey(b, p); break;
				}
				settle(p);
				return false;
			}
			switch (b)
			{
			case Button::B: close(); break;
			// (Display steps the mode on SELECT; every other sheet closes)
			case Button::Select: close(); break;
			// (the equalizer turns on and off with Y, as the Sound page had SELECT; elsewhere Y closes: List opens there)
			case Button::Y:
				if (!(tab == TAB_EQ && eqMode == EQ_MAIN && eqMainKey(b, p))) close();
				break;
			default:
				if (tab == TAB_EQ && eqMainKey(b, p)) break;
				if (!listKey(b, p)) break;
				break;
			}
			settle(p);
			return false;
		}

		// ---- touch ----
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			bool r = pointerImpl(pt, x, y, p);
			if (done && tab == TAB_EQ) p.commitEq();
			return r;
		}
		bool pointerImpl(const Pointer& pt, int x, int y, Player& p)
		{
			if (ui.touchIsLocked()) return false;
			if (gone(p)) { close(); return false; }
			// Display: a tile (a tap sets it, as A does), the foot row as one of them
			if (tab == TAB_SHOW)
			{
				lay::DisplayPage d = displayLayout();
				if (pt.kind == Pointer::Down)
				{
					press = P_NONE;
					pressAt = -1;
					if (!d.sheet.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
					int at = lay::displayAt(d, x, y);
					if (at < 0 && d.foot.w > 0 && d.foot.hit(x, y)) at = DISPLAY_FOOT;
					if (at >= 0) { press = P_TILE; pressAt = at; cursor = at; }
					return false;
				}
				if (pt.kind != Pointer::Up) return false;
				int kind = press, at = pressAt;
				press = P_NONE;
				pressAt = -1;
				bool onFoot = at == DISPLAY_FOOT;
				if (kind == P_TILE && at >= 0 && (onFoot ? d.foot.hit(x, y) : lay::displayAt(d, x, y) == at)) setTile(at, p);
				return false;
			}
			lay::ListSheet s = layout(p);
			if (pt.kind == Pointer::Down)
			{
				press = P_NONE;
				pressAt = -1;
				// the veil: closed (the lift that follows presses nothing)
				if (!s.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
				if (tab == TAB_EQ && eqMode == EQ_MAIN)
				{
					EqLayout e = eqLayout(s);
					if (e.save.hit(x, y)) press = P_SAVE;
					else if (switchHit(e.toggleX, e.cy, x, y)) press = P_ON;
					else if (e.prevCap.hit(x, y) || e.prevArrow.hit(x, y) || Rect{e.body.x, e.cy - 20, 48, 40}.hit(x, y)) press = P_PREV;
					else if (e.nextCap.hit(x, y) || e.nextArrow.hit(x, y)) press = P_NEXT;
					else if (e.name.hit(x, y)) press = P_NAME;
					if (press != P_NONE) return false;
					for (int i = 0; i < EQ_BANDS; i++)
						if (e.band[i].hit(x, y)) { bandGrab(i, x, y, p); return false; }
					return false;
				}
				int n = isList() ? listCount(p) : count(p);
				int r = rows.down(pt, x, y, rowsView(p));
				// (the section names of the preset list are no choice)
				if (r >= 0 && r < n && !(eqMode == EQ_LIST && lineAt(r, p).off)) { press = P_ROW; pressAt = r; cursor = r; }
				return false;
			}
			if (pt.kind == Pointer::Move)
			{
				rows.move(pt);	// (the rows follow the finger past the slop; a tap still chooses)
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int kind = press, at = pressAt;
			press = P_NONE;
			pressAt = -1;
			bool tap = rows.up();
			if (kind == P_ROW && !tap) { settle(p); return false; }	// (a drag or a caught glide: nothing chosen)
			if (kind == P_ROW && at < listCount(p) && rows.list.rowAt(x, y) == at) choose(at, p);
			else if (kind == P_SAVE && eqLayout(s).save.hit(x, y)) ui.sound->eqSave(p);
			else if (kind == P_ON && switchHit(eqLayout(s).toggleX, eqLayout(s).cy, x, y)) ui.sound->eqToggle(p);
			else if (kind == P_PREV && (eqLayout(s).prevCap.hit(x, y) || eqLayout(s).prevArrow.hit(x, y))) ui.sound->eqStep(-1, p);
			else if (kind == P_NEXT && (eqLayout(s).nextCap.hit(x, y) || eqLayout(s).nextArrow.hit(x, y))) ui.sound->eqStep(1, p);
			else if (kind == P_NAME && eqLayout(s).name.hit(x, y))
			{
				eqMode = EQ_LIST;
				auto e = ui.sound->eqEntries(p);
				cursor = std::max(0, currentRow(p));
				if (cursor < (int)e.size() && e[cursor].kind == 0) cursor++;
			}
			settle(p);
			return false;
		}

		// ---- drawing ----
		void drawEq(const Player& p, const lay::ListSheet& s)
		{
			Kit& k = *ui.kit;
			EqLayout e = eqLayout(s);
			const Settings& st = p.settings;
			bool on = st.eqOn, custom = ui.sound->eqCustom(p);
			std::string preset = custom ? CUSTOM_NAME : st.eqPreset;
			int cy = e.cy;
			ui.drawTextMid("Preset", F_I12, DIM, e.body.x, cy, 46);
			k.caps("L2", e.prevCap.x, cy, ACCENT, SURFACE);
			// ‹ name ›: the stepper's arrows, as every other stepper (and the old Sound page) has them
			ui.drawGlyphMid(G_LEFT, e.prevArrow.x + e.prevArrow.w / 2, cy, on ? TEXT : DIM);
			ui.drawGlyphMid(G_RIGHT, e.nextArrow.x + e.nextArrow.w / 2, cy, on ? TEXT : DIM);
			ui.drawTextMid(preset, F_P16, on ? TEXT : DIM, e.name.x + e.name.w / 2, cy, e.name.w - 2 * e.prevArrow.w - 8, 2);
			k.caps("R2", e.nextCap.x, cy, ACCENT, SURFACE);
			// On / Off on Y, then Save on A
			ui.drawTextMid(on ? "On" : "Off", F_I12, on ? TEXT : DIM, e.onLabelX, cy, 24, 1);
			k.caps("Y", e.onCapX, cy, ACCENT, SURFACE);
			k.toggle(e.toggleX, cy, on);
			Kit::Key save;
			save.glyph = &G_CHECK;
			save.label = !custom ? "Saved" : ui.sound->eqOriginIsUser(p) ? "Save" : "Save as";
			save.key = "A";
			save.off = !custom;
			k.key(e.save, save);

			// the scale, and the ten bands: a gain each, its value over it and its frequency under
			ui.fill({e.body.x, e.trackY + e.trackH / 2, e.body.w, 1}, RULE);
			for (double g : {6.0, -6.0}) ui.fill({e.body.x, bandGainY(e, g), e.body.w, 1}, RULE);
			ui.fill({e.body.x, bandGainY(e, 0), e.body.w, 2}, SCREEN);
			static const char* FREQS[EQ_BANDS] = {"31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
			for (int i = 0; i < EQ_BANDS; i++)
			{
				int cx = e.track[i].x + e.track[i].w / 2;
				double g = st.eqGains[i];
				int ky = bandGainY(e, g);
				bool focus = i == ui.sound->eqBand;
				// (the column the cursor is on, a lighter ground then its bar yellow)
				ui.fillRound({e.band[i].x, e.band[i].y, e.band[i].w, e.band[i].h}, focus ? BUTTON : SURFACE, R_BUTTON);
				ui.fillRound({cx - 1, e.trackY, 2, e.trackH}, SCREEN, 1);
				SDL_Color bar = !on ? DIM : focus ? ACCENT : PRESSED;
				if (ky < e.trackY + e.trackH / 2) ui.fill({cx - 3, ky, 6, e.trackY + e.trackH / 2 - ky}, bar);
				else if (ky > e.trackY + e.trackH / 2) ui.fill({cx - 3, e.trackY + e.trackH / 2, 6, ky - (e.trackY + e.trackH / 2)}, bar);
				if (ui.slideHeld(SL_EQ, i))
				{
					ui.fillRound({cx - 16, ky - 9, 32, 18}, BG, R_KNOB + 2);
					ui.fillRound({cx - 14, ky - 7, 28, 14}, ACCENT, R_KNOB + 1);
				}
				else ui.fillRound({cx - 11, ky - 5, 22, 10}, !on ? DIM : focus ? ACCENT : KNOB, R_KNOB);
				ui.drawTextMid(gainTextOf(g), F_P12, focus ? ACCENT : std::fabs(g) >= 0.05 ? TEXT : DIM, cx, e.gainY, 0, 2);
				ui.drawTextMid(FREQS[i], F_P12, focus ? ACCENT : DIM, cx, e.freqY, 0, 2);
			}
		}
		// a gain as the Sound page writes it: "+3", "0", "-4", "+2.5"
		static std::string gainTextOf(double g)
		{
			char b[16];
			int whole = (int)std::lround(g);
			if (std::fabs(g - whole) >= 0.05) snprintf(b, sizeof b, "%+.1f", g);
			else if (whole) snprintf(b, sizeof b, "%+d", whole);
			else return "0";
			return b;
		}

		// Display: "Top screen shows", the four modes, the style's line, the fifteen styles, and the foot row
		// (docs/redesign "music-view"; the switch: components.md "Match album art", offered here too)
		void drawDisplay(const Player& p)
		{
			Kit& k = *ui.kit;
			lay::DisplayPage d = displayLayout();
			k.sheet(d.sheet, {glyphFor(tab, p), labels()[tab], "", false});
			int show = std::clamp((int)p.settings.musicShow, 0, SHOWS - 1);
			static const Glyph* MODE_GLYPH[SHOWS] = {&G_PICTURE, &G_ARTVIZ, &G_VIS_BARS, &G_TITLE_ONLY};
			ui.drawTextMid("TOP SCREEN SHOWS", F_I11B, DIM, d.modesLabel.x + 2, d.modesLabel.y + d.modesLabel.h / 2, d.modesLabel.w);
			for (int i = 0; i < DISPLAY_N; i++)
			{
				Kit::Choice c;
				c.on = i < SHOWS ? i == show : i == displayInUse(p, true);
				c.focused = i == cursor;
				c.pressed = press == P_TILE && pressAt == i;
				if (i < SHOWS)
				{
					c.glyph = MODE_GLYPH[i];
					c.name = musicShowLabel(i);
					c.font = F_P13;
				}
				else
				{
					int v = VIS_SHOWN[i - SHOWS];
					c.glyph = VIS_GLYPHS[v];
					c.name = visualizerLabel(v);
					c.over = true;
					c.font = F_P12;
				}
				k.choiceTile(d.item(i), c);
			}
			// the style, named over the grid; "not shown now" while the mode shows no visualizer (nothing dimmed: it
			// can still be set, and is kept for a mode that shows it)
			int lx = d.styleLine.x + 2, cy = d.styleLine.y + d.styleLine.h / 2;
			ui.drawTextMid("VISUALIZER", F_I11B, DIM, lx, cy, d.styleLine.w);
			lx += k.textW("VISUALIZER", F_I11B) + 8;
			std::string line = visualizerLabel(p.settings.visualizer);
			if (showHidesVisualizer(p)) line += std::string(" \xC2\xB7 not shown now: the top screen shows ") + musicShowLabel(show);
			ui.drawTextMid(line, F_I12, DIM_ON, lx, cy, d.styleLine.x + d.styleLine.w - lx);
			// the foot: Match album art's switch, as the Settings row is (a radio dot for on)
			if (d.foot.w > 0)
			{
				Kit::Row row;
				row.font = F_P16;
				row.name = "Ambient mode";
				row.note = p.settings.matchAlbumArt ? "On" : "Off";
				row.focused = cursor == DISPLAY_FOOT;
				row.pressed = press == P_TILE && pressAt == DISPLAY_FOOT;
				row.radio = true;
				row.radioOn = p.settings.matchAlbumArt;
				k.row(d.foot, row);
			}
			k.hints({{"D-PAD", "Move"}, {"A", "Set"}, {"SELECT", "Next mode"}, {"B", "Close"}});
		}

		void draw(const Player& p) override
		{
			if (gone(p)) { close(); return; }
			Kit& k = *ui.kit;
			if (tab == TAB_SHOW)
			{
				drawDisplay(p);
				return;
			}
			lay::ListSheet s = layout(p);
			k.sheet(s, {glyphFor(tab, p), labels()[tab], "", false});
			if (tab == TAB_EQ && eqMode == EQ_MAIN)
			{
				drawEq(p, s);
				k.hints({{"LEFT/RIGHT", "Band"}, {"UP/DOWN", "Level"}, {"L2/R2", "Preset"}, {"X", "Options"}, {"B", "Close"}});
				return;
			}
			// the rows: Show and its style row, the effects, the speeds; the equalizer's dialogs as rows too
			int n = listCount(p);
			if (n <= 0) { close(); return; }
			settle(p);
			int shownN = shown(p);
			// each row's look from a copy of its line (the list may draw a row after this draw)
			auto lines = std::make_shared<std::vector<Line>>();
			for (int i = 0; i < n; i++) lines->push_back(lineAt(i, p));
			const int focus = cursor, pressed = press == P_ROW && !rows.moved ? pressAt : -1;
			auto rowOf = [lines, focus, pressed](int i) {
				const Line& l = (*lines)[i];
				Kit::Row row;
				row.font = F_P16;
				row.name = l.name;
				row.note = l.note;
				row.focused = i == focus;
				row.pressed = i == pressed;
				row.radio = !l.off;
				row.radioOn = !l.off && l.on;
				row.off = l.off;
				return row;
			};
			Rect v = SheetRows::view(s, shownN);
			rows.settleOn(rowsKind(), cursor, v, n, 0);
			rows.draw(ui, v, n, rowOf);
			ui.drawScrollBar(s.rowsY, shownN * lay::LIST_ROW, rows.first(), shownN, n);
			if (eqMode == EQ_LIST) k.hints({{"UP/DOWN", "Move"}, {"A", "Choose"}, {"B", "Back"}});
			else if (eqMode == EQ_DELETE) k.hints({{"UP/DOWN", "Move"}, {"A", "Choose"}, {"B", "Cancel"}});
			else if (eqMode == EQ_OPTIONS) k.hints({{"UP/DOWN", "Move"}, {"A", "Choose"}, {"B", "Back"}});
			else
			{
				std::vector<Hint> hints{{"UP/DOWN", "Move"}};
				if (n > shownN) hints.push_back({"LEFT/RIGHT", "Page"});
				hints.push_back({"A", "Choose"});
				hints.push_back({"SELECT/B", "Close"});
				k.hints(hints);
			}
		}

		std::string signature(const Player& p) const override
		{
			// the tab, the rows and the finger; the equalizer's mode and preset; the settings' own are Ui's
			std::string sig = std::to_string(tab) + "," + std::to_string(cursor) + "," + std::to_string(rows.moved) + "," +
				std::to_string(press) + "," + std::to_string(pressAt) + "," + std::to_string(eqMode) + "," +
				std::to_string(ui.sound->eqBand) + "," +
				std::to_string(p.eqPresetSerial()) + "," + std::to_string(p.playingAudio());
			// the presets the equalizer cycles through (the stash of a Custom one shows as Custom)
			for (auto& c : ui.sound->eqCycle(p)) sig += "|" + c;
			return sig;
		}
	};

	void Ui::PlayerPage::openSound(bool effects, Player& p)
	{
		ui.pushOverlay(std::make_unique<SongViewSheet>(ui, effects ? SongViewSheet::TAB_FX : SongViewSheet::TAB_EQ, p, true));
	}

	void Ui::PlayerPage::openView(int tab, Player& p)
	{
		pressed = T_NONE;
		if (p.playingAudio()) ui.pushOverlay(std::make_unique<SongViewSheet>(ui, tab, p));
		else ui.pushOverlay(std::make_unique<ViewSheet>(ui, tab, p));
	}
	// Play on: where it plays (the Session's output), the handheld or a dashboard page that can play. A sheet of rows;
	// the list is read each time (a page that closes leaves it)
	struct Ui::PlayOnSheet : Ui::Overlay
	{
		static constexpr int SHOWN = 6;
		int cursor = 0, scroll = 0, pressed = -1;

		explicit PlayOnSheet(Ui& u) : Overlay(u)
		{
			// on the first that isn't where it plays (the one wanted, most of the time)
			auto list = ui.outChoices();
			std::string cur = here();
			for (int i = 0; i < (int)list.size(); i++)
				if (list[i].tab != cur) { cursor = i; break; }
			fit((int)list.size());
		}
		const char* name() const override { return "playon"; }
		std::string here() const
		{
			SessionHost& sh = SessionHost::get();
			return sh.pageIsOutput() ? sh.session().out.tab : "";
		}
		void fit(int n)
		{
			cursor = std::clamp(cursor, 0, std::max(0, n - 1));
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + SHOWN) scroll = cursor - SHOWN + 1;
			scroll = std::clamp(scroll, 0, std::max(0, n - SHOWN));
		}
		lay::Sheet layout(int n) const { return lay::sheet({0, std::min(n, SHOWN), 1, 0}); }

		void choose(int i, Player& p)
		{
			auto list = ui.outChoices();
			close();
			if (i < 0 || i >= (int)list.size()) return;
			ui.playOnSend(list[i], p);	// (where it plays already: just closed)
		}
		bool key(Button b, Player& p) override
		{
			int n = (int)ui.outChoices().size();
			fit(n);
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			case Button::A: choose(cursor, p); break;
			case Button::B: case Button::X: close(); break;
			default: break;
			}
			fit(n);
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			if (ui.touchIsLocked()) return false;
			int n = (int)ui.outChoices().size();
			fit(n);
			lay::Sheet s = layout(n);
			if (pt.kind == Pointer::Down)
			{
				pressed = -1;
				if (!s.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
				for (int i = 0; i < s.spec.rows; i++)
					if (s.row(i).hit(x, y)) pressed = scroll + i;
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int i = pressed;
			pressed = -1;
			if (i >= scroll && i < n && s.row(i - scroll).hit(x, y)) choose(i, p);
			return false;
		}
		void draw(const Player& p) override
		{
			auto list = ui.outChoices();
			int n = (int)list.size();
			// (what played has gone under it: closed)
			if (!p.playing() || p.playingPhoto() || ui.page != Page::Main) { close(); return; }
			fit(n);
			lay::Sheet s = layout(n);
			Kit& k = *ui.kit;
			std::string title = !p.title.empty() ? p.title : entryTitle(p.currentPath());
			double pos = ui.shownPosition(p);
			k.sheet(s, {&G_QR, "Play on", title + (pos >= 0 ? " \xC2\xB7 from " + formatTime(pos) : "")});
			SessionHost& sh = SessionHost::get();
			std::string cur = here();
			for (int i = 0; i < s.spec.rows; i++)
			{
				const OutChoice& c = list[scroll + i];
				bool hh = c.tab.empty(), at = c.tab == cur;
				Kit::Row row;
				row.glyph = hh ? &G_CARD : &G_LAPTOP;
				row.name = c.name;
				row.focused = scroll + i == cursor;
				row.pressed = scroll + i == pressed;
				// where it plays now says so; the handheld says what comes with it
				if (at) row.pill = !hh && sh.pageWaits() ? "Waiting for a tap there" : (hh ? !p.paused : sh.session().playing) ? "Playing now" : hh ? "Paused here" : "Paused there";
				else if (hh) row.note = "Equalizer, effects, visualizer";
				k.row(s.row(i), row);
			}
			ui.drawScrollBar(s.rowsY, s.spec.rows * lay::SHEET_ROW, scroll, SHOWN, n);
			k.hints({{"UP/DOWN", "Move"}, {"A", "Play there"}, {"B", "Back"}});
		}
		std::string signature(const Player& p) const override
		{
			std::string sig = std::to_string(cursor) + "," + std::to_string(scroll) + "," + std::to_string(pressed) + here() + "|" + std::to_string(p.paused);
			for (auto& c : ui.outChoices()) sig += "," + c.name;
			return sig;
		}
	};

	void Ui::PlayerPage::openPlayOn() { ui.pushOverlay(std::make_unique<PlayOnSheet>(ui)); }

	// ---- drawing ----

	std::vector<Ui::Hint> Ui::PlayerPage::hintList(const Player& p, bool chapters) const
	{
		if (!p.settings.buttonHotkeys) return {{"", "Button hotkeys are off: the keys on this screen do it all"}};
		if (ui.pageDrives(p))
			return {{"A", ui.pagePlaying() ? "Pause there" : "Play there"}, {"Hold A", "Play here"}, {"L/R", "Step"}, {"X/SELECT", "More"}, {"B", "Back"}};
		std::vector<Hint> out{{"LEFT/RIGHT", "Seek"}, {"UP/DOWN", "Volume"}, {"X/SELECT", "More"}, {"Y", "List"}};
		if (chapters) out.push_back({"L2/R2", "Chapter"});
		out.push_back({"B", "Back"});
		return out;
	}

	void Ui::PlayerPage::drawBar(const Player& p, const lay::PlayerLayout& l)
	{
		Kit& k = *ui.kit;
		// (while dragged, and just after until playback catches up: where the finger is or left it)
		double pos = ui.shownPosition(p);
		bool dragging = ui.slideHeld(SL_SEEK), held = dragging || seekHeld >= 0;
		double heldFrac = dragging ? ui.slide.value : seekHeld;
		double frac = held ? heldFrac : p.duration > 0 && pos >= 0 ? std::clamp(pos / p.duration, 0.0, 1.0) : 0;
		std::vector<double> starts;
		for (auto& c : p.chapters) starts.push_back(c.time);
		// under a finger: the knob grown, the time there over it (the chapter's name goes under the bar)
		k.progress(l.bar, frac, lay::barMarks(l.bar, p.duration, starts), dragging,
			dragging ? formatTime(frac * p.duration) + " / " + formatTime(p.duration) : "");
		int cy = l.times.y + l.times.h / 2, mid = l.times.x + l.times.w / 2;
		ui.drawTextMid(formatTime(held ? heldFrac * p.duration : pos), F_SMALL, TEXT, l.times.x, cy);
		ui.drawTextMid(formatTime(p.duration), F_SMALL, DIM, l.times.x + l.times.w, cy, 0, 1);
		// how long is left, at the speed it plays (half at 2x)
		double left = timeLeft(held ? heldFrac * p.duration : pos, p.duration, p.speed);
		// dragging the bar of a file with chapters: the chapter under the finger instead ("3 · The Dragon")
		int under = -1;
		if (dragging && p.duration > 0)
			for (int i = 0; i < (int)p.chapters.size(); i++)
				if (p.chapters[i].time <= ui.slide.value * p.duration) under = i;
		if (under >= 0)
			ui.drawTextMid(std::to_string(under + 1) + " \xC2\xB7 " + chapterLabel(p.chapters[under], under + 1), F_I12, TEXT, mid, cy, 320, 2);
		else if (left >= 0) ui.drawTextMid(formatTime(left) + " left", F_I12, DIM, mid, cy, 0, 2);
	}

	void Ui::PlayerPage::draw(const Player& p)
	{
		if (p.playingPhoto()) { ui.drawPhoto(p); return; }
		// Match album art: the song's blurred art and its veil are the ground the page draws over (a video, a
		// photo, the switch off or High contrast: the theme's own, as drawBackdrop says)
		ui.drawBackdrop(p);

		Kit& k = *ui.kit;
		auto chipList = chips(p);
		lay::PlayerLayout l = lay::playerLayout((int)chipList.size(), repeatMarkShown(p));
		bool err = !p.error.empty(), hotkeys = p.settings.buttonHotkeys, audio = p.playingAudio();
		auto capOf = [&](const char* key) { return std::string(hotkeys ? key : ""); };
		if (ui.roots.empty()) ui.roots = browseRoots();	// (started with a file: the browser never read them)

		// ---- the header (Kit::header): back, what it plays from, its place in the list, the status marks, More ----
		// ---- and the title, with how it plays on its right ----
		std::string name = p.currentPath().substr(p.currentPath().rfind('/') + 1);
		const Glyph* stMark = nullptr;
		std::string state;
		SDL_Color stInk = ACCENT;
		if (err)
		{
			stMark = &G_ALERT;
			state = !p.errorNet ? "Can't play this file" : p.errorNetFound ? "Lost the connection" : "Can't reach it";
			stInk = DANGER;
		}
		else if (p.loading())
		{
			state = "Loading" + std::string(SDL_GetTicks() / 400 % 4, '.');
			stInk = DIM_ON;
		}
		else if (ui.predicting(p))
		{
			// a page plays it: where is the first thing to know
			stMark = &G_QR;
			state = (SessionHost::get().pageWaits() ? "Waiting for " : ui.pagePlaying() ? "In " : "Paused in ") + ui.pageName();
		}
		else if (p.paused)
		{
			stMark = &G_PAUSE;
			state = "Paused";
			stInk = DIM_ON;
		}
		else
		{
			stMark = &G_PLAY;
			state = "Playing";
		}
		if (!hotkeys) state += " \xC2\xB7 buttons off";
		Kit::Header head;
		head.crumb = crumb(p, head.mark);
		head.crumbEnd = p.source().kind != SourceKind::Playlist;
		head.title = !p.title.empty() ? p.title : err ? name : "Opening...";
		head.marquee = !p.title.empty();
		head.titleFor = [&](int room) { return ui.playingTitle(p, head.title, F_P28, room); };
		head.state = state;
		head.stateMark = stMark;
		head.stateInk = stInk;
		head.backPressed = pressed == T_BACK;
		head.morePressed = pressed == T_MORE;
		head.right = [&](int right, int cy) {
			// its place in the list, then the status marks, right to left; the crumb ends at what they leave
			ui.drawBattery(right, cy - 8, p, l.back.x + l.back.w + 130);
			int left = right - (616 - ui.statusLeft(p)) - 10;
			if (p.count() > 1)
			{
				std::string count = std::to_string(p.index() + 1) + " / " + std::to_string(p.count());
				ui.drawTextMid(count, F_I12, DIM, left, cy, 0, 1);
				left -= k.textW(count, F_I12) + 12;
			}
			return left;
		};
		k.header(l, head);

		// ---- the line under it, then the bar (a file that didn't open: what went wrong, in their place) ----
		bool chapterLine = chapterLineShown(p);
		if (err)
		{
			auto lines = ui.wrapText(p.errorNet ? p.error + ". A tries again from where it stopped" : p.error, F_I14, l.line.w, 3);
			for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], F_I14, TEXT, l.line.x, l.line.y + 16 + (int)i * 22, l.line.w);
		}
		else
		{
			if (chapterLine)
			{
				// the chapter playing: the count first, so a long name can be cut without losing the place. A tap opens the list
				int c = ui.shownChapter(p), n = (int)p.chapters.size();
				Kit::Line line;
				line.glyph = &G_CHAPTERS;
				line.text = chapterUntitled(p.chapters[c].title) ? "Chapter " + std::to_string(c + 1) + " of " + std::to_string(n)
					: std::to_string(c + 1) + " / " + std::to_string(n) + " \xC2\xB7 " + p.chapters[c].title;
				line.keys = capOf("L2/R2");
				line.pressed = pressed == T_LINE;
				line.font = F_I14;
				k.line(l.line, line);
			}
			else if (audio && (!p.artist().empty() || !p.album().empty()))
			{
				// a song: who it's by, and the album
				int cy = l.line.y + l.line.h / 2, tx = l.line.x + 26;
				if (p.songArtMissing())
				{
					// a song with no cover: the Art placeholder at 30, where its thumbnail would be
					k.artPlaceholder({l.line.x + 2, cy - 15, 30, 30}, p.artPlaceholderKey());
					tx = l.line.x + 40;
				}
				else ui.drawGlyphMid(G_NOTE, l.line.x + 10, cy, DIM_ON);
				if (!p.artist().empty())
				{
					ui.drawTextMid(p.artist(), F_P14, TEXT, tx, cy, 300);
					tx += std::min(300, k.textW(p.artist(), F_P14)) + 8;
				}
				if (!p.album().empty())
					ui.drawTextMid((p.artist().empty() ? "" : "\xC2\xB7  ") + p.album(), F_I12, DIM_ON, tx, cy, l.line.x + l.line.w - tx);
			}
			drawBar(p, l);
		}

		// ---- the transport: what is pressed all the time ----
		{
			Uint32 now = SDL_GetTicks();
			bool drives = ui.pageDrives(p) && !err;
			// a page plays it: the page's play and pause (a hold plays here)
			bool still = err || (ui.pageDrives(p) ? !ui.pagePlaying() : p.paused);
			Kit::Transport t[5];
			t[0].glyph = &G_PREV; t[0].label = "Prev"; t[0].key = capOf("L");	// never dimmed: it can always go back to the start
			t[1].glyph = &G_REW; t[1].label = "5 s"; t[1].key = capOf("LEFT");
			t[2].glyph = still ? &G_PLAY_BIG : &G_PAUSE_BIG; t[2].label = still ? "Play" : "Pause"; t[2].key = capOf("A");
			t[2].primary = true;
			if (err && p.errorNet) { t[2].glyph = &G_REFRESH; t[2].label = "Retry"; }
			if (drives)
			{
				// the hold that plays it here, the key filling as it goes
				t[2].third = ui.playHoldAt ? "Keep holding..." : "Hold: play it here";
				if (ui.playHoldAt) t[2].hold = std::min(1.0, (now - ui.playHoldAt) / (double)PLAY_HOLD_MS);
			}
			t[3].glyph = &G_FF; t[3].label = "10 s"; t[3].key = capOf("RIGHT");
			t[4].glyph = &G_NEXT; t[4].label = "Next"; t[4].key = capOf("R");
			for (int i = 0; i < 5; i++)
			{
				t[i].off = !usable(T_PREV + i, p);
				t[i].pressed = pressed == T_PREV + i && !t[i].off;
				k.transport(l.transport[i], t[i]);
			}
		}

		// ---- Like, Playlist, Sleep, Stop ----
		{
			Uint32 now = SDL_GetTicks();
			bool liked = ui.likedNow(p), added = ui.addFlashUntil && now < ui.addFlashUntil;
			Kit::Key keys[4];
			keys[0].glyph = liked ? &G_HEART : &G_HEART_O; keys[0].label = liked ? "Liked" : "Like"; keys[0].key = hasL3() ? capOf("L3") : "";
			keys[0].lit = liked;
			// Add to playlist: no button of its own; a check for a moment once it's added
			keys[1].glyph = added ? &G_CHECK : &G_PLUS; keys[1].label = "Playlist"; keys[1].lit = added;
			// Sleep: the sleep timer's sheet (no button of its own: X, then its tile). While one runs it reads what is
			// left, in yellow with the dot (away from normal)
			bool sleeping = ui.sleep->running();
			keys[2].glyph = &G_CLOCK; keys[2].label = sleeping ? ui.sleep->label() : "Sleep"; keys[2].lit = sleeping;
			// Stop: the row's fourth, at the right edge, in plain ink (More's red tile stays). It stops everywhere,
			// the place kept: no cap, since no button does it on this screen
			keys[3].glyph = &G_STOP; keys[3].label = "Stop";
			for (int i = 0; i < 4; i++)
			{
				keys[i].off = !usable(T_LIKE + i, p);
				keys[i].pressed = pressed == T_LIKE + i && !keys[i].off;
				k.key(l.keys[i], keys[i]);
			}
			if (sleeping) KitNew::awayDot(k, l.keys[2], lay::AwayTone::Away);
		}

		// ---- the chips: what is set sometimes, each saying its state ----
		for (int i = 0; i < l.chips; i++)
		{
			Kit::Chip c = chipList[i].look;
			c.pressed = pressed == T_CHIP0 + i;
			k.chip(l.chip[i], c);
		}

		// ---- the Next line: what plays after this, before the list is opened ----
		{
			NextUp nx = nextUp(p);
			Kit::Line line;
			line.label = "Next";
			line.pressed = pressed == T_NEXTLINE;
			if (nx.round)
			{
				line.glyph = &G_SHUFFLE;
				line.text = "A new shuffled round";
			}
			else if (nx.any)
			{
				line.glyph = isAudioFile(nx.path) ? &G_NOTE : &G_FILM;
				line.text = entryTitle(nx.path);
				line.note = nx.note;
			}
			else if (p.settings.loop != LOOP_OFF)
			{
				// one item under a Repeat: it has no next, and it doesn't end either
				line.glyph = p.settings.loop == LOOP_ONE ? &G_LOOP_ONE : &G_LOOP;
				line.text = audio ? "This song again" : "This video again";
				line.note = loopLabel(p.settings.loop);
			}
			else
			{
				line.text = "Nothing after this";
				line.note = "End of the list";
				line.dim = true;
			}
			k.line(l.next, line);
		}

		// ---- a song's Repeat mark, at the Next line's end: dim while Off, the accent and a dot on All and One ----
		if (l.repeat.w > 0)
		{
			Kit::Chip c;
			c.glyph = p.settings.loop == LOOP_ONE ? &G_LOOP_ONE : &G_LOOP;
			c.away = p.settings.loop != LOOP_OFF;
			c.off = err;
			c.pressed = pressed == T_REPEAT;
			k.chip(l.repeat, c);
		}

		k.hints(hintList(p, !p.chapters.empty() && !err));

		// locked: the controls under a veil, one large lock to hold
		if (touchLocked) k.lock(l.lock, lockHoldAt ? std::max(0.02, std::min(1.0, (SDL_GetTicks() - lockHoldAt) / (double)UNLOCK_HOLD_MS)) : 0);
	}

	std::string Ui::PlayerPage::signature(const Player& p) const
	{
		// what draw() shows that Ui's own signature doesn't hold: the finger, the lock, what comes next,
		// the song's tags (they arrive after its title)
		if (p.playingPhoto()) return "";
		NextUp nx = nextUp(p);
		std::string sig = "|pp" + std::to_string(pressed) + "," +
			std::to_string(touchLocked) + "," + std::to_string(seekHeld >= 0) + "|" + nx.path + "|" + nx.note + (nx.round ? "r" : "") + (nx.any ? "a" : "") +
			"|rp" + std::to_string(p.settings.loop) + (repeatMarkShown(p) ? "m" : "") +
			"|" + p.artist() + "|" + p.album() + (p.songArtMissing() ? "|ph" + p.artPlaceholderKey() : "");
		if (touchLocked && lockHoldAt) sig += "|hold" + std::to_string((SDL_GetTicks() - lockHoldAt) / 50);
		// Match album art's backdrop: which answer the page draws over, and (while it runs) the cross-fade's step
		if (ui.backdropOn(p))
		{
			sig += "|bg" + std::to_string(ui.backdropHash());
			if (ui.backdropFading()) sig += "." + std::to_string((SDL_GetTicks() - ui.backdropFadeAt()) / 33);
		}
		else if (ui.backdropLeaving()) sig += "|bgout" + std::to_string((SDL_GetTicks() - ui.backdropLeaveAt()) / 33);
		return sig;
	}
}
