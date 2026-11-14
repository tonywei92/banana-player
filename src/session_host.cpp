#include "session_host.h"
#include "player.h"
#include "ui.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <random>

namespace bp
{
	SessionHost& SessionHost::get()
	{
		static SessionHost host;
		return host;
	}

	SessionHost::SessionHost()
	{
		std::random_device rd;
		s.boot = ((uint64_t)rd() << 32 | rd()) & 0x1FFFFFFFFFFFFFull;	// (an exact number in JavaScript)
	}

	static std::string base(const std::string& p) { return p.empty() ? "-" : p.substr(p.rfind('/') + 1); }

	static uint64_t randomSeed()
	{
		std::random_device rd;
		return ((uint64_t)rd() << 32) ^ rd() ^ SDL_GetTicks();
	}

	// the Shuffle setting (it lasts, as Loop: settings.conf)
	static void shuffleSetting(Player* p, bool on)
	{
		if (!p || p->settings.shuffle == on) return;
		Settings st = p->settings;
		st.shuffle = on;
		p->setSettings(st);
	}

	TrackPick pickOf(const std::vector<Track>& list, int64_t selectedId)
	{
		TrackPick k;
		k.set = true;
		k.of = (int)list.size();
		for (int i = 0; i < (int)list.size(); i++)
			if (list[i].id == selectedId)
			{
				k.ord = i;
				k.lang = list[i].lang;
				k.label = trackLabel(list[i], i + 1);
				k.external = list[i].external;
			}
		return k;
	}

	std::vector<TrackInfo> infosOf(const std::vector<Track>& list)
	{
		std::vector<TrackInfo> out;
		for (int i = 0; i < (int)list.size(); i++) out.push_back({list[i].lang, trackLabel(list[i], i + 1), list[i].external, true});
		return out;
	}

	Intent SessionHost::make(IntentType t, const std::string& origin, bool legacyBasis)
	{
		Intent i;
		i.type = t;
		i.origin = origin;
		bool host = t == IntentType::Tick || t == IntentType::TabGone || t == IntentType::TabBack || t == IntentType::Slept ||
			t == IntentType::Woke;
		// (a tab's own intents carry its seq, Date.now()*1000+n: the words numbered here for it go on from the last)
		uint64_t last = seqs[origin];
		for (auto& e : s.seen)
			if (e.origin == origin) last = std::max(last, e.seq);
		i.seq = host ? 0 : (seqs[origin] = last + 1);
		i.basis = legacyBasis ? publishedV : s.v;
		if (t == IntentType::Open || t == IntentType::SetShuffle || t == IntentType::Step || t == IntentType::Ended) i.rnd = randomSeed();
		return i;
	}

	Result SessionHost::submit(const Intent& in, bool live)
	{
		uint32_t now = SDL_GetTicks();
		Intent i = in;
		Result r = apply(s, i, now);
		// a new item routed to the browser that the browser can't have (not served, a format it doesn't play, its tab
		// not there): again, with the host's word (the reducer can't know, §5)
		if (r.reject.empty() && r.s.index >= 0 && r.s.itemV == s.v + 1 && r.s.out.kind == OutKind::Browser &&
			(!browserCan || !browserCan(r.s.queue[r.s.index], i.origin == r.s.out.tab ? "" : r.s.out.tab)))
		{
			i.browserCan = false;
			r = apply(s, i, now);
		}
		bool quiet = (i.type == IntentType::Tick && r.s.v == s.v && r.fx.empty()) ||
			((i.type == IntentType::Pos || i.type == IntentType::Blocked) && r.s.v == s.v) || r.reject == "stale-report";
		if (!quiet)
		{
			std::string extra;
			if (r.reject.empty() && r.s.v != s.v)
			{
				extra = " out=" + std::string(r.s.out.kind == OutKind::Handheld ? "hh" : r.s.out.tab) +
					(r.s.pref != r.s.out ? " pref=" + std::string(r.s.pref.kind == OutKind::Handheld ? "hh" : r.s.pref.tab) : "") +
					" file=" + base(r.s.index >= 0 ? r.s.queue[r.s.index] : "") + (r.s.playing ? " playing" : " paused");
			}
			fprintf(stderr, "session: %s%s%s%s\n", describe(s, i, r).c_str(), i.origin == "hh" ? "" : (" from " + i.origin).c_str(),
				extra.c_str(), live && !r.fx.empty() && r.reject.empty() ? " (run)" : "");
		}
		Session old = s;
		s = r.s;
		if (live && !r.dup)
		{
			// the handheld's copy takes the picks when it becomes the output, or a new item there
			if (r.reject.empty() && s.out.kind == OutKind::Handheld && s.index >= 0 && (old.out.kind != OutKind::Handheld || old.load != s.load))
				picksWanted = true;
			// (run as the loop pass ends: Next x15 in one pass is one load; a rejected intent's notes still go)
			pendingFx.insert(pendingFx.end(), r.fx.begin(), r.fx.end());
		}
		return r;
	}

