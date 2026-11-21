// Reading books in the dashboard (docs/redesign/web-books.md §8): a book's outline, a text's parts as the
// handheld's own blocks, a comic's pages as the archive holds them, an EPUB's pictures, and the one reading place.
// A PDF is drawn in the browser by pdf.js (web/vendor/pdfjs, the owner's call over the handheld drawing each page):
// the page reads the file through /api/file, so here it only has its place.
//
// Books are opened on the server's threads, each with a source of its own (the handheld's readers keep theirs on the
// main thread): a small cache, the last two, under one lock. Only the place and the bookmarks are the Ui's, reached
// through WebServer::call. Every path goes through resolve (the roots, the deny list); the gate has already asked for
// the session, and for X-BP + Origin on the POSTs.
#include "web/server.h"
#include "ui.h"
#include "ui_docs.h"
#include "json.h"
#include "bookplace.h"
#include "pagebook.h"
#include "textsource.h"
#include "epub.h"
#include "comicinfo.h"
#include "webmedia.h"
#include "httplib.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	namespace
	{
		// a TXT's parts: this many bytes each (at a piece's start, textdoc.h), so a part is a few hundred blocks
		constexpr uint64_t TXT_PART = 96 * 1024;
		constexpr int IDLE_S = 120;	// a book unused this long is closed at the next look

		struct OpenBook
		{
			std::string path, title;
			long long size = 0;
			int64_t mtime = 0;
			std::unique_ptr<TextSource> text;	// a TXT, a reflowable EPUB
			std::unique_ptr<PageBook> pages;	// a comic (CBZ, CBR, a picture EPUB)
			std::vector<uint64_t> parts;		// a text's parts: the first block's id of each (an EPUB: a spine document)
			bool epub = false, rtl = false;
			std::chrono::steady_clock::time_point used;
			std::mutex m;	// a source is used by one thread at a time
		};

		std::mutex cacheLock;
		std::vector<std::shared_ptr<OpenBook>> cache;	// the last two, newest last

		std::string extLower(const std::string& p)
		{
			size_t d = p.rfind('.'), s = p.rfind('/');
			if (d == std::string::npos || (s != std::string::npos && d < s)) return "";
			std::string e = p.substr(d + 1);
			for (char& c : e) c = (char)tolower((unsigned char)c);
			return e;
		}

		// a text's parts: an EPUB's spine documents; a TXT cut every TXT_PART bytes at the piece there
		void cutParts(OpenBook& b)
		{
			TextSource& s = *b.text;
			if (b.epub)
			{
				uint64_t n = s.end() >> 32;
				for (uint64_t i = 0; i < n; i++) b.parts.push_back(i << 32);
				return;
			}
			uint64_t first = s.first(), end = s.end();
			b.parts.push_back(first);
			for (uint64_t at = first + TXT_PART; at < end; at += TXT_PART)
			{
				uint64_t id = s.snap(at);
				if (id > b.parts.back() && id < end) b.parts.push_back(id);
			}
		}

		std::shared_ptr<OpenBook> openBook(const std::string& path, std::string& err)
		{
			struct stat st;
			if (stat(path.c_str(), &st) != 0) return err = "This book is no longer on the card.", nullptr;
			auto now = std::chrono::steady_clock::now();
			std::lock_guard<std::mutex> g(cacheLock);
			// the idle ones go (their files closed), and a changed file is opened anew
			cache.erase(std::remove_if(cache.begin(), cache.end(), [&](const std::shared_ptr<OpenBook>& b) {
				return now - b->used > std::chrono::seconds(IDLE_S) ||
					(b->path == path && (b->size != (long long)st.st_size || b->mtime != (int64_t)st.st_mtime));
			}), cache.end());
			for (auto& b : cache)
				if (b->path == path)
				{
					b->used = now;
					return b;
				}
			auto b = std::make_shared<OpenBook>();
			b->path = path;
			b->size = (long long)st.st_size;
			b->mtime = (int64_t)st.st_mtime;
			b->used = now;
			std::string e = extLower(path);
			b->epub = e == "epub";
			bool pictures = isPageBookName(path) && !b->epub;
			if (b->epub)
			{
				Epub ep;
				if (!ep.open(path)) return err = ep.error(), nullptr;
				pictures = ep.pictureBook();
				b->title = ep.title();
			}
			if (pictures)
			{
				b->pages = openPageBook(path, err);
				if (!b->pages) return nullptr;
				std::string xml;
				ComicInfo ci;
				if (b->pages->comicInfo(xml) && parseComicInfo(xml, ci))
				{
					b->rtl = ci.rightToLeft;
					if (b->title.empty()) b->title = ci.title;
				}
			}
			else if (b->epub || e == "txt")
			{
				b->text = b->epub ? openEpubText(path, err) : openTextFile(path, err);
				if (!b->text) return nullptr;
				if (b->title.empty()) b->title = b->text->title();
				cutParts(*b);
			}
			else return err = "This kind of file can't be read here.", nullptr;
			fprintf(stderr, "web: book opened %s (%s)\n", path.substr(path.rfind('/') + 1).c_str(), b->text ? "text" : "pages");
			cache.push_back(b);
			if (cache.size() > 2) cache.erase(cache.begin());
			return b;
		}

		void placeJson(JsonWriter& j, const ResumeEntry* e)
		{
			if (!e || e->position < 0 || (e->aid != PLACE_TEXT && e->aid != PLACE_PAGES)) { j.null(); return; }
			j.obj().key("t").str(e->aid == PLACE_PAGES ? "pages" : "text").key("pos").num(e->position).key("share").num(e->duration)
				.key("rev").num((double)e->rev).key("when").num((double)e->when).key("by").str(e->by).end();
		}

		// a block as the page lays it out: kind, id, its text in runs of one style ([style, text]), a picture's name
		void blockJson(JsonWriter& j, uint64_t id, const TextBlock& b)
		{
			j.obj().key("id").num((double)id).key("k").num((int)b.kind);
			if (b.kind == TextBlock::Image) j.key("img").str(b.image);
			if (b.spans.empty()) j.key("t").str(b.text);
			else
			{
				std::vector<TextBlock::Span> sp = b.spans;
				std::sort(sp.begin(), sp.end(), [](const TextBlock::Span& a, const TextBlock::Span& c) { return a.from < c.from; });
				j.key("r").arr();
				size_t at = 0, n = b.text.size();
				for (auto& s : sp)
				{
					size_t from = std::min<size_t>(std::max<size_t>(s.from, at), n), to = std::min<size_t>(s.to, n);
					if (from > at) j.arr().num(0).str(b.text.substr(at, from - at)).end();
					if (to > from) j.arr().num((int)s.style).str(b.text.substr(from, to - from)).end();
					at = std::max(at, to);
				}
				if (at < n) j.arr().num(0).str(b.text.substr(at)).end();
				j.end();
			}
			j.end();
		}

		std::string value(const httplib::Request& req, const std::string& name)
		{
			return req.has_param(name) ? req.get_param_value(name) : "";
		}

		// the bytes of a page or a picture, as the archive holds them: cached by the browser, never run (an SVG's
		// scripts, a page opened on its own)
		void sendBytes(const httplib::Request& req, httplib::Response& res, const OpenBook& b, const std::string& name,
			std::vector<uint8_t>& bytes, const std::string& tag)
		{
			std::string mime = mimeFor(name);
			if (mime.rfind("image/", 0) != 0) return WebServer::fail(res, 404, "Not a picture");
			std::string etag = "\"" + std::to_string(b.size) + "-" + std::to_string(b.mtime) + "-" + tag + "\"";
			res.set_header("ETag", etag);
			res.set_header("Cache-Control", "private, max-age=3600");
			res.set_header("X-Content-Type-Options", "nosniff");
			res.set_header("Content-Security-Policy", "sandbox");
			if (req.get_header_value("If-None-Match") == etag)
			{
				res.status = 304;
				return;
			}
			res.set_content(std::string(bytes.begin(), bytes.end()), mime);
			bytes.clear();
			bytes.shrink_to_fit();	// (the buffer goes back at once: the mmap threshold rule)
		}
	}

	void registerBookRoutes(WebServer& ws)
	{
		httplib::Server& svr = ws.http();
		WebServer* w = &ws;

		// a book's outline: {title, kind (text / pages / pdf), parts [[id, share]] and contents [{t, id, d}] (a text),
		// pages and rtl (a comic), para (a text's blocks are paragraphs, not a TXT's lines), place, marks}
		svr.Get("/api/book", [w](const httplib::Request& req, httplib::Response& res) {
			std::string v = value(req, "p"), real;
			if (!w->resolve(v, real, false, res)) return;
			if (!isDocumentName(real) && !isBookName(real)) return WebServer::fail(res, 400, "Not a book");
			JsonWriter j;
			j.obj();
			if (isPdfName(real))
			{
				std::string n = real.substr(real.rfind('/') + 1);
				j.key("kind").str("pdf").key("title").str(stripBookExt(n));
			}
			else
			{
				std::string err;
				auto b = openBook(real, err);
				if (!b) return WebServer::fail(res, 422, err.empty() ? "This book couldn't be opened here." : err);
				std::lock_guard<std::mutex> g(b->m);
				std::string n = real.substr(real.rfind('/') + 1);
				j.key("title").str(b->title.empty() ? stripBookExt(n) : b->title);
				if (b->pages)
				{
					j.key("kind").str("pages").key("pages").num((int)b->pages->pages().size()).key("rtl").boolean(b->rtl);
				}
				else
				{
					TextSource& s = *b->text;
					j.key("kind").str("text").key("para").boolean(s.paragraphs()).key("parts").arr();
					for (uint64_t id : b->parts) j.arr().num((double)id).num(s.fraction(id)).end();
					j.end().key("contents").arr();
					for (auto& c : s.contents()) j.obj().key("t").str(c.title).key("id").num((double)c.id).key("d").num(c.depth).end();
					j.end();
				}
			}
			// the place and the bookmarks are the handheld's
			int status = 200;
			std::string tail = w->call([real](Player&, Ui& u) {
				JsonWriter t;
				t.obj().key("place");
				placeJson(t, u.docs->placeOf(real));
				t.key("marks").arr();
				u.docs->docMarksLoad();
				for (auto& m : u.docs->docMarks)
					if (m.path == real) t.obj().key("id").num((double)m.id).key("frac").num(m.frac).key("label").str(m.label).end();
				return t.end().end().out();
			}, 3000, status);
			if (status != 200) return WebServer::json(res, tail, status);
			// (the two objects joined: the outline's fields, then the handheld's)
			std::string head = j.end().out();
			WebServer::json(res, head.substr(0, head.size() - 1) + "," + tail.substr(1));
		});

		// one part of a text: {i, blocks: [{id, k, t | r, img}]}
		svr.Get("/api/book/part", [w](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!w->resolve(value(req, "p"), real, false, res)) return;
			std::string err;
			auto b = openBook(real, err);
			if (!b || !b->text) return WebServer::fail(res, 422, b ? "Not a text" : err);
			std::lock_guard<std::mutex> g(b->m);
			long i = atol(value(req, "i").c_str());
			if (i < 0 || i >= (long)b->parts.size()) return WebServer::fail(res, 404, "No such part");
			TextSource& s = *b->text;
			uint64_t id = b->parts[i], stop = i + 1 < (long)b->parts.size() ? b->parts[i + 1] : s.end();
			JsonWriter j;
			j.obj().key("i").num((int)i).key("blocks").arr();
			TextBlock blk;
			uint64_t next = 0;
			while (id < stop && s.block(id, blk, next))
			{
				blockJson(j, id, blk);
				if (next <= id) break;
				id = next;
			}
			j.end().end();
			WebServer::json(res, j.out());
		});

		// an EPUB's picture (its name in the archive, as a block names it)
		svr.Get("/api/book/img", [w](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!w->resolve(value(req, "p"), real, false, res)) return;
			std::string err, name = value(req, "n");
			auto b = openBook(real, err);
			if (!b || !b->text) return WebServer::fail(res, 404, "No picture");
			std::vector<uint8_t> bytes;
			{
				std::lock_guard<std::mutex> g(b->m);
				if (!b->text->image(name, bytes)) return WebServer::fail(res, 404, "No picture");
			}
			sendBytes(req, res, *b, name, bytes, name);
		});

		// a comic's page n (0-based): the archive's picture as it is
		svr.Get("/api/book/page", [w](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!w->resolve(value(req, "p"), real, false, res)) return;
			std::string err;
			auto b = openBook(real, err);
			if (!b || !b->pages) return WebServer::fail(res, 422, b ? "Not a book of pages" : err);
			long n = atol(value(req, "n").c_str());
			std::vector<uint8_t> bytes;
			std::string name;
			{
				std::lock_guard<std::mutex> g(b->m);
				const auto& names = b->pages->pages();
				if (n < 0 || n >= (long)names.size()) return WebServer::fail(res, 404, "No such page");
				name = names[n];
				if (!b->pages->read(name, bytes)) return WebServer::fail(res, 422, b->pages->error());
			}
			sendBytes(req, res, *b, name, bytes, std::to_string(n));
		});

		// the place: {p, t (text / pages), pos, share, base (the rev read), force, tab} -> {verdict, place}; a
		// conflict (the handheld or another browser moved it elsewhere since base) is a 409 with the stored place
		svr.Post("/api/book/place", [w](const httplib::Request& req, httplib::Response& res) {
			std::string v = value(req, "p"), real;
			if (!w->resolve(v, real, false, res)) return;
			if (!isDocumentName(real) && !isBookName(real)) return WebServer::fail(res, 400, "Not a book");
			PlaceWrite pw;
			pw.path = real;
			pw.kind = value(req, "t") == "pages" ? PLACE_PAGES : PLACE_TEXT;
			pw.position = strtod(value(req, "pos").c_str(), nullptr);
			pw.share = strtod(value(req, "share").c_str(), nullptr);
			pw.base = strtoull(value(req, "base").c_str(), nullptr, 10);
			pw.force = value(req, "force") == "1";
			pw.by = w->tabName(value(req, "tab"));
			pw.when = (int64_t)time(nullptr);
			if (pw.position < 0 || pw.position != pw.position) return WebServer::fail(res, 400, "No place");
			int status = 200;
			std::string body = w->call([pw](Player&, Ui& u) {
				ResumeEntry out;
				PlaceVerdict verdict = u.docs->webPlace(pw, out);
				if (verdict == PlaceVerdict::Conflict) WebCall::setStatus(409);
				JsonWriter j;
				j.obj().key("verdict").str(verdict == PlaceVerdict::Written ? "written" : verdict == PlaceVerdict::Same ? "same" : "conflict").key("place");
				placeJson(j, &out);
				return j.end().out();
			}, 3000, status);
			WebServer::json(res, body, status);
		});

		// bookmarks (a text's): {p, id, frac, label} added (or renamed at that place); {p, id} removed
		svr.Post("/api/book/mark", [w](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!w->resolve(value(req, "p"), real, false, res)) return;
			if (!isDocumentName(real)) return WebServer::fail(res, 400, "Not a book");
			uint64_t id = strtoull(value(req, "id").c_str(), nullptr, 10);
			double frac = std::clamp(strtod(value(req, "frac").c_str(), nullptr), 0.0, 1.0);
			std::string label = value(req, "label").substr(0, 200);
			if (label.empty()) label = "Bookmark";
			int status = 200;
			std::string body = w->call([real, id, frac, label](Player&, Ui& u) {
				u.docs->webMarkAdd(real, id, frac, label);
				return std::string("{\"ok\":true}");
			}, 3000, status);
			WebServer::json(res, body, status);
		});
		svr.Post("/api/book/mark/remove", [w](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!w->resolve(value(req, "p"), real, false, res)) return;
			uint64_t id = strtoull(value(req, "id").c_str(), nullptr, 10);
			int status = 200;
			std::string body = w->call([real, id](Player&, Ui& u) {
				if (!u.docs->webMarkRemove(real, id)) WebCall::setStatus(404);
				return std::string("{\"ok\":true}");
			}, 3000, status);
			WebServer::json(res, body, status);
		});
	}
}
