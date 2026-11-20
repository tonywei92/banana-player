// Music (ui_music.h): the three views of the cards' index, an album's page and an artist's. Everything is drawn
// inside lay::shell().body (the shell draws the rail, the header and the hints); the touches test the same Rects
// the drawing asked for. The third view is the browser's own playlists page — show() opens it, and nothing of it is
// here (Shell::section maps PLROOT and a playlist's page to this section, so the rail keeps Music lit under them).
#include "ui_music.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "mediaworker.h"
#include "session_host.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>

namespace bp
{
	namespace
	{
		// (the file-scope helpers name what they use in full: Ui's own names are private to it, and its nested
		// types need the Ui:: prefix here)
		lay::Box musicInner()
		{
			lay::Shell s = lay::shell();
			return {s.body.x + lay::SP12, s.body.y, s.body.w - 2 * lay::SP12, s.body.h};
		}

		// the unknown word as the page shows it (the snapshot's word 0)
		std::string wordOr(const MediaSnapshot& snap, uint32_t w, const char* unknown)
		{
			return w ? snap.at(w) : unknown;
		}

		// the empty page's sentence, wrapped to the kit's 400-unit line
		const char* EMPTY_MUSIC_LINE = "Put your music in a folder named Music on either card. "
			"Albums and artists are made from the songs' tags.";

		// a track's length: the index read it with the tags, else the durations cache, else blank
		std::string songLength(const MediaSnapshot& snap, const MediaSong& s)
		{
			if (s.duration > 0) return formatTime(s.duration);
			double secs = -1;
			if (cachedDuration(s.path, secs) && secs > 0) return formatTime(secs);
			return "";
		}
	}

	std::shared_ptr<const MediaSnapshot> Ui::Music::snapshot() const
	{
		return source ? source->snapshot() : nullptr;
	}

	// ---- the views, and the pages below them ----

	void Ui::Music::show(Player& p)
	{
		if (view == 2)
		{
			// the third view: the browser's own playlists page, where it was left (or its top)
			ui.page = Page::Browse;
			ui.browse->openSection(TAB_PLAYLISTS, p);
			return;
		}
		ui.page = Page::Music;
	}

	void Ui::Music::open(Player& p) { show(p); }

	void Ui::Music::viewStep(int dir, Player& p)
	{
		// L2 / R2 and the chips: the three views, wrapping (Albums · Artists · Playlists)
		view = ((view + dir) % 3 + 3) % 3;
		albumAt = artistAt = -1;
		cursor = scroll = 0;
		keyFocus = -1;
		show(p);
	}

	void Ui::Music::openAlbum(int i, Player&)
	{
		auto snap = snapshot();
		if (!snap || i < 0 || i >= (int)snap->albums.size()) return;
		albumAt = i;
		rowCursor = scroll = 0;
		keyFocus = 0;	// Play, focused on opening (the mock-up's two openings)
	}

	void Ui::Music::openArtist(int i, Player&)
	{
		auto snap = snapshot();
		if (!snap || i < 0 || i >= (int)snap->artists.size()) return;
		artistAt = i;
		rowCursor = scroll = 0;
		keyFocus = 0;
		artistZone = 0;	// the keys, then their albums
		artistAlbum = 0;
	}

	void Ui::Music::fitRows(int at, int rows)
	{
		// the scroll that keeps row `at` in view (a list that came back shorter: at its end)
		scroll = std::clamp(scroll, std::max(0, at - rows + 1), std::max(0, at));
	}

	// ---- playing (the play request, as Files' Play all: main opens it and the Session takes it) ----

	void Ui::Music::play(const std::vector<std::string>& paths, int index, const std::string& title, bool shuffle,
		Player& p)
	{
		if (paths.empty()) return;
		if (shuffle)
		{
			// a shuffled start, as Files' Shuffle play: the lasting setting on, a random first song
			std::random_device rd;
			index = (int)(rd() % (unsigned)paths.size());
			SessionHost::get().setShuffle(true);
		}
		ui.playRequest = {};
		ui.playRequest.paths = paths;
		ui.playRequest.index = std::clamp(index, 0, (int)paths.size() - 1);
		// a list like a folder (the Session's kind for a list of any order); its name is the player's crumb
		ui.playRequest.source.kind = SourceKind::Folder;
		ui.playRequest.source.title = title;
	}

	std::vector<PlaylistEntry> Ui::Music::albumEntries(int album) const
	{
		std::vector<PlaylistEntry> out;
		auto snap = snapshot();
		if (!snap || album < 0 || album >= (int)snap->albums.size()) return out;
		for (uint32_t si : snap->albums[album].songs)
		{
			const MediaSong& s = snap->songs[si];
			out.push_back({s.path, snap->at(s.title), (double)s.duration});
		}
		return out;
	}

	std::vector<PlaylistEntry> Ui::Music::artistEntries(int artist) const
	{
		std::vector<PlaylistEntry> out;
		auto snap = snapshot();
		if (!snap || artist < 0 || artist >= (int)snap->artists.size()) return out;
		for (uint32_t ai : artistAlbumsByYear(artist))
			for (uint32_t si : snap->albums[ai].songs)
			{
				const MediaSong& s = snap->songs[si];
				out.push_back({s.path, snap->at(s.title), (double)s.duration});
			}
		return out;
	}

	std::vector<uint32_t> Ui::Music::artistAlbumsByYear(int artist) const
	{
		// their albums newest first (the mock-up's order); an album's year is its first song that has one, and an
		// album with none sorts after the dated ones, by name
		auto snap = snapshot();
		std::vector<uint32_t> out;
		if (!snap || artist < 0 || artist >= (int)snap->artists.size()) return out;
		out = snap->artists[artist].albums;
		auto year = [&](uint32_t ai) {
			int32_t y = 0;
			for (uint32_t si : snap->albums[ai].songs)
				if (snap->songs[si].year) { y = snap->songs[si].year; break; }
			return y;
		};
		std::stable_sort(out.begin(), out.end(), [&](uint32_t a, uint32_t b) {
			int32_t ya = year(a), yb = year(b);
			if ((ya != 0) != (yb != 0)) return yb == 0;		// the dated ones first
			if (ya != yb) return ya > yb;
			return naturalLess(snap->at(snap->albums[a].name), snap->at(snap->albums[b].name));
		});
		return out;
	}

