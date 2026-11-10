// The web dashboard's player routes: play (a file with its folder, a selection, a playlist; at a position, for the
// hand-off from a browser), the transport, volume, the equalizer and effects, "Play in this browser", and the
// snapshot of what plays. Every change runs on the main thread (WebServer::call) and uses Player's asynchronous
// calls only: nothing here waits for mpv.
#include "web/server.h"
#include "json.h"
#include "player.h"
#include "playlist.h"
#include "playlists.h"
#include "photo.h"
#include "substyle.h"
#include "tracks.h"
#include "ui.h"
#include "session_host.h"
#include "webstate.h"
#include <httplib.h>
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <dirent.h>
#include <random>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		// a form's values: "p[]" (the web app's arrays) first, else plain "p"
		std::vector<std::string> values(const httplib::Request& req, const std::string& name)
		{
			std::vector<std::string> out;
			std::string arr = name + "[]";
			size_t n = req.get_param_value_count(arr);
			for (size_t i = 0; i < n; i++) out.push_back(req.get_param_value(arr, i));
			if (out.empty())
			{
				n = req.get_param_value_count(name);
				for (size_t i = 0; i < n; i++) out.push_back(req.get_param_value(name, i));
			}
			return out;
		}

		std::string value(const httplib::Request& req, const std::string& name)
		{
			return req.has_param(name) ? req.get_param_value(name) : "";
		}

		std::string error(const std::string& message, const std::string& code = "")
		{
			JsonWriter j;
			j.obj().key("error").str(message);
			if (!code.empty()) j.key("code").str(code);
			return j.end().out();
		}

		bool isDir(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}

		// a folder's videos and songs, in name order (as the handheld adds a folder to a playlist)
		std::vector<std::string> folderMedia(const std::string& dir)
		{
			std::vector<std::string> names;
			if (DIR* d = opendir(dir.c_str()))
			{
				while (dirent* e = readdir(d)) names.push_back(e->d_name);
				closedir(d);
			}
			int photos = 0;
			std::vector<std::string> out;
			for (auto& e : folderEntries(dir, names, photos))
				if (!isDir(e.path)) out.push_back(e.path);
			return out;
		}

		std::string baseOf(const std::string& p)
		{
			size_t s = p.rfind('/');
			return s == std::string::npos ? p : p.substr(s + 1);
		}

		// a playlist's id: its file's name less ".m3u"
		std::string playlistId(const std::string& file)
		{
			std::string b = baseOf(file);
			if (b.size() > 4 && b.compare(b.size() - 4, 4, ".m3u") == 0) b.resize(b.size() - 4);
			return b;
		}

		// the request's paths made real (on the server's thread: the checks touch the card); false: answered
		bool resolveAll(WebServer& ws, const std::vector<std::string>& in, std::vector<std::string>& out, httplib::Response& res)
		{
			for (auto& v : in)
			{
				std::string real;
				if (!ws.resolve(v, real, false, res)) return false;
				out.push_back(real);
			}
			return true;
		}

		void answer(httplib::Response& res, const std::string& body, int status)
		{
			WebServer::json(res, body, status);
		}
	}

	// Ui's insides the routes need (a friend of Ui's), all on the main thread
	struct WebPlayerGlue
	{
		// where the file just opened starts: a seek still waiting for it (applied once it knows its length), else its place
		static double openPos(Player& p, Ui& u)
		{
			return u.seekPath == p.currentPath() && SDL_GetTicks() < u.seekUntil && u.seekTo >= 0 ? u.seekTo : p.position;
		}

		// the source's queue as it plays: its files and where the one playing is among them
		static std::vector<std::string> queue(Player& p, Ui& u, int& index)
		{
			index = 0;
			if (!p.playing()) return {};
			// the session's list when it has this file: the order it plays in (shuffled or not)
			const Session& ss = SessionHost::get().session();
			if (ss.index >= 0 && !p.playingPhoto() && ss.queue[ss.index] == p.currentPath())
			{
				index = ss.index;
				return ss.queue;
			}
			const PlaySource& src = p.source();
			if (src.kind == SourceKind::Playlist && !src.id.empty())
			{
				Playlist pl;
				int skipped = 0;
				if (!u.plStore || !u.plStore->load(src.id, pl, skipped)) return {p.currentPath()};
				std::vector<std::string> out;
				if (u.plOrderId == src.id && (int)u.plOrder.size() == p.count())
				{
					for (int i : u.plOrder)
						if (i >= 0 && i < (int)pl.entries.size()) out.push_back(pl.entries[i].path);
				}
				else
					for (auto& e : pl.entries)
						if (!isPhotoFile(e.path)) out.push_back(e.path);
				index = p.index();
				if (index >= (int)out.size() || out[index] != p.currentPath())
				{
					auto it = std::find(out.begin(), out.end(), p.currentPath());
					index = it == out.end() ? 0 : (int)(it - out.begin());
				}
				return out;
			}
			if (src.kind == SourceKind::Playlist)
			{
				// a web selection (or a shuffled folder)
				auto it = std::find(u.webSelection.begin(), u.webSelection.end(), p.currentPath());
				if (it == u.webSelection.end()) return {p.currentPath()};
				index = (int)(it - u.webSelection.begin());
				return u.webSelection;
			}
			if (src.kind == SourceKind::Folder)
			{
				int i = 0;
				std::vector<std::string> out = siblingVideos(p.currentPath(), i);
				index = i;
				return out;
			}
			return {p.currentPath()};
		}

		static WebSnapshot snapshot(WebServer& ws, Player& p, Ui& u)
		{
			WebSnapshot s;
			// (a photo is the photo viewer's, never this player's: what plays under it is what the page sees)
			if (p.playing())
			{
				std::string path = p.currentPath();
				s.path = ws.toVirtual(path);
				s.title = webTitle(path, p.title);
				s.artist = p.artist();
				s.album = p.album();
				s.kind = p.playingPhoto() ? "photo" : p.playingAudio() ? "audio" : "video";
				// the handheld's bottom screen: its player, or the browser with this playing behind it
				s.screen = u.playerShown(p) ? "player" : "browser";
				const PlaySource& src = p.source();
				s.sourceKind = src.kind == SourceKind::Playlist ? (src.id.empty() ? "selection" : "playlist") : "folder";
				s.sourceTitle = src.title;
				s.sourceId = src.kind == SourceKind::Playlist ? (src.id.empty() ? "" : playlistId(src.id)) : ws.toVirtual(src.id);
				s.liked = u.plStore && u.plStore->isLiked(path);
				s.index = p.index();
				s.count = p.count();
				s.loading = p.loading();
				s.paused = p.paused;
				s.position = std::max(0.0, p.position);
				s.duration = std::max(0.0, p.duration);
				s.speed = p.speed;
				s.shuffled = p.settings.shuffle;	// (the setting: the page shows it)
				// "Chapter 4", not mpv's "(unnamed)" (as the handheld's list says it)
				for (size_t i = 0; i < p.chapters.size(); i++) s.chapters.push_back({chapterLabel(p.chapters[i], (int)i + 1), p.chapters[i].time});
				s.chapter = p.chapters.empty() ? -1 : p.currentChapter();
				// a video's audio and subtitles, from the track list mpv already sent (nothing asked of it here)
				if (s.kind == "video")
				{
					s.hasTracks = true;
					auto add = [&](TrackType type, std::vector<WebTrack>& out, int64_t& chosen) {
						auto list = p.tracksOf(type);
						for (size_t i = 0; i < list.size(); i++)
						{
							const Track& t = list[i];
							out.push_back({t.id, trackLabel(t, (int)i + 1), t.selected, t.isDefault, t.forced, t.external, t.lang});
							if (t.selected) chosen = t.id;
						}
					};
					add(TrackType::Audio, s.audioTracks, s.aid);
					add(TrackType::Sub, s.subTracks, s.sid);
				}
			}
			s.positionAtMs = SDL_GetTicks();
			s.volume = u.volumeLevel();
			s.eqOn = p.settings.eqOn;
			s.eqPreset = p.settings.eqPreset;
			for (int i = 0; i < EQ_BANDS; i++) s.eqGains[i] = p.settings.eqGains[i];
			s.fx = soundFxKey(p.settings.soundFx);
			s.loop = p.settings.loop == LOOP_ALL ? "all" : p.settings.loop == LOOP_ONE ? "one" : "no";
			s.shuffleMode = p.settings.shuffle;
			const SessionHost& sh = SessionHost::get();
			s.sessionBoot = sh.boot();
			s.sessionV = sh.version();
			s.session = sessionJson(sh.session(), SDL_GetTicks(), [&ws](const std::string& path) { return ws.servedVirtual(path); });
			s.clients = ws.clients();
			s.playlistSerial = u.playlistSerial();
			s.showHiddenFolders = p.settings.showHiddenFolders;
			s.mangaOn = p.settings.mangaOn;
			return s;
		}

		// the snapshot as the page gets it, with the battery (for the desktop's sidebar)
		static std::string snapshotJson(const WebSnapshot& s, const Ui& u)
		{
			std::string j = toJson(s);
			if (!j.empty() && j.back() == '}') j.insert(j.size() - 1, ",\"battery\":" + std::to_string(u.battery.percent));
			return j;
		}

		// a position to go to once the file that opens (or is open) knows its length
		static void seekWhenOpen(Player& p, Ui& u, const std::string& path, double t)
		{
			if (t <= 0) return;
			if (p.currentPath() == path && p.loaded && p.duration > 0)
			{
				p.seekFraction(std::clamp(t / p.duration, 0.0, 1.0));
				return;
			}
			u.seekPath = path;
			u.seekTo = t;
			u.seekUntil = SDL_GetTicks() + 5000;
		}

		// what plays already, asked for again: on the handheld, there and playing. While a page is the output it plays on
		// there (it is the selected output), unless this is an old page's hand-off of its own file (handBack: /api/play
		// with t), which makes the handheld the output (the Session's SetOutput)
		static void playsAlready(Player& p, Ui& u, double t, bool handBack)
		{
			if (u.pageIsOut())
			{
				if (handBack) SessionHost::get().takeOver(t > 0 ? t : -1);
				return;
			}
			if (t > 0) seekWhenOpen(p, u, p.currentPath(), t);
			if (p.paused)
			{
				SessionHost::get().play(true, "web");
				p.unpause();
			}
			u.showPlayer();
		}

		// a new pick to play on the handheld, chosen so on a page: the handheld the selected output first (its pick then
		// opens there; a page that played stops)
		static void handheldChosen(Player& p, Ui& u, const std::string& tab)
		{
			(void)p;
			(void)u;
			if (SessionHost::get().prefIsPage() || SessionHost::get().pageIsOutput())
				SessionHost::get().chooseOutput(handheldOut(), tab.empty() ? "web" : tab);
		}

		// one file with the files beside it, as the browser opens it (at t: an old page's hand-off); already open: there,
		// playing. Opened as the handheld's own picks are (main, as the loop comes round): on the selected output
		static void openFile(Player& p, Ui& u, const std::string& path, double t, bool handBack)
		{
			if (p.playing() && p.currentPath() == path)
			{
				playsAlready(p, u, t, handBack);
				return;
			}
			u.flushPlaylist(p);
			int index = 0;
			std::vector<std::string> list = siblingVideos(path, index);
			u.playRequest = {};
			u.playRequest.paths = list;
			u.playRequest.index = index;
			if (t > 0) seekWhenOpen(p, u, path, t);
		}

		static void openSelection(Player& p, Ui& u, std::vector<std::string> paths, const std::string& title, bool shuffle)
		{
			u.flushPlaylist(p);
			PlaySource src;
			src.kind = SourceKind::Playlist;
			src.title = title;
			u.webSelection = paths;
			u.playRequest = {};
			u.playRequest.paths = paths;
			u.playRequest.source = src;
			if (shuffle && !paths.empty())
			{
				// Shuffle play: Shuffle on (it lasts), from a random one; the list keeps its order
				std::random_device rd;
				u.playRequest.index = (int)(rd() % (unsigned)paths.size());
				SessionHost::get().setShuffle(true, "web");
			}
		}

		// a saved playlist from item index (shuffled: from a random one), at t if given
		static std::string playPlaylist(Player& p, Ui& u, const std::string& id, int index, bool shuffle, double t, bool handBack = false)
		{
			std::string file = u.plWebFile(id);
			if (file.empty())
			{
				WebCall::setStatus(404);
				return error("No such playlist");
			}
			u.plFlush(p);
			Playlist pl;
			int skipped = 0;
			if (!u.plStore->load(file, pl, skipped))
			{
				WebCall::setStatus(500);
				return error("The playlist couldn't be read");
			}
			if (pl.entries.empty())
			{
				WebCall::setStatus(409);
				return error(pl.name + " is empty");
			}
			index = std::clamp(index, 0, (int)pl.entries.size() - 1);
			// that item of it already playing (the hand-off back): only the position
			if (!shuffle && p.playing() && p.source().kind == SourceKind::Playlist && p.source().id == file &&
				p.currentPath() == pl.entries[index].path)
			{
				playsAlready(p, u, t, handBack);
				return "{\"ok\":true}";
			}
			// (main opens it as the loop comes round: Ui's play request, as the browser's own Play)
			u.plPlay(pl, index, shuffle, p);
			if (t > 0 && !shuffle) seekWhenOpen(p, u, pl.entries[index].path, t);
			return "{\"ok\":true}";
		}

		static std::string eqJson(const Player& p)
		{
			JsonWriter j;
			j.obj().key("on").boolean(p.settings.eqOn).key("preset").str(p.settings.eqPreset).key("gains").arr();
			for (double g : p.settings.eqGains) j.num(g);
			return j.end().end().out();
		}

		static void setVolume(Player& p, Ui& u, int v)
		{
			// as the quick settings do it (BananaOS's script, on its thread), and its card over the top panel
			u.volumePct = v;
			if (u.volume.present()) u.volume.set(v);
			p.showLevel("Volume", v);
			u.levelMs = 1500;
		}

		static void eqGains(Player& p, Ui& u, const EqGains& g)
		{
			Settings s = p.settings;
			if (s.eqGains == g && s.eqOn) return;
			// the first edit of a preset: Custom, made from it (as a band moved on the handheld)
			if (!u.eqCustom(p))
			{
				s.eqOrigin = s.eqPreset;
				s.eqPreset.clear();
				u.stashOn = false;
			}
			s.eqOn = true;
			s.eqGains = g;
			// heard at once; written a second after the last change (tick)
			p.setSettings(s, false);
			u.eqWebDirtyAt = SDL_GetTicks();
		}

		static std::string subStyleJson(const Player& p)
		{
			JsonWriter j;
			j.obj().key("settings").arr();
			const auto& all = subStyleSettings();
			for (size_t k = 0; k < all.size(); k++)
			{
				const StyleSetting& st = all[k];
				j.obj().key("key").str(st.key).key("label").str(st.label).key("choices").arr();
				for (auto& c : st.choices) j.obj().key("id").str(c.id).key("label").str(c.label).end();
				int pick = p.settings.style.pick[k];
				j.end().key("value").str(pick >= 0 && pick < (int)st.choices.size() ? st.choices[pick].id : "").end();
			}
			j.end();
			const Track* sub = p.selected(TrackType::Sub);
			j.key("styled").boolean(sub && (sub->codec == "ass" || sub->codec == "ssa"));
			return j.end().out();
		}

		static bool stopIfPlaying(Player& p, Ui& u, const std::string& real)
		{
			// the session's item (or a folder it's in): stopped everywhere; else a photo here, stopped as the web's Stop
			if (!real.empty() && SessionHost::get().fileGone(real)) return true;
			if (!p.playing() || real.empty()) return false;
			const std::string& cur = p.currentPath();
			std::string dir = real.back() == '/' ? real : real + "/";
			if (cur != real && cur.compare(0, dir.size(), dir) != 0) return false;
			// as the web's Stop: main stops it as the loop comes round (back to the browser if the player showed)
			u.stopRequest = true;
			return true;
		}

		static void routes(WebServer& ws);
	};

	WebSnapshot webSnapshot(WebServer& ws, Player& p, Ui& u) { return WebPlayerGlue::snapshot(ws, p, u); }
	std::string webSnapshotJson(const WebSnapshot& s, const Ui& u) { return WebPlayerGlue::snapshotJson(s, u); }

	// (a member of the glue: its handlers reach Ui's insides too)
	void WebPlayerGlue::routes(WebServer& ws)
	{
		httplib::Server& s = ws.http();
		WebServer* w = &ws;

		// play: p (a file with its folder, or a folder's media), p[] (several: a list of their own; a folder's media in
		// its place), playlist + index; shuffle; t (seconds: the hand-off from a browser)
		auto play = [w](const httplib::Request& req, httplib::Response& res) {
			double t = atof(value(req, "t").c_str());
			// (t: an old page's hand-off of its own file; its aid/sid are no longer read: the tracks are the session's)
			bool handBack = req.has_param("t") || value(req, "to") == "handheld";
			// to=handheld: the page's explicit "Play on handheld" (else a pick plays on the selected output)
			bool toHandheld = value(req, "to") == "handheld";
			std::string tab = value(req, "tab");
			bool shuffle = value(req, "shuffle") == "1" || value(req, "shuffle") == "true";
			if (req.has_param("playlist"))
			{
				std::string id = value(req, "playlist");
				int index = atoi(value(req, "index").c_str());
				int status = 200;
				std::string body = w->call([=](Player& p, Ui& u) {
					if (toHandheld) WebPlayerGlue::handheldChosen(p, u, tab);
					return WebPlayerGlue::playPlaylist(p, u, id, index, shuffle, t, handBack);
				}, 2000, status);
				answer(res, body, status);
				return;
			}
			std::vector<std::string> real;
			if (!resolveAll(*w, values(req, "p"), real, res)) return;
			if (real.empty())
			{
				WebServer::fail(res, 400, "Nothing to play");
				return;
			}
			// one file: it and the files beside it, as the handheld's browser opens it
			if (real.size() == 1 && !isDir(real[0]))
			{
				std::string path = real[0];
				if (!isMediaFile(path))
				{
					answer(res, error("That isn't something the player plays", "type"), 415);
					return;
				}
				int status = 200;
				std::string body = w->call([=](Player& p, Ui& u) {
					if (toHandheld && !(p.playing() && p.currentPath() == path)) WebPlayerGlue::handheldChosen(p, u, tab);
					WebPlayerGlue::openFile(p, u, path, t, handBack);
					return std::string("{\"ok\":true}");
				}, 2000, status);
				answer(res, body, status);
				return;
			}
			// a folder, or several: their media in order (a folder's own, not its folders'); the card read here
			std::vector<std::string> list;
			for (auto& r : real)
			{
				if (isDir(r))
					for (auto& m : folderMedia(r)) list.push_back(m);
				else if (isVideoFile(r) || isAudioFile(r)) list.push_back(r);
			}
			if (list.empty())
			{
				answer(res, error("Nothing to play there", "type"), 404);
				return;
			}
			// a single folder plays as the handheld plays a folder (its first file, the rest after); with shuffle, or
			// several, as a list of their own
			bool oneFolder = real.size() == 1 && !shuffle;
			std::string title = real.size() == 1 ? baseOf(real[0]) : "Selection";
			int status = 200;
			std::string body = w->call([=](Player& p, Ui& u) {
				if (toHandheld) WebPlayerGlue::handheldChosen(p, u, tab);
				if (oneFolder) WebPlayerGlue::openFile(p, u, list[0], 0, false);
				else WebPlayerGlue::openSelection(p, u, list, title, shuffle);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		};
		s.Post("/api/play", play);

		// the transport
		s.Post("/api/cmd", [w](const httplib::Request& req, httplib::Response& res) {
			std::string c = value(req, "c");
			double t = atof(value(req, "t").c_str()), d = atof(value(req, "d").c_str());
			int dir = atoi(value(req, "dir").c_str()), i = atoi(value(req, "i").c_str());
			std::string v = value(req, "v");
			static const char* KNOWN[] = {"toggle", "pause", "play", "next", "prev", "stop", "seek", "seekrel", "chapter",
				"chapterto", "loop", "shuffle"};
			if (std::find_if(std::begin(KNOWN), std::end(KNOWN), [&](const char* k) { return c == k; }) == std::end(KNOWN))
			{
				WebServer::fail(res, 400, "Unknown command");
				return;
			}
			int status = 200;
			std::string body = w->call([=](Player& p, Ui& u) -> std::string {
				if (c == "loop")
				{
					int loop = v == "all" ? LOOP_ALL : v == "one" ? LOOP_ONE : v == "no" || v == "off" ? LOOP_OFF : -1;
					if (loop < 0)
					{
						WebCall::setStatus(400);
						return error("Loop is no, all or one");
					}
					// (the setting, which the session follows; Shuffle stays as it is)
					Settings st = p.settings;
					st.loop = loop;
					p.setSettings(st);
					if (p.playingAudio()) p.osd(u.modeLabel(p));
					return "{\"ok\":true}";
				}
				if (c == "shuffle")
				{
					// Shuffle on or off (v=on|off, else the other): the setting (nothing playing too), Loop as it is.
					// What plays plays on; Next follows a hidden random order of the list (or its own again)
					SessionHost& sh = SessionHost::get();
					bool on = v == "on" || v == "1" ? true : v == "off" || v == "0" ? false : !sh.shuffleMode();
					sh.setShuffle(on, "web");
					if (p.playingAudio()) p.osd(u.modeLabel(p));
					return "{\"ok\":true}";
				}
				if (!p.playing())
				{
					WebCall::setStatus(409);
					return error("Nothing is playing on the handheld", "playing");
				}
				// a page is the output (another tab, this one a remote): Play / Pause / seeks are the session's, which the
				// page follows; Stop stops everywhere (the handheld's Stop)
				SessionHost& sh = SessionHost::get();
				if (u.pageIsOut())
				{
					// (the page plays, pauses and seeks as the session says; the copy here stays held)
					if (c == "toggle" || c == "play" || c == "pause")
					{
						sh.playLive(c == "toggle" ? !sh.session().playing : c == "play", "web");
						return "{\"ok\":true}";
					}
					if (c == "seek" || c == "seekrel") { sh.seekLive(c == "seek" ? t : d, c == "seekrel", "web"); return "{\"ok\":true}"; }
					if (c == "chapter" || c == "chapterto")
					{
						int k = c == "chapterto" ? i : u.shownChapter(p) + (dir < 0 ? -1 : 1);
						if (k >= 0 && k < (int)p.chapters.size()) sh.seekLive(p.chapters[k].time, false, "web");
						return "{\"ok\":true}";
					}
					if (c == "stop") { sh.stop("web"); return "{\"ok\":true}"; }
				}
				// (the Session follows: the same, as intents from the web)
				// Next / Previous: the session's Step (the handheld follows in its list, or the page plays the next)
				if ((c == "next" || c == "prev") && sh.stepsHere(p))
				{
					sh.step(c == "next" ? 1 : -1, "web");
					return "{\"ok\":true}";
				}
				if (c == "toggle") sh.play(p.paused, "web");
				else if (c == "pause" || c == "play") sh.play(c == "play", "web");
				else if (c == "seek" || c == "seekrel") sh.seek(c == "seek" ? t : d, c == "seekrel", "web");
				if (c == "toggle") p.togglePause();
				else if (c == "pause") { if (!p.paused) p.togglePause(); }
				else if (c == "play") { if (p.paused) p.unpause(); }
				else if (c == "next") { if (!p.next()) p.osd("This is the last one"); }
				else if (c == "prev") { if (!p.back()) p.osd("This is the first one"); }
				else if (c == "stop") { if (!sh.stop("web").reject.empty()) u.stopRequest = true; }
				else if (c == "seek") { if (p.duration > 0) p.seekFraction(std::clamp(t / p.duration, 0.0, 1.0)); }
				else if (c == "seekrel") p.seekRelative(d);
				else if (c == "chapter") p.stepChapter(dir < 0 ? -1 : 1);
				else if (c == "chapterto")
				{
					if (i < 0 || i >= (int)p.chapters.size())
					{
						WebCall::setStatus(400);
						return error("No such chapter");
					}
					p.seekChapter(i);
					p.showChapterCard(i);
				}
				return "{\"ok\":true}";
			}, 2000, status);
			answer(res, body, status);
		});

		// the source's queue: Up next, and "Continue here" in a browser
		s.Get("/api/source", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([w](Player& p, Ui& u) {
				int index = 0;
				std::vector<std::string> q = WebPlayerGlue::queue(p, u, index);
				JsonWriter j;
				j.obj().key("items").arr();
				// the one playing by its tag; the others (their tags unread: the card isn't probed for a queue) by name
				std::string cur = p.playing() ? p.currentPath() : "";
				for (auto& path : q)
					j.obj().key("p").str(w->toVirtual(path)).key("title").str(path == cur ? webTitle(path, p.title) : webTitle(path)).end();
				j.end().key("index").num(index);
				return j.end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// key=showHiddenFolders value=0|1: the handheld's Settings toggle (saved to settings.conf)
		// key=manga-reader value=0|1: the manga reader's switch (the dashboard's Manage extensions card)
		s.Post("/api/setting", [w](const httplib::Request& req, httplib::Response& res) {
			std::string key = value(req, "key"), v = value(req, "value");
			if (key != "showHiddenFolders" && key != "manga-reader")
			{
				WebServer::fail(res, 400, "Unknown setting");
				return;
			}
			if (v != "0" && v != "1")
			{
				WebServer::fail(res, 400, "value is 0 or 1");
				return;
			}
			bool on = v == "1";
			int status = 200;
			std::string body = w->call([key, on](Player& p, Ui& u) {
				// the manga reader: its switch only (the handheld applies it: its places come and go)
				if (key == "manga-reader")
				{
					if (p.settings.mangaOn != on)
					{
						Settings st = p.settings;
						st.mangaOn = on;
						p.setSettings(st);
					}
					return std::string("{\"mangaOn\":") + (on ? "true" : "false") + "}";
				}
				if (p.settings.showHiddenFolders != on)
				{
					Settings st = p.settings;
					st.showHiddenFolders = on;
					p.setSettings(st);
					// the browser re-lists where it stands (a hidden folder in or out)
					u.folderChanged(u.browseFolder(), p);
				}
				return std::string("{\"showHiddenFolders\":") + (on ? "true" : "false") + "}";
			}, 2000, status);
			answer(res, body, status);
		});

		s.Post("/api/volume", [w](const httplib::Request& req, httplib::Response& res) {
			if (!req.has_param("v"))
			{
				WebServer::fail(res, 400, "No volume");
				return;
			}
			int v = std::clamp(atoi(value(req, "v").c_str()), 0, 100);
			int status = 200;
			std::string body = w->call([v](Player& p, Ui& u) {
				WebPlayerGlue::setVolume(p, u, v);
				return "{\"volume\":" + std::to_string(v) + "}";
			}, 2000, status);
			answer(res, body, status);
		});

		// the presets: the built-ins, then yours
		s.Get("/api/eq", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([](Player& p, Ui&) {
				JsonWriter j;
				j.obj().key("presets").arr();
				for (auto& e : p.eqPresets())
				{
					j.obj().key("name").str(e.name).key("gains").arr();
					for (double g : e.gains) j.num(g);
					j.end().key("builtin").boolean(e.builtin).end();
				}
				return j.end().end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// on=0|1, preset=<name> ("" or Custom: Custom back), gains=<ten dB values>
		s.Post("/api/eq", [w](const httplib::Request& req, httplib::Response& res) {
			bool hasOn = req.has_param("on"), hasPreset = req.has_param("preset"), hasGains = req.has_param("gains");
			bool on = value(req, "on") == "1" || value(req, "on") == "true";
			std::string preset = value(req, "preset");
			EqGains g{};
			if (hasGains && !parseEqGains(value(req, "gains"), g))
			{
				WebServer::fail(res, 400, "The equalizer needs ten values");
				return;
			}
			int status = 200;
			std::string body = w->call([=](Player& p, Ui& u) {
				if (hasPreset)
				{
					std::string name = preset.empty() ? "Custom" : preset;
					bool known = name == "Custom";
					for (auto& e : p.eqPresets()) known = known || e.name == name;
					if (!known)
					{
						WebCall::setStatus(404);
						return error("No preset called " + preset, "name");
					}
					u.eqPick(name, p);
				}
				if (hasGains) WebPlayerGlue::eqGains(p, u, g);
				if (hasOn && on != p.settings.eqOn) u.eqToggle(p);
				return WebPlayerGlue::eqJson(p);
			}, 2000, status);
			answer(res, body, status);
		});

		s.Post("/api/fx", [w](const httplib::Request& req, httplib::Response& res) {
			std::string key = value(req, "key");
			SoundFx fx = soundFxFromKey(key);
			if (key != soundFxKey(fx))
			{
				WebServer::fail(res, 400, "No such effect");
				return;
			}
			int status = 200;
			std::string body = w->call([fx](Player& p, Ui& u) {
				u.fxChoose((int)fx, p);
				return "{\"fx\":\"" + std::string(soundFxKey(fx)) + "\"}";
			}, 2000, status);
			answer(res, body, status);
		});

		// a video's audio track or subtitles on the handheld: kind=audio|sub, id (mpv's, as the snapshot's tracks have
		// it; sub 0: off). As its Audio & Subtitles page chooses (the language remembered, the top panel says it)
		s.Post("/api/track", [w](const httplib::Request& req, httplib::Response& res) {
			std::string kind = value(req, "kind");
			if ((kind != "audio" && kind != "sub") || !req.has_param("id"))
			{
				WebServer::fail(res, 400, "kind is audio or sub, with an id");
				return;
			}
			int64_t id = atoll(value(req, "id").c_str());
			TrackType type = kind == "audio" ? TrackType::Audio : TrackType::Sub;
			int status = 200;
			std::string body = w->call([=](Player& p, Ui& u) -> std::string {
				if (!p.playing() || p.playingAudio() || p.playingPhoto())
				{
					WebCall::setStatus(409);
					return error("No video is playing on the handheld", "playing");
				}
				if (!u.chooseTrack(p, type, id))
				{
					WebCall::setStatus(404);
					return error(type == TrackType::Audio ? "No such audio track" : "No such subtitles", "track");
				}
				return JsonWriter().obj().key("ok").boolean(true).key("kind").str(kind).key("id").num(id).end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// how plain subtitles look (Settings › Subtitles on the handheld): the settings with their choices and what's
		// chosen; styled: the subtitles showing are ASS/SSA, which keep their own look
		s.Get("/api/substyle", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([](Player& p, Ui&) { return WebPlayerGlue::subStyleJson(p); }, 2000, status);
			answer(res, body, status);
		});

		// key (font, size, color, border, border-color) = value (a choice's id): kept in the settings and on screen at
		// once, as the handheld's own setting
		s.Post("/api/substyle", [w](const httplib::Request& req, httplib::Response& res) {
			std::string key = value(req, "key"), v = value(req, "value");
			const auto& all = subStyleSettings();
			int k = -1, c = -1;
			for (size_t i = 0; i < all.size() && k < 0; i++)
				if (key == all[i].key)
				{
					k = (int)i;
					for (size_t n = 0; n < all[i].choices.size() && c < 0; n++)
						if (v == all[i].choices[n].id) c = (int)n;
				}
			if (k < 0 || c < 0)
			{
				WebServer::fail(res, 400, k < 0 ? "No such subtitle setting" : "No such choice");
				return;
			}
			int status = 200;
			std::string body = w->call([k, c](Player& p, Ui&) {
				if (p.settings.style.pick[k] != c)
				{
					Settings st = p.settings;
					st.style.pick[k] = c;
					p.setSettings(st);
				}
				return WebPlayerGlue::subStyleJson(p);
			}, 2000, status);
			answer(res, body, status);
		});

		// a page's intent (docs/design/session.md §3): type, tab (its origin), seq, basis, and the type's fields.
		// Open (q[]: the queue's virtual paths, index, t, skind folder|playlist, stitle, sid; to=browser|handheld when
		// the pick says where), Step (n), SetOutput (to=handheld|browser, at), SetAudio / SetSub (a pick: ord, of, lang,
		// label, external; off=1 subtitles off), and the reports Loaded (load, dur), Ended (load), CantPlay (load,
		// reason) and Blocked (load, t: its play() refused without a tap; it waits for one). The answer:
		// {ok|rejected|dup, reason, v, session}
		s.Post("/api/intent", [w](const httplib::Request& req, httplib::Response& res) {
			std::string type = value(req, "type"), tab = value(req, "tab");
			static const std::pair<const char*, IntentType> TYPES[] = {{"Open", IntentType::Open}, {"Step", IntentType::Step},
				{"Ended", IntentType::Ended}, {"Pos", IntentType::Pos}, {"Play", IntentType::Play}, {"Pause", IntentType::Pause},
				{"Stop", IntentType::Stop}, {"SetOutput", IntentType::SetOutput},
				{"SetAudio", IntentType::SetAudio}, {"SetSub", IntentType::SetSub}, {"Loaded", IntentType::Loaded},
				{"CantPlay", IntentType::CantPlay}, {"Blocked", IntentType::Blocked}, {"SetShuffle", IntentType::SetShuffle}};
			Intent i;
			bool known = false;
			for (auto& t : TYPES)
				if (type == t.first) { i.type = t.second; known = true; }
			if (!known || tab.empty() || tab == "hh" || tab == "web")
			{
				WebServer::fail(res, 400, known ? "No tab" : "Unknown intent");
				return;
			}
			i.origin = tab;
			i.seq = strtoull(value(req, "seq").c_str(), nullptr, 10);
			i.basis = strtoull(value(req, "basis").c_str(), nullptr, 10);
			i.load = strtoull(value(req, "load").c_str(), nullptr, 10);
			i.dur = atof(value(req, "dur").c_str());
			// (Loaded: a song, which Loop all goes round; a video plays its list once, as on the handheld)
			i.audioOnly = value(req, "audio") == "1";
			i.reason = value(req, "reason");
			if (i.type == IntentType::Pos)
			{
				i.t = std::max(0.0, atof(value(req, "t").c_str()));
				i.playing = value(req, "playing") == "1";
			}
			if (i.type == IntentType::Blocked && req.has_param("t"))
			{
				i.hasT = true;
				i.t = std::max(0.0, atof(value(req, "t").c_str()));
			}
			if (i.type == IntentType::Open)
			{
				if (!resolveAll(*w, values(req, "q"), i.queue, res)) return;
				i.index = atoi(value(req, "index").c_str());
				i.t = std::max(0.0, atof(value(req, "t").c_str()));
				std::string kind = value(req, "skind");
				i.source.kind = kind == "playlist" || kind == "selection" ? 2 : 0;
				i.source.title = value(req, "stitle");
				i.source.id = value(req, "sid");
				std::string to = value(req, "to");
				if (to == "handheld" || to == "browser")
				{
					i.hasOut = true;
					i.out = to == "handheld" ? handheldOut() : browserOut(tab);
				}
			}
			if (i.type == IntentType::SetShuffle) i.on = value(req, "on") == "1";
			if (i.type == IntentType::Step) i.n = std::clamp(atoi(value(req, "n").c_str()), -1000, 1000);
			if (i.type == IntentType::SetOutput)
			{
				std::string to = value(req, "to");
				if (to != "handheld" && to != "browser")
				{
					WebServer::fail(res, 400, "to is handheld or browser");
					return;
				}
				i.hasOut = true;
				i.out = to == "handheld" ? handheldOut() : browserOut(tab);
				if (req.has_param("at"))
				{
					i.hasT = true;
					i.t = std::max(0.0, atof(value(req, "at").c_str()));
				}
			}
			if (i.type == IntentType::SetAudio || i.type == IntentType::SetSub)
			{
				TrackPick k;
				k.set = true;
				k.ord = value(req, "off") == "1" ? -1 : atoi(value(req, "ord").c_str());
				k.of = atoi(value(req, "of").c_str());
				k.lang = value(req, "lang");
				k.label = value(req, "label");
				k.external = value(req, "external") == "1" || value(req, "external") == "true";
				(i.type == IntentType::SetAudio ? i.audio : i.sub) = k;
			}
			int status = 200;
			std::string body = w->call([w, i](Player&, Ui&) {
				SessionHost& sh = SessionHost::get();
				Result r = sh.fromPage(i);
				JsonWriter j;
				j.obj();
				if (r.dup) j.key("dup").boolean(true);
				if (!r.reject.empty()) j.key("rejected").boolean(true).key("reason").str(r.reject);
				else j.key("ok").boolean(true);
				j.key("v").num(sh.version());
				j.key("session").raw(sessionJson(sh.session(), SDL_GetTicks(), [w](const std::string& path) { return w->servedVirtual(path); }));
				return j.end().out();
			}, 2000, status);
			answer(res, body, status);
		});
	}

	void registerPlayerRoutes(WebServer& ws) { WebPlayerGlue::routes(ws); }

	bool stopIfPlaying(WebServer& ws, const std::string& real)
	{
		int status = 200;
		std::string body = ws.call([real](Player& p, Ui& u) {
			return std::string(WebPlayerGlue::stopIfPlaying(p, u, real) ? "{\"stopped\":true}" : "{\"stopped\":false}");
		}, 2000, status);
		return status == 200 && body.find("true") != std::string::npos;
	}
}
