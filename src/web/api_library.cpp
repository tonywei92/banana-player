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
#include "httplib.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
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
	}

	void registerLibraryRoutes(WebServer& ws)
	{
		httplib::Server& svr = ws.http();

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
				for (auto& b : listBooks(bookFolders()))
				{
					std::string bp = ws.servedVirtual(b.path);
					if (bp.empty()) continue;
					w.obj().key("p").str(bp).key("n").str(nameOf(b.path));
					if (b.size > 0) w.key("size").num(b.size);
					if (b.mtime) w.key("mtime").num(b.mtime);
					w.end();
				}
				w.end().end();
				return w.out();
			}, 4000, status);
			WebServer::json(res, body, status);
		});
	}
}