	bool SessionHost::holdsItem() const
	{
		return player && s.index >= 0 && player->playing() && !player->playingPhoto() && player->currentPath() == file();
	}

	void SessionHost::bringHere(bool show)
	{
		if (!player || !ui || s.index < 0 || holdsItem()) return;
		// (today's code had moved on without it, or stopped: the session's item, at its place)
		fprintf(stderr, "session: the handheld opens %s to play it\n", base(file()).c_str());
		ui->flushPlaylist(*player);
		if (s.source.kind == 2)
		{
			PlaySource src;
			src.kind = SourceKind::Playlist;
			src.title = s.source.title;
			player->openPlaylist(s.queue, s.index, src);
		}
		else player->open(s.queue, s.index);
		ui->seekPath = file();
		ui->seekTo = s.pos;
		ui->seekUntil = SDL_GetTicks() + 8000;
		// (its screens show what plays, here or held for a page, as a pick here does)
		if (show) ui->showPlayer();
	}

	void SessionHost::seekHere(double t)
	{
		if (!player || !ui) return;
		if (s.out.kind == OutKind::Handheld) bringHere();
		if (holdsItem() && player->loaded && !player->loading() && player->duration > 0)
		{
			if (std::abs(player->position - t) > 0.3) player->seekFraction(std::clamp(t / player->duration, 0.0, 1.0));
			return;
		}
		ui->seekPath = file();
		ui->seekTo = t;
		ui->seekUntil = SDL_GetTicks() + 8000;
	}

	void SessionHost::applyPicks()
	{
		// (the handheld's file, whichever: a pick made over its own list fits it even where the session lags)
		if (!player || !player->playing() || player->playingPhoto() || player->playingAudio()) return;
		for (TrackType type : {TrackType::Audio, TrackType::Sub})
		{
			const TrackPick& k = type == TrackType::Audio ? s.audio : s.sub;
			if (!k.set) continue;
			std::vector<Track> list = player->tracksOf(type);
			int m = matchTrack(k, infosOf(list));
			int64_t id = -1;
			if (m == MATCH_OFF) id = type == TrackType::Sub ? 0 : -1;
			else if (m >= 0) id = list[m].id;
			if (id < 0) continue;	// the default: mpv's own choice stays
			const Track* cur = player->selected(type);
			if ((cur ? cur->id : 0) == id) continue;
			player->selectTrack(type, id);
			player->osd(std::string(type == TrackType::Audio ? "Audio: " : "Subtitles: ") + (m >= 0 ? trackLabel(list[m], m + 1) : "Off"));
			fprintf(stderr, "session: %s track %lld (%s)\n", type == TrackType::Audio ? "audio" : "sub", (long long)id,
				m >= 0 ? trackLabel(list[m], m + 1).c_str() : "off");
		}
	}

