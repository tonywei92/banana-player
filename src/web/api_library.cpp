// The cards' library for the dashboard's own tabs (docs/redesign: the Videos and Music pages, and Books): the
// index that Music and Videos read (mediaindex.h), the places the resume store keeps for the video rows, and the
// books' folders' walk. All of it is the UI's own, so the whole answer is built in one ws.call on the main
// thread; the paths go out as the dashboard's virtual ones (servedVirtual — what the web never serves is left
// out, never listed), and books open on the handheld through the Recent route's open, the same the Recent cards
// use (api_playlists.cpp). The tabs themselves: docs/redesign/src/screens.js "videos" notes ("a tab of its own,
// between Files and Music... a click plays in the browser or on the handheld, as from Files") and the Music
// page's ("the same grid... a click opens the album, with Play and Shuffle"; "Playlists is no longer a tab of the
// dashboard: it is Music's third view there too").
#include "web/server.h"
#include "ui.h"
#include "json.h"
#include "mediaindex.h"
#include "resume.h"
#include "format.h"
#include "playlist.h"
#include "pagebook.h"
#include "pagedecode.h"
#include "epub.h"
#include "videocache.h"
#include "ui_docs.h"
#include "httplib.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <set>
#include <sys/stat.h>
#include <errno.h>
#include <string>
#include <vector>

namespace bp
{
	namespace
	{
		// the file's name as a row titles it: less its extension, the part after the last slash (the index keeps
		// only the folded form for videos, so the shown one is made here)
		std::string nameOf(const std::string& path)
		{
			std::string n = path.substr(path.rfind('/') + 1);
			size_t dot = n.rfind('.');
			if (dot != std::string::npos && dot > 0) n.resize(dot);
			return n;
		}

		// one song of an album's or an artist's page, as the row draws it
		void songJson(JsonWriter& w, const MediaSnapshot& snap, const MediaSong& s, WebServer& ws, bool withAlbum)
		{
			std::string p = ws.servedVirtual(s.path);
			w.obj();
			if (p.empty()) { w.key("gone").boolean(true).key("p").str(s.path); w.end(); return; }
			w.key("p").str(p).key("title").str(snap.at(s.title));
			if (withAlbum) w.key("album").str(snap.at(s.album));
			if (s.track > 0) w.key("track").num(s.track);
			if (s.duration > 0) w.key("dur").num(s.duration);
			if (s.video) w.key("video").boolean(true);
			w.end();
		}


		// an image's type by its first bytes (the covers are sent as they are in the book)
		std::string sniffMime(const std::string& b)
		{
			if (b.size() >= 4 && (unsigned char)b[0] == 0x89 && b[1] == 'P') return "image/png";
			if (b.size() >= 6 && !memcmp(b.data(), "GIF8", 4)) return "image/gif";
			if (b.size() >= 12 && !memcmp(b.data(), "RIFF", 4) && !memcmp(b.data() + 8, "WEBP", 4)) return "image/webp";
			return "image/jpeg";
		}

		// a PDF's first page, drawn small, as a 24-bit BMP (the browser shows one; no encoder is linked in)
		std::string bmpOf(const PageImage& img)
		{
			std::string out;
			uint32_t row = ((uint32_t)img.w * 3 + 3) & ~3u, size = 54 + row * (uint32_t)img.h;
			auto le = [&](uint32_t v, int n) { for (int i = 0; i < n; i++) out += (char)((v >> (8 * i)) & 0xFF); };
			out += "BM"; le(size, 4); le(0, 4); le(54, 4); le(40, 4); le((uint32_t)img.w, 4); le((uint32_t)img.h, 4);
			le(1, 2); le(24, 2); le(0, 4); le(row * (uint32_t)img.h, 4); le(2835, 4); le(2835, 4); le(0, 4); le(0, 4);
			for (int y = img.h - 1; y >= 0; y--)
			{
				const uint8_t* px = &img.rgba[(size_t)y * img.w * 4];
				for (int x = 0; x < img.w; x++) { out += (char)px[x * 4 + 2]; out += (char)px[x * 4 + 1]; out += (char)px[x * 4]; }
				for (uint32_t pad = (uint32_t)img.w * 3; pad < row; pad++) out += '\0';
			}
			return out;
		}