	void Ui::Music::playAlbum(int album, int fromSong, bool shuffle, Player& p)
	{
		auto snap = snapshot();
		if (!snap || album < 0 || album >= (int)snap->albums.size()) return;
		std::vector<std::string> paths;
		for (uint32_t si : snap->albums[album].songs) paths.push_back(snap->songs[si].path);
		if (paths.empty()) return;
		play(paths, fromSong, "Album \xC2\xB7 " + wordOr(*snap, snap->albums[album].name, "Unknown album"), shuffle, p);
	}

	void Ui::Music::playArtist(int artist, int fromSong, bool shuffle, Player& p)
	{
		auto snap = snapshot();
		if (!snap || artist < 0 || artist >= (int)snap->artists.size()) return;
		std::vector<PlaylistEntry> songs = artistEntries(artist);
		if (songs.empty()) return;
		std::vector<std::string> paths;
		for (auto& e : songs) paths.push_back(e.path);
		play(paths, fromSong, "Artist \xC2\xB7 " + wordOr(*snap, snap->artists[artist].name, "Unknown artist"),
			shuffle, p);
	}

	// ---- the head (the shell asks) ----

	std::string Ui::Music::countsLine(const MediaSnapshot& snap) const
	{
		auto n = [](size_t what, const char* one, const char* many) {
			return std::to_string(what) + " " + (what == 1 ? one : many);
		};
		return n(snap.albums.size(), "album", "albums") + " \xC2\xB7 " + n(snap.artists.size(), "artist", "artists") +
			" \xC2\xB7 " + n(snap.songs.size(), "song", "songs");
	}

	std::string Ui::Music::fromLine(const MediaSnapshot& snap) const
	{
		std::string from = mediaFromWords(snap.musicFolders);
		return from.empty() ? "" : "From " + from + " \xC2\xB7 " + countsLine(snap);
	}

	std::vector<Ui::Kit::FilterItem> Ui::Music::viewItems() const
	{
		// the views' chips (the counts: the snapshot's, and the playlists' own; -1 while the first read is on)
		auto snap = snapshot();
		int albums = -1, artists = -1, playlists = -1;
		if (snap && generation() > 0)
		{
			albums = (int)snap->albums.size();
			artists = (int)snap->artists.size();
		}
		if (ui.browse->playlists->plStore) playlists = (int)ui.browse->playlists->plStore->list().size();
		return {{"Albums", albums}, {"Artists", artists}, {"Playlists", playlists}};
	}

	void Ui::Music::accentFirst(Kit::Empty& e, const std::vector<std::string>& lines, const char* word) const
	{
		// the word a sentence of the empty state carries in ACCENT (the folder an empty page names: "Music")
		for (size_t i = 0; i < lines.size(); i++)
		{
			size_t at = lines[i].find(word);
			if (at != std::string::npos)
			{
				e.accent.push_back({(int)i, (int)at, (int)strlen(word)});
				return;
			}
		}
	}

	std::vector<Ui::Hint> Ui::Music::hintList(const Player&) const
	{
		if (albumAt >= 0) return {{"A", "Play"}, {"Y", "Shuffle"}, {"\xE2\x96\xB2/\xE2\x96\xBC", "Keys, songs"}, {"X", "More"}, {"B", "Albums"}};
		if (artistAt >= 0) return {{"A", "Play / open"}, {"Y", "Shuffle"}, {"\xE2\x96\xB2/\xE2\x96\xBC", "Albums, songs"}, {"X", "More"}, {"B", "Artists"}};
		// the views (the mock-up's music-albums and music-artists)
		return {{"A", "Open"}, {"X", "More"}, {"L/R", "View"}, {"L3", "Search"}, {"B", "Home"}};
	}

	Ui::ShellHead Ui::Music::head(const Player& p) const
	{
		ShellHead h;
		h.back = ui.backIconShown(p);
		h.more = true;
		h.hints = hintList(p);
		auto snap = snapshot();
		if (snap && albumAt >= 0 && albumAt < (int)snap->albums.size())
		{
			const MediaAlbum& a = snap->albums[albumAt];
			h.title = wordOr(*snap, a.name, "Unknown album");
			h.crumb = wordOr(*snap, a.albumArtist, "Unknown artist");
			std::string counts = std::to_string(a.songs.size()) + (a.songs.size() == 1 ? " song" : " songs");
			int videos = 0;
			double total = 0;
			int32_t year = 0;
			for (uint32_t si : a.songs)
			{
				const MediaSong& s = snap->songs[si];
				if (s.video) videos++;
				if (s.duration > 0) total += s.duration;
				if (!year && s.year) year = s.year;
			}
			if (videos) counts += ", " + std::to_string(videos) + (videos == 1 ? " a video" : " videos");
			if (year) h.crumb += " \xC2\xB7 " + std::to_string(year);
			h.crumb += " \xC2\xB7 " + counts;
			if (total > 0) h.crumb += " \xC2\xB7 " + formatTime(total);
			return h;
		}
		if (snap && artistAt >= 0 && artistAt < (int)snap->artists.size())
		{
			const MediaArtist& a = snap->artists[artistAt];
			h.title = wordOr(*snap, a.name, "Unknown artist");
			h.crumb = "Artist \xC2\xB7 " + std::to_string(a.albums.size()) + (a.albums.size() == 1 ? " album" : " albums") +
				" \xC2\xB7 " + std::to_string(a.songs) + (a.songs == 1 ? " song" : " songs");
			return h;
		}
		h.title = "Music";
		h.more = false;		// the empty page's keys are its own; the reading one has nothing to ask More of
		if (!snap || generation() == 0)
		{
			// the first read's live line, as the mock-up draws it (music-reading): where from is known at once,
			// how far is the pass's own counter ("From Music on both cards · 41 albums so far" — the songs are
			// what the walk counts, so they are what the line says)
			h.crumb = "Reading the cards";
			if (ui.indexWorker)
			{
				auto pr = ui.indexWorker->progress();
				if (pr.reading)
				{
					MediaFolders mf = mediaFolders();
					std::string from = mediaFromWords(mf.music);
					h.crumb = (from.empty() ? std::string("Reading the cards") : "From " + from) + " \xC2\xB7 " +
						std::to_string(pr.songs) + (pr.songs == 1 ? " song so far" : " songs so far");
				}
			}
		}
		else if (snap->musicFolders.empty()) h.crumb = "No Music folder on either card";
		else if (snap->songs.empty()) h.crumb = mediaEmptyWords(snap->musicFolders);
		else { h.crumb = fromLine(*snap); h.more = true; }
		return h;
	}