	void SessionHost::reportHere()
	{
		if (!player || s.out.kind != OutKind::Handheld || !holdsItem() || !player->loaded || player->loading()) return;
		// where it is, to the moment (the hand-off starts the page there), and what it plays
		if (s.loaded && !s.parked)
		{
			Intent i = make(IntentType::Pos);
			i.load = s.load;
			i.t = std::max(0.0, player->position);
			i.playing = !player->paused;
			submit(i);
		}
		if (player->playingAudio()) return;
		for (TrackType type : {TrackType::Audio, TrackType::Sub})
		{
			std::vector<Track> list = player->tracksOf(type);
			if (list.empty()) continue;
			const Track* cur = player->selected(type);
			TrackPick k = pickOf(list, cur ? cur->id : 0);
			if (k == (type == TrackType::Audio ? s.audio : s.sub)) continue;
			Intent i = make(type == TrackType::Audio ? IntentType::SetAudio : IntentType::SetSub);
			(type == TrackType::Audio ? i.audio : i.sub) = k;
			submit(i);
		}
	}

	void SessionHost::run(const std::vector<Effect>& fx)
	{
		if (!player) return;
		Player& p = *player;
		for (const Effect& e : coalesce(fx))
			switch (e.kind)
			{
			case Effect::HhLoad:
				// the item on the handheld: already there (today's code opened it: a pick here), in its list (Next,
				// Previous, the end of one), else the session's list opened. Held for a page: paused, it never sounds
				if (s.index >= 0 && e.path == file())
				{
					// (held for a page while a photo is open here: the photo stays; pass() holds the copy once it closes)
					if (e.hold && p.playingPhoto()) break;
					if (!holdsItem() && !(p.playing() && !p.playingPhoto() && p.jumpTo(e.path, s.index))) bringHere();
					// held, but it ended in an error (the network dropped it; Play tries again): opened anew
					else if (holdsItem() && !p.error.empty()) p.jumpTo(e.path, s.index);
					if (e.t > 0) seekHere(e.t);
					if (e.hold) p.holdPaused();
				}
				break;
			case Effect::HhSeek: seekHere(e.t); break;
			case Effect::HhPlay:
				bringHere();
				// (unpause reopens the sound output of a copy that was only held: Player::wakeHeld)
				if (holdsItem() || p.playing()) p.unpause();
				break;
			case Effect::HhPause:
				if (p.playing() && !p.playingPhoto()) p.pause();
				break;
			case Effect::HhTracks: picksWanted = true; break;
			case Effect::HhUnload: if (p.playing() && !p.playingPhoto()) unload = endedFx ? 2 : 1; break;
			case Effect::Note: p.osd(e.text, 2400); break;
			default: break;	// (stops, photos: today's code, till step e)
			}
	}

	Result SessionHost::fromPage(const Intent& in)
	{
		Intent i = in;
		if (i.type == IntentType::SetShuffle)
		{
			shuffleSetting(player, i.on);	// (the mode is the setting)
		}
		// (the reducer is deterministic: the host draws Shuffle's orders)
		if (i.type == IntentType::SetShuffle || i.type == IntentType::Open || i.type == IntentType::Step || i.type == IntentType::Ended)
			i.rnd = randomSeed();
		if (i.type == IntentType::SetOutput && i.hasOut && i.out.kind != OutKind::Handheld) reportHere();
		// a page sends the handheld its file: its screens show it, as an /api/play did
		if ((i.type == IntentType::SetOutput || i.type == IntentType::Open) && i.hasOut && i.out.kind == OutKind::Handheld) showHere = true;
		Result r = submit(i, true);
		if (!r.reject.empty()) showHere = false;
		// a pick for the handheld: on its file even where the session's item lags behind today's (steps c-e)
		// and one made in a page that is the output: the handheld's held copy takes it too, so its Audio & Subtitles
		// shows what plays and a hand-off back finds it set
		if (r.reject.empty() && (i.type == IntentType::SetAudio || i.type == IntentType::SetSub) && (s.out.kind == OutKind::Handheld || holdsItem()))
			picksWanted = true;
		return r;
	}

