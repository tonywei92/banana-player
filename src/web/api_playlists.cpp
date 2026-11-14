// The web dashboard's playlist routes: the saved playlists, one's items, and their edits. The store is the bottom
// panel's (Ui's), so everything runs on the main thread (WebServer::call); the card is read here only for what the
// web adds (a folder's media) and for whether an item is still there, on the server's thread.
//
// Every edit carries the rev the page last read (FNV-1a of the playlist as it's written): another edit since (on the
// handheld, in another browser) answers 409 with code "rev" and the rev now, and the page reads it again.
#include "pagebook.h"
#include "web/server.h"
#include "json.h"
#include "browser.h"
#include "netpath.h"
#include "player.h"
#include "playlist.h"
#include "playlists.h"
#include "photo.h"
#include "ui.h"
#include "webstate.h"
#include <httplib.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
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

		std::string fnvHex(const std::string& s)
		{
			uint64_t h = 1469598103934665603ull;
			for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
			char buf[17];
			snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
			return buf;
		}

		std::string revOf(const Playlist& pl) { return fnvHex(serializeM3u(pl)); }

		std::string idOf(const std::string& file)
		{
			size_t s = file.rfind('/');
			std::string b = s == std::string::npos ? file : file.substr(s + 1);
			if (b.size() > 4 && b.compare(b.size() - 4, 4, ".m3u") == 0) b.resize(b.size() - 4);
			return b;
		}

		bool isDir(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}

		std::string nameError(PlNameError e)
		{
			switch (e)
			{
			case PlNameError::Empty: return "Give it a name";
			case PlNameError::TooLong: return "Names can be 40 characters at most";
			case PlNameError::BadChar: return "Names can't contain /";
			case PlNameError::Taken: return "A playlist with that name already exists";
			default: return "That name can't be used";
			}
		}

		void answer(httplib::Response& res, const std::string& body, int status) { WebServer::json(res, body, status); }
	}

	struct WebPlaylistGlue
	{
		// a Recent row's place on the cards or the network ("Videos", "MY-PC"), as the handheld's Home says it
		static std::string where(Ui& u, const std::string& path)
		{
			if (isNetPath(path))
			{
				NetPath np;
				const NetServer* sv = u.netStore && parseNetPath(path, np) ? u.netStore->find(np.server) : nullptr;
				return sv ? serverName(*sv) : "Network";
			}
			std::string w = u.whereOf(parentFolder(path));
			return w.substr(0, w.find(" / "));
		}
		// a document's place, from the handheld's own (a PDF's or a comic's page of so many; a text's or a book's share)
		struct DocPlace { bool paged = false; int page = 0, pages = 0; double frac = 0; bool started = false; };
		static DocPlace docPlace(Ui& u, const std::string& path)
		{
			DocPlace d;
			u.docPlacesLoad();
			const ResumeEntry* e = u.docPlaces->find(path);
			d.paged = e && (e->aid == 2 || (e->aid != 1 && e->duration > 1));
			if (!e) return d;
			d.started = e->position > 0;
			if (d.paged)
			{
				d.pages = (int)e->duration;
				d.page = (int)e->position + 1;
				d.frac = e->duration > 0 ? std::clamp((e->position + 1) / e->duration, 0.0, 1.0) : 0;
			}
			else d.frac = std::clamp(e->duration, 0.0, 1.0);
			return d;
		}
		// a book, document or comic of Recent, opened on the handheld at its place (a network one copied first)
		static void open(Player& p, Ui& u, const std::string& path) { u.openFile(path, p); }
		// the playlist (its file by id) as it is on the card, the bottom panel's edits written first
		static bool load(Player& p, Ui& u, const std::string& id, Playlist& pl, std::string& err)
		{
			std::string file = u.plWebFile(id);
			if (file.empty())
			{
				WebCall::setStatus(404);
				err = error("No such playlist");
				return false;
			}
			u.plFlush(p);
			int skipped = 0;
			if (!u.plStore->load(file, pl, skipped))
			{
				if (u.plStore->isBuiltinFile(file))
				{
					// Liked has no file till the first like
					pl = Playlist();
					pl.file = file;
					pl.name = PlaylistStore::LIKED_NAME;
					return true;
				}
				WebCall::setStatus(500);
				err = error("The playlist couldn't be read");
				return false;
			}
			return true;
		}

		// an edit: rev checked, applied (edit returns remap), saved at once, the bottom panel and the player told
		template <typename Edit>
		static std::string edit(WebServer& ws, Player& p, Ui& u, const std::string& id, const std::string& rev, Edit fn,
			bool allowLiked = false)
		{
			// Liked is managed by the Like button: only removing from it (an unlike) goes through here
			if (!allowLiked && id == PlaylistStore::LIKED_ID) return builtin();
			Playlist pl;
			std::string err;
			if (!load(p, u, id, pl, err)) return err;
			if (revOf(pl) != rev)
			{
				WebCall::setStatus(409);
				return JsonWriter().obj().key("error").str("This playlist changed on another device").key("code").str("rev")
					.key("rev").str(revOf(pl)).end().out();
			}
			std::string why;
			std::vector<int> remap = fn(pl, why);
			if (!why.empty())
			{
				WebCall::setStatus(400);
				return error(why);
			}
			if (!u.plStore->save(pl))
			{
				WebCall::setStatus(500);
				return error("The playlist couldn't be saved");
			}
			u.plWebEdited(pl.file, remap, p);
			ws.notify("playlists", "{\"serial\":" + std::to_string(u.plStore->serial()) + "}");
			return JsonWriter().obj().key("rev").str(revOf(pl)).key("count").num((int64_t)pl.entries.size()).end().out();
		}

		static std::string builtin()
		{
			WebCall::setStatus(403);
			return error("Liked is managed with the Like button", "builtin");
		}

		static bool hasStore(Ui& u)
		{
			if (u.plStore) return true;
			WebCall::setStatus(503);
			return false;
		}

		static void routes(WebServer& ws);
	};

	// (a member of the glue: its handlers reach Ui's insides too)
	void WebPlaylistGlue::routes(WebServer& ws)
	{
		httplib::Server& s = ws.http();
		WebServer* w = &ws;

		s.Get("/api/playlists", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				u.plFlush(p);
				JsonWriter j;
				j.obj().key("serial").num(u.plStore->serial()).key("playlists").arr();
				for (auto& i : u.plStore->list())
				{
					j.obj().key("id").str(idOf(i.file)).key("name").str(i.name).key("count").num(i.count);
					if (i.builtin) j.key("builtin").boolean(true);	// Liked: first, no rename/delete/add, unliked by Like
					j.end();
				}
				return j.end().end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// one playlist: read on the main thread; its items' places and whether they're there worked out here
		s.Get("/api/playlist", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id");
			auto pl = std::make_shared<Playlist>();
			// the file playing on the handheld: its tag title and length, known without reading the card
			struct Now { std::string path, title; double duration = -1; };
			auto now = std::make_shared<Now>();
			int status = 200;
			std::string body = w->call([id, pl, now](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				std::string err;
				if (!WebPlaylistGlue::load(p, u, id, *pl, err)) return err;
				if (p.playing())
				{
					now->path = p.currentPath();
					now->title = p.title;
					now->duration = p.loaded ? p.duration : -1;
				}
				return std::string();
			}, 2000, status);
			if (status != 200)
			{
				answer(res, body, status);
				return;
			}
			JsonWriter j;
			j.obj().key("id").str(id).key("name").str(pl->name).key("rev").str(revOf(*pl));
			if (id == PlaylistStore::LIKED_ID) j.key("builtin").boolean(true);
			j.key("items").arr();
			for (auto& e : pl->entries)
			{
				j.obj();
				bool net = isNetPath(e.path);
				// a network item: its bpnet:// path (no credentials in it), not one the web can open
				if (net) j.key("p").str("").key("net").str(e.path);
				else j.key("p").str(w->toVirtual(e.path));
				// a title of its own (#EXTINF's, given by hand or by another player) as it is; else the tag of the one
				// playing, or the file's name without its track number
				bool own = !e.title.empty() && e.title != entryTitle(e.path);
				bool playing = !now->path.empty() && e.path == now->path;
				j.key("title").str(own ? e.title : playing ? webTitle(e.path, now->title) : webTitle(e.path));
				// seconds, -1 unknown (only the one playing is known without probing the card)
				j.key("duration").num(e.duration > 0 ? e.duration : playing && now->duration > 0 ? now->duration : -1.0);
				j.key("missing").boolean(!net && access(e.path.c_str(), F_OK) != 0);
				j.key("kind").str(isAudioFile(e.path) ? "audio" : isPhotoFile(e.path) ? "photo" : "video");
				j.end();
			}
			answer(res, j.end().end().out(), 200);
		});

		s.Post("/api/playlists/create", [w](const httplib::Request& req, httplib::Response& res) {
			std::string name = value(req, "name");
			int status = 200;
			std::string body = w->call([w, name](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				std::string file;
				PlNameError e = u.plStore->create(name, file);
				if (e != PlNameError::None)
				{
					WebCall::setStatus(e == PlNameError::Taken ? 409 : 400);
					return error(nameError(e), e == PlNameError::Taken ? "exists" : "name");
				}
				u.plWebListChanged(p);
				w->notify("playlists", "{\"serial\":" + std::to_string(u.plStore->serial()) + "}");
				return JsonWriter().obj().key("id").str(idOf(file)).end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		s.Post("/api/playlists/rename", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id"), name = value(req, "name");
			int status = 200;
			std::string body = w->call([w, id, name](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				if (id == PlaylistStore::LIKED_ID) return WebPlaylistGlue::builtin();
				std::string old = u.plWebFile(id), file;
				if (old.empty())
				{
					WebCall::setStatus(404);
					return error("No such playlist");
				}
				u.plFlush(p);
				PlNameError e = u.plStore->rename(old, name, file);
				if (e != PlNameError::None)
				{
					WebCall::setStatus(e == PlNameError::Taken ? 409 : 400);
					return error(nameError(e), e == PlNameError::Taken ? "exists" : "name");
				}
				u.plWebRenamed(old, file, name, p);
				w->notify("playlists", "{\"serial\":" + std::to_string(u.plStore->serial()) + "}");
				return JsonWriter().obj().key("ok").boolean(true).key("id").str(idOf(file)).end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		s.Post("/api/playlists/delete", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id");
			int status = 200;
			std::string body = w->call([w, id](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				if (id == PlaylistStore::LIKED_ID) return WebPlaylistGlue::builtin();
				std::string file = u.plWebFile(id);
				if (file.empty())
				{
					WebCall::setStatus(404);
					return error("No such playlist");
				}
				// its edits not written yet: never (written after, they'd bring it back)
				u.plWebDeleted(file, p);
				if (!u.plStore->remove(file))
				{
					WebCall::setStatus(500);
					return error("The playlist couldn't be deleted");
				}
				u.plWebListChanged(p);
				w->notify("playlists", "{\"serial\":" + std::to_string(u.plStore->serial()) + "}");
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		// add p[] (files, and folders: their own media) at `at` (an index in the list as it is; none: the end)
		s.Post("/api/playlist/add", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id"), rev = value(req, "rev");
			int at = req.has_param("at") ? atoi(value(req, "at").c_str()) : -1;
			std::vector<PlaylistEntry> add;
			for (auto& v : values(req, "p"))
			{
				std::string real;
				if (!w->resolve(v, real, false, res)) return;
				if (isDir(real))
				{
					std::vector<std::string> names;
					if (DIR* d = opendir(real.c_str()))
					{
						while (dirent* e = readdir(d)) names.push_back(e->d_name);
						closedir(d);
					}
					int photos = 0;
					for (auto& e : folderEntries(real, names, photos))
						if (!isDir(e.path)) add.push_back(e);
				}
				else if (isVideoFile(real) || isAudioFile(real))
				{
					PlaylistEntry e;
					e.path = real;
					e.title = entryTitle(real);
					add.push_back(e);
				}
			}
			if (add.empty())
			{
				answer(res, error("Only songs and videos go in a playlist", "type"), 400);
				return;
			}
			int status = 200;
			std::string body = w->call([w, id, rev, at, add](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				return WebPlaylistGlue::edit(*w, p, u, id, rev, [&](Playlist& pl, std::string& why) {
					int dropped = 0;
					int where = at < 0 || at > (int)pl.entries.size() ? -1 : at;
					std::vector<int> remap = addEntries(pl.entries, add, where, PL_MAX_ENTRIES, dropped);
					if (dropped == (int)add.size()) why = "The playlist is full";
					return remap;
				});
			}, 2000, status);
			answer(res, body, status);
		});

		// Like: p = a file's virtual path, or "current" (what plays on the handheld); on=1 likes, on=0 unlikes, none
		// toggles; at=N (with on=1) puts it at that index instead of the top (Undo after an Unlike). Answers {liked, path,
		// serial, full: a like made room in a full Liked, dropped: the one that went ("" not in the web's tree)}; every change
		// is a "playlists" event for the other pages.
		s.Post("/api/like", [w](const httplib::Request& req, httplib::Response& res) {
			std::string v = value(req, "p");
			if (v.empty())
			{
				WebServer::fail(res, 400, "No file");
				return;
			}
			std::string real;
			bool current = v == "current";
			if (!current)
			{
				if (!w->resolve(v, real, false, res)) return;
				if (!PlaylistStore::likable(real))
				{
					answer(res, error("Only songs and videos can be liked", "type"), 400);
					return;
				}
			}
			int on = req.has_param("on") ? (value(req, "on") == "1" || value(req, "on") == "true" ? 1 : 0) : -1;
			int at = req.has_param("at") ? std::max(0, atoi(value(req, "at").c_str())) : 0;
			int status = 200;
			std::string body = w->call([w, real, current, on, at](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				std::string path = current ? (p.playing() ? p.currentPath() : "") : real;
				if (path.empty())
				{
					WebCall::setStatus(409);
					return error("Nothing is playing", "idle");
				}
				if (!PlaylistStore::likable(path))
				{
					WebCall::setStatus(400);
					return error("Only songs and videos can be liked", "type");
				}
				bool was = u.plStore->isLiked(path), want = on < 0 ? !was : on == 1;
				bool now = was;
				std::string dropped;
				if (want != was)
				{
					std::string title = current && p.loaded ? p.title : "";
					now = u.setLikedPath(p, path, title, current && p.loaded ? p.duration : -1, want, at, &dropped);
					if (now != want)
					{
						WebCall::setStatus(500);
						return error("Liked couldn't be saved");
					}
					w->notify("playlists", "{\"serial\":" + std::to_string(u.plStore->serial()) + "}");
				}
				return JsonWriter().obj().key("liked").boolean(now).key("path").str(w->toVirtual(path))
					.key("serial").num(u.plStore->serial()).key("full").boolean(!dropped.empty())
					.key("dropped").str(dropped.empty() ? "" : w->toVirtual(dropped)).end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// the liked files' virtual paths (a page marks its rows with them); network ones aren't in the web's tree
		s.Get("/api/likes", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([w](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				Playlist pl;
				std::string err;
				if (!WebPlaylistGlue::load(p, u, PlaylistStore::LIKED_ID, pl, err)) return err;
				JsonWriter j;
				j.obj().key("serial").num(u.plStore->serial()).key("count").num((int64_t)pl.entries.size()).key("paths").arr();
				for (auto& e : pl.entries)
					if (!isNetPath(e.path)) j.str(w->toVirtual(e.path));
				return j.end().end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// Recent, as the handheld's Home strip lists it (history.tsv, newest first, with resume.tsv's places): {on (Keep
		// history), serial (Player::recentSerial, as the "recent" event says), items: [{p, title, kind, position (-1:
		// none kept), duration (-1: unknown), frac, played (no place kept: "Played"), playing (the handheld's file),
		// gone (not on the card)}]}. Network files aren't in the web's tree, and what it never serves is left out
		s.Get("/api/recent", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([w](Player& p, Ui& u) {
				JsonWriter j;
				j.obj().key("on").boolean(p.settings.keepHistory).key("serial").num(p.recentSerial()).key("items").arr();
				std::string cur = p.playing() ? p.currentPath() : "";
				for (auto& path : p.history().items())
				{
					// a network file: by its bpnet:// path (no credentials in it), its connection still saved; it plays on
					// the handheld only, and whether it's still there is only known by asking its server
					bool net = isNetPath(path);
					NetPath np;
					if (net && !(u.netStore && parseNetPath(path, np) && u.netStore->find(np.server))) continue;
					struct stat st;
					bool gone = !net && stat(path.c_str(), &st) != 0;
					if (!net && !gone && !p.settings.showHiddenFolders && insideHiddenFolder(path)) continue;
					std::string v = net ? path : w->servedVirtual(path);
					if (v.empty()) continue;
					std::string where = WebPlaylistGlue::where(u, path);
					// a book, document or comic: its page or how far it's read (they open on the handheld)
					if (isDocumentName(path) || isComicName(path))
					{
						std::string e = path.substr(path.rfind('.') == std::string::npos ? path.size() : path.rfind('.') + 1);
						for (char& c : e) c = (char)tolower((unsigned char)c);
						const char* kind = isComicName(path) ? "comic" : e == "epub" ? "epub" : isPdfName(path) ? "pdf" : "text";
						auto d = WebPlaylistGlue::docPlace(u, path);
						j.obj().key("p").str(v).key("title").str(webTitle(path)).key("kind").str(kind).key("where").str(where)
							.key("net").boolean(net).key("gone").boolean(gone).key("started").boolean(d.started)
							.key("frac").num(d.frac).key("page").num(d.page).key("pages").num(d.pages).end();
						continue;
					}
					bool audio = isAudioFile(path), now = path == cur;
					RecentProgress rp = recentProgress(p.resumeStore().find(path), audio);
					// the file playing: where it is now (its kept place is from when it last stopped)
					if (now && p.loaded && !audio && p.position >= 0 && p.duration > 0)
					{
						rp.position = p.position;
						rp.duration = p.duration;
						rp.frac = std::clamp(p.position / p.duration, 0.0, 1.0);
						rp.played = false;
					}
					j.obj().key("p").str(v).key("title").str(webTitle(path)).key("kind").str(audio ? "audio" : "video").key("where").str(where).key("net").boolean(net)
						.key("position").num(rp.position).key("duration").num(rp.duration).key("frac").num(rp.frac)
						.key("played").boolean(rp.played).key("playing").boolean(now).key("gone").boolean(gone).end();
				}
				return j.end().end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// Open on handheld (p: a Recent row's path): a book, document or comic, at its place, on the handheld's screens
		// (a network one is copied to the card first, with its progress there). Only Recent's own rows
		s.Post("/api/recent/open", [w](const httplib::Request& req, httplib::Response& res) {
			std::string v = value(req, "p");
			int status = 200;
			std::string body = w->call([w, v](Player& p, Ui& u) {
				std::string path;
				for (auto& h : p.history().items())
					if (isNetPath(h) ? h == v : w->servedVirtual(h) == v) path = h;
				if (path.empty() || !(isDocumentName(path) || isComicName(path)))
				{
					WebCall::setStatus(404);
					return error("Not a book or document in Recent", "missing");
				}
				WebPlaylistGlue::open(p, u, path);
				return JsonWriter().obj().key("ok").boolean(true).end().out();
			}, 3000, status);
			answer(res, body, status);
		});

		// Remove from Recent (p: a virtual path), as the handheld's card menu: out of the history, its place kept
		s.Post("/api/recent/remove", [w](const httplib::Request& req, httplib::Response& res) {
			std::string v = value(req, "p");
			if (v.empty())
			{
				WebServer::fail(res, 400, "No file");
				return;
			}
			// (a gone file can't resolve: its path is matched by its virtual one instead)
			int status = 200;
			std::string body = w->call([w, v](Player& p, Ui& u) {
				std::string path;
				for (auto& h : p.history().items())
					if (isNetPath(h) ? h == v : w->servedVirtual(h) == v) path = h;
				if (path.empty())
				{
					WebCall::setStatus(404);
					return error("Not in Recent", "missing");
				}
				p.removeFromHistory(path);
				u.recentEdited(p);
				return JsonWriter().obj().key("ok").boolean(true).key("serial").num(p.recentSerial()).end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// Clear Recent, as Home's menu: the history emptied, the places kept
		s.Post("/api/recent/clear", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([](Player& p, Ui& u) {
				p.clearHistory();
				u.recentEdited(p);
				return JsonWriter().obj().key("ok").boolean(true).key("serial").num(p.recentSerial()).end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		auto indexes = [](const httplib::Request& req, std::set<int>& sel) {
			for (auto& v : values(req, "i"))
			{
				char* end = nullptr;
				long n = strtol(v.c_str(), &end, 10);
				if (v.empty() || *end || n < 0) return false;
				sel.insert((int)n);
			}
			return !sel.empty();
		};

		s.Post("/api/playlist/remove", [w, indexes](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id"), rev = value(req, "rev");
			std::set<int> sel;
			if (!indexes(req, sel))
			{
				WebServer::fail(res, 400, "No items");
				return;
			}
			int status = 200;
			std::string body = w->call([w, id, rev, sel](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				return WebPlaylistGlue::edit(*w, p, u, id, rev, [&](Playlist& pl, std::string& why) {
					if (*sel.rbegin() >= (int)pl.entries.size()) why = "No such item";
					return why.empty() ? removeEntries(pl.entries, sel) : std::vector<int>();
				}, true);	// (from Liked it's an unlike)
			}, 2000, status);
			answer(res, body, status);
		});

		// move i[] to `to`: an insertion index in the list before the move (to = count: the end)
		s.Post("/api/playlist/move", [w, indexes](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id"), rev = value(req, "rev");
			int to = atoi(value(req, "to").c_str());
			std::set<int> sel;
			if (!indexes(req, sel) || !req.has_param("to"))
			{
				WebServer::fail(res, 400, "No items, or nowhere to move them");
				return;
			}
			int status = 200;
			std::string body = w->call([w, id, rev, sel, to](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				return WebPlaylistGlue::edit(*w, p, u, id, rev, [&](Playlist& pl, std::string& why) {
					if (*sel.rbegin() >= (int)pl.entries.size()) { why = "No such item"; return std::vector<int>(); }
					// moveEntriesTo's `at` is where the block starts in the list after: the ones before `to` not moved
					int t = std::clamp(to, 0, (int)pl.entries.size());
					int before = (int)std::count_if(sel.begin(), sel.end(), [&](int k) { return k < t; });
					return moveEntriesTo(pl.entries, sel, t - before);
				});
			}, 2000, status);
			answer(res, body, status);
		});

		s.Post("/api/playlist/dedupe", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id"), rev = value(req, "rev");
			int status = 200;
			std::string body = w->call([w, id, rev](Player& p, Ui& u) {
				if (!WebPlaylistGlue::hasStore(u)) return error("Playlists need a state folder");
				return WebPlaylistGlue::edit(*w, p, u, id, rev, [&](Playlist& pl, std::string&) {
					int removed = 0;
					return removeDuplicates(pl.entries, removed);
				});
			}, 2000, status);
			answer(res, body, status);
		});
	}

	void registerPlaylistRoutes(WebServer& ws) { WebPlaylistGlue::routes(ws); }
}
