// The web dashboard's manga routes (docs/design/manga.md section 8): the library, repositories, sources, downloads
// and covers. v1 is manage, not read. Every handler goes through WebServer::call (main thread), reaching the app via
// Ui's mangaNet (repos, queue, downloader) and mangaStore. Every endpoint answers 404 while the manga extension is off;
// changes are announced on SSE as a "manga" notify.
#include "web/server.h"
#include "json.h"
#include "mangastore.h"
#include "manganet.h"
#include "mangajava.h"
#include "mangarepos.h"
#include "dlqueue.h"
#include "mangadownloader.h"
#include "player.h"
#include "ui.h"
#include <httplib.h>
#include <cstdio>
#include <functional>
#include <string>

namespace bp
{
	namespace
	{
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

		void answer(httplib::Response& res, const std::string& body, int status) { WebServer::json(res, body, status); }

		// the extension must be on for any manga endpoint to work; false answers 404 already
		bool mangaReady(Player& p, Ui& u)
		{
			if (!p.settings.mangaOn)
			{
				WebCall::setStatus(404);
				return false;
			}
			// the dashboard counts as using the reader: its stores and threads start as on the handheld (MangaNet's
			// repos, queue and downloader exist only once started)
			if (u.mangaNet && !u.mangaNet->started()) u.mangaNet->start();
			return u.mangaNet && u.mangaNet->started();
		}

		// a "manga" SSE event with the stores' serials, so pages reload what changed
		void mangaNotify(WebServer& ws, Ui& u)
		{
			JsonWriter j;
			j.obj();
			if (u.mangaStorePublic()) j.key("store").num((int64_t)u.mangaStorePublic()->serial());
			if (u.mangaNet)
			{
				j.key("repos").num((int64_t)u.mangaNet->repos().serial());
				j.key("queue").num((int64_t)u.mangaNet->queue().serial());
			}
			ws.notify("manga", j.end().out());
		}

		// the JSON for one library series
		void writeSeries(JsonWriter& j, const MangaSeries& s, MangaStore* store)
		{
			j.obj().key("id").str(s.id).key("title").str(s.title).key("source").str(s.source).key("cover").str(s.cover)
				.key("added").num(s.added).key("lastRead").num(s.lastRead).key("inLibrary").boolean(s.inLibrary);
			if (!s.readingMode.empty()) j.key("readingMode").str(s.readingMode);
			j.end();
		}

		// the JSON for one chapter progress record
		void writeProgress(JsonWriter& j, const ChapterProgress& p)
		{
			j.obj().key("seriesId").str(p.seriesId).key("chapterId").str(p.chapterId).key("title").str(p.chapterTitle)
				.key("page").num(p.page).key("pages").num(p.pages).key("read").boolean(p.read).key("at").num(p.at).end();
		}

		// the JSON for one chapter the dashboard's series view shows
		void writeWebChapter(JsonWriter& j, const Ui::MangaWebChapter& c)
		{
			j.obj().key("id").str(c.id).key("number").num(c.number).key("title").str(c.title)
				.key("read").boolean(c.read).key("page").num(c.page).key("pages").num(c.pages);
			j.key("download").obj().key("state").str(c.download).key("done").num(c.done).key("total").num(c.total).end();
			j.end();
		}
	}