	// ---- the More sheet (X) ----

	void Ui::Music::openMoreSheet(Player&)
	{
		auto snap = snapshot();
		if (!snap) return;
		// what is focused: a song (the pages' rows), else the album, else the artist
		std::string songPath, songTitle;
		int album = -1, artist = -1;
		if (albumAt >= 0 && albumAt < (int)snap->albums.size())
		{
			album = albumAt;
			if (keyFocus < 0 && rowCursor >= 0 && rowCursor < (int)snap->albums[album].songs.size())
			{
				const MediaSong& s = snap->songs[snap->albums[album].songs[rowCursor]];
				songPath = s.path;
				songTitle = snap->at(s.title);
			}
		}
		else if (artistAt >= 0 && artistAt < (int)snap->artists.size())
		{
			artist = artistAt;
			if (keyFocus < 0 && artistZone == 1)
			{
				std::vector<PlaylistEntry> songs = artistEntries(artist);
				if (rowCursor >= 0 && rowCursor < (int)songs.size())
				{
					songPath = songs[rowCursor].path;
					songTitle = songs[rowCursor].title;
				}
			}
		}
		else if (view == 0 && cursor >= 0 && cursor < (int)snap->albums.size()) album = cursor;
		else if (view == 1 && cursor >= 0 && cursor < (int)snap->artists.size()) artist = cursor;
		// asked for its list each time it draws, so a Like row says what it is now
		ui.openMore([this, songPath, songTitle, album, artist](const Player& pl) {
			MoreSpec m;
			auto snap = this->snapshot();
			if (!snap) { m.title = "Music"; return m; }
			std::vector<PlaylistEntry> entries;
			std::string name, note;
			if (!songPath.empty())
			{
				entries = {{songPath, songTitle, -1}};
				name = songTitle;
			}
			else if (album >= 0 && album < (int)snap->albums.size())
			{
				entries = albumEntries(album);
				name = wordOr(*snap, snap->albums[album].name, "Unknown album");
				note = std::to_string(entries.size()) + (entries.size() == 1 ? " song" : " songs");
			}
			else if (artist >= 0 && artist < (int)snap->artists.size())
			{
				entries = artistEntries(artist);
				name = wordOr(*snap, snap->artists[artist].name, "Unknown artist");
				note = std::to_string(entries.size()) + (entries.size() == 1 ? " song" : " songs");
			}
			if (name.empty()) { m.title = "Music"; return m; }
			m.glyph = &G_NOTE;
			m.title = name;
			m.note = note;
			auto row = [](const Glyph* g, std::string label, std::string nt, std::function<bool(Player&)> act,
				bool lit = false, bool stay = false) {
				MoreItem i;
				i.glyph = g;
				i.label = std::move(label);
				i.note = std::move(nt);
				i.key = "";
				i.act = std::move(act);
				i.lit = lit;
				i.stay = stay;
				return i;
			};
			// these very entries into a playlist (a new one starts from the album's or the artist's name)
			if (!entries.empty() && ui.browse->playlists->plStore)
				m.rows.push_back(row(&G_PLUS, "Add to playlist\xE2\x80\xA6", note,
					[this, entries, name](Player& p) { ui.browse->playlists->openPickerEntries(entries, name, p); return false; }));
			bool likable = !songPath.empty() && ui.browse->playlists->plStore && PlaylistStore::likable(songPath);
			bool liked = likable && ui.browse->playlists->plStore->isLiked(songPath);
			if (likable)
				m.rows.push_back(row(liked ? &G_HEART : &G_HEART_O, liked ? "Unlike" : "Like", "",
					[this, songPath, songTitle, liked](Player& p) {
						bool was = ui.browse->playlists->plStore && ui.browse->playlists->plStore->isLiked(songPath);
						if (ui.setLikedPath(p, songPath, songTitle, -1, !was) != was) ui.likeCard(p, !was, songTitle);
						return false;
					}, liked, true));
			if (!entries.empty())
				m.rows.push_back(row(&G_FOLDER, "Show in folder", "",
					[this, path = entries.front().path](Player& p) {
						ui.page = Page::Browse;
						ui.showInFolder(path, p);
						return false;
					}));
			ui.moreTwoColumns(m, ui.sourceMoreRows(LibKind::Music, pl));
			return m;
		});
	}

	// ---- the keys ----