	void SessionHost::takeOver(double at)
	{
		// (with the page's own word of its place: from that tab, the old output; else the handheld's own)
		Intent i = make(IntentType::SetOutput, at >= 0 && s.out.kind == OutKind::Browser ? s.out.tab : "hh");
		i.hasOut = true;
		i.out = handheldOut();
		if (at >= 0)
		{
			i.hasT = true;
			i.t = at;
		}
		submit(i, true);
		submit(make(IntentType::Play), true);
	}

	void SessionHost::sendTo(const Output& to)
	{
		if (to == s.out && to == s.pref) return;
		if (to.kind != OutKind::Handheld) reportHere();
		Intent i = make(IntentType::SetOutput);
		i.hasOut = true;
		i.out = to;
		submit(i, true);
	}

	void SessionHost::chooseOutput(const Output& to, const std::string& origin)
	{
		Intent i = make(IntentType::SetOutput, origin);
		i.hasOut = true;
		i.out = to;
		submit(i);
	}

	void SessionHost::pageCant(const std::string& reason)
	{
		if (s.index < 0 || s.out.kind != OutKind::Browser) return;
		Intent i = make(IntentType::CantPlay, s.out.tab);
		i.load = s.load;
		i.reason = reason;
		submit(i, true);
	}

	void SessionHost::handheldPicked(TrackType type, int64_t id)
	{
		if (!player || s.index < 0 || !holdsItem()) return;
		// (the id asked for: mpv answers the choice a moment later)
		Intent i = make(type == TrackType::Audio ? IntentType::SetAudio : IntentType::SetSub);
		(type == TrackType::Audio ? i.audio : i.sub) = pickOf(player->tracksOf(type), id);
		submit(i);
	}