	// the manga route handlers; called from server.cpp's routes()
	void registerMangaRoutes(WebServer& ws)
	{
		httplib::Server& s = ws.http();
		WebServer* w = &ws;

		// the whole picture the Manga pages need: the extension's state, the library, the repositories and sources,
		// the download queue's status. One call, one serial each, so a page knows when to refetch
		s.Get("/api/manga/state", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([w](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				JsonWriter j;
				j.obj().key("on").boolean(true);
				// the library
				j.key("library").arr();
				if (u.mangaStorePublic())
					for (auto& s : u.mangaStorePublic()->library())
						writeSeries(j, s, u.mangaStorePublic());
				j.end();
				// repositories
				j.key("repos").arr();
				if (u.mangaNet)
					for (auto& r : u.mangaNet->repos().repos())
					{
						j.obj().key("url").str(r.url).key("name").str(r.name).key("count").num((int64_t)r.sourceCount)
							.key("lastFetched").num(r.lastFetched).key("haveIndex").boolean(r.haveIndex).end();
					}
				j.end();
				// installed sources
				j.key("installed").arr();
				if (u.mangaNet)
					for (auto& src : u.mangaNet->repos().installed())
					{
						j.obj().key("id").str(src.fullId).key("name").str(src.name).key("lang").str(src.lang)
							.key("version").str(src.version).key("repo").str(src.repo).key("adult").boolean(src.adult)
							.key("update").boolean(src.updateAvailable).end();
					}
				j.end();
				// available sources (not installed yet)
				j.key("available").arr();
				if (u.mangaNet)
					for (auto& a : u.mangaNet->repos().available())
					{
						j.obj().key("id").str(a.fullId).key("name").str(a.name).key("lang").str(a.lang)
							.key("version").str(a.version).key("repo").str(a.repo).key("repoName").str(a.repoName)
							.key("adult").boolean(a.adult).end();
					}
				j.end();
				// settings the dashboard needs (adult filter)
				j.key("showAdult").boolean(p.settings.mangaAdult);
				// download status summary
				if (u.mangaNet)
				{
					auto st = u.mangaNet->downloader().status();
					j.key("dl").obj().key("running").str(st.runningId).key("done").num(st.done).key("total").num(st.total)
						.key("bytes").num((int64_t)st.bytes).key("low").boolean(st.lowSpace).end();
				}
				return j.end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// add a repository by URL
		s.Post("/api/manga/repo", [w](const httplib::Request& req, httplib::Response& res) {
			std::string url = value(req, "url");
			if (url.empty()) { WebServer::fail(res, 400, "No URL"); return; }
			int status = 200;
			std::string body = w->call([w, url](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaNet) { WebCall::setStatus(503); return error("Manga isn't running"); }
				std::string err;
				// a Keiyoushi-style index (index.pb / index.min.json) is a Java repository: Suwayomi takes it
				if (MangaJava::isRepoUrl(url))
				{
					auto st = u.mangaNet->javaExt().state();
					if (st == JavaExtHost::State::NotInstalled || st == JavaExtHost::State::Downloading)
					{
						WebCall::setStatus(409);
						return error("Install Java extensions first (Manga settings on the handheld)");
					}
					u.mangaNet->javaExt().want();
					if (!u.mangaNet->javaSources().addRepo(url, err))
					{
						WebCall::setStatus(400);
						return error(err);
					}
					mangaNotify(*w, u);
					return std::string("{\"ok\":true,\"java\":true}");
				}
				if (!u.mangaNet->repos().addRepo(url, err))
				{
					WebCall::setStatus(400);
					return error(err);
				}
				u.mangaNet->repos().save();
				mangaNotify(*w, u);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		// remove a repository (url: the repository's URL)
		s.Delete("/api/manga/repo", [w](const httplib::Request& req, httplib::Response& res) {
			std::string url = req.get_param_value("url");
			if (url.empty()) url = value(req, "url");
			if (url.empty()) { WebServer::fail(res, 400, "No URL"); return; }
			bool alsoSources = req.get_param_value("sources") == "1";
			int status = 200;
			std::string body = w->call([w, url, alsoSources](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaNet) { WebCall::setStatus(503); return error("Manga isn't running"); }
				if (!u.mangaNet->repos().removeRepo(url, alsoSources))
				{
					WebCall::setStatus(404);
					return error("No such repository");
				}
				u.mangaNet->repos().save();
				mangaNotify(*w, u);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		// refresh a repository's index (or all, when url is empty)
		s.Post("/api/manga/repo/refresh", [w](const httplib::Request& req, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([w](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaNet) { WebCall::setStatus(503); return error("Manga isn't running"); }
				u.mangaNet->repos().refresh(true);
				mangaNotify(*w, u);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		// install a source (id: the source's full id "<repo-hash>/<id>")
		s.Post("/api/manga/install", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id");
			if (id.empty()) { WebServer::fail(res, 400, "No source id"); return; }
			// split the full id into repo hash and source id; the repos manager needs the repo URL
			size_t slash = id.find('/');
			if (slash == std::string::npos) { WebServer::fail(res, 400, "Bad source id"); return; }
			int status = 200;
			std::string body = w->call([w, id](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaNet) { WebCall::setStatus(503); return error("Manga isn't running"); }
				// find the repo URL from the full id's hash prefix
				std::string repoUrl;
				for (auto& r : u.mangaNet->repos().repos())
					if (id.compare(0, MangaRepos::repoHash(r.url).size(), MangaRepos::repoHash(r.url)) == 0)
					{ repoUrl = r.url; break; }
				if (repoUrl.empty()) { WebCall::setStatus(404); return error("No such repository"); }
				std::string srcId = id.substr(id.find('/') + 1);
				if (!u.mangaNet->repos().install(repoUrl, srcId))
				{
					WebCall::setStatus(400);
					return error("Couldn't start the install");
				}
				mangaNotify(*w, u);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		// update an installed source
		s.Post("/api/manga/update", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id");
			if (id.empty()) { WebServer::fail(res, 400, "No source id"); return; }
			int status = 200;
			std::string body = w->call([w, id](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaNet) { WebCall::setStatus(503); return error("Manga isn't running"); }
				std::string repoUrl;
				for (auto& r : u.mangaNet->repos().repos())
					if (id.compare(0, MangaRepos::repoHash(r.url).size(), MangaRepos::repoHash(r.url)) == 0)
					{ repoUrl = r.url; break; }
				if (repoUrl.empty()) { WebCall::setStatus(404); return error("No such repository"); }
				std::string srcId = id.substr(id.find('/') + 1);
				if (!u.mangaNet->repos().update(repoUrl, srcId))
				{
					WebCall::setStatus(400);
					return error("Couldn't start the update");
				}
				mangaNotify(*w, u);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		// uninstall a source (id: full id)
		s.Post("/api/manga/uninstall", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id");
			if (id.empty()) { WebServer::fail(res, 400, "No source id"); return; }
			int status = 200;
			std::string body = w->call([w, id](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaNet) { WebCall::setStatus(503); return error("Manga isn't running"); }
				if (!u.mangaNet->repos().uninstall(id))
				{
					WebCall::setStatus(404);
					return error("No such source");
				}
				u.mangaNet->repos().save();
				mangaNotify(*w, u);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		// the library (GET) and remove from it (DELETE)
		s.Get("/api/manga/library", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				JsonWriter j;
				j.obj().key("items").arr();
				if (u.mangaStorePublic())
					for (auto& s : u.mangaStorePublic()->library())
						writeSeries(j, s, u.mangaStorePublic());
				j.end();
				if (u.mangaStorePublic()) j.key("serial").num((int64_t)u.mangaStorePublic()->serial());
				return j.end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		s.Delete("/api/manga/library", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = req.get_param_value("id");
			if (id.empty()) id = value(req, "id");
			if (id.empty()) { WebServer::fail(res, 400, "No series id"); return; }
			int status = 200;
			std::string body = w->call([w, id](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaStorePublic()) { WebCall::setStatus(503); return error("Manga store isn't ready"); }
				u.mangaStorePublic()->removeSeries(id);
				u.mangaStorePublic()->save();
				mangaNotify(*w, u);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		// mark chapters read or unread (POST /api/manga/read {seriesId, chapters[], read: 0|1})
		s.Post("/api/manga/read", [w](const httplib::Request& req, httplib::Response& res) {
			std::string sid = value(req, "seriesId");
			bool read = value(req, "read") != "0";
			std::vector<std::string> chapters;
			size_t n = req.get_param_value_count("chapters[]");
			for (size_t i = 0; i < n; i++) chapters.push_back(req.get_param_value("chapters[]", i));
			if (n == 0)
			{
				n = req.get_param_value_count("chapters");
				for (size_t i = 0; i < n; i++) chapters.push_back(req.get_param_value("chapters", i));
			}
			if (sid.empty() || chapters.empty()) { WebServer::fail(res, 400, "No series or chapters"); return; }
			int status = 200;
			std::string body = w->call([w, sid, chapters, read](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaStorePublic()) { WebCall::setStatus(503); return error("Manga store isn't ready"); }
				u.mangaStorePublic()->markRead(sid, chapters, read);
				u.mangaStorePublic()->save();
				mangaNotify(*w, u);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		// a cover from the cache only: the dashboard never makes the handheld fetch one on its behalf. The key is the
		// cover URL (the same key the reader uses when it fetches and caches a cover). The cover cache's pathFor()
		// hashes the key into a file name; the file's bytes, when there, are served as the cover image. A cache miss
		// is 404: the dashboard waits for the reader to fetch it when its card shows on the handheld
		// GET /api/manga/chapters?s=<series id>: a local series from the card's scan, a source series from its
		// worker (202 with a retry while the answer comes, as the handheld's series page asks it)
		s.Get("/api/manga/chapters", [w](const httplib::Request& req, httplib::Response& res) {
			std::string sid = value(req, "s");
			if (sid.empty()) { WebServer::fail(res, 400, "No series id"); return; }
			int status = 200;
			std::string body = w->call([sid](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				Ui::MangaWebSeries s;
				if (!u.mangaWebChapters(sid, s))
				{
					if (s.pending)
					{
						WebCall::setStatus(202);
						JsonWriter j;
						j.obj().key("pending").boolean(true).key("retry").num(500).end();
						return j.end().out();
					}
					WebCall::setStatus(404);
					return error(s.error.empty() ? "No such series" : s.error);
				}
				JsonWriter j;
				j.obj().key("series").obj().key("id").str(s.id).key("title").str(s.title).key("source").str(s.source)
					.key("local").boolean(s.local).end();
				j.key("items").arr();
				for (const auto& c : s.items) writeWebChapter(j, c);
				j.end();
				return j.end().out();
			}, 2000, status);
			if (status == 202) res.set_header("Retry-After", "1");
			answer(res, body, status);
		});

		// a cover from the cache only: the dashboard never makes the handheld fetch one on its behalf. The cover is
		// looked up in the page cache by a key the manga store recorded (series' cover URL); the cache's pathFor()
		// hashes the key into a file name, so the same key always lands in the same place
		s.Get("/api/manga/cover", [w](const httplib::Request& req, httplib::Response& res) {
			std::string key = req.get_param_value("s");	// the cover URL (the source's or the server's)
			if (key.empty()) key = req.get_param_value("k");	// (the dashboard's older name)
			if (key.empty()) { WebServer::fail(res, 400, "No cover key"); return; }
			int status = 200;
			std::string body = w->call([w, key, &res](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				PageCache& cc = u.mangaCoverCacheRef();
				std::vector<uint8_t> bytes;
				if (!cc.has(key) || !cc.get(key, bytes))
				{
					WebCall::setStatus(404);
					return error("Cover not cached");
				}
				// the content type: a JPEG, PNG or WebP, guessed from the file's own magic bytes so the browser
				// draws it without the reader telling it (the cache holds the bytes as the server sent them)
				const char* ct = "image/jpeg";
				if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
					ct = "image/png";
				else if (bytes.size() >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F'
					&& bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P')
					ct = "image/webp";
				res.set_header("Content-Type", ct);
				res.set_content(std::string(bytes.begin(), bytes.end()), ct);
				return std::string();	// set_content already wrote the body; the empty string is ignored
			}, 2000, status);
			if (status != 200) answer(res, body, status);
		});

		// the download queue
		s.Get("/api/manga/downloads", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				JsonWriter j;
				j.obj().key("items").arr();
				if (u.mangaNet)
				{
					for (auto& it : u.mangaNet->queue().items())
					{
						j.obj().key("id").str(it.id).key("seriesId").str(it.seriesId).key("seriesTitle").str(it.seriesTitle)
							.key("chapterId").str(it.chapterId).key("chapterTitle").str(it.chapterTitle)
							.key("source").str(it.source).key("done").num(it.done).key("total").num((int64_t)it.pageUrls.size())
							.key("bytes").num((int64_t)it.bytes).key("added").num(it.added);
						const char* st = "waiting";
						switch (it.state)
						{
						case DlItem::Waiting: st = "waiting"; break;
						case DlItem::Running: st = "running"; break;
						case DlItem::Paused: st = "paused"; break;
						case DlItem::Failed: st = "failed"; break;
						case DlItem::Done: st = "done"; break;
						}
						j.key("state").str(st);
						if (!it.error.empty()) j.key("error").str(it.error);
						j.end();
					}
					auto dl = u.mangaNet->downloader().status();
					j.key("status").obj().key("running").str(dl.runningId).key("done").num(dl.done).key("total").num(dl.total)
						.key("bytes").num((int64_t)dl.bytes).key("low").boolean(dl.lowSpace).end();
					j.key("allPaused").boolean(u.mangaNet->queue().allPaused());
				}
				j.end();
				if (u.mangaNet) j.key("serial").num((int64_t)u.mangaNet->queue().serial());
				return j.end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// enqueue downloads (series, chapters[]); the source's worker is asked for each chapter's page list first,
		// exactly as the handheld's Download sheet does, then the downloader takes the queue items
		s.Post("/api/manga/download", [w](const httplib::Request& req, httplib::Response& res) {
			std::string series = value(req, "series");
			std::vector<std::string> chapters;
			size_t n = req.get_param_value_count("chapters[]");
			for (size_t i = 0; i < n; i++) chapters.push_back(req.get_param_value("chapters[]", i));
			if (n == 0)
			{
				n = req.get_param_value_count("chapters");
				for (size_t i = 0; i < n; i++) chapters.push_back(req.get_param_value("chapters", i));
			}
			if (series.empty() || chapters.empty()) { WebServer::fail(res, 400, "No series or chapters"); return; }
			int status = 200;
			std::string body = w->call([w, series, chapters](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaNet) { WebCall::setStatus(503); return error("Manga isn't running"); }
				int added = 0;
				std::string why;
				if (!u.mangaWebDownload(series, chapters, added, why))
				{
					WebCall::setStatus(400);
					return error(why);
				}
				if (added > 0)
				{
					u.mangaNet->queue().save();
					u.mangaNet->queue().resumeAll();	// one ask also lifts an all-pause
				}
				mangaNotify(*w, u);
				JsonWriter j;
				j.obj().key("ok").boolean(true).key("added").num(added).end();
				return j.end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// delete a download (id: the queue item's id)
		s.Delete("/api/manga/download", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = req.get_param_value("id");
			if (id.empty()) id = value(req, "id");
			if (id.empty()) { WebServer::fail(res, 400, "No download id"); return; }
			int status = 200;
			std::string body = w->call([w, id](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaNet) { WebCall::setStatus(503); return error("Manga isn't running"); }
				u.mangaNet->downloader().cancel(id);
				u.mangaNet->queue().remove(id);
				u.mangaNet->queue().save();
				mangaNotify(*w, u);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		// pause or resume a download (or all)
		s.Post("/api/manga/download/pause", [w](const httplib::Request& req, httplib::Response& res) {
			std::string id = value(req, "id");
			bool resume = value(req, "resume") == "1";
			int status = 200;
			std::string body = w->call([w, id, resume](Player& p, Ui& u) {
				if (!mangaReady(p, u)) return error("Manga is off");
				if (!u.mangaNet) { WebCall::setStatus(503); return error("Manga isn't running"); }
				if (id.empty())
				{
					if (resume) u.mangaNet->queue().resumeAll();
					else u.mangaNet->queue().pauseAll();
				}
				else if (resume) u.mangaNet->queue().resume(id);
				else u.mangaNet->queue().pause(id);
				u.mangaNet->queue().save();
				mangaNotify(*w, u);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});
	}
}