	bool Ui::Music::key(Button b, Player& p)
	{
		auto snap = snapshot();
		Rect inner = musicInner();
		// the views' step (L2 / R2) works everywhere in the section, the empty page too (Playlists needs no Music
		// folder); the pages below it don't take it (the mock-up gives them none)
		if (b == Button::L || b == Button::R)
		{
			if (albumAt < 0 && artistAt < 0) viewStep(b == Button::R ? 1 : -1, p);
			return false;
		}
		// an empty page (no Music folder, or none with a song in it): its two keys, and B Home
		if (!snap || snap->songs.empty())
		{
			switch (b)
			{
			case Button::A: ui.page = Page::Browse; ui.browse->openSection(TAB_LOCAL, p); break;
			case Button::Y: ui.openWeb(); break;
			case Button::B: ui.page = Page::Browse; ui.browse->enterFolder(HOMEDIR, p); break;
			default: break;
			}
			return false;
		}
		if (b == Button::X) { openMoreSheet(p); return false; }
		// ---- an album's page ----
		if (albumAt >= 0 && albumAt < (int)snap->albums.size())
		{
			const MediaAlbum& a = snap->albums[albumAt];
			int n = (int)a.songs.size();
			lay::PageHead h = lay::pageHead(inner, 2, 3);
			int rows = std::max(1, h.content.h / lay::TRACK_ROW_H);
			switch (b)
			{
			case Button::Up:
				if (keyFocus == 1) keyFocus = 0;
				else if (keyFocus < 0 && rowCursor > 0) rowCursor--;
				else if (keyFocus < 0) keyFocus = 0;		// the first row: back to the keys
				break;
			case Button::Down:
				if (keyFocus >= 0 && n) { keyFocus = -1; rowCursor = std::min(rowCursor, n - 1); }
				else if (keyFocus < 0 && rowCursor < n - 1) rowCursor++;
				break;
			case Button::Left:
				if (keyFocus == 1) keyFocus = 0;
				else if (keyFocus < 0) rowCursor = std::max(0, rowCursor - rows);
				break;
			case Button::Right:
				if (keyFocus == 0) keyFocus = 1;
				else if (keyFocus < 0) rowCursor = std::min(n - 1, rowCursor + rows);
				break;
			case Button::A:
				if (keyFocus == 0) playAlbum(albumAt, 0, false, p);
				else if (keyFocus == 1) playAlbum(albumAt, 0, true, p);
				else if (n) playAlbum(albumAt, rowCursor, false, p);
				break;
			case Button::Y: playAlbum(albumAt, 0, true, p); break;
			case Button::B:
				// back to the grid (the cursor never left the album's cell)
				view = 0;
				albumAt = -1;
				keyFocus = -1;
				scroll = 0;
				break;
			default: break;
			}
			if (keyFocus < 0) fitRows(rowCursor, rows);
			return false;
		}
		// ---- an artist's page ----
		if (artistAt >= 0 && artistAt < (int)snap->artists.size())
		{
			std::vector<uint32_t> albums = artistAlbumsByYear(artistAt);
			std::vector<PlaylistEntry> songs = artistEntries(artistAt);
			lay::PageHead h = lay::pageHead(inner, 1, 3);
			int rows = std::max(1, (h.content.h - lay::LINE_H - lay::SP6) / lay::MEDIA_ROW_H);
			switch (b)
			{
			case Button::Up:
				if (keyFocus == 1) keyFocus = 0;
				else if (keyFocus < 0 && artistZone == 1 && rowCursor > 0) rowCursor--;
				else if (keyFocus < 0 && artistZone == 1) { artistZone = 0; keyFocus = -1; }
				else if (keyFocus < 0 && artistZone == 0) keyFocus = 0;		// the strip: back to the keys
				break;
			case Button::Down:
				if (keyFocus >= 0)
				{
					if (!albums.empty()) { keyFocus = -1; artistZone = 0; }
					else if (!songs.empty()) { keyFocus = -1; artistZone = 1; rowCursor = 0; }
				}
				else if (artistZone == 0 && !songs.empty()) { artistZone = 1; rowCursor = 0; }
				else if (artistZone == 1 && rowCursor < (int)songs.size() - 1) rowCursor++;
				break;
			case Button::Left:
				if (keyFocus == 1) keyFocus = 0;
				else if (keyFocus < 0 && artistZone == 0 && !albums.empty())
					artistAlbum = (artistAlbum + (int)albums.size() - 1) % (int)albums.size();
				else if (keyFocus < 0 && artistZone == 1) rowCursor = std::max(0, rowCursor - rows);
				break;
			case Button::Right:
				if (keyFocus == 0) keyFocus = 1;
				else if (keyFocus < 0 && artistZone == 0 && !albums.empty())
					artistAlbum = (artistAlbum + 1) % (int)albums.size();
				else if (keyFocus < 0 && artistZone == 1) rowCursor = std::min(std::max(0, (int)songs.size() - 1),
					rowCursor + rows);
				break;
			case Button::A:
				if (keyFocus == 0) playArtist(artistAt, 0, false, p);
				else if (keyFocus == 1) playArtist(artistAt, 0, true, p);
				else if (artistZone == 0 && artistAlbum < (int)albums.size()) openAlbum((int)albums[artistAlbum], p);
				else if (artistZone == 1 && rowCursor < (int)songs.size()) playArtist(artistAt, rowCursor, false, p);
				break;
			case Button::Y: playArtist(artistAt, 0, true, p); break;
			case Button::B:
				// back to the list (the cursor never left the artist's row)
				view = 1;
				artistAt = -1;
				keyFocus = -1;
				scroll = 0;
				break;
			default: break;
			}
			if (keyFocus < 0 && artistZone == 1) fitRows(rowCursor, rows);
			return false;
		}
		// ---- the views ----
		if (view == 0)
		{
			// the Albums grid: ◀/▶ a cell, ▲/▼ a row (the mock-up's "D-pad Move")
			int n = (int)snap->albums.size(), cols = 5;
			lay::MusicHeader mh = lay::musicHeader(inner, {});
			int gy = mh.strip.y + lay::CHIP_STRIP + lay::SP8;
			Rect gridArea{inner.x, gy, inner.w, inner.y + inner.h - gy};
			lay::AlbumGrid g = lay::albumGrid(gridArea, n, scroll, cols);
			int shown = g.shown;
			switch (b)
			{
			case Button::Left: cursor = std::max(0, cursor - 1); break;
			case Button::Right: cursor = std::min(n - 1, cursor + 1); break;
			case Button::Up: cursor = std::max(0, cursor - cols); break;
			case Button::Down: cursor = std::min(n - 1, cursor + cols); break;
			case Button::A: if (n) openAlbum(cursor, p); break;
			case Button::B: ui.page = Page::Browse; ui.browse->enterFolder(HOMEDIR, p); break;
			default: break;
			}
			// the grid scrolls by whole rows, the cursor's row kept in view
			scroll = std::clamp(scroll, std::max(0, cursor / cols - (shown - 1)), std::max(0, cursor / cols));
			return false;
		}
		// the Artists list
		int n = (int)snap->artists.size();
		lay::MusicHeader mh = lay::musicHeader(inner, {});
		int gy = mh.strip.y + lay::CHIP_STRIP + lay::SP8;
		int rows = std::max(1, (inner.y + inner.h - gy) / lay::ARTIST_ROW_H);
		switch (b)
		{
		case Button::Up: if (n) cursor = (cursor + n - 1) % n; break;
		case Button::Down: if (n) cursor = (cursor + 1) % n; break;
		case Button::Left: cursor = std::max(0, cursor - rows); break;
		case Button::Right: cursor = std::min(n - 1, cursor + rows); break;
		case Button::A: if (n) openArtist(cursor, p); break;
		case Button::B: ui.page = Page::Browse; ui.browse->enterFolder(HOMEDIR, p); break;
		default: break;
		}
		fitRows(cursor, rows);
		return false;
	}