	void SessionHost::open(const std::vector<std::string>& paths, int index, int sourceKind, const std::string& title,
		const std::string& origin, const Output* out, double t, bool live, const std::string& sourceId)
	{
		Intent i = make(IntentType::Open, origin);
		i.queue = paths;
		i.index = index;
		i.source.kind = sourceKind;
		i.source.title = title;
		// a playlist's: its name as the pages know it (the file's, less ".m3u"; never the path, which is in the state
		// folder): an edit to it reaches the list that plays (playlistEdited)
		std::string stem = sourceId.substr(sourceId.rfind('/') + 1);
		if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".m3u") == 0) stem.resize(stem.size() - 4);
		i.source.id = stem;
		i.t = t;
		if (out)
		{
			i.hasOut = true;
			i.out = *out;
		}
		submit(i, live);
	}

	bool SessionHost::shuffleMode() const { return player ? player->settings.shuffle : s.shuffle; }


	void SessionHost::setShuffle(bool on, const std::string& origin)
	{
		shuffleSetting(player, on);
		if (s.shuffle == on) return;
		Intent i = make(IntentType::SetShuffle, origin);
		i.on = on;
		submit(i, true);
	}

	Result SessionHost::step(int n, const std::string& origin)
	{
		Intent i = make(IntentType::Step, origin);
		i.n = n;
		return submit(i, true);
	}

	void SessionHost::playLive(bool on, const std::string& origin) { submit(make(on ? IntentType::Play : IntentType::Pause, origin), true); }

	void SessionHost::seekLive(double t, bool relative, const std::string& origin)
	{
		Intent i = make(relative ? IntentType::SeekBy : IntentType::Seek, origin);
		i.t = t;
		submit(i, true);
	}

	bool SessionHost::stepsHere(const Player& p) const
	{
		return s.index >= 0 && !p.playingPhoto() && (pageIsOutput() || (p.playing() && p.currentPath() == file()));
	}

	Result SessionHost::ended()
	{
		Intent i = make(IntentType::Ended);
		i.load = s.load;
		endedFx = true;
		Result r = submit(i, true);
		return r;
	}

	void SessionHost::toggle(const std::string& origin) { submit(make(s.playing ? IntentType::Pause : IntentType::Play, origin)); }
	void SessionHost::play(bool on, const std::string& origin) { submit(make(on ? IntentType::Play : IntentType::Pause, origin)); }

	Result SessionHost::stop(const std::string& origin) { return submit(make(IntentType::Stop, origin), true); }

	bool SessionHost::fileGone(const std::string& path)
	{
		bool had = s.index >= 0;
		Intent i = make(IntentType::FileGone, "web");
		i.path = path;
		submit(i, true);
		return had && s.index < 0;
	}

	void SessionHost::playlistEdited(const std::string& id, const std::vector<std::string>& paths, const std::vector<int>& remap)
	{
		// (the session names a playlist by its file's name less ".m3u", here and in the pages)
		std::string stem = id.substr(id.rfind('/') + 1);
		if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".m3u") == 0) stem.resize(stem.size() - 4);
		if (s.index < 0 || s.source.kind != 2 || s.source.id.empty() || s.source.id != stem)
		{
			if (s.index >= 0 && s.source.kind == 2) fprintf(stderr, "session: playlist %s edited, another plays (%s)\n", stem.c_str(), s.source.id.c_str());
			return;
		}
		// (the session's list is the playlist as it was opened; one that differs, say a file deleted since: left be)
		if (remap.size() != s.queue.size())
		{
			fprintf(stderr, "session: playlist edited, its list here differs (%zu vs %zu): not followed\n", remap.size(), s.queue.size());
			return;
		}
		Intent i = make(IntentType::Requeue);
		i.source = s.source;
		i.queue = paths;
		i.remap = remap;
		int at = remap[s.index];
		if (at < 0)
		{
			// the one playing taken out: kept at its place in the new list till another plays
			at = 0;
			for (int k = 0; k < s.index; k++)
				if (remap[k] >= 0) at = std::max(at, remap[k] + 1);
			i.queue.insert(i.queue.begin() + std::min(at, (int)i.queue.size()), s.queue[s.index]);
			for (auto& r : i.remap)
				if (r >= at) r++;
			i.remap[s.index] = at;
		}
		i.index = at;
		Result r = submit(i, true);
		if (!r.reject.empty()) fprintf(stderr, "session: playlist edited, Requeue rejected (%s)\n", r.reject.c_str());
	}

	void SessionHost::seek(double t, bool relative, const std::string& origin)
	{
		Intent i = make(relative ? IntentType::SeekBy : IntentType::Seek, origin);
		i.t = t;
		submit(i);
	}


	void SessionHost::woke() { submit(make(IntentType::Woke), true); }

	int sourceKindOf(const Player& p)
	{
		SourceKind k = p.source().kind;
		return k == SourceKind::Playlist ? 2 : k == SourceKind::NetFolder ? 1 : 0;
	}

	void SessionHost::pass(Player& p, Ui& u, const std::function<bool(const std::string&)>& tabUp,
		const std::function<bool(const std::string&)>& tabBye)
	{
		uint32_t now = SDL_GetTicks();
		// the handheld as the output: its reports (it is the clock)
		if (s.index >= 0 && s.out.kind == OutKind::Handheld && !s.parked && holdsItem())
		{
			if (!p.error.empty())
			{
				if (erroredLoad != s.load && p.loads() != erroredPlayerLoad)
				{
					erroredLoad = s.load;
					erroredPlayerLoad = p.loads();
					Intent i = make(IntentType::CantPlay);
					i.load = s.load;
					i.reason = p.errorNet ? "net" : "error";
					submit(i);
				}
			}
			else if (!s.loaded)
			{
				if (p.loaded && !p.loading())
				{
					Intent i = make(IntentType::Loaded);
					i.load = s.load;
					i.dur = std::max(0.0, p.duration);
					i.audioOnly = p.playingAudio();
					submit(i);
					posAt = now;
				}
			}
			else
			{
				// its play state, once it settles (mpv answers a pause a moment later; the comparison sees it first)
				bool agree = p.paused == !s.playing;
				if (agree) mismatchSince = 0;
				else if (!mismatchSince) mismatchSince = now;
				if (now - posAt >= 1000 && (agree || now - mismatchSince >= 3000))
				{
					posAt = now;
					Intent i = make(IntentType::Pos);
					i.load = s.load;
					i.t = std::max(0.0, p.position);
					i.playing = !p.paused;
					submit(i);
				}
			}
		}
		// the session's picks on the handheld's copy, once its tracks are known
		if (s.out.kind != OutKind::Handheld && !holdsItem()) picksWanted = false;
		else if (picksWanted && p.playing() && p.loaded && !p.loading())
		{
			picksWanted = false;
			applyPicks();
		}
		// I2: the handheld's mpv never sounds while a page is the output. Every way today's code plays here goes
		// through takeOver first; anything that slips by is paused (and said in the log), once it lasts
		if (pageIsOutput() && p.playing() && !p.playingPhoto() && !p.paused && !p.loading())
		{
			if (!soundingSince) soundingSince = now;
			else if (now - soundingSince >= 500)
			{
				soundingSince = 0;
				fprintf(stderr, "session: invariant I2 repaired (the handheld played %s while %s is the output)\n",
					base(p.currentPath()).c_str(), s.out.tab.c_str());
				p.pause();
			}
		}
		else soundingSince = 0;
		// the handheld's Shuffle (its settings, as Loop) is the session's
		if (p.settings.shuffle != s.shuffle)
		{
			Intent i = make(IntentType::SetShuffle);
			i.on = p.settings.shuffle;
			submit(i, true);
		}
		// the handheld's Loop (its settings) is the session's, for songs and videos alike
		int loop = p.settings.loop;
		if (loop != s.loop)
		{
			Intent i = make(IntentType::SetLoop);
			i.loop = loop;
			submit(i);
		}
		if (now - tickAt >= 250)
		{
			tickAt = now;
			submit(make(IntentType::Tick), true);
		}
		if (now - tabsAt >= 500)
		{
			tabsAt = now;
			for (const Output* o : {&s.out, &s.pref})
			{
				if (o->kind != OutKind::Browser) continue;
				std::string tab = o->tab;
				bool up = tabUp && tabUp(tab);
				auto it = tabState.find(tab);
				bool was = it == tabState.end() ? true : it->second;
				tabState[tab] = up;
				// a goodbye (the page closing said so): with the going, or after it if its stream went quiet first
				bool bye = !up && tabBye && tabBye(tab);
				if (up) tabByeTold.erase(tab);
				if (up != was || (bye && !tabByeTold.count(tab)))
				{
					Intent i = make(up ? IntentType::TabBack : IntentType::TabGone);
					i.path = tab;
					i.on = bye;
					if (bye) tabByeTold[tab] = true;
					submit(i, true);
				}
			}
		}
		// a page the output and nothing here (a photo closed, a stop of the copy): the item held here again, quietly
		if (pageIsOutput() && !p.playing() && player) bringHere(false), pendingFx.push_back({Effect::HhLoad, file(), "", s.pos, s.load, true});
		if (!pendingFx.empty())
		{
			std::vector<Effect> fx;
			fx.swap(pendingFx);
			run(fx);
		}
		endedFx = false;
		if (showHere && ui && s.out.kind == OutKind::Handheld && s.index >= 0)
		{
			showHere = false;
			ui->showPlayer();
		}
		(void)u;
	}
}
