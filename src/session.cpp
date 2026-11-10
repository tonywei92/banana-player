#include "session.h"
#include "photo.h"
#include "playlists.h"
#include "json.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bp
{
	const char* intentName(IntentType t)
	{
		static const char* NAMES[] = {"Open", "Play", "Pause", "Seek", "SeekBy", "Step", "SetAudio", "SetSub", "SetSpeed",
			"SetLoop", "SetOutput", "Stop", "PhotoPark", "PhotoResume", "FileGone", "Loaded", "Pos", "Ended", "CantPlay",
			"Tick", "TabGone", "TabBack", "Slept", "Woke", "Blocked", "SetShuffle", "Requeue"};
		return NAMES[(int)t];
	}

	const char* effectName(Effect::Kind k)
	{
		static const char* NAMES[] = {"HhLoad", "HhPlay", "HhPause", "HhSeek", "HhTracks", "HhSpeed", "HhUnload", "HhPhoto",
			"Note", "PageToast"};
		return NAMES[(int)k];
	}

	int matchTrack(const TrackPick& pick, const std::vector<TrackInfo>& list)
	{
		if (!pick.set) return MATCH_DEFAULT;
		if (pick.ord < 0) return MATCH_OFF;
		auto playable = [&](int k) { return list[k].playable ? k : MATCH_DEFAULT; };
		// the same list (as many of its kind): by place, which tells two "English" tracks apart
		if (pick.of > 0 && (int)list.size() == pick.of && pick.ord < (int)list.size()) return playable(pick.ord);
		if (!pick.label.empty())
		{
			int any = -1;
			for (int k = 0; k < (int)list.size(); k++)
				if (list[k].label == pick.label)
				{
					if (list[k].playable) return k;
					if (any < 0) any = k;
				}
			if (any >= 0) return MATCH_DEFAULT;	// there, but not for this renderer: its default, the pick kept
		}
		if (!pick.lang.empty())
		{
			auto lower = [](std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; };
			std::string want = lower(pick.lang);
			for (int k = 0; k < (int)list.size(); k++)
				if (list[k].playable && lower(list[k].lang) == want && list[k].external == pick.external) return k;
			for (int k = 0; k < (int)list.size(); k++)
				if (list[k].playable && lower(list[k].lang) == want) return k;
		}
		return MATCH_DEFAULT;
	}

	double posNow(const Session& s, int64_t now)
	{
		double p = s.pos;
		if (s.playing && !s.parked && s.loaded && !s.blocked && s.index >= 0 && now > s.posAt) p += (now - s.posAt) / 1000.0 * s.speed;
		if (s.dur > 0) p = std::min(p, s.dur);
		return std::max(0.0, p);
	}

	int shufflePlace(const Session& s)
	{
		auto it = std::find(s.order.begin(), s.order.end(), s.index);
		return s.index < 0 || it == s.order.end() ? -1 : (int)(it - s.order.begin());
	}

	bool hasNext(const Session& s)
	{
		if (s.index < 0) return false;
		// (Next at the last one: Shuffle starts a new round, a Loop goes back to the first; only In order stops there)
		if (s.shuffle || s.loop != SESSION_LOOP_OFF) return true;
		return s.index + 1 < (int)s.queue.size();
	}

	std::string outOrigin(const Session& s) { return s.out.kind == OutKind::Handheld ? "hh" : s.out.tab; }

	namespace
	{
		// what a client sees change: everything but the clock (pos, posAt), the host's timers, the seen store and v
		bool samePublished(const Session& a, const Session& b)
		{
			return a.queue == b.queue && a.index == b.index && a.source == b.source && a.shuffle == b.shuffle && a.order == b.order && a.load == b.load &&
				a.playing == b.playing && a.speed == b.speed && a.loop == b.loop && a.audio == b.audio && a.sub == b.sub &&
				a.out == b.out && a.pref == b.pref && a.outV == b.outV && a.itemV == b.itemV && a.playV == b.playV &&
				a.seekN == b.seekN && a.parked == b.parked && a.error == b.error && a.dur == b.dur &&
				a.audioOnly == b.audioOnly && a.loaded == b.loaded && a.blocked == b.blocked && a.ended == b.ended && a.boot == b.boot &&
				a.cantRun == b.cantRun && a.skipBack == b.skipBack;
		}

		bool sameAll(const Session& a, const Session& b)
		{
			return samePublished(a, b) && a.v == b.v && a.pos == b.pos && a.posAt == b.posAt && a.loadAt == b.loadAt &&
				a.goneAt == b.goneAt && a.goneTab == b.goneTab;
		}

		double clampPos(const Session& s, double t)
		{
			if (!(t >= 0)) t = 0;	// (NaN too)
			return s.dur > 0 ? std::min(t, s.dur) : t;
		}

		struct Ctx
		{
			Session& n;
			const Session& s;
			const Intent& i;
			int64_t now;
			uint64_t nv;	// the v this apply gives, if it changes anything
			std::vector<Effect>& fx;

			void note(const std::string& text, bool page = true)
			{
				Effect e;
				e.kind = Effect::Note;
				e.text = text;
				fx.push_back(e);
				if (page)
				{
					e.kind = Effect::PageToast;
					fx.push_back(e);
				}
			}
			void effect(Effect::Kind k, double t = 0)
			{
				Effect e;
				e.kind = k;
				e.t = t;
				fx.push_back(e);
			}
			// the item (re)loads on the output: reports for the last load are stale from here
			void newLoad()
			{
				n.load++;
				n.loaded = false;
				n.loadAt = now;
			}
			void newItem(int index)
			{
				n.index = index;
				n.itemV = nv;
				n.pos = 0;
				n.posAt = now;
				n.error.clear();
				n.dur = 0;
				n.audioOnly = false;
				n.parked = false;
				n.skipBack = false;
				newLoad();
			}
			void setPlaying(bool on)
			{
				if (n.playing == on) return;
				if (!on)
				{
					n.pos = posNow(n, now);
					n.posAt = now;
				}
				else n.posAt = now;
				n.playing = on;
				n.playV = nv;
			}
			void setOut(const Output& o)
			{
				if (n.out == o) return;
				n.out = o;
				n.outV = nv;
			}
			// an item picked or stepped to: where the picks go (pref), unless a browser can't have this one
			void route()
			{
				// (one the browser can't have: an Open (a file picked) plays on the handheld; Next, Previous and a file's
				// end leave it to the browser, which says it can't and the session skips it there: browserCant)
				if (n.pref.kind == OutKind::Browser && !i.browserCan && i.type == IntentType::Open)
				{
					setOut(handheldOut());
					note("This one plays on the handheld");
				}
				else setOut(n.pref);
			}
			// the handheld's copy: loaded (held when another output plays it), played when it is the output
			void loadHere()
			{
				if (n.index < 0) return;
				Effect e;
				e.kind = Effect::HhLoad;
				e.path = n.queue[n.index];
				e.t = n.pos;
				e.load = n.load;
				e.hold = n.out.kind != OutKind::Handheld;
				fx.push_back(e);
				if (hhShouldPlay(n)) effect(Effect::HhPlay);
			}
			// Shuffle's hidden order from the item (first), or none
			void newOrder()
			{
				n.order.clear();
				if (n.shuffle && n.index >= 0) n.order = shuffleOrder((int)n.queue.size(), n.index, i.rnd);
			}
			// Shuffle with Loop all, its round played: a new round (not the one just played first, as a player would
			// never play one song twice in a row), from its first
			int newRound()
			{
				int size = (int)s.queue.size();
				n.order = shuffleOrder(size, -1, i.rnd ^ 0x5DEECE66Dull);
				if (size > 1 && n.order[0] == s.index) std::swap(n.order[0], n.order[1]);
				return n.order[0];
			}
			void stopItem()
			{
				n.order.clear();
				n.queue.clear();
				n.index = -1;
				setPlaying(false);
				n.parked = false;
				n.error.clear();
				n.itemV = nv;
				n.dur = 0;
				n.audioOnly = false;
				n.pos = 0;
				n.posAt = now;
				newLoad();
				effect(Effect::HhUnload);
			}
			// a browser output can't play the item (its format, a load that never answered): the handheld, at the place
			// a browser output can't play the item (its format, a load that never answered): it's skipped there, said in
			// the page, and the list goes on in the browser (the owner: moving it to the handheld was annoying). A
			// list none of which plays there stops once each has been tried; the end of the list stops as Ended does
			void browserCant()
			{
				// (the page says so: "… isn't supported in this browser · skipped")
				int size = (int)s.queue.size();
				int run = s.cantRun + 1;
				if (run < size && (s.skipBack ? prevItem() : nextItem()))
				{
					n.cantRun = run;
					return;
				}
				// the end of the list going forward: finished as Ended's; none of it plays here (or nothing before it
				// going back): stopped
				if (run < size && !s.skipBack) { finishList(); return; }
				stopItem();
				note("Nothing more plays in this browser", false);
			}
			// the end of the list: stopped, and kept for the pages (the list and its last item, to play again from any
			// row; nothing autoplays)
			void finishList()
			{
				std::string title = s.source.title;
				stopItem();
				n.ended.queue = s.queue;
				n.ended.index = s.index;
				n.ended.source = s.source;
				n.ended.dur = s.dur;
				n.ended.audioOnly = s.audioOnly;
				note("Finished", false);
				Effect e;
				e.kind = Effect::PageToast;
				e.text = "End of " + (title.empty() ? std::string("the list") : title);
				fx.push_back(e);
			}
			// the item before this one, after a Previous (Shuffle: back along the hidden order); false at the start
			bool prevItem()
			{
				int size = (int)s.queue.size();
				int to;
				if (s.shuffle && (int)s.order.size() == size)
				{
					int at = shufflePlace(s);
					if (at <= 0) return false;
					to = s.order[at - 1];
				}
				else if (s.index > 0) to = s.index - 1;
				else if (s.loop == SESSION_LOOP_ALL && size > 1) to = size - 1;
				else return false;
				newItem(to);
				n.skipBack = true;
				route();
				loadHere();
				return true;
			}
			// the item after this one (Shuffle: the hidden order's next, a new round with Loop all), loaded where it
			// plays; false at the end of the list
			bool nextItem()
			{
				int size = (int)s.queue.size();
				if (s.shuffle && (int)s.order.size() == size)
				{
					int at = shufflePlace(s);
					if (at + 1 >= size && s.loop != SESSION_LOOP_ALL) return false;
					newItem(at + 1 < size ? s.order[at + 1] : newRound());
				}
				else
				{
					if (s.index + 1 >= size && s.loop != SESSION_LOOP_ALL) return false;
					newItem(s.index + 1 < size ? s.index + 1 : 0);
				}
				route();
				loadHere();
				return true;
			}
			bool guardedStale() const { return i.basis < s.itemV || i.basis < s.outV; }

			std::string run()
			{
				bool host = i.type == IntentType::Tick || i.type == IntentType::TabGone || i.type == IntentType::TabBack ||
					i.type == IntentType::Slept || i.type == IntentType::Woke;
				if (host && i.origin != "hh") return "origin";
				bool report = i.type == IntentType::Loaded || i.type == IntentType::Pos || i.type == IntentType::Ended ||
					i.type == IntentType::CantPlay || i.type == IntentType::Blocked;
				// rule 2: reports only from the output, for its current load
				if (report)
				{
					if (s.index < 0 || i.origin != outOrigin(s) || i.load != s.load) return "stale-report";
					if (s.parked) return "parked";	// the photo has the handheld's mpv: its place is frozen
				}
				switch (i.type)
				{
				case IntentType::Open:
				{
					std::vector<std::string> q;
					int index = -1;
					for (int k = 0; k < (int)i.queue.size(); k++)
					{
						if (isPhotoFile(i.queue[k])) continue;
						if (k == i.index) index = (int)q.size();
						q.push_back(i.queue[k]);
					}
					if (q.empty()) return "empty";
					if (index < 0) return "photo";
					if (i.hasOut && i.out.kind == OutKind::Browser && i.out.tab.empty()) return "bad-output";
					n.queue = q;
					n.source = i.source;
					newItem(index);
					newOrder();	// (Shuffle: a new hidden order, from the item picked)
					n.pos = clampPos(n, i.t);
					n.playing = true;
					n.playV = nv;
					n.audio = i.hasAudio ? i.audio : TrackPick();
					n.sub = i.hasSub ? i.sub : TrackPick();
					if (i.hasOut) n.pref = i.out;
					route();
					loadHere();
					return "";
				}
				case IntentType::Play:
					if (s.index < 0) return "nothing";
					if (!s.error.empty()) return "error";
					if (s.parked && s.out.kind == OutKind::Handheld)
					{
						// Play over a photo: what it stopped, again at its place
						n.parked = false;
						setPlaying(true);
						newLoad();
						loadHere();
						return "";
					}
					if (s.playing) return "";
					setPlaying(true);
					if (hhShouldPlay(n)) effect(Effect::HhPlay);
					return "";
				case IntentType::Pause:
					if (s.index < 0) return "nothing";
					if (!s.playing) return "";
					setPlaying(false);
					if (s.out.kind == OutKind::Handheld) effect(Effect::HhPause);
					return "";
				case IntentType::Seek:
				case IntentType::SeekBy:
					if (s.index < 0) return "nothing";
					n.pos = clampPos(n, i.type == IntentType::Seek ? i.t : posNow(s, now) + i.t);
					n.posAt = now;
					n.seekN++;
					if (s.out.kind == OutKind::Handheld) effect(Effect::HhSeek, n.pos);
					return "";
				case IntentType::Step:
				{
					if (s.index < 0) return "nothing";
					int k = i.n;
					bool did = false;
					// Prev RESTART_S or more in: the first press goes back to the start, the rest step
					if (k < 0 && posNow(s, now) >= RESTART_S)
					{
						n.pos = 0;
						n.posAt = now;
						n.seekN++;
						k++;
						did = true;
						if (s.out.kind == OutKind::Handheld) effect(Effect::HhSeek, 0);
					}
					if (k == 0) return "";
					int size = (int)s.queue.size();
					int to = s.index + k;
					bool round = false;
					if (s.shuffle && (int)s.order.size() == size)
					{
						// Shuffle: along the hidden order (Previous back in it), to its ends; Next past its end with
						// Loop all: a new round
						int at = shufflePlace(s);
						if (at + k >= size && k > 0) round = true;	// (Next past the round's end: a new one)
						else to = s.order[std::clamp(at + k, 0, size - 1)];
					}
					else if (s.loop != SESSION_LOOP_OFF) to = ((to % size) + size) % size;	// (Loop all or one: round)
					else to = std::clamp(to, 0, size - 1);
					if (to == s.index)
					{
						if (did) return "";
						note(k < 0 ? "This is the first one" : "This is the last one", false);
						return k < 0 ? "first" : "last";
					}
					bool wasError = !s.error.empty();
					if (round) to = newRound();
					newItem(to);
					n.skipBack = k < 0;
					if (wasError) setPlaying(true);	// stepping away from a file that failed: the next one plays
					route();
					loadHere();
					return "";
				}
				case IntentType::SetAudio:
				case IntentType::SetSub:
				{
					TrackPick& to = i.type == IntentType::SetAudio ? n.audio : n.sub;
					to = i.type == IntentType::SetAudio ? i.audio : i.sub;
					to.set = true;
					if (to != (i.type == IntentType::SetAudio ? s.audio : s.sub) && s.index >= 0 && s.out.kind == OutKind::Handheld)
						effect(Effect::HhTracks);
					return "";
				}
				case IntentType::SetSpeed:
					if (!(i.speed > 0) || i.speed > 16) return "bad-speed";
					if (s.index >= 0)
					{
						n.pos = posNow(s, now);
						n.posAt = now;
					}
					n.speed = i.speed;
					if (n.speed != s.speed) effect(Effect::HhSpeed);
					return "";
				case IntentType::SetLoop:
					if (i.loop < SESSION_LOOP_OFF || i.loop > SESSION_LOOP_ONE) return "bad-loop";
					n.loop = i.loop;
					return "";
				case IntentType::SetShuffle:
					// the setting (nothing playing too: the next list's); what plays plays on, Next follows a new
					// hidden order from it (on) or the queue's (off)
					if (i.on == s.shuffle) return "";
					n.shuffle = i.on;
					newOrder();
					return "";
				case IntentType::SetOutput:
				{
					if (guardedStale()) return "stale";
					if (!i.hasOut || (i.out.kind == OutKind::Browser && i.out.tab.empty())) return "bad-output";
					n.pref = i.out;
					if (s.out == i.out) return "";
					if (s.index >= 0)
					{
						// the place the old output had got to (its own word, if it is the sender), where the new one starts
						double p = posNow(s, now);
						if (i.hasT && i.origin == outOrigin(s)) p = i.t;
						n.pos = clampPos(n, p);
						n.posAt = now;
					}
					bool wasHeld = s.out.kind != OutKind::Handheld;
					setOut(i.out);
					n.goneAt = 0;
					n.goneTab.clear();
					if (s.index < 0) return "";
					n.parked = false;
					newLoad();
					if (n.out.kind == OutKind::Handheld)
					{
						effect(Effect::HhSeek, n.pos);
						if (hhShouldPlay(n))
						{
							Effect e;
							e.kind = Effect::HhPlay;
							e.aoReload = wasHeld;
							fx.push_back(e);
						}
					}
					else effect(Effect::HhPause);
					return "";
				}
				case IntentType::Stop:
					if (guardedStale()) return "stale";
					if (s.index < 0)
					{
						// a list that ended by itself, still offered: Stop puts it away (nothing playing)
						if (s.ended.index < 0) return "nothing";
						n.ended = Finished();
						return "";
					}
					stopItem();
					return "";
				case IntentType::PhotoPark:
					if (i.origin != "hh") return "origin";
					if (s.index < 0) return "nothing";
					effect(Effect::HhPhoto);
					// a browser output plays on: only the handheld's mpv shows the photo
					if (s.out.kind != OutKind::Handheld || s.parked) return "";
					n.pos = clampPos(n, i.hasT ? i.t : posNow(s, now));
					n.posAt = now;
					n.parked = true;
					if (n.playing)
					{
						n.playing = false;
						n.playV = nv;
					}
					return "";
				case IntentType::PhotoResume:
					if (i.origin != "hh") return "origin";
					if (!s.parked) return "not-parked";
					n.parked = false;
					setPlaying(true);
					newLoad();
					loadHere();
					return "";
				case IntentType::FileGone:
				{
					if (i.path.empty()) return "nothing";
					std::string dir = i.path.back() == '/' ? i.path : i.path + "/";
					auto gone = [&](const std::string& p) { return p == i.path || p.compare(0, dir.size(), dir) == 0; };
					if (s.ended.index >= 0)
					{
						// the list that ended: without what went; its last item gone, nothing to show
						if (gone(s.ended.queue[s.ended.index])) n.ended = Finished();
						else
						{
							Finished f = s.ended;
							f.queue.clear();
							for (int k = 0; k < (int)s.ended.queue.size(); k++)
							{
								if (!gone(s.ended.queue[k])) f.queue.push_back(s.ended.queue[k]);
								else if (k < s.ended.index) f.index--;
							}
							n.ended = f;
						}
					}
					if (s.index >= 0 && gone(s.queue[s.index]))
					{
						stopItem();
						note("Stopped: the file was deleted");
						return "";
					}
					std::vector<std::string> q;
					int index = s.index;
					for (int k = 0; k < (int)s.queue.size(); k++)
					{
						if (!gone(s.queue[k])) q.push_back(s.queue[k]);
						else if (k < s.index) index--;
					}
					n.queue = q;
					n.index = index;
					// the hidden order without them, its places renumbered
					std::vector<int> to(s.queue.size(), -1);
					for (int k = 0, m = 0; k < (int)s.queue.size(); k++)
						if (!gone(s.queue[k])) to[k] = m++;
					n.order.clear();
					for (int k : s.order)
						if (k >= 0 && k < (int)to.size() && to[k] >= 0) n.order.push_back(to[k]);
					return "";
				}
				case IntentType::Requeue:
				{
					// the playlist that plays, edited: Next goes on to what was added after its last song
					if (s.index < 0) return "nothing";
					if (!(i.source == s.source)) return "other-list";
					int size = (int)i.queue.size();
					if (i.remap.size() != s.queue.size() || i.index < 0 || i.index >= size || i.remap[s.index] != i.index ||
						i.queue[i.index] != s.queue[s.index])
						return "bad-remap";
					std::vector<char> used(size, 0);
					for (int k = 0; k < (int)i.remap.size(); k++)
					{
						int r = i.remap[k];
						if (r < -1 || r >= size || (r >= 0 && used[r])) return "bad-remap";
						if (r >= 0)
						{
							if (i.queue[r] != s.queue[k]) return "bad-remap";
							used[r] = 1;
						}
					}
					for (auto& q : i.queue)
						if (isPhotoFile(q)) return "photo";
					n.queue = i.queue;
					n.index = i.index;
					// the hidden order: the same, less what went, what came after (played last, in the list's order)
					if (!s.order.empty())
					{
						n.order.clear();
						for (int k : s.order)
							if (k >= 0 && k < (int)i.remap.size() && i.remap[k] >= 0) n.order.push_back(i.remap[k]);
						for (int k = 0; k < size; k++)
							if (!used[k]) n.order.push_back(k);
					}
					return "";
				}
				case IntentType::Loaded:
					n.loaded = true;
					n.cantRun = 0;
					n.dur = std::max(0.0, i.dur);
					n.audioOnly = i.audioOnly;
					n.pos = clampPos(n, n.pos);
					n.posAt = now;	// the clock runs from here
					if (n.error == "timeout") n.error.clear();
					return "";
				case IntentType::Pos:
					n.loaded = true;
					n.pos = clampPos(n, i.t);
					n.posAt = now;
					// the output's play state, unless it was sent before the last Play/Pause (that wish stands)
					if (i.basis >= s.playV && n.playing != i.playing) n.playing = i.playing;
					if (i.playing) n.blocked = false;	// it sounds: the tap came
					return "";
				case IntentType::Blocked:
					// a browser that wouldn't start the sound by itself: it waits for a tap there (the handheld says so,
					// B there plays here). Only while the wish is to play: paused, there's nothing to wait for
					if (s.out.kind != OutKind::Browser) return "not-browser";
					if (!s.playing) return "paused";
					n.blocked = true;
					n.loaded = true;
					if (i.hasT) n.pos = clampPos(n, i.t);
					n.posAt = now;
					return "";
				case IntentType::Ended:
				{
					if (s.loop == SESSION_LOOP_ONE)
					{
						n.pos = 0;
						n.posAt = now;
						n.seekN++;
						if (s.out.kind == OutKind::Handheld) effect(Effect::HhSeek, 0);
						return "";
					}
					// (Shuffle: the next of the hidden order; its last played, a new round with Loop all, else the end)
					if (nextItem()) return "";
					finishList();
					return "";
				}
				case IntentType::CantPlay:
					if (s.out.kind == OutKind::Browser)
					{
						browserCant();
						return "";
					}
					n.error = i.reason.empty() ? "error" : i.reason;
					n.loaded = false;
					if (n.playing)
					{
						n.playing = false;
						n.playV = nv;
					}
					return "";
				case IntentType::Tick:
				{
					if (s.goneAt && now - s.goneAt >= TAB_GRACE_MS)
					{
						std::string tab = s.goneTab;
						n.goneAt = 0;
						n.goneTab.clear();
						if (s.out.kind == OutKind::Browser && s.out.tab == tab)
						{
							// where it was when the tab went; never sound by itself: paused here
							if (s.index >= 0)
							{
								n.pos = posNow(s, std::max(s.goneAt, s.posAt));
								n.posAt = now;
							}
							setOut(handheldOut());
							if (n.playing)
							{
								n.playing = false;
								n.playV = nv;
							}
							if (s.index >= 0)
							{
								newLoad();
								effect(Effect::HhSeek, n.pos);
								note("Your browser went \xC2\xB7 paused here", false);
							}
						}
						if (n.pref.kind == OutKind::Browser && n.pref.tab == tab) n.pref = handheldOut();
					}
					// a load nobody answered: a browser can't (the handheld plays it), the handheld failed
					if (n.index >= 0 && !n.loaded && n.error.empty() && !n.parked && now - n.loadAt >= LOAD_TIMEOUT_MS)
					{
						if (n.out.kind == OutKind::Browser)
						{
							Session before = n;
							Ctx c{n, before, i, now, nv, fx};
							c.browserCant();
						}
						else
						{
							n.error = "timeout";
							if (n.playing)
							{
								n.playing = false;
								n.playV = nv;
							}
						}
					}
					return "";
				}
				case IntentType::TabGone:
					if (i.path.empty()) return "";
					if ((s.out.kind == OutKind::Browser && s.out.tab == i.path) || (s.pref.kind == OutKind::Browser && s.pref.tab == i.path))
					{
						if (!s.goneAt || s.goneTab != i.path)
						{
							n.goneTab = i.path;
							n.goneAt = now;
						}
					}
					return "";
				case IntentType::TabBack:
					if (s.goneAt && s.goneTab == i.path)
					{
						n.goneAt = 0;
						n.goneTab.clear();
					}
					return "";
				case IntentType::Slept:
					return "";
				case IntentType::Woke:
					// the lid opened: paused here and there, at the place (Play continues)
					if (s.index < 0 || !s.playing) return "";
					setPlaying(false);
					if (s.out.kind == OutKind::Handheld) effect(Effect::HhPause);
					return "";
				}
				return "unknown";
			}
		};

		void remember(Session& s, const std::string& origin, uint64_t seq, const std::string& reject)
		{
			auto it = std::find_if(s.seen.begin(), s.seen.end(), [&](const OriginSeen& e) { return e.origin == origin; });
			if (it != s.seen.end()) s.seen.erase(it);
			s.seen.push_back({origin, seq, s.v, reject});
			if (s.seen.size() > SEEN_MAX) s.seen.erase(s.seen.begin());
		}
	}

	Result apply(const Session& s, const Intent& i, int64_t now)
	{
		Result r;
		// rule 1: a seq at or under the origin's last: the answer it had, nothing done again
		if (i.seq > 0)
			for (auto& e : s.seen)
				if (e.origin == i.origin && i.seq <= e.seq)
				{
					r.s = s;
					r.dup = true;
					r.reject = e.reject;
					return r;
				}
		r.s = s;
		std::vector<Effect> fx;
		Ctx c{r.s, s, i, now, s.v + 1, fx};
		std::string why = c.run();
		// waiting for a tap lasts only while that output plays that load and the wish is still to play: a pause, a
		// reload (Next, a hand-off), another output or a stop end it (a new load reports again if it must)
		Session& w = r.s;
		if (w.blocked && (!w.playing || w.out != s.out || w.index < 0 || w.load != s.load)) w.blocked = false;
		// the list that ended is only offered while nothing plays: anything that plays an item ends it
		if (w.index >= 0 && w.ended.index >= 0) w.ended = Finished();
		if (!why.empty())
		{
			// all or nothing: the old session (only what it says may go out)
			r.s = s;
			r.reject = why;
			for (auto& e : fx)
				if (e.kind == Effect::Note || e.kind == Effect::PageToast) r.fx.push_back(e);
		}
		else
		{
			if (!samePublished(r.s, s)) r.s.v = s.v + 1;
			r.fx = fx;
		}
		if (i.seq > 0) remember(r.s, i.origin, i.seq, r.reject);
		return r;
	}

	std::vector<Effect> coalesce(const std::vector<Effect>& fx)
	{
		int last = -1, lastPlay = -1;
		for (int k = 0; k < (int)fx.size(); k++)
		{

			if (fx[k].kind == Effect::HhLoad || fx[k].kind == Effect::HhUnload) last = k;
			if (fx[k].kind == Effect::HhPlay || fx[k].kind == Effect::HhPause) lastPlay = k;
		}
		std::vector<Effect> out;
		for (int k = 0; k < (int)fx.size(); k++)
		{
			const Effect& e = fx[k];
			bool before = k < last;
			switch (e.kind)
			{
			case Effect::HhLoad: case Effect::HhUnload: if (k != last) continue; break;
			case Effect::HhSeek: case Effect::HhTracks: if (before) continue; break;
			case Effect::HhPlay: case Effect::HhPause: if (before || k != lastPlay) continue; break;
			case Effect::Note: case Effect::PageToast:
				if (std::find(out.begin(), out.end(), e) != out.end()) continue;
				break;
			default: break;
			}
			out.push_back(e);
		}
		return out;
	}

	std::string checkInvariants(const Session& s)
	{
		if (s.out.kind == OutKind::Browser && s.out.tab.empty()) return "I1: a browser output with no tab";
		if (s.pref.kind == OutKind::Browser && s.pref.tab.empty()) return "I1: a browser pref with no tab";
		if (s.index < -1 || s.index >= (int)s.queue.size()) return "I7: index out of the queue";
		for (auto& p : s.queue)
			if (isPhotoFile(p)) return "I7: a photo in the queue";
		if (s.index < 0 && (s.playing || s.parked || !s.error.empty())) return "I7: nothing plays, yet playing/parked/error";
		if (s.pos < 0 || (s.dur > 0 && s.pos > s.dur + 1e-9)) return "I9: pos outside [0, dur]";
		if (s.playing && s.parked && s.out.kind == OutKind::Handheld) return "I9: playing while parked on the handheld";
		if (s.blocked && (!s.playing || s.out.kind != OutKind::Browser || s.index < 0 || !s.loaded))
			return "I11: waiting for a tap, yet not a browser output that should play";
		if (s.ended.index >= 0 && s.index >= 0) return "I12: a finished list kept while an item plays";
		if (s.ended.index >= (int)s.ended.queue.size() || (s.ended.index < 0 && !s.ended.queue.empty()))
			return "I12: the finished list's index out of its queue";
		// I13: Shuffle's hidden order: each place of the queue once, while an item plays with Shuffle on
		if (!s.order.empty() || (s.shuffle && s.index >= 0))
		{
			if (!s.shuffle || s.index < 0 || s.order.size() != s.queue.size()) return "I13: a hidden order that doesn't fit the list";
			std::vector<int> o = s.order;
			std::sort(o.begin(), o.end());
			for (int k = 0; k < (int)o.size(); k++)
				if (o[k] != k) return "I13: the hidden order isn't each place once";
		}
		if (s.seen.size() > SEEN_MAX) return "I10: the seen store over its size";
		return "";
	}

	std::string checkTransition(const Session& old, const Intent& i, const Result& r, int64_t now)
	{
		if (std::string bad = checkInvariants(r.s); !bad.empty()) return bad;
		if (r.dup)
		{
			if (!sameAll(r.s, old) || !(r.s.seen == old.seen)) return "I10: a duplicate changed the session";
			if (!r.fx.empty()) return "I10: a duplicate gave effects";
			return "";
		}
		if (!r.reject.empty())
		{
			if (!sameAll(r.s, old)) return "I4: a rejected intent changed the session";
			for (auto& e : r.fx)
				if (e.kind != Effect::Note && e.kind != Effect::PageToast) return "I4: a rejected intent gave effects";
		}
		bool changed = !samePublished(r.s, old);
		if (changed && r.s.v != old.v + 1) return "I3: a change without one v";
		if (!changed && r.s.v != old.v) return "I3: v moved with nothing changed";
		// the selected output moves only by a choice: SetOutput, an Open that names where (Play here / Play on the
		// handheld), or the timeout of its tab gone (Tick); not by Stop, the end of a list, a photo, a file it can't play
		if (r.s.pref != old.pref && i.type != IntentType::SetOutput && !(i.type == IntentType::Open && i.hasOut) &&
			i.type != IntentType::Tick)
			return std::string("pref: moved by ") + intentName(i.type);
		// the finished list: made only by a list's own end, put away by a Stop
		// (a list ends by itself: its last file's end, or the browser output skipping the rest it can't play)
		if (old.ended.index < 0 && r.s.ended.index >= 0 && i.type != IntentType::Ended && i.type != IntentType::CantPlay && i.type != IntentType::Tick)
			return std::string("I12: a finished list made by ") + intentName(i.type);
		if (i.type == IntentType::Stop && r.reject.empty() && !r.dup && r.s.ended.index >= 0) return "I12: a Stop kept the finished list";
		bool report = i.type == IntentType::Loaded || i.type == IntentType::Pos || i.type == IntentType::Ended ||
			i.type == IntentType::CantPlay || i.type == IntentType::Blocked;
		if (report && r.reject.empty() && (i.origin != outOrigin(old) || i.load != old.load)) return "I8: a report from elsewhere taken";
		for (auto& e : r.fx)
		{
			if (e.kind == Effect::HhPlay && !hhShouldPlay(r.s)) return "I2: the handheld told to play while it isn't the output";
			if (e.kind == Effect::HhLoad && r.s.out.kind != OutKind::Handheld && !e.hold) return "I2: an unheld load for another output";
		}
		(void)now;
		return "";
	}

	std::string describe(const Session& old, const Intent& i, const Result& r)
	{
		char head[96];
		if (r.dup) return std::string(intentName(i.type)) + " dup";
		if (!r.reject.empty()) return std::string(intentName(i.type)) + " rejected (" + r.reject + ")";
		snprintf(head, sizeof head, "%s v%llu\xE2\x86\x92%llu fx=[", intentName(i.type), (unsigned long long)old.v, (unsigned long long)r.s.v);
		std::string out = head;
		for (size_t k = 0; k < r.fx.size(); k++)
		{
			if (k) out += ",";
			out += effectName(r.fx[k].kind);
		}
		return out + "]";
	}

	std::string sessionJson(const Session& s, int64_t now, const std::function<std::string(const std::string&)>& virt)
	{
		JsonWriter j;
		auto out = [&](const char* name, const Output& o) {
			j.key(name).obj().key("kind").str(o.kind == OutKind::Handheld ? "handheld" : "browser").key("tab").str(o.tab).end();
		};
		auto pick = [&](const char* name, const TrackPick& p) {
			j.key(name).obj().key("set").boolean(p.set).key("ord").num(p.ord).key("of").num(p.of).key("lang").str(p.lang)
				.key("label").str(p.label).key("external").boolean(p.external).end();
		};
		j.obj().key("boot").num(s.boot).key("v").num(s.v);
		out("out", s.out);
		out("pref", s.pref);
		j.key("load").num(s.load).key("loaded").boolean(s.loaded).key("index").num(s.index);
		j.key("file").str(s.index >= 0 ? virt(s.queue[s.index]) : "");
		int qindex = -1, n = 0;
		j.key("queue").arr();
		for (int k = 0; k < (int)s.queue.size(); k++)
		{
			std::string v = virt(s.queue[k]);
			if (v.empty()) continue;
			if (k == s.index) qindex = n;
			j.str(v);
			n++;
		}
		j.end().key("qindex").num(qindex);
		j.key("source").obj().key("kind").str(s.source.kind == 2 ? "playlist" : s.source.kind == 1 ? "net" : "folder")
			.key("title").str(s.source.title).key("id").str(s.source.id).end();
		j.key("shuffle").boolean(s.shuffle);
		j.key("playing").boolean(s.playing).key("pos").num(posNow(s, now)).key("dur").num(s.dur).key("speed").num(s.speed);
		pick("audio", s.audio);
		pick("sub", s.sub);
		j.key("playV").num(s.playV).key("seekN").num(s.seekN);
		j.key("parked").boolean(s.parked).key("blocked").boolean(s.blocked).key("error").str(s.error);
		// the list that ended by itself, as the queue is published (its unserved files left out)
		std::string last = s.ended.index >= 0 ? virt(s.ended.queue[s.ended.index]) : "";
		if (last.empty()) j.key("ended").null();
		else
		{
			int at = -1;
			n = 0;
			j.key("ended").obj().key("queue").arr();
			for (int k = 0; k < (int)s.ended.queue.size(); k++)
			{
				std::string v = k == s.ended.index ? last : virt(s.ended.queue[k]);
				if (v.empty()) continue;
				if (k == s.ended.index) at = n;
				j.str(v);
				n++;
			}
			const SessionSource& src = s.ended.source;
			j.end().key("index").num(at).key("source").obj().key("kind").str(src.kind == 2 ? "playlist" : src.kind == 1 ? "net" : "folder")
				.key("title").str(src.title).key("id").str(src.id).end();
			j.key("dur").num(s.ended.dur).key("audio").boolean(s.ended.audioOnly).end();
		}
		return j.end().out();
	}
}