		// a book's cover as the file holds it: an EPUB's own image (else its first picture), a comic's first page, a
		// PDF's first page drawn. false: the book has none (the page keeps its drawn sleeve)
		bool bookCoverBytes(const std::string& path, std::string& bytes, std::string& mime)
		{
			std::string low = path.substr(path.rfind('.') == std::string::npos ? path.size() : path.rfind('.'));
			for (char& c : low) c = (char)tolower((unsigned char)c);
			std::vector<uint8_t> b;
			if (low == ".epub")
			{
				Epub e;
				if (!e.open(path)) return false;
				std::string name = e.coverImage();
				if (name.empty()) { auto pics = e.pictures(); if (!pics.empty()) name = pics.front(); }
				if (name.empty() || !e.read(name, b)) return false;
			}
			else if (isPageBookName(path))
			{
				std::string err;
				auto book = openPageBook(path, err);
				if (!book || book->pages().empty()) return false;
				if (book->rendered())
				{
					PageImage img = book->render(0, 240, 2000000);
					if (!img.ok()) return false;
					bytes = bmpOf(img);
					mime = "image/bmp";
					return true;
				}
				if (!book->read(book->pages()[0], b)) return false;
			}
			else return false;
			if (b.empty() || b.size() > (size_t)16 << 20) return false;
			bytes.assign((const char*)b.data(), b.size());
			mime = sniffMime(bytes);
			return true;
		}

		// Upload's targets: a section's folder on a card is the one already there by any of its names (the first,
		// by path), else "Videos" / "Music" / "Books" to be made. Only the card's top level counts, as the index reads.
		bool kindOf(const std::string& s, LibKind& kind)
		{
			if (s == "videos") kind = LibKind::Videos;
			else if (s == "music") kind = LibKind::Music;
			else if (s == "books") kind = LibKind::Books;
			else return false;
			return true;
		}
		std::string sectionFolderOn(const std::string& card, LibKind kind, bool& there)
		{
			for (auto& f : libraryFolders(kind))
				if (f.compare(0, card.size() + 1, card + "/") == 0 && f.find('/', card.size() + 1) == std::string::npos)
				{
					there = true;
					return f;
				}
			there = false;
			const char* name = kind == LibKind::Videos ? "Videos" : kind == LibKind::Music ? "Music" : "Books";
			return (card == "/" ? "" : card) + "/" + name;
		}
		// the card as the dashboard names it ("SD1", "SD2": the index's order) and its root's index there, -1 when the
		// dashboard doesn't serve that card
		int rootOf(WebServer& ws, const std::string& card)
		{
			auto roots = ws.roots();
			for (size_t i = 0; i < roots.size(); i++)
				if (roots[i].path == card) return (int)i;
			return -1;
		}
	}