	// ---- a finger ----

	bool Ui::Music::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		auto snap = snapshot();
		Rect inner = musicInner();
		bool reading = !snap || generation() == 0;
		bool empty = !reading && snap->songs.empty();
		if (pt.kind != Pointer::Down)
		{
			if (touch == T_NONE) return false;
			if (pt.kind == Pointer::Move)
			{
				if (!touchMoved && std::abs(y - touchY) > 6) touchMoved = true;
				if (touchMoved)
				{
					// a drag scrolls whatever the finger went down on, by that thing's own rows
					scroll = lay::dragScroll(scrollStart, touchY, y, dragRow, 0, dragCount, dragShown);
				}
				return false;
			}
			// the lift: a tap if it never moved, on what it is still over
			int t = touch;
			bool moved = touchMoved;
			touch = T_NONE;
			touchMoved = false;
			if (moved || t == T_NONE) return false;
			switch (t)
			{
			case T_EMPTY1:
				if (empty && emptyKey[0].hit(x, y)) { ui.page = Page::Browse; ui.browse->openSection(TAB_LOCAL, p); }
				return false;
			case T_EMPTY2:
				if (empty && emptyKey[1].hit(x, y)) ui.openWeb();
				return false;
			default: break;
			}
			if (!snap || empty) return false;
			if (t == T_GROUND) return false;		// the ground: a drag was its only meaning
			if (t >= T_CHIP && t < T_CHIP + 3)
			{
				view = t - T_CHIP;
				albumAt = artistAt = -1;
				cursor = scroll = 0;
				show(p);
				return false;
			}
			if (t >= T_ALBUM && t < T_ARTIST)
			{
				// the Albums grid's cells, or the artist page's strip (the strip's first cell is off its left)
				if (artistAt >= 0 && artistAt < (int)snap->artists.size() && keyFocus < 0 && artistZone == 0)
				{
					std::vector<uint32_t> albums = artistAlbumsByYear(artistAt);
					int i = albumScroll + (t - T_ALBUM);
					if (i >= albumScroll && i < (int)albums.size()) openAlbum((int)albums[i], p);
					return false;
				}
				if (albumAt < 0 && artistAt < 0 && view == 0 && t - T_ALBUM < (int)snap->albums.size())
					openAlbum(t - T_ALBUM, p);
				return false;
			}
			if (t >= T_ARTIST && t < T_KEY)
			{
				if (artistAt < 0 && view == 1 && t - T_ARTIST < (int)snap->artists.size()) openArtist(t - T_ARTIST, p);
				return false;
			}
			if (t >= T_KEY && t < T_KEY + 3)
			{
				// the head's keys: Play, Shuffle, and Add (touch only; its rows are in X · More by the buttons)
				if (albumAt >= 0 && albumAt < (int)snap->albums.size())
				{
					if (t == T_KEY) playAlbum(albumAt, 0, false, p);
					else if (t == T_KEY + 1) playAlbum(albumAt, 0, true, p);
					else if (t == T_KEY + 2)
						ui.browse->playlists->openPickerEntries(albumEntries(albumAt),
							wordOr(*snap, snap->albums[albumAt].name, "Unknown album"), p);
				}
				else if (artistAt >= 0 && artistAt < (int)snap->artists.size())
				{
					if (t == T_KEY) playArtist(artistAt, 0, false, p);
					else if (t == T_KEY + 1) playArtist(artistAt, 0, true, p);
					else if (t == T_KEY + 2)
						ui.browse->playlists->openPickerEntries(artistEntries(artistAt),
							wordOr(*snap, snap->artists[artistAt].name, "Unknown artist"), p);
				}
				return false;
			}
			if (t >= T_TRACK && t < T_SONG && albumAt >= 0 && albumAt < (int)snap->albums.size())
			{
				int i = t - T_TRACK;
				if (i < (int)snap->albums[albumAt].songs.size()) playAlbum(albumAt, i, false, p);
				return false;
			}
			if (t >= T_SONG && artistAt >= 0 && artistAt < (int)snap->artists.size() && keyFocus < 0 && artistZone == 1)
				playArtist(artistAt, t - T_SONG, false, p);
			return false;
		}
		// the down: what is under the finger (the same Rects the drawing asked for)
		touch = T_NONE;
		touchMoved = false;
		touchY = y;
		scrollStart = scroll;
		dragRow = 60;
		dragCount = 0;
		dragShown = 1;
		if (empty)
		{
			std::vector<std::string> lines = ui.wrapText(EMPTY_MUSIC_LINE, F_I14, 400, 99);
			lay::EmptyState e = lay::emptyState(inner, (int)lines.size(), 2);
			emptyKey[0] = e.key[0];
			emptyKey[1] = e.key[1];
			if (e.key[0].hit(x, y)) touch = T_EMPTY1;
			else if (e.key[1].hit(x, y)) touch = T_EMPTY2;
			return false;
		}
		if (reading) return false;
		Kit& k = *ui.kit;
		lay::MusicHeader mh = lay::musicHeader(inner, k.filterWidths(viewItems()));
		if (albumAt < 0 && artistAt < 0)
		{
			int chip = lay::filterAt(mh.chips, x, y);
			if (chip >= 0) { touch = T_CHIP + chip; return false; }
		}
		// ---- an album's page ----
		if (albumAt >= 0 && albumAt < (int)snap->albums.size())
		{
			const MediaAlbum& a = snap->albums[albumAt];
			lay::PageHead h = lay::pageHead(inner, 2, 3);
			for (int i = 0; i < 3; i++)
				if (h.key[i].w > 0 && h.key[i].hit(x, y)) { touch = T_KEY + i; return false; }
			int rows = std::max(1, h.content.h / lay::TRACK_ROW_H);
			dragRow = lay::TRACK_ROW_H;
			dragCount = (int)a.songs.size();
			dragShown = rows;
			for (int i = 0; i < rows && scroll + i < (int)a.songs.size(); i++)
			{
				Rect r{inner.x, h.content.y + i * lay::TRACK_ROW_H, inner.w, lay::TRACK_ROW_H};
				if (r.hit(x, y)) { touch = T_TRACK + scroll + i; rowCursor = scroll + i; return false; }
			}
			touch = T_GROUND;		// the page's own ground: a drag still scrolls the songs
			return false;
		}
		// ---- an artist's page ----
		if (artistAt >= 0 && artistAt < (int)snap->artists.size())
		{
			std::vector<uint32_t> albums = artistAlbumsByYear(artistAt);
			std::vector<PlaylistEntry> songs = artistEntries(artistAt);
			lay::PageHead h = lay::pageHead(inner, 1, 3);
			for (int i = 0; i < 3; i++)
				if (h.key[i].w > 0 && h.key[i].hit(x, y)) { touch = T_KEY + i; return false; }
			Rect strip{inner.x, h.content.y + lay::LINE_H + lay::SP6, inner.w, lay::ALBUM_CELL_H};
			if (keyFocus < 0 && artistZone == 0 && strip.hit(x, y) && !albums.empty())
			{
				int per = std::min(5, (int)albums.size());
				for (int j = 0; j < per; j++)
				{
					Rect cell{inner.x + j * (lay::ALBUM_ART + lay::ALBUM_COL_GAP), strip.y, lay::ALBUM_ART,
						lay::ALBUM_CELL_H};
					if (cell.hit(x, y)) { touch = T_ALBUM + j; return false; }
				}
				touch = T_GROUND;		// the strip's own ground: one row, a drag does nothing
				return false;
			}
			Rect list{inner.x, h.content.y, inner.w, h.content.h};
			if (keyFocus < 0 && artistZone == 1 && list.hit(x, y) && !songs.empty())
			{
				int rows = std::max(1, (h.content.h - lay::LINE_H - lay::SP6) / lay::MEDIA_ROW_H);
				dragRow = lay::MEDIA_ROW_H;
				dragCount = (int)songs.size();
				dragShown = rows;
				for (int i = 0; i < rows && scroll + i < (int)songs.size(); i++)
				{
					Rect r{inner.x, h.content.y + lay::LINE_H + lay::SP6 + i * lay::MEDIA_ROW_H, inner.w,
						lay::MEDIA_ROW_H};
					if (r.hit(x, y)) { touch = T_SONG + scroll + i; rowCursor = scroll + i; return false; }
				}
				touch = T_GROUND;
				return false;
			}
			return false;
		}
		// ---- the views ----
		if (view == 0)
		{
			int n = (int)snap->albums.size(), cols = 5;
			int gy = mh.strip.y + lay::CHIP_STRIP + lay::SP8;
			Rect gridArea{inner.x, gy, inner.w, inner.y + inner.h - gy};
			lay::AlbumGrid g = lay::albumGrid(gridArea, n, scroll, cols);
			dragRow = lay::ALBUM_CELL_H + lay::ALBUM_ROW_GAP;
			dragCount = (n + cols - 1) / cols;
			dragShown = g.shown;
			for (int i = g.first * cols; i < n && i < (g.first + g.shown) * cols; i++)
				if (g.cell[i].hit(x, y))
				{
					touch = T_ALBUM + (i - g.first * cols);
					cursor = i;
					return false;
				}
			touch = T_GROUND;		// the grid's own ground: a drag still scrolls it
			return false;
		}
		// the Artists list
		int n = (int)snap->artists.size();
		int gy = mh.strip.y + lay::CHIP_STRIP + lay::SP8;
		int rows = std::max(1, (inner.y + inner.h - gy) / lay::ARTIST_ROW_H);
		dragRow = lay::ARTIST_ROW_H;
		dragCount = n;
		dragShown = rows;
		for (int i = 0; i < rows && scroll + i < n; i++)
		{
			Rect r{inner.x, gy + i * lay::ARTIST_ROW_H, inner.w, lay::ARTIST_ROW_H};
			if (r.hit(x, y))
			{
				touch = T_ARTIST + scroll + i;
				cursor = scroll + i;
				return false;
			}
		}
		touch = T_GROUND;		// the list's own ground: a drag still scrolls it
		return false;
	}

	// ---- the drawing ----

	void Ui::Music::draw(const Player& p)
	{
		auto snap = snapshot();
		Rect inner = musicInner();
		Kit& k = *ui.kit;
		bool reading = !snap || generation() == 0;
		// the one the page plays (the mark in the number's place, the tile's corner)
		auto plays = [&](const std::string& path) {
			return p.playing() && !p.playingPhoto() && p.currentPath() == path;
		};

		// ---- an album's page ----
		if (!reading && snap && albumAt >= 0 && albumAt < (int)snap->albums.size())
		{
			const MediaAlbum& a = snap->albums[albumAt];
			const std::string name = wordOr(*snap, a.name, "Unknown album");
			lay::PageHead h = lay::pageHead(inner, 2, 3);
			Kit::PageHead v;
			if (!a.songs.empty())
			{
				const MediaSong& first = snap->songs[a.songs.front()];
				v.art = [this, path = first.path, name](Rect r) { ui.drawSongArt(path, name, r, 5); };
			}
			v.artKey = name;
			v.title = name;
			std::string counts = std::to_string(a.songs.size()) + (a.songs.size() == 1 ? " song" : " songs");
			int videos = 0;
			double total = 0;
			int32_t year = 0;
			for (uint32_t si : a.songs)
			{
				const MediaSong& s = snap->songs[si];
				if (s.video) videos++;
				if (s.duration > 0) total += s.duration;
				if (!year && s.year) year = s.year;
			}
			if (videos) counts += ", " + std::to_string(videos) + (videos == 1 ? " a video" : " videos");
			v.lines = {wordOr(*snap, a.albumArtist, "Unknown artist") + (year ? " \xC2\xB7 " + std::to_string(year) : ""),
				counts + (total > 0 ? " \xC2\xB7 " + formatTime(total) : "")};
			v.focused = keyFocus;
			v.pressed = touch >= T_KEY && touch < T_KEY + 3 && !touchMoved ? touch - T_KEY : -1;
			v.hotkeys = p.settings.buttonHotkeys;
			k.pageHead(h, v);
			int rows = std::max(1, h.content.h / lay::TRACK_ROW_H);
			if (keyFocus < 0) fitRows(rowCursor, rows);
			auto* store = ui.browse->playlists->plStore.get();
			for (int i = 0; i < rows && scroll + i < (int)a.songs.size(); i++)
			{
				const MediaSong& s = snap->songs[a.songs[scroll + i]];
				Kit::Track t;
				t.number = s.track ? std::to_string(s.track) : "";
				bool here = plays(s.path);
				t.playing = here && !p.paused;
				t.paused = here && p.paused;
				t.name = snap->at(s.title);
				if (s.video) t.pill = "VIDEO";
				t.heart = store && store->isLiked(s.path);
				t.note = songLength(*snap, s);
				t.focused = keyFocus < 0 && scroll + i == rowCursor;
				t.pressed = touch == T_TRACK + scroll + i && !touchMoved;
				Rect r{inner.x, h.content.y + i * lay::TRACK_ROW_H, inner.w, lay::TRACK_ROW_H};
				k.trackRow(lay::trackRow(r), t);
			}
			return;
		}

		// ---- an artist's page ----
		if (!reading && snap && artistAt >= 0 && artistAt < (int)snap->artists.size())
		{
			const MediaArtist& a = snap->artists[artistAt];
			const std::string name = wordOr(*snap, a.name, "Unknown artist");
			std::vector<uint32_t> albums = artistAlbumsByYear(artistAt);
			std::vector<PlaylistEntry> songs = artistEntries(artistAt);
			lay::PageHead h = lay::pageHead(inner, 1, 3);
			Kit::PageHead v;
			v.disc = true;
			v.title = name;
			double total = 0;
			for (auto& e : songs)
				if (e.duration > 0) total += e.duration;
			v.lines = {std::to_string(albums.size()) + (albums.size() == 1 ? " album" : " albums") + " \xC2\xB7 " +
				std::to_string(a.songs) + (a.songs == 1 ? " song" : " songs") +
				(total > 0 ? " \xC2\xB7 " + formatTime(total) : "")};
			v.focused = keyFocus;
			v.pressed = touch >= T_KEY && touch < T_KEY + 3 && !touchMoved ? touch - T_KEY : -1;
			v.hotkeys = p.settings.buttonHotkeys;
			k.pageHead(h, v);
			auto caps = [&](const std::string& text, Rect box) {
				Kit::SectionLine line;
				line.caps = true;
				line.text = text;
				k.sectionLine(lay::sectionLine(box, false), line);
			};
			auto* store = ui.browse->playlists->plStore.get();
			if (keyFocus >= 0 || artistZone == 0)
			{
				// their albums as one strip of cover cells (the focused one kept in view), the songs' line below
				caps("Albums \xC2\xB7 " + std::to_string(albums.size()), {inner.x, h.content.y, inner.w, lay::LINE_H});
				int per = std::min(5, (int)albums.size());
				// the strip's first cell: the focused one kept in view, and never past the strip's end
				albumScroll = std::clamp(albumScroll, std::max(0, artistAlbum - per + 1),
					std::max(0, std::min(artistAlbum, (int)albums.size() - per)));
				Rect strip{inner.x, h.content.y + lay::LINE_H + lay::SP6, inner.w, lay::ALBUM_CELL_H};
				for (int j = 0; j < per; j++)
				{
					const MediaAlbum& al = snap->albums[albums[albumScroll + j]];
					const std::string an = wordOr(*snap, al.name, "Unknown album");
					Kit::Album cell;
					if (!al.songs.empty())
					{
						const MediaSong& first = snap->songs[al.songs.front()];
						cell.art = [this, path = first.path, an](Rect r) { ui.drawSongArt(path, an, r, 5); };
					}
					cell.artKey = an;
					cell.title = an;
					int32_t year = 0;
					for (uint32_t si : al.songs)
						if (snap->songs[si].year) { year = snap->songs[si].year; break; }
					cell.artist = (year ? std::to_string(year) + " \xC2\xB7 " : "") + std::to_string(al.songs.size()) +
						(al.songs.size() == 1 ? " song" : " songs");
					bool here = false;
					for (uint32_t si : al.songs)
						if (plays(snap->songs[si].path)) { here = true; break; }
					cell.playing = here && !p.paused;
					cell.paused = here && p.paused;
					cell.focused = keyFocus < 0 && artistZone == 0 && albumScroll + j == artistAlbum;
					Rect box{inner.x + j * (lay::ALBUM_ART + lay::ALBUM_COL_GAP), strip.y, lay::ALBUM_ART,
						lay::ALBUM_CELL_H};
					k.albumTile(lay::albumTile(box), cell);
				}
				// the songs' line under the strip, when the room for it is there
				int below = strip.y + strip.h + lay::SP12;
				if (!songs.empty() && below + lay::LINE_H <= inner.y + inner.h)
					caps("Songs \xC2\xB7 " + std::to_string(songs.size()), {inner.x, below, inner.w, lay::LINE_H});
				return;
			}
			// their songs (▼ from the albums): the list, the focused one kept in view
			caps("Songs \xC2\xB7 " + std::to_string(songs.size()), {inner.x, h.content.y, inner.w, lay::LINE_H});
			int rows = std::max(1, (h.content.h - lay::LINE_H - lay::SP6) / lay::MEDIA_ROW_H);
			fitRows(rowCursor, rows);
			for (int i = 0; i < rows && scroll + i < (int)songs.size(); i++)
			{
				const PlaylistEntry& e = songs[scroll + i];
				Kit::Media m;
				m.glyph = &G_NOTE;
				m.name = e.title;
				m.note = e.duration > 0 ? formatTime(e.duration) : "";
				bool here = plays(e.path);
				if (here) { m.pill = "Now"; m.tone = Kit::Pill::Live; }
				m.heart = store && store->isLiked(e.path);
				m.focused = scroll + i == rowCursor;
				m.pressed = touch == T_SONG + scroll + i && !touchMoved;
				Rect r{inner.x, h.content.y + lay::LINE_H + lay::SP6 + i * lay::MEDIA_ROW_H, inner.w, lay::MEDIA_ROW_H};
				k.mediaRow(lay::mediaRow(r), m);
			}
			return;
		}

		// ---- the views ----
		std::vector<Kit::FilterItem> views = viewItems();
		lay::MusicHeader mh = lay::musicHeader(inner, k.filterWidths(views));
		if (!reading && snap->songs.empty())
		{
			// no Music folder, or none with a song in it (the mock-up's music-empty): the two keys, and the
			// folder's name in the kit's accent
			std::vector<std::string> lines = ui.wrapText(EMPTY_MUSIC_LINE, F_I14, 400, 99);
			lay::EmptyState e = lay::emptyState(inner, (int)lines.size(), 2);
			emptyKey[0] = e.key[0];
			emptyKey[1] = e.key[1];
			Kit::Empty v;
			v.glyph = &G_NOTE;
			v.title = "No music here yet";
			v.lines = lines;
			accentFirst(v, lines, "Music");
			v.key1 = "Browse Files";
			v.glyph1 = &G_FOLDER;
			v.key2 = "Web dashboard";
			v.glyph2 = &G_QR;
			v.pressed1 = touch == T_EMPTY1 && !touchMoved;
			v.pressed2 = touch == T_EMPTY2 && !touchMoved;
			v.hotkeys = p.settings.buttonHotkeys;
			k.emptyState(e, v);
			return;
		}
		// the view chips (the "From …" line they sat under is the header's crumb, once drawn here as well)
		k.filters(mh.chips, views, view, -1,
			touch >= T_CHIP && touch < T_CHIP + 3 && !touchMoved ? touch - T_CHIP : -1);
		if (reading)
		{
			// the first read, as the spec draws it (music-reading): which card, how far, a bar over the folders
			// (the tags are the slow part, so the bar rests between folders — better honest than clever)
			lay::Notice n = lay::notice({inner.x, mh.strip.y + lay::CHIP_STRIP + lay::SP8, inner.w, 0}, true, true, 0);
			Kit::NoticeRow row;
			row.tone = Kit::NoticeTone::Working;
			row.glyph = &G_REFRESH;
			auto pr = ui.indexWorker ? ui.indexWorker->progress() : MediaIndexWorker::Progress{};
			std::string card = walkCardName(pr.card);
			row.text = card.empty() ? "Reading the Music folders" : "Reading Music on " + card;
			row.second = pr.foldersKnown > 0
				? std::to_string(std::min(pr.foldersDone, pr.foldersKnown)) + " of " + std::to_string(pr.foldersKnown) +
					" folders \xC2\xB7 " + std::to_string(pr.songs) + (pr.songs == 1 ? " song read" : " songs read")
				: "the list lands when it's done";
			row.share = pr.foldersKnown > 0 ? (double)pr.foldersDone / pr.foldersKnown : 0;
			k.notice(n, row);
			return;
		}
		if (view == 0)
		{
			// the Albums grid
			int n = (int)snap->albums.size();
			int gy = mh.strip.y + lay::CHIP_STRIP + lay::SP8;
			Rect gridArea{inner.x, gy, inner.w, inner.y + inner.h - gy};
			lay::AlbumGrid g = lay::albumGrid(gridArea, n, scroll, 5);
			for (int i = g.first * 5; i < n && i < (g.first + g.shown) * 5; i++)
			{
				const MediaAlbum& a = snap->albums[i];
				const std::string name = wordOr(*snap, a.name, "Unknown album");
				Kit::Album cell;
				if (!a.songs.empty())
				{
					const MediaSong& first = snap->songs[a.songs.front()];
					cell.art = [this, path = first.path, name](Rect r) { ui.drawSongArt(path, name, r, 5); };
				}
				cell.artKey = name;
				cell.title = name;
				cell.artist = wordOr(*snap, a.albumArtist, "Unknown artist");
				bool here = false;
				for (uint32_t si : a.songs)
					if (plays(snap->songs[si].path)) { here = true; break; }
				cell.playing = here && !p.paused;
				cell.paused = here && p.paused;
				cell.focused = i == cursor;
				k.albumTile(lay::albumTile(g.cell[i]), cell);
			}
			return;
		}
		// the Artists list
		int n = (int)snap->artists.size();
		int gy = mh.strip.y + lay::CHIP_STRIP + lay::SP8;
		int rows = std::max(1, (inner.y + inner.h - gy) / lay::ARTIST_ROW_H);
		fitRows(cursor, rows);
		for (int i = 0; i < rows && scroll + i < n; i++)
		{
			const MediaArtist& a = snap->artists[scroll + i];
			Kit::Artist row;
			row.name = wordOr(*snap, a.name, "Unknown artist");
			row.second = std::to_string(a.albums.size()) + (a.albums.size() == 1 ? " album" : " albums") + " \xC2\xB7 " +
				std::to_string(a.songs) + (a.songs == 1 ? " song" : " songs");
			row.focused = scroll + i == cursor;
			row.pressed = touch == T_ARTIST + scroll + i && !touchMoved;
			Rect r{inner.x, gy + i * lay::ARTIST_ROW_H, inner.w, lay::ARTIST_ROW_H};
			k.artistRow(lay::artistRow(r), row);
		}
	}

	std::string Ui::Music::signature(const Player&) const
	{
		// everything the drawing shows that Ui::signature doesn't already hold (the playing file and its pause are
		// its base; a Like moves the store's serial, its |pl term, and so does the playlists' count), and the
		// reading notice's step, so the bar moves while the first pass walks
		char buf[160];
		snprintf(buf, sizeof buf, "|mu%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d", view, albumAt, artistAt, cursor, scroll,
			rowCursor, keyFocus, artistZone * 8 + artistAlbum, albumScroll, touch, (int)generation());
		std::string s = buf;
		if (!snapshot() || generation() == 0)
		{
			s += "R";
			if (ui.indexWorker)
			{
				auto pr = ui.indexWorker->progress();
				s += "." + std::to_string(pr.foldersDone + 1000 * pr.foldersKnown + pr.songs / 25 + pr.videos / 25);
			}
		}
		s += "a" + std::to_string(ui.artSerial);		// an album's art arrived: the tiles draw it now
		return s;
	}
}
