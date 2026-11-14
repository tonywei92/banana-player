// Files API: roots, list, probe, streaming, covers, uploads and the multi-select operations (mkdir, rename, move, copy,
// delete). Everything here runs on pool threads (Copy, and Move to another card: a job thread of their own); Player is
// asked only through ws.call (which file is playing).
#include <cctype>
#include "web/server.h"
#include "ui.h"
#include "browser.h"
#include "json.h"
#include "photo.h"
#include "tracks.h"
#include "playlist.h"
#include "web/probe.h"
#include "durprobe.h"
#include "durations.h"
#include "web/remux.h"
#include "web/subs.h"
#include "webmedia.h"
#include "webpath.h"
#include "treecopy.h"
#include "httplib.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <thread>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/magic.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#endif
#ifdef __APPLE__
#include <stdio.h>
#endif

#include "player.h"

namespace bp
{
	namespace
	{
		const uint64_t MiB = 1024ull * 1024;
		const uint64_t SPACE_MARGIN = 64 * MiB;
		const uint64_t VFAT_LIMIT = 4096 * MiB;

		std::string lower(std::string s)
		{
			for (auto& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		bool isSubtitleFile(const std::string& name)
		{
			size_t dot = name.rfind('.');
			if (dot == std::string::npos) return false;
			std::string e = lower(name.substr(dot + 1));
			return e == "srt" || e == "ass" || e == "ssa" || e == "vtt" || e == "sub" || e == "idx" || e == "lrc";
		}

		const char* kindOf(const std::string& name, bool dir)
		{
			if (dir) return "folder";
			if (isVideoFile(name)) return "video";
			if (isAudioFile(name)) return "audio";
			if (isPhotoFile(name)) return "photo";
			if (isSubtitleFile(name)) return "sub";
			// the documents and comics the handheld's Files opens (an icon each on the page; it opens none of them)
			size_t dot = name.rfind('.');
			std::string e = dot == std::string::npos ? "" : name.substr(dot + 1);
			for (char& c : e) c = (char)std::tolower((unsigned char)c);
			if (e == "txt") return "text";
			if (e == "epub") return "epub";
			if (e == "pdf") return "pdf";
			if (e == "cbz" || e == "cbr") return "comic";
			return "other";
		}

		std::string joinV(const std::string& vp, const std::string& name)
		{
			std::string b = vp;
			while (!b.empty() && b.back() == '/') b.pop_back();
			return b + "/" + name;
		}

		// "0/a/b" -> "0/a"; "" at a root
		bool parentV(const std::string& vp, std::string& parent)
		{
			std::string b = vp;
			while (!b.empty() && b.back() == '/') b.pop_back();
			size_t i = b.rfind('/');
			if (i == std::string::npos) return false;
			parent = b.substr(0, i);
			return true;
		}

		bool isRootV(const std::string& vp)
		{
			std::string b = vp;
			while (!b.empty() && b.back() == '/') b.pop_back();
			return b.find('/') == std::string::npos;
		}

		std::string baseName(const std::string& p)
		{
			size_t i = p.rfind('/');
			return i == std::string::npos ? p : p.substr(i + 1);
		}

		std::string dirName(const std::string& p)
		{
			size_t i = p.rfind('/');
			if (i == std::string::npos) return ".";
			return i == 0 ? "/" : p.substr(0, i);
		}

		bool freeSpace(const std::string& dir, uint64_t& freeB, uint64_t& totalB)
		{
			struct statvfs sv;
			if (statvfs(dir.c_str(), &sv) != 0) return false;
			freeB = (uint64_t)sv.f_bavail * sv.f_frsize;
			totalB = (uint64_t)sv.f_blocks * sv.f_frsize;
			return true;
		}

		bool onVfat(const std::string& dir)
		{
#ifdef __linux__
			struct statfs sf;
			return statfs(dir.c_str(), &sf) == 0 && (unsigned long)sf.f_type == 0x4d44;	// MSDOS_SUPER_MAGIC
#else
			(void)dir;
			return false;
#endif
		}

		std::string randHex()
		{
			static std::mutex m;
			static std::mt19937_64 gen{std::random_device{}()};
			std::lock_guard<std::mutex> g(m);
			char b[20];
			snprintf(b, sizeof b, "%016llx", (unsigned long long)gen());
			return b;
		}

		// whether the file playing on the handheld is `real` (or inside it, for a folder). Asked once per request
		struct PlayingGuard
		{
			WebServer& ws;
			bool asked = false;
			bool failed = false;
			std::string cur;

			explicit PlayingGuard(WebServer& w) : ws(w) {}

			void ask()
			{
				if (asked) return;
				asked = true;
				int status = 200;
				std::string r = ws.call([](Player& p, Ui&) { return p.playing() ? p.currentPath() : std::string(); }, 2000, status);
				if (status != 200) failed = true;
				else cur = r;
				char* rp = cur.empty() ? nullptr : realpath(cur.c_str(), nullptr);
				if (rp)
				{
					cur = rp;
					free(rp);
				}
			}
			// 0: free; else the HTTP status to refuse with
			int busy(const std::string& real, bool folder)
			{
				ask();
				if (failed) return 503;
				if (cur.empty()) return 0;
				if (cur == real) return 409;
				if (folder && cur.size() > real.size() && cur.compare(0, real.size(), real) == 0 && cur[real.size()] == '/') return 409;
				return 0;
			}
		};

		struct Failed { std::string p, error; int status; };

		// job: a Copy or Move job started for the rest (its JSON), "" if none
		std::string multiAnswer(const std::vector<std::string>& done, const std::vector<Failed>& failed, int& status, const std::string& job = "")
		{
			JsonWriter w;
			w.obj().key("ok").boolean(failed.empty());
			w.key("done").arr();
			for (auto& d : done) w.str(d);
			w.end().key("failed").arr();
			for (auto& f : failed) w.obj().key("p").str(f.p).key("error").str(f.error).end();
			w.end();
			if (!failed.empty())
			{
				const std::string& e0 = failed[0].error;
				bool playing = e0.find("playing on the handheld") != std::string::npos;
				w.key("error").str(playing ? "It's playing on your handheld" : e0);
				if (playing) w.key("code").str("playing");
				else if (e0 == "exists") w.key("code").str("exists");
			}
			if (!job.empty()) w.key("job").raw(job);
			w.end();
			status = (done.empty() && !failed.empty() && job.empty()) ? failed[0].status : 200;
			return w.out();
		}

		std::vector<std::string> paramList(const httplib::Request& req, const char* key)
		{
			std::vector<std::string> v;
			for (std::string k : {std::string(key) + "[]", std::string(key)})
			{
				size_t n = req.get_param_value_count(k);
				for (size_t i = 0; i < n; i++) v.push_back(req.get_param_value(k, i));
				if (!v.empty()) break;
			}
			return v;
		}

		// the visible entries of a folder, up to ITEMS_CAP (more: true when there are more than that)
		const int ITEMS_CAP = 9999;
		int countVisible(const std::string& dir, bool& more)
		{
			more = false;
			DIR* d = opendir(dir.c_str());
			if (!d) return 0;
			int n = 0;
			while (dirent* e = readdir(d))
			{
				if (e->d_name[0] == '.') continue;
				if (n >= ITEMS_CAP)
				{
					more = true;
					break;
				}
				n++;
			}
			closedir(d);
			return n;
		}

		std::string percentEncode(const std::string& s)
		{
			static const char* hex = "0123456789ABCDEF";
			std::string o;
			for (unsigned char c : s)
			{
				if (isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') o += (char)c;
				else
				{
					o += '%';
					o += hex[c >> 4];
					o += hex[c & 15];
				}
			}
			return o;
		}

		std::string asciiFallback(const std::string& s)
		{
			std::string o;
			for (unsigned char c : s) o += (c < 0x20 || c >= 0x7f || c == '"' || c == '\\' || c == '%') ? '_' : (char)c;
			return o;
		}

		// A folder may go only when it holds no hidden folder (a folder the player keeps out of sight); `bad` names the
		// first one
		bool deletable(const std::string& dir, std::string& bad)
		{
			DIR* d = opendir(dir.c_str());
			if (!d)
			{
				bad = baseName(dir);
				return false;
			}
			bool ok = true;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n == "." || n == "..") continue;
				std::string full = dir + "/" + n;
				struct stat st;
				if (lstat(full.c_str(), &st) != 0) continue;
				if (S_ISDIR(st.st_mode))
				{
					if (n[0] == '.' || !deletable(full, bad))
					{
						if (n[0] == '.') bad = n;
						ok = false;
						break;
					}
				}
			}
			closedir(d);
			return ok;
		}

		const char* errText(int e)
		{
			switch (e)
			{
			case EEXIST: return "exists";
			case ENOENT: return "not found";
			case EACCES: case EPERM: case EROFS: return "not allowed";
			case ENOSPC: return "space";
			default: return strerror(e);
			}
		}

		std::atomic<int> uploads{0};

		struct UploadSlot
		{
			bool ok;
			UploadSlot() : ok(uploads.fetch_add(1) < 2) {}
			~UploadSlot() { uploads.fetch_sub(1); }
		};

		// the answer to a request whose body we won't read: the connection must close, or the body would be read as
		// the next request
		// {error, code}: the message is shown as is; code is one of exists, rev, name, playing, type (or empty)
		void failCode(httplib::Response& res, int status, const std::string& msg, const std::string& code)
		{
			JsonWriter w;
			w.obj().key("error").str(msg);
			if (!code.empty()) w.key("code").str(code);
			w.end();
			WebServer::json(res, w.out(), status);
		}

		void failClose(httplib::Response& res, int status, const std::string& msg, const std::string& code = "")
		{
			failCode(res, status, msg, code);
			res.set_header("Connection", "close");
		}

		/* Copy, and Move to another card: a job of its own on the handheld (a thread), as gigabytes between cards take
		   minutes. The request answers at once with the job; the pages ask GET /api/jobs while one runs (an SSE "jobs"
		   event as one starts or ends, never per piece: progress would push the stream's other news out of its replay).
		   It goes on with no page open. Each item is copied out of sight and put in place last (treecopy.h); a move
		   removes the original only after that, and only if the handheld isn't playing it by then. */
		struct JobItem { std::string vp, real, target, name; bool dir; };
		// a file or folder moved or renamed here: what the handheld keeps of it (places, playlists, Liked, bookmarks, the
		// manga reader's series) follows it, on the main thread
		void movedOnHandheld(WebServer& ws, const std::string& from, const std::string& to)
		{
			int st = 200;
			ws.call([from, to](Player& p, Ui& u) { u.pathMoved(from, to, p); return std::string(); }, 5000, st);
		}

		struct Job
		{
			std::string id;
			bool copy = false;
			std::string toV, toName, realTo;
			std::vector<JobItem> items;
			std::atomic<bool> cancel{false};
			// under jobsLock
			std::string state = "waiting";	// waiting (another job copies), counting, copying, done, cancelled
			uint64_t total = 0, doneB = 0;
			std::string cur;
			int itemsDone = 0;
			std::vector<std::string> done;
			std::vector<Failed> failed;
			std::chrono::steady_clock::time_point ended;
		};
		std::mutex jobsLock;
		std::vector<std::shared_ptr<Job>> jobs;
		// one job copies at a time: two at once on one card are only slower
		std::mutex jobRun;
		std::atomic<unsigned> jobSeq{0};

		bool jobOver(const Job& j) { return j.state == "done" || j.state == "cancelled"; }

		// under jobsLock
		void jobJson(JsonWriter& w, const Job& j)
		{
			w.obj().key("id").str(j.id).key("kind").str(j.copy ? "copy" : "move").key("state").str(j.state)
				.key("total").num(j.total).key("done").num(j.doneB).key("n").num((int)j.items.size()).key("itemsDone").num(j.itemsDone)
				.key("cur").str(j.cur).key("to").str(j.toV).key("toName").str(j.toName);
			w.key("ok").arr();
			for (auto& d : j.done) w.str(d);
			w.end().key("failed").arr();
			for (auto& f : j.failed) w.obj().key("p").str(f.p).key("error").str(f.error).end();
			w.end().end();
		}

		// finished jobs are kept a while (a page opened later sees how one went), then forgotten. Under jobsLock
		void pruneJobs()
		{
			auto now = std::chrono::steady_clock::now();
			jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&](const std::shared_ptr<Job>& j) {
				return jobOver(*j) && now - j->ended > std::chrono::minutes(10);
			}), jobs.end());
		}

		void runJob(WebServer& ws, std::shared_ptr<Job> j)
		{
			std::unique_lock<std::mutex> run(jobRun);
			auto failItem = [&](const JobItem& it, const std::string& why) {
				std::lock_guard<std::mutex> g(jobsLock);
				j->failed.push_back({it.vp, why, 500});
			};
			{
				std::lock_guard<std::mutex> g(jobsLock);
				if (j->state == "cancelled") return;	// cancelled while it waited (that answer's given)
				j->state = "counting";
			}
			// a copy cut short by the player quitting left its hidden folder: nothing else copies now
			if (DIR* d = opendir(j->realTo.c_str()))
			{
				std::vector<std::string> stale;
				while (dirent* e = readdir(d))
					if (strncmp(e->d_name, ".bp-copy-", 9) == 0) stale.push_back(j->realTo + "/" + e->d_name);
				closedir(d);
				for (auto& s : stale) removeTree(s);
			}
			// measured first: what can't be copied whole is refused before a byte is written
			std::vector<JobItem> go;
			uint64_t total = 0;
			bool vfat = onVfat(j->realTo);
			for (auto& it : j->items)
			{
				TreeInfo info;
				if (!scanTree(it.real, info, [&ws](const std::string& p) { return ws.denies(p); }))
				{
					failItem(it, info.bad.empty() ? "can't be read" : "holds " + info.bad + ", which can't be copied (a link or a special file)");
					continue;
				}
				if (vfat && info.biggest >= VFAT_LIMIT)
				{
					failItem(it, "has a file over 4 GB, too big for that card");
					continue;
				}
				total += info.bytes;
				go.push_back(it);
			}
			uint64_t fr = 0, tot = 0;
			if (!go.empty() && freeSpace(j->realTo, fr, tot) && total + SPACE_MARGIN > fr)
			{
				// what can be used, as the uploads count it: a little is always left free
				uint64_t room = fr > SPACE_MARGIN ? fr - SPACE_MARGIN : 0;
				std::string why = "Not enough space on " + j->toName + ": needs " + fileNote("", -1, (long long)total) + ", there's " +
					fileNote("", -1, (long long)room) + " to use";
				for (auto& it : go) failItem(it, why);
				go.clear();
			}
			{
				std::lock_guard<std::mutex> g(jobsLock);
				j->total = total;
				j->state = "copying";
			}
			std::set<std::string> touched;
			for (auto& it : go)
			{
				if (j->cancel) break;
				uint64_t before;
				{
					std::lock_guard<std::mutex> g(jobsLock);
					j->cur = it.name;
					before = j->doneB;
				}
				int r = copyTree(it.real, it.target, [&](uint64_t add, const std::string& rel) {
					std::lock_guard<std::mutex> g(jobsLock);
					j->doneB += add;
					j->cur = it.dir ? it.name + "/" + rel : it.name;
					return !j->cancel.load();
				});
				if (r != 0)
				{
					{
						// what this item had counted goes back out: the bar shows what's on the card
						std::lock_guard<std::mutex> g(jobsLock);
						j->doneB = before;
					}
					if (r == ECANCELED) break;
					failItem(it, r == EEXIST ? "exists" : r == ENOSPC ? "Not enough space on " + j->toName : r == EFBIG ? "a file is too big for that card" : errText(r));
					continue;
				}
				touched.insert(j->realTo);
				if (!j->copy)
				{
					// asked again now: the handheld may have started it while it copied
					PlayingGuard pg(ws);
					if (int b = pg.busy(it.real, it.dir))
					{
						removeTree(it.target);
						failItem(it, b == 409 ? "playing on the handheld" : "busy");
						continue;
					}
					removeTree(it.real);
					touched.insert(dirName(it.real));
					struct stat st;
					if (lstat(it.real.c_str(), &st) == 0)
					{
						failItem(it, "copied, but the original couldn't all be removed");
						continue;
					}
					movedOnHandheld(ws, it.real, it.target);
				}
				std::lock_guard<std::mutex> g(jobsLock);
				j->itemsDone++;
				j->done.push_back(it.vp);
			}
			run.unlock();
			for (auto& t : touched)
			{
				ws.post(WebCall::Kind::FsChanged, t);
				std::string v = ws.toVirtual(t);
				if (!v.empty()) ws.notify("fs", JsonWriter().obj().key("dir").str(v).end().out());
			}
			{
				std::lock_guard<std::mutex> g(jobsLock);
				j->state = j->cancel ? "cancelled" : "done";
				j->cur.clear();
				j->ended = std::chrono::steady_clock::now();
			}
			ws.notify("jobs", "{}");
		}

		std::string startJob(WebServer& ws, bool copy, std::vector<JobItem> items, const std::string& toV, const std::string& realTo)
		{
			auto j = std::make_shared<Job>();
			j->id = std::to_string(++jobSeq) + "-" + randHex().substr(0, 6);
			j->copy = copy;
			j->toV = toV;
			j->realTo = realTo;
			j->items = std::move(items);
			std::string nv = toV;
			while (nv.size() > 1 && nv.back() == '/') nv.pop_back();
			j->toName = baseName(nv);
			if (isRootV(nv))
			{
				auto roots = ws.roots();
				size_t idx = (size_t)atoi(nv.c_str());
				j->toName = idx < roots.size() ? roots[idx].name : "the card";
			}
			JsonWriter w;
			{
				std::lock_guard<std::mutex> g(jobsLock);
				pruneJobs();
				jobs.push_back(j);
				jobJson(w, *j);
			}
			// the job outlives the request (and any page): a thread of its own, holding the job, gone when it ends
			std::thread([&ws, j]() { runJob(ws, j); }).detach();
			ws.notify("jobs", "{}");
			return w.out();
		}

		/* Move and Copy's checks, item by item, the same for both: a root can't go, nor a folder into itself, nor (a
		   move) what plays on the handheld, and nothing is ever replaced. false: `failed` says why, or it's where it
		   was asked to go already (a move to its own folder: `noop`) */
		bool planItem(WebServer& ws, PlayingGuard& pg, const std::string& vp, const std::string& vto, const std::string& realTo, bool copy,
			JobItem& out, std::vector<Failed>& failed, bool& noop)
		{
			noop = false;
			if (isRootV(vp))
			{
				failed.push_back({vp, copy ? "a root can't be copied" : "a root can't be moved", 403});
				return false;
			}
			std::string real;
			httplib::Response probe;
			struct stat st;
			if (!ws.resolve(vp, real, false, probe) || lstat(real.c_str(), &st) != 0)
			{
				failed.push_back({vp, "not found", 404});
				return false;
			}
			bool dir = S_ISDIR(st.st_mode);
			std::string name = baseName(real), target = realTo + "/" + name, check;
			if (dir && (realTo == real || realTo.compare(0, real.size() + 1, real + "/") == 0))
			{
				failed.push_back({vp, "a folder can't go into itself", 409});
				return false;
			}
			// a copy only reads, so what plays can be copied
			if (!copy)
				if (int b = pg.busy(real, dir))
				{
					failed.push_back({vp, b == 409 ? "playing on the handheld" : "busy", b});
					return false;
				}
			if (dirName(real) == realTo)
			{
				if (!copy) noop = true;
				else failed.push_back({vp, "exists", 409});
				return false;
			}
			if (!ws.resolve(joinV(vto, name), check, true, probe))
			{
				failed.push_back({vp, "not allowed there", 403});
				return false;
			}
			if (lstat(target.c_str(), &st) == 0)
			{
				failed.push_back({vp, "exists", 409});
				return false;
			}
			out = {vp, real, target, name, dir};
			return true;
		}
	}

	void registerFileRoutes(WebServer& ws)
	{
		httplib::Server& svr = ws.http();
		// uploads are streamed, never held, and the server's default cap (100 MB) is for the small bodies
		svr.set_payload_max_length((size_t)1 << 40);

		svr.Get("/api/roots", [&ws](const httplib::Request&, httplib::Response& res) {
			JsonWriter w;
			w.obj().key("roots").arr();
			auto roots = ws.roots();
			for (size_t i = 0; i < roots.size(); i++)
			{
				uint64_t fr = 0, tot = 0;
				freeSpace(roots[i].path, fr, tot);
				w.obj().key("i").num((int)i).key("path").str(std::to_string(i)).key("name").str(roots[i].name).key("free").num(fr).key("total").num(tot).end();
			}
			w.end().end();
			WebServer::json(res, w.out());
		});

		svr.Get("/api/ls", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string vp = req.get_param_value("p"), real;
			if (!ws.resolve(vp, real, false, res)) return;
			struct stat st;
			if (stat(real.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return WebServer::fail(res, 404, "not a folder");
			DIR* d = opendir(real.c_str());
			if (!d) return WebServer::fail(res, 403, "can't read this folder");
			// the handheld's setting (read on the main thread); unreachable: as if off, the safe side
			int shSt = 200;
			bool showHidden = ws.call([](Player& p, Ui&) { return std::string(p.settings.showHiddenFolders ? "1" : "0"); }, 1000, shSt) == "1" &&
				shSt == 200;
			struct Ent { std::string name; bool dir; bool hidden; uint64_t size; long long mtime; };
			std::vector<Ent> ents;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (n[0] == '.') continue;
				struct stat es;
				if (stat((real + "/" + n).c_str(), &es) != 0) continue;
				struct stat ls;
				if (lstat((real + "/" + n).c_str(), &ls) == 0 && S_ISLNK(ls.st_mode))
				{
					// a link that leaves the roots (or a denied folder) isn't shown
					std::string lr;
					httplib::Response scratch;
					if (!ws.resolve(joinV(vp, n), lr, false, scratch)) continue;
				}
				if (!S_ISDIR(es.st_mode) && !S_ISREG(es.st_mode)) continue;
				bool isDir = S_ISDIR(es.st_mode), hid = isDir && isHiddenFolder(real + "/" + n);
				if (hid && !showHidden) continue;
				ents.push_back({n, isDir, hid, (uint64_t)es.st_size, (long long)es.st_mtime});
			}
			closedir(d);
			std::sort(ents.begin(), ents.end(), [](const Ent& a, const Ent& b) {
				if (a.dir != b.dir) return a.dir;
				return naturalLess(a.name, b.name);
			});
			uint64_t fr = 0, tot = 0;
			freeSpace(real, fr, tot);
			std::string nv = vp, par;
			while (nv.size() > 1 && nv.back() == '/') nv.pop_back();
			auto roots = ws.roots();
			std::string name = baseName(nv);
			if (isRootV(nv))
			{
				size_t idx = (size_t)atoi(nv.c_str());
				if (idx < roots.size()) name = roots[idx].name;
			}
			JsonWriter w;
			w.obj().key("path").str(nv).key("p").str(nv).key("name").str(name).key("total").num(tot).key("parent");
			if (parentV(nv, par)) w.str(par);
			else w.null();
			w.key("free").num(fr).key("items").arr();
			for (auto& e : ents)
			{
				w.obj().key("name").str(e.name).key("p").str(joinV(nv, e.name)).key("dir").boolean(e.dir)
					.key("size").num(e.dir ? 0 : e.size).key("mtime").num(e.mtime).key("kind").str(kindOf(e.name, e.dir));
				if (e.dir)
				{
					if (e.hidden) w.key("hidden").boolean(true);
					bool more = false;
					int n = countVisible(real + "/" + e.name, more);
					w.key("items").num(n);
					if (more) w.key("itemsMore").boolean(true);	// n is the cap: "9999+ items"
				}
				else
				{
					Playable pl = isMediaFile(e.name) ? playableByName(e.name) : Playable::No;
					w.key("playable").str(pl == Playable::Yes ? "yes" : pl == Playable::Maybe ? "maybe" : "no");
				}
				w.end();
			}
			w.end().end();
			WebServer::json(res, w.out());
		});

		// a folder's ".hidden" marker, as the handheld's Hide folder makes it: on=1 creates the empty file, on=0 removes it
		svr.Post("/api/hide", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string vp = req.get_param_value("p"), real, on = req.get_param_value("on");
			if (on != "0" && on != "1") return WebServer::fail(res, 400, "on is 0 or 1");
			if (isRootV(vp)) return WebServer::fail(res, 403, "a root can't be hidden");
			if (!ws.resolve(vp, real, false, res)) return;
			struct stat st;
			if (stat(real.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return WebServer::fail(res, 404, "not a folder");
			std::string marker = real + "/.hidden";
			bool ok = true;
			if (on == "1")
			{
				if (!isHiddenFolder(real))
				{
					FILE* f = fopen(marker.c_str(), "w");
					ok = f && fclose(f) == 0;
				}
			}
			else if (isHiddenFolder(real)) ok = unlink(marker.c_str()) == 0;
			if (!ok) return WebServer::fail(res, 403, "Couldn't change the folder (is the card read-only?)");
			ws.post(WebCall::Kind::FsChanged, dirName(real));
			JsonWriter w;
			w.obj().key("ok").boolean(true).key("hidden").boolean(isHiddenFolder(real)).end();
			WebServer::json(res, w.out());
		});

		// the lengths of up to 32 files at once (the rows of Up next and Files as they scroll into view): {"<p>": seconds,
		// or -1 for none (not media, empty, corrupt)}. A path that can't be had (outside the roots, gone) is -1 too; one
		// left out (the request's 6 s spent: slow probes) is to be asked again
		svr.Post("/api/durations", [&ws](const httplib::Request& req, httplib::Response& res) {
			auto items = paramList(req, "p");
			if (items.empty()) return WebServer::fail(res, 400, "no paths");
			if (items.size() > 32) return WebServer::fail(res, 400, "32 paths at most");
			auto end = std::chrono::steady_clock::now() + std::chrono::seconds(6);
			std::set<std::string> seen;
			JsonWriter w;
			w.obj();
			for (auto& vp : items)
			{
				if (!seen.insert(vp).second) continue;
				if (std::chrono::steady_clock::now() > end) break;
				std::string real;
				httplib::Response scratch;
				double d = -1;
				if (ws.resolve(vp, real, false, scratch) && (isAudioFile(real) || isVideoFile(real))) d = probeDuration(real);
				w.key(vp).num(d > 0 ? std::round(d * 1000) / 1000 : -1.0);
			}
			w.end();
			WebServer::json(res, w.out());
		});

		svr.Get("/api/media", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return;
			MediaInfo m;
			if (!probeMedia(real, m)) return WebServer::fail(res, 422, "not a media file");
			JsonWriter w;
			w.obj().key("container").str(m.container).key("vcodec").str(m.vcodec).key("acodec").str(m.acodec)
				.key("duration").num(m.duration).key("mime").str(m.mime).key("playable").str(m.playable).key("chapters").arr();
			for (auto& c : m.chapters) w.obj().key("title").str(c.title).key("start").num(c.start).end();
			w.end().key("title").str(m.title).key("artist").str(m.artist).key("album").str(m.album)
				.key("hasCover").boolean(m.hasCover);
			auto optStr = [&](const char* k, const std::string& v) { if (!v.empty()) w.key(k).str(v); };
			auto optNum = [&](const char* k, double v) { if (v > 0) w.key(k).num(v); };
			optStr("year", m.year); optStr("genre", m.genre); optStr("track", m.track); optStr("disc", m.disc);
			optStr("albumArtist", m.albumArtist); optStr("composer", m.composer);
			optNum("size", (double)m.size); optNum("bitrate", (double)m.bitrate);
			optNum("sampleRate", m.sampleRate); optNum("channels", m.channels); optStr("channelLayout", m.channelLayout);
			optNum("audioBitrate", (double)m.audioBitrate);
			optNum("width", m.width); optNum("height", m.height); optNum("fps", m.fps);
			optNum("videoBitrate", (double)m.videoBitrate); optStr("pixFmt", m.pixFmt);
			w.key("audio").arr();
			for (auto& a : listAudio(m))
				w.obj().key("i").num(a.i).key("label").str(a.label).key("default").boolean(a.isDefault).key("codec").str(a.codec).key("channels").num(a.channels).key("lang").str(a.lang).end();
			w.end().key("subs").arr();
			auto subs = listSubtitles(real, m);
			for (auto& s : subs)
				w.obj().key("i").num(s.i).key("label").str(s.label).key("lang").str(s.lang).key("codec").str(s.codec)
					.key("kind").str(s.kind).key("external").boolean(s.external).key("styled").boolean(s.styled).end();
			w.end();
			// the subtitle to start with: the one the handheld remembers (resume.tsv's last language), else a forced
			// track, else none. Only a text or picture one can show
			std::string lastLang;
			bool lastOff = false;
			if (!subs.empty())
			{
				int st = 200;
				std::string r = ws.call([](Player& p, Ui&) {
					return std::string(p.resumeStore().lastSubOff ? "-" : "+") + p.resumeStore().lastSubLang;
				}, 1000, st);
				if (st == 200 && !r.empty())
				{
					lastOff = r[0] == '-';
					lastLang = r.substr(1);
				}
			}
			int def = -1;
			if (!lastOff)
			{
				std::string want = languageName(lastLang);
				for (auto& s : subs)
					if (def < 0 && s.kind != "image" && !lastLang.empty() && !s.lang.empty() && languageName(s.lang) == want) def = s.i;
				for (auto& s : subs)
					if (def < 0 && s.kind != "image" && s.forced) def = s.i;
			}
			w.key("subDefault").num(def).end();
			WebServer::json(res, w.out());
		});

		// one subtitle track as WebVTT: 200 the text, 202 {"progress"} while an embedded one is being read out of the
		// file, 415 for picture subtitles
		svr.Get("/api/subs", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return;
			MediaInfo m;
			if (!probeMedia(real, m)) return WebServer::fail(res, 422, "not a media file");
			auto subs = listSubtitles(real, m);
			std::string is = req.get_param_value("i");
			int i = is.empty() || is.find_first_not_of("0123456789") != std::string::npos ? -1 : atoi(is.c_str());
			if (i < 0 || i >= (int)subs.size()) return WebServer::fail(res, 404, "no such subtitle track");
			SubsAnswer a = subtitleVtt(real, subs[i]);
			res.set_header("Cache-Control", "no-store");
			res.set_header("X-Content-Type-Options", "nosniff");
			if (a.status == 200) res.set_content(a.body, a.type);
			else WebServer::json(res, a.body, a.status);
			res.status = a.status;
		});

		// an ASS/SSA track as the whole script, for JASSUB: 200 the text, 202 {"progress"} while an embedded one is read
		// out of the file, 415 not an ASS track
		svr.Get("/api/ass", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return;
			MediaInfo m;
			if (!probeMedia(real, m)) return WebServer::fail(res, 422, "not a media file");
			auto subs = listSubtitles(real, m);
			std::string is = req.get_param_value("i");
			int i = is.empty() || is.find_first_not_of("0123456789") != std::string::npos ? -1 : atoi(is.c_str());
			if (i < 0 || i >= (int)subs.size()) return WebServer::fail(res, 404, "no such subtitle track");
			SubsAnswer a = subtitleAss(real, subs[i]);
			res.set_header("Cache-Control", "no-store");
			if (a.status == 200) res.set_content(a.body, a.type);
			else WebServer::json(res, a.body, a.status);
			res.status = a.status;
		});

		// picture subtitles (VobSub, PGS, DVB) a window at a time: /api/picsubs?p=&i=&from=&to= (seconds, at most 120 s)
		svr.Get("/api/picsubs", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return;
			MediaInfo m;
			if (!probeMedia(real, m)) return WebServer::fail(res, 422, "not a media file");
			auto subs = listSubtitles(real, m);
			std::string is = req.get_param_value("i");
			int i = is.empty() || is.find_first_not_of("0123456789") != std::string::npos ? -1 : atoi(is.c_str());
			if (i < 0 || i >= (int)subs.size()) return WebServer::fail(res, 404, "no such subtitle track");
			auto num = [&](const char* k) {
				std::string v = req.get_param_value(k);
				return v.empty() || v.size() > 16 || v.find_first_not_of("0123456789.") != std::string::npos ? -1.0 : atof(v.c_str());
			};
			SubsAnswer a = pictureSubs(real, subs[i], num("from"), num("to"));
			res.set_header("Cache-Control", "no-store");
			WebServer::json(res, a.body, a.status);
			res.status = a.status;
		});

		// the fonts inside the file (Matroska attachments) for its ASS subtitles: {fonts: [{i, name, size}]}; one of
		// them by /api/font?p=&i=
		svr.Get("/api/fonts", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return;
			JsonWriter w;
			w.obj().key("fonts").arr();
			for (auto& f : listFonts(real)) w.obj().key("i").num(f.i).key("name").str(f.name).key("size").num((double)f.size).end();
			w.end().end();
			WebServer::json(res, w.out());
		});
		svr.Get("/api/font", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string real, bytes, mime;
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return;
			std::string is = req.get_param_value("i");
			int i = is.empty() || is.find_first_not_of("0123456789") != std::string::npos ? -1 : atoi(is.c_str());
			if (!fontAttachment(real, i, bytes, mime)) return WebServer::fail(res, 404, "no such font");
			res.set_header("Cache-Control", "private, max-age=3600");
			res.set_content(bytes, mime);
		});

		// Direct Stream: the video with audio track a (the /api/media audio list's i), copied into fragmented MP4 from
		// the keyframe at or before t. /api/remux-start says where that is ({start}) or why it can't ({error}, 422);
		// /api/remux streams it, one per page tab (tab=): a newer one ends the older
		auto remuxArgs = [&ws](const httplib::Request& req, httplib::Response& res, std::string& real, int& stream, double& t) {
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return false;
			MediaInfo m;
			if (!probeMedia(real, m))
			{
				WebServer::fail(res, 422, "not a media file");
				return false;
			}
			std::string as = req.get_param_value("a");
			int a = as.empty() || as.find_first_not_of("0123456789") != std::string::npos ? -1 : atoi(as.c_str());
			if (a < 0 || a >= (int)m.audio.size())
			{
				WebServer::fail(res, 404, "no such audio track");
				return false;
			}
			stream = m.audio[a].index;
			t = std::max(0.0, atof(req.get_param_value("t").c_str()));
			if (!(t < 1e7)) t = 0;
			return true;
		};
		svr.Get("/api/remux-start", [remuxArgs](const httplib::Request& req, httplib::Response& res) {
			std::string real, why;
			int stream = -1;
			double t = 0, start = 0;
			if (!remuxArgs(req, res, real, stream, t)) return;
			if (!remuxStart(real, stream, t, start, why))
			{
				fprintf(stderr, "remux: %s audio stream %d at %.1f refused: %s\n", real.c_str(), stream, t, why.c_str());
				return WebServer::fail(res, 422, why);
			}
			JsonWriter w;
			w.obj().key("start").num(start).end();
			WebServer::json(res, w.out());
		});
		svr.Get("/api/remux", [remuxArgs](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			int stream = -1;
			double t = 0;
			if (!remuxArgs(req, res, real, stream, t)) return;
			// one stream per tab: the newest wins (a seek, a switch of track)
			static std::mutex lock;
			static std::map<std::string, std::shared_ptr<std::atomic<bool>>>* byTab = new std::map<std::string, std::shared_ptr<std::atomic<bool>>>;
			auto stop = std::make_shared<std::atomic<bool>>(false);
			std::string tab = req.get_param_value("tab").substr(0, 64);
			{
				std::lock_guard<std::mutex> g(lock);
				auto& cur = (*byTab)[tab];
				if (cur) *cur = true;
				cur = stop;
			}
			res.set_header("Cache-Control", "no-store");
			res.set_chunked_content_provider("video/mp4",
				[real, stream, t, stop](size_t, httplib::DataSink& sink) {
					std::string err;
					remuxRun(real, stream, t, [&sink](const uint8_t* b, size_t n) { return sink.write((const char*)b, n); },
						[stop] { return stop->load(); }, err);
					sink.done();
					return true;
				},
				[tab, stop](bool) {
					std::lock_guard<std::mutex> g(lock);
					auto it = byTab->find(tab);
					if (it != byTab->end() && it->second == stop) byTab->erase(it);
				});
		});

		// the page couldn't play a Direct Stream it was given: what its browser said, for log.txt (the handheld never
		// sees the failure itself)
		svr.Post("/api/remux-failed", [](const httplib::Request& req, httplib::Response& res) {
			auto clean = [](std::string v) {
				v = v.substr(0, 300);
				for (char& c : v)
					if ((unsigned char)c < 32) c = ' ';
				return v;
			};
			fprintf(stderr, "remux: the browser couldn't play %s (audio %s at %s): %s · %s\n", clean(req.get_param_value("p")).c_str(),
				clean(req.get_param_value("a")).c_str(), clean(req.get_param_value("t")).c_str(), clean(req.get_param_value("why")).c_str(),
				clean(req.get_param_value("ua")).c_str());
			WebServer::json(res, "{\"ok\":true}");
		});

		svr.Get("/api/file", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return;
			int fd = open(real.c_str(), O_RDONLY);
			struct stat st;
			if (fd < 0 || fstat(fd, &st) != 0 || !S_ISREG(st.st_mode))
			{
				if (fd >= 0) close(fd);
				return WebServer::fail(res, 404, "not a file");
			}
#ifdef __linux__
			posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
			std::string name = baseName(real);
			res.set_header("X-Content-Type-Options", "nosniff");
			res.set_header("Cache-Control", "private, no-cache");
			// httplib adds it only to HEAD; without it a browser may not seek in an <audio>/<video> by Range
			res.set_header("Accept-Ranges", "bytes");
			if (req.get_param_value("dl") == "1")
				res.set_header("Content-Disposition", "attachment; filename=\"" + asciiFallback(name) + "\"; filename*=UTF-8''" + percentEncode(name));
			auto dropped = std::make_shared<uint64_t>(0);
			res.set_content_provider((size_t)st.st_size, mimeFor(name),
				[fd, dropped](size_t offset, size_t length, httplib::DataSink& sink) {
					char buf[64 * 1024];
					size_t want = std::min(length, sizeof buf);
					ssize_t n = pread(fd, buf, want, (off_t)offset);
					if (n <= 0) return false;
#ifdef __linux__
					// what was sent is behind us: let the page cache go (a card's 1 GB file would fill the RAM)
					if (offset > *dropped + 8 * MiB)
					{
						posix_fadvise(fd, (off_t)*dropped, (off_t)(offset - *dropped), POSIX_FADV_DONTNEED);
						*dropped = offset;
					}
#endif
					return sink.write(buf, (size_t)n);
				},
				[fd](bool) { close(fd); });
		});

		svr.Get("/api/cover", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string real, bytes, mime;
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return;
			if (!probeCover(real, bytes, mime)) return WebServer::fail(res, 404, "no cover");
			res.set_header("Cache-Control", "private, max-age=3600");
			res.set_header("X-Content-Type-Options", "nosniff");
			res.set_content(bytes, mime);
		});

		svr.Put("/api/upload", [&ws](const httplib::Request& req, httplib::Response& res, const httplib::ContentReader& reader) {
			UploadSlot slot;
			if (!slot.ok) return failClose(res, 429, "too many uploads at once");
			std::string lenStr = req.get_header_value("Content-Length");
			if (lenStr.empty() || lenStr.find_first_not_of("0123456789") != std::string::npos || lenStr.size() > 18)
				return failClose(res, 411, "Content-Length required");
			uint64_t length = strtoull(lenStr.c_str(), nullptr, 10);

			std::string name = req.get_param_value("name"), rel = req.get_param_value("rel");
			bool overwrite = req.get_param_value("overwrite") == "1";
			if (!validName(name) || name[0] == '.') return failClose(res, 400, "That name can't be used", "name");
			std::vector<std::string> parts;
			for (size_t i = 0; i < rel.size();)
			{
				size_t j = rel.find('/', i);
				if (j == std::string::npos) j = rel.size();
				std::string part = rel.substr(i, j - i);
				if (!part.empty())
				{
					if (!validName(part) || part[0] == '.') return failClose(res, 400, "That folder name can't be used", "name");
					parts.push_back(part);
				}
				i = j + 1;
			}
			std::string vdir = req.get_param_value("dir"), realDir;
			if (!ws.resolve(vdir, realDir, false, res))
			{
				res.set_header("Connection", "close");
				return;
			}
			struct stat st;
			if (stat(realDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return failClose(res, 404, "not a folder");

			uint64_t fr = 0, tot = 0;
			if (freeSpace(realDir, fr, tot) && fr < length + SPACE_MARGIN)
			{
				WebServer::fail(res, 507, "space");
				JsonWriter w;
				w.obj().key("error").str("space").key("need").num(length + SPACE_MARGIN).key("free").num(fr).end();
				res.body = w.out();
				res.set_header("Connection", "close");
				return;
			}
			if (length >= VFAT_LIMIT && onVfat(realDir)) return failClose(res, 413, "this card's format can't hold files of 4 GB or more");

			// the folder upload's folders, each checked and made in turn
			std::vector<std::string> touched;
			std::string curV = vdir;
			for (auto& part : parts)
			{
				std::string next = joinV(curV, part), nreal;
				if (!ws.resolve(next, nreal, true, res))
				{
					res.set_header("Connection", "close");
					return;
				}
				struct stat ps;
				if (stat(nreal.c_str(), &ps) != 0)
				{
					if (mkdir(nreal.c_str(), 0755) != 0 && errno != EEXIST) return failClose(res, 500, errText(errno));
					touched.push_back(realDir);
				}
				else if (!S_ISDIR(ps.st_mode)) return failClose(res, 409, "a file is in the way");
				realDir = nreal;
				curV = next;
			}

			std::string target = realDir + "/" + name;
			struct stat ts;
			bool exists = lstat(target.c_str(), &ts) == 0;
			if (exists && (!overwrite || S_ISDIR(ts.st_mode)))
				return failClose(res, 409, "There's already a file called " + name + " here", "exists");
			if (exists)
			{
				PlayingGuard pg(ws);
				if (int b = pg.busy(target, false)) return failClose(res, b, b == 409 ? "That file is playing on the handheld" : "Busy, try again", b == 409 ? "playing" : "");
			}

			// old leftovers of a dropped upload
			if (DIR* d = opendir(realDir.c_str()))
			{
				time_t now = time(nullptr);
				while (dirent* e = readdir(d))
				{
					std::string n = e->d_name;
					if (n.compare(0, 11, ".bp-upload-") != 0 || n.size() < 5 || n.compare(n.size() - 5, 5, ".part") != 0) continue;
					struct stat ps;
					if (stat((realDir + "/" + n).c_str(), &ps) == 0 && now - ps.st_mtime > 3600) unlink((realDir + "/" + n).c_str());
				}
				closedir(d);
			}

			std::string part = realDir + "/.bp-upload-" + randHex() + ".part";
			int fd = open(part.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
			if (fd < 0) return failClose(res, errno == ENOSPC ? 507 : 500, errText(errno));

			std::vector<char> buf;
			buf.reserve(256 * 1024);
			uint64_t total = 0, synced = 0;
			int err = 0;
			auto flush = [&]() {
				for (size_t off = 0; off < buf.size() && !err;)
				{
					ssize_t w = write(fd, buf.data() + off, buf.size() - off);
					if (w < 0) err = errno;
					else off += (size_t)w;
				}
				buf.clear();
			};
			bool read = reader([&](const char* data, size_t n) {
				if (total + n > length) return false;
				total += n;
				while (n > 0)
				{
					size_t take = std::min(n, buf.capacity() - buf.size());
					buf.insert(buf.end(), data, data + take);
					data += take;
					n -= take;
					if (buf.size() == buf.capacity()) flush();
				}
				if (err) return false;
#ifdef __linux__
				if (total - synced >= 8 * MiB)
				{
					// keep the dirty pages from piling up in RAM: write them out and let them go
					sync_file_range(fd, (off64_t)synced, (off64_t)(total - synced), SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE);
					posix_fadvise(fd, (off_t)synced, (off_t)(total - synced), POSIX_FADV_DONTNEED);
					synced = total;
				}
#endif
				return true;
			});
			if (read && !err) flush();
			if (read && !err && total == length && fsync(fd) != 0) err = errno;
			close(fd);
			if (!read || err || total != length)
			{
				unlink(part.c_str());
				return failClose(res, err == ENOSPC ? 507 : 400, err ? errText(err) : "upload cut short");
			}

			int r = overwrite ? (rename(part.c_str(), target.c_str()) == 0 ? 0 : errno) : renameNoReplace(part, target);
			if (r != 0)
			{
				unlink(part.c_str());
				return failCode(res, r == EEXIST ? 409 : 500, r == EEXIST ? "There's already a file called " + name + " here" : errText(r), r == EEXIST ? "exists" : "");
			}
			for (auto& t : touched) ws.post(WebCall::Kind::FsChanged, t);
			ws.post(WebCall::Kind::FsChanged, realDir);
			JsonWriter w;
			w.obj().key("ok").boolean(true).key("p").str(joinV(curV, name)).key("size").num(total).end();
			WebServer::json(res, w.out());
		});

		svr.Post("/api/mkdir", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string vp = req.get_param_value("p"), name = req.get_param_value("name"), real;
			if (!validName(name) || name[0] == '.') return failCode(res, 400, "That name can't be used", "name");
			if (!ws.resolve(vp, real, false, res)) return;
			struct stat st;
			if (stat(real.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return WebServer::fail(res, 404, "not a folder");
			std::string target = real + "/" + name, check;
			if (!ws.resolve(joinV(vp, name), check, true, res)) return;
			if (mkdir(target.c_str(), 0755) != 0) { int e = errno; return failCode(res, e == EEXIST ? 409 : 500, e == EEXIST ? "There's already a folder called " + name + " here" : errText(e), e == EEXIST ? "exists" : ""); }
			ws.post(WebCall::Kind::FsChanged, real);
			JsonWriter w;
			w.obj().key("ok").boolean(true).key("p").str(joinV(vp, name)).end();
			WebServer::json(res, w.out());
		});

		svr.Post("/api/rename", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string vp = req.get_param_value("p"), name = req.get_param_value("name"), real;
			if (isRootV(vp)) return WebServer::fail(res, 403, "a root can't be renamed");
			if (!validName(name) || name[0] == '.') return failCode(res, 400, "That name can't be used", "name");
			if (!ws.resolve(vp, real, false, res)) return;
			struct stat st;
			if (lstat(real.c_str(), &st) != 0) return WebServer::fail(res, 404, "not found");
			bool dir = S_ISDIR(st.st_mode);
			PlayingGuard pg(ws);
			if (int b = pg.busy(real, dir)) return failCode(res, b, b == 409 ? "That file is playing on the handheld" : "Busy, try again", b == 409 ? "playing" : "");
			std::string target = dirName(real) + "/" + name;
			if (target == real)
			{
				JsonWriter w;
				std::string par;
				parentV(vp, par);
				w.obj().key("ok").boolean(true).key("p").str(vp).end();
				return WebServer::json(res, w.out());
			}
			int r = renameNoReplace(real, target);
			if (r != 0) return failCode(res, r == EEXIST ? 409 : r == ENOENT ? 404 : 500, r == EEXIST ? "There's already an item called " + name + " here" : errText(r), r == EEXIST ? "exists" : "");
			movedOnHandheld(ws, real, target);
			ws.post(WebCall::Kind::FsChanged, dirName(real));
			std::string par;
			parentV(vp, par);
			JsonWriter w;
			w.obj().key("ok").boolean(true).key("p").str(joinV(par, name)).end();
			WebServer::json(res, w.out());
		});

		// Move: a rename on the same card, at once; what goes to another card is copied by a job (startJob), which
		// removes each original after its copy is in place. Copy: always a job. The answer: done/failed as for the other
		// multi-select operations, and `job` when one started
		auto moveOrCopy = [&ws](const httplib::Request& req, httplib::Response& res, bool copy) {
			auto items = paramList(req, "p");
			std::string vto = req.get_param_value("to"), realTo;
			if (items.empty()) return WebServer::fail(res, 400, copy ? "nothing to copy" : "nothing to move");
			if (!ws.resolve(vto, realTo, false, res)) return;
			struct stat ds;
			if (stat(realTo.c_str(), &ds) != 0 || !S_ISDIR(ds.st_mode)) return WebServer::fail(res, 404, "not a folder");
			PlayingGuard pg(ws);
			std::vector<std::string> done;
			std::vector<Failed> failed;
			std::vector<std::string> touched{realTo};
			std::vector<JobItem> job;
			for (auto& vp : items)
			{
				JobItem it;
				bool noop = false;
				if (!planItem(ws, pg, vp, vto, realTo, copy, it, failed, noop))
				{
					if (noop) done.push_back(vp);
					continue;
				}
				if (copy)
				{
					job.push_back(it);
					continue;
				}
				int r = renameNoReplace(it.real, it.target);
				if (r == EXDEV)
				{
					job.push_back(it);
					continue;
				}
				if (r != 0)
				{
					failed.push_back({vp, errText(r), r == EEXIST ? 409 : r == ENOENT ? 404 : 500});
					continue;
				}
				touched.push_back(dirName(it.real));
				done.push_back(vp);
				movedOnHandheld(ws, it.real, it.target);
			}
			std::sort(touched.begin(), touched.end());
			touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
			if (!done.empty())
				for (auto& t : touched) ws.post(WebCall::Kind::FsChanged, t);
			std::string jobJs = job.empty() ? "" : startJob(ws, copy, std::move(job), vto, realTo);
			int status;
			std::string body = multiAnswer(done, failed, status, jobJs);
			WebServer::json(res, body, status);
		};
		svr.Post("/api/move", [moveOrCopy](const httplib::Request& req, httplib::Response& res) { moveOrCopy(req, res, false); });
		svr.Post("/api/copy", [moveOrCopy](const httplib::Request& req, httplib::Response& res) { moveOrCopy(req, res, true); });

		// the Copy and Move jobs running, waiting, or ended lately
		svr.Get("/api/jobs", [](const httplib::Request&, httplib::Response& res) {
			JsonWriter w;
			w.obj().key("jobs").arr();
			{
				std::lock_guard<std::mutex> g(jobsLock);
				pruneJobs();
				for (auto& j : jobs) jobJson(w, *j);
			}
			w.end().end();
			WebServer::json(res, w.out());
		});

		// Cancel: the item being copied is removed (nothing half-copied stays); what's already copied, or moved, stays
		svr.Post("/api/jobs/cancel", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string id = req.get_param_value("id");
			bool found = false;
			{
				std::lock_guard<std::mutex> g(jobsLock);
				for (auto& j : jobs)
				{
					if (j->id != id) continue;
					found = true;
					j->cancel = true;
					// one still waiting for another job ends now; one copying ends at its next piece
					if (j->state == "waiting")
					{
						j->state = "cancelled";
						j->ended = std::chrono::steady_clock::now();
					}
				}
			}
			if (!found) return WebServer::fail(res, 404, "No such job");
			ws.notify("jobs", "{}");
			WebServer::json(res, "{\"ok\":true}");
		});

		svr.Post("/api/delete", [&ws](const httplib::Request& req, httplib::Response& res) {
			auto items = paramList(req, "p");
			if (items.empty()) return WebServer::fail(res, 400, "nothing to delete");
			bool stop = req.get_param_value("stop") == "1";	// stop the handheld if it plays what's deleted
			PlayingGuard pg(ws);
			std::vector<std::string> done, touched;
			std::vector<Failed> failed;
			for (auto& vp : items)
			{
				if (isRootV(vp))
				{
					failed.push_back({vp, "a root can't be deleted", 403});
					continue;
				}
				std::string real;
				httplib::Response probe;
				if (!ws.resolve(vp, real, false, probe))
				{
					failed.push_back({vp, "not found", 404});
					continue;
				}
				struct stat st;
				if (lstat(real.c_str(), &st) != 0)
				{
					failed.push_back({vp, "not found", 404});
					continue;
				}
				bool dir = S_ISDIR(st.st_mode);
				int busy = pg.busy(real, dir);
				if (busy == 409 && stop)
				{
					// "Stop and delete": the handheld lets go of the file first (back to its browser)
					stopIfPlaying(ws, real);
					pg.cur.clear();
					busy = 0;
				}
				if (busy)
				{
					failed.push_back({vp, busy == 409 ? "playing on the handheld" : "busy", busy});
					continue;
				}
				if (dir)
				{
					std::string bad;
					if (!deletable(real, bad))
					{
						failed.push_back({vp, "has a hidden folder: " + bad, 409});
						continue;
					}
					removeTree(real);
					struct stat after;
					if (lstat(real.c_str(), &after) == 0)
					{
						failed.push_back({vp, "couldn't remove everything", 500});
						continue;
					}
				}
				else if (unlink(real.c_str()) != 0)
				{
					failed.push_back({vp, errText(errno), errno == ENOENT ? 404 : 500});
					continue;
				}
				touched.push_back(dirName(real));
				done.push_back(vp);
			}
			std::sort(touched.begin(), touched.end());
			touched.erase(std::unique(touched.begin(), touched.end()), touched.end());
			for (auto& t : touched) ws.post(WebCall::Kind::FsChanged, t);
			int status;
			std::string body = multiAnswer(done, failed, status);
			WebServer::json(res, body, status);
		});
	}
}