	void registerLibraryRoutes(WebServer& ws)
	{
		httplib::Server& svr = ws.http();

		// Upload's folder: made when it is missing (Videos / Music / Books at the top of the card), and its virtual path
		// either way. A POST: the server's X-BP + Origin + session checks have already run for it
		svr.Post("/api/library/folder", [&ws](const httplib::Request& req, httplib::Response& res) {
			LibKind kind;
			std::string k = req.get_param_value("kind"), c = req.get_param_value("card");
			std::vector<std::string> cards = mediaCards();
			if (!kindOf(k, kind) || c.empty() || c.find_first_not_of("0123456789") != std::string::npos || c.size() > 3)
				return WebServer::fail(res, 400, "Unknown section or card");
			size_t ci = (size_t)atoi(c.c_str());
			if (ci >= cards.size() || rootOf(ws, cards[ci]) < 0) return WebServer::fail(res, 404, "No such card");
			bool there = false;
			std::string real = sectionFolderOn(cards[ci], kind, there), check;
			if (!there)
			{
				// (resolve for create checks the parent is inside a root and not denied, as every upload does)
				if (!ws.resolve(ws.toVirtual(real), check, true, res)) return;
				if (mkdir(real.c_str(), 0755) != 0 && errno != EEXIST)
					return WebServer::fail(res, 500, std::string("Couldn't make the folder: ") + strerror(errno));
				fprintf(stderr, "library: made %s for an upload from the dashboard\n", real.c_str());
				ws.post(WebCall::Kind::FsChanged, cards[ci]);
			}
			std::string v = ws.servedVirtual(real);
			if (v.empty()) return WebServer::fail(res, 403, "That folder isn't served");
			JsonWriter w;
			w.obj().key("ok").boolean(true).key("p").str(v).key("made").boolean(!there).end();
			WebServer::json(res, w.out());
		});


		// a video's list thumbnail from the handheld's cache (<state>/cache/videos, keyed by path + size + mtime, as
		// videoartworker writes it). 404 until the handheld has made one; the key changes with the file, so it
		// is cacheable and answers If-None-Match
		svr.Get("/api/vthumb", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return;
			struct stat st;
			if (stat(real.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return WebServer::fail(res, 404, "no thumbnail");
			std::string name = videoCacheName(real, (long long)st.st_size, (int64_t)st.st_mtime);
			std::string etag = "\"" + name + "\"";
			if (req.get_header_value("If-None-Match") == etag) { res.status = 304; res.set_header("ETag", etag); return; }
			VideoCacheFiles f = videoCacheFiles(ws.stateDir() + "/cache/videos", name);
			std::string bytes;
			if (FILE* fp = fopen(f.small.c_str(), "rb"))
			{
				char buf[8192];
				size_t n;
				while ((n = fread(buf, 1, sizeof buf, fp)) > 0 && bytes.size() < ((size_t)1 << 20)) bytes.append(buf, n);
				fclose(fp);
			}
			if (bytes.empty()) return WebServer::fail(res, 404, "no thumbnail");
			res.set_header("Cache-Control", "private, max-age=86400");
			res.set_header("ETag", etag);
			res.set_header("X-Content-Type-Options", "nosniff");
			res.set_content(bytes, "image/jpeg");
		});

		// a book's cover (Books' tiles): as the file holds it; 404 for one with none
		svr.Get("/api/bookcover", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string real, bytes, mime;
			if (!ws.resolve(req.get_param_value("p"), real, false, res)) return;
			if (!isBookName(real) || !bookCoverBytes(real, bytes, mime)) return WebServer::fail(res, 404, "no cover");
			res.set_header("Cache-Control", "private, max-age=3600");
			res.set_header("X-Content-Type-Options", "nosniff");
			res.set_content(bytes, mime);
		});

		// Music's Folders view: with no ?dir the music folders on the cards; with one, that folder's subfolders (with
		// the songs under each) and the songs directly in it, from the index. Only under a music folder
		svr.Get("/api/library/music", [&ws](const httplib::Request& req, httplib::Response& res) {
			std::string dir = req.get_param_value("dir"), real;
			if (!dir.empty() && !ws.resolve(dir, real, false, res)) return;
			int status = 200;
			std::string body = ws.call([&](Player&, Ui& u) {
				JsonWriter w;
				w.obj();
				auto snap = u.mediaIndex ? u.mediaIndex->snapshot() : nullptr;
				MediaFolders mf = mediaFolders();
				if (!snap) { w.key("reading").boolean(true).end(); return w.out(); }
				auto parentOf = [](const std::string& p) { size_t i = p.rfind('/'); return i == std::string::npos ? std::string() : p.substr(0, i); };
				std::vector<std::string> rootsReal = mf.music;
				if (dir.empty())
				{
					w.key("dir").str("").key("folders").arr();
					for (auto& r : rootsReal)
					{
						std::string v = ws.servedVirtual(r);
						if (v.empty()) continue;
						int n = 0;
						for (auto& s : snap->songs) if (s.path.compare(0, r.size() + 1, r + "/") == 0) n++;
						w.obj().key("p").str(v).key("n").str(r.substr(r.rfind('/') + 1)).key("count").num(n).end();
					}
					w.end().key("songs").arr().end().end();
					return w.out();
				}
				// the folder must be one of the music folders or inside one
				std::string root;
				for (auto& r : rootsReal)
					if (real == r || real.compare(0, r.size() + 1, r + "/") == 0) root = r;
				if (root.empty()) { WebCall::setStatus(404); return std::string("{\"error\":\"Not a music folder\"}"); }
				std::map<std::string, int> sub;
				std::vector<const MediaSong*> here;
				for (auto& s : snap->songs)
				{
					if (s.path.compare(0, real.size() + 1, real + "/") != 0) continue;
					std::string rest = s.path.substr(real.size() + 1);
					size_t slash = rest.find('/');
					if (slash == std::string::npos) here.push_back(&s);
					else sub[rest.substr(0, slash)]++;
				}
				std::string up = real == root ? "" : ws.servedVirtual(parentOf(real));
				w.key("dir").str(dir).key("up").str(up).key("name").str(real.substr(real.rfind('/') + 1)).key("folders").arr();
				for (auto& kv : sub)
				{
					std::string v = ws.servedVirtual(real + "/" + kv.first);
					if (!v.empty()) w.obj().key("p").str(v).key("n").str(kv.first).key("count").num(kv.second).end();
				}
				w.end().key("songs").arr();
				std::sort(here.begin(), here.end(), [&](const MediaSong* a, const MediaSong* b) { return naturalLess(a->path, b->path); });
				for (const MediaSong* s : here) songJson(w, *snap, *s, ws, true);
				w.end().end();
				return w.out();
			}, 4000, status);
			WebServer::json(res, body, status);
		});

		// one answer for the three pages: the counts and the tiles; ?album=?artist= asks for that one's songs
		svr.Get("/api/library", [&ws](const httplib::Request& req, httplib::Response& res) {
			int album = atoi(req.get_param_value("album").c_str());
			int artist = atoi(req.get_param_value("artist").c_str());
			bool asked = req.has_param("album") || req.has_param("artist");
			int status = 200;
			std::string body = ws.call([&](Player& p, Ui& u) {
				JsonWriter w;
				w.obj();
				// an album's or an artist's own page: the songs of it, the one the click asked for
				if (asked)
				{
					auto snap = u.mediaIndex ? u.mediaIndex->snapshot() : nullptr;
					if (!snap) { w.key("reading").boolean(true).end(); return w.out(); }
					std::string title, note;
					std::vector<const MediaSong*> songs;
					bool byAlbum = req.has_param("album");
					if (byAlbum && album >= 0 && album < (int)snap->albums.size())
					{
						const MediaAlbum& a = snap->albums[album];
						title = snap->at(a.name);
						std::string who = a.albumArtist ? snap->at(a.albumArtist) : "Unknown artist";
						int32_t year = 0, videos = 0;
						double total = 0;
						for (uint32_t si : a.songs)
						{
							songs.push_back(&snap->songs[si]);
							if (!year && snap->songs[si].year) year = snap->songs[si].year;
							if (snap->songs[si].video) videos++;
							if (snap->songs[si].duration > 0) total += snap->songs[si].duration;
						}
						note = who + (year ? " \xC2\xB7 " + std::to_string(year) : "") + " \xC2\xB7 " +
							std::to_string(a.songs.size()) + (a.songs.size() == 1 ? " song" : " songs") +
							(videos ? ", " + std::to_string(videos) + (videos == 1 ? " a video" : " videos") : "") +
							(total > 0 ? " \xC2\xB7 " + formatTime(total) : "");
					}
					else if (!byAlbum && artist >= 0 && artist < (int)snap->artists.size())
					{
						const MediaArtist& a = snap->artists[artist];
						title = snap->at(a.name);
						note = std::to_string(a.albums.size()) + (a.albums.size() == 1 ? " album" : " albums") +
							" \xC2\xB7 " + std::to_string(a.songs) + (a.songs == 1 ? " song" : " songs");
						// their songs by album and track, as the handheld's artist page lists them
						for (uint32_t ai : a.albums)
							for (uint32_t si : snap->albums[ai].songs) songs.push_back(&snap->songs[si]);
					}
					w.key("title").str(title).key("sub").str(note).key("songs").arr();
					for (const MediaSong* s : songs) songJson(w, *snap, *s, ws, !byAlbum);
					w.end().end();
					return w.out();
				}
				// the three pages' one answer: the from-lines, the videos with their places, the tiles, the shelf
				auto snap = u.mediaIndex ? u.mediaIndex->snapshot() : nullptr;
				w.key("gen").num(snap ? (int)u.mediaIndex->generation() : 0);
				MediaFolders mf = mediaFolders();
				w.key("musicFrom").str(mediaFromWords(mf.music));
				w.key("videoFrom").str(mediaFromWords(mf.videos));
				// the empty Music page's upload button: the first Music folder, as the dashboard names it ("" when
				// there is none, or one it never serves)
				std::string musicDir = mf.music.empty() ? std::string() : ws.servedVirtual(mf.music.front());
				w.key("musicDir").str(musicDir);
				w.key("videoDirs").arr();
				for (auto& d : mf.videos)
				{
					std::string v = ws.servedVirtual(d);
					if (!v.empty()) w.str(v);
				}
				w.end();
				// where Upload can send (the Videos, Music and Books tabs): each card the dashboard serves, named as the
				// handheld's cards are, with the section folder already there ("" when there is none yet)
				w.key("cards").arr();
				{
					std::vector<std::string> cards = mediaCards();
					for (size_t c = 0; c < cards.size(); c++)
					{
						if (rootOf(ws, cards[c]) < 0) continue;
						w.obj().key("card").num((int)c).key("name").str("SD" + std::to_string(c + 1));
						for (const char* k : {"videos", "music", "books"})
						{
							LibKind kind;
							kindOf(k, kind);
							bool there = false;
							std::string folder = sectionFolderOn(cards[c], kind, there);
							w.key(k).str(there ? ws.servedVirtual(folder) : "");
						}
						w.end();
					}
				}
				w.end();
				w.key("videos").arr();
				if (snap)
				{
					// the video rows' places, as the handheld's Not finished reads them; `at` is the store's own
					// order (newest first), which that view sorts by
					std::map<std::string, std::tuple<double, double, int>> place;
					for (auto& e : p.resumeStore().all())
						if (e.position >= 0 && worthResuming(e.position, e.duration))
							place[e.path] = {e.position, e.duration, (int)place.size()};
					for (auto& v : snap->videos)
					{
						std::string vp = ws.servedVirtual(v.path);
						if (vp.empty()) continue;
						w.obj().key("p").str(vp).key("n").str(nameOf(v.path));
						if (v.size > 0) w.key("size").num(v.size);
						if (v.mtime) w.key("mtime").num(v.mtime);
						auto it = place.find(v.path);
						if (it != place.end())
							w.key("pos").num(std::get<0>(it->second)).key("dur").num(std::get<1>(it->second))
								.key("at").num(std::get<2>(it->second));
						w.end();
					}
				}
				w.end().key("albums").arr();
				if (snap)
				{
					for (size_t i = 0; i < snap->albums.size(); i++)
					{
						const MediaAlbum& a = snap->albums[i];
						std::string song;
						int32_t year = 0;
						int videos = 0;
						for (uint32_t si : a.songs)
						{
							const MediaSong& s = snap->songs[si];
							if (song.empty()) song = ws.servedVirtual(s.path);
							if (!year && s.year) year = s.year;
							if (s.video) videos++;
						}
						// a whole album under folders the dashboard never serves: not listed either
						if (song.empty()) continue;
						w.obj().key("i").num((int)i).key("name").str(snap->at(a.name))
							.key("artist").str(a.albumArtist ? snap->at(a.albumArtist) : "Unknown artist");
						if (year) w.key("year").num(year);
						w.key("n").num((int)a.songs.size());
						if (videos) w.key("videos").num(videos);
						w.key("song").str(song).end();
					}
				}
				w.end().key("artists").arr();
				if (snap)
				{
					for (auto& a : snap->artists)
					{
						std::string song;
						for (uint32_t ai : a.albums)
							for (uint32_t si : snap->albums[ai].songs)
								if (!(song = ws.servedVirtual(snap->songs[si].path)).empty()) break;
						if (song.empty()) continue;
						w.obj().key("name").str(snap->at(a.name))
							.key("albums").num((int)a.albums.size()).key("songs").num((int)a.songs).end();
					}
				}
				w.end().key("books").arr();
				{
					// the handheld's own shelf: every book opened from anywhere (its place kept, newest first) and the
					// cards' Books folders, merged (ui_books.cpp's build); a TXT is not a book
					struct Shelf { std::string path; long long size = 0; int64_t mtime = 0; int at = -1; };
					std::vector<Shelf> shelf;
					std::set<std::string> seen;
					u.docs->docPlacesLoad();
					int at = 0;
					for (auto& e : u.docs->docPlaces->all())
					{
						at++;
						struct stat st;
						if (!isBookName(e.path) || stat(e.path.c_str(), &st) != 0 || !seen.insert(e.path).second) continue;
						shelf.push_back({e.path, (long long)st.st_size, (int64_t)st.st_mtime, at});
					}
					for (auto& b : listBooks(bookFolders()))
						if (seen.insert(b.path).second) shelf.push_back({b.path, b.size, b.mtime, -1});
					for (auto& b : shelf)
					{
						std::string bp = ws.servedVirtual(b.path);
						if (bp.empty()) continue;
						w.obj().key("p").str(bp).key("n").str(nameOf(b.path));
						if (b.size > 0) w.key("size").num(b.size);
						if (b.mtime) w.key("mtime").num(b.mtime);
						if (b.at > 0) w.key("at").num(b.at);
						// how far: a paged book (PDF, comic) a page of so many, an EPUB the share (as the Recent cards)
						const ResumeEntry* e = u.docs->placeOf(b.path);
						if (e && e->position > 0)
						{
							bool paged = e->aid == 2 || (e->aid != 1 && e->duration > 1);
							w.key("started").boolean(true);
							if (paged)
							{
								w.key("page").num((int)e->position + 1).key("pages").num((int)e->duration);
								w.key("frac").num(e->duration > 0 ? std::clamp((e->position + 1) / e->duration, 0.0, 1.0) : 0.0);
							}
							else w.key("frac").num(std::clamp(e->duration, 0.0, 1.0));
						}
						w.end();
					}
				}
				w.end().end();
				return w.out();
			}, 4000, status);
			WebServer::json(res, body, status);
		});
	}
}
