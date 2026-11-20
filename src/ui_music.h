// Music, the rail's section (Page::Music; docs/redesign/src/screens.js: music-albums, music-artists, music-album,
// music-artist, music-empty, music-reading, and the rule in docs/redesign/components.md "Search and Music: the
// index"). An area of Ui (Ui::Area, docs/notes/ui-areas.md): three views of the cards' media index — Albums, Artists,
// and Playlists — read through MediaIndex::snapshot() (src/mediaindex.h; the worker's, handed in by main).
//
// The third view is not drawn here: it is the browser's own playlists pages (browseDir PLROOT, and a playlist's own
// below it), opened by show() and drawn under this section (Shell::section maps them here); all their behaviour is
// the browser's and the playlists', untouched. L2 / R2, or a chip, steps between the views from either side.
//
// Below the views an album's page and an artist's page are kept here (one Page, the area's own state): the page head
// with Play / Shuffle / Add (lay::pageHead), an album's track rows (lay::trackRow), an artist's albums as cover
// cells (lay::albumTile, one strip) and their songs as rows (lay::mediaRow). The art is the shared song-art cache
// (Ui::drawSongArt), asked for the album's first song; the signature follows the index's generation and the art's
// arrivals (Ui::artSerial).
#pragma once
#include "ui.h"
#include "ui_shell.h"
#include "ui_kit.h"
#include "mediaindex.h"

namespace bp
{
	struct Ui::Music : Ui::Area
	{
		explicit Music(Ui& u) : Area(u) {}

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Music; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		// a section page like Files: its downs come after the header's keys
		bool pointerDownLate() const override { return true; }
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;

		// the cards' index, handed by main (the worker owns it; a null one draws the reading notice)
		MediaIndex* source = nullptr;
		std::shared_ptr<const MediaSnapshot> snapshot() const;
		uint32_t generation() const { return source ? source->generation() : 0; }

		// ---- the three views: 0 Albums, 1 Artists, 2 Playlists (the browser's own page) ----
		int view = 0;
		void open(Player& p);			// the rail's way in: the view as it was left
		void show(Player& p);			// to `view` (Playlists: the browser's playlists page)
		void viewStep(int dir, Player& p);	// L2 / R2 and the chips: the views, wrapping
		// a page below the views: the album or the artist open, an index into the snapshot's list (-1 none).
		// `cursor` stays the views' own (the cell or row B comes back to); `rowCursor` is the page's rows
		int albumAt = -1, artistAt = -1;
		void openAlbum(int i, Player& p);
		void openArtist(int i, Player& p);

		// ---- the focus, in whatever is shown ----
		int cursor = 0, scroll = 0;		// the views' cell or row, and the first row shown
		int rowCursor = 0;				// an album's tracks, an artist's songs
		int keyFocus = -1;				// the head's keys (-1: the rows below them)
		int artistZone = 0;				// an artist's page: 0 their albums, 1 their songs
		int artistAlbum = 0, albumScroll = 0;	// the album strip's focus, and its first cell shown
		void fitRows(int at, int rows);		// the scroll that keeps row `at` in view

		// ---- playing (through the Session, as Files does: the play request, main opens it) ----
		void play(const std::vector<std::string>& paths, int index, const std::string& title, bool shuffle, Player& p);
		void playAlbum(int album, int fromSong, bool shuffle, Player& p);
		void playArtist(int artist, int fromSong, bool shuffle, Player& p);
		// the album's (or the artist's) songs as playlist entries, in their order
		std::vector<PlaylistEntry> albumEntries(int album) const;
		std::vector<PlaylistEntry> artistEntries(int artist) const;
		// an artist's albums newest first (an album's year: its first song that has one)
		std::vector<uint32_t> artistAlbumsByYear(int artist) const;

		// ---- the More sheet (X): what the pages have (Add to playlist, Like, Show in folder) ----
		void openMoreSheet(Player& p);

		// ---- a finger ----
		// what it went down on: T_CHIP + i the views' chips, T_ALBUM + i a cell in the Albums grid or the artist's
		// strip, T_ARTIST + i a row, T_KEY + i a head key, T_TRACK + i an album's track, T_SONG + i an artist's
		// song, T_GROUND the page's own ground (a drag scrolls, a tap does nothing), T_EMPTY1 / T_EMPTY2 the
		// empty page's keys
		enum { T_NONE = -1, T_CHIP = 0, T_ALBUM = 10, T_ARTIST = 100, T_KEY = 200, T_TRACK = 300, T_SONG = 400,
			T_GROUND = 499, T_EMPTY1 = 500, T_EMPTY2 = 501 };
		int touch = T_NONE;
		int touchY = 0, scrollStart = 0;
		bool touchMoved = false;
		// what a drag scrolls: the thing under the finger's own rows
		int dragRow = 60, dragCount = 0, dragShown = 1;
		Rect emptyKey[2]{};

		// ---- the shell's header (Ui::Shell::head asks this) ----
		ShellHead head(const Player& p) const;
		// the line under the title's words: "From Music on both cards · 148 albums · 1,912 songs"
		std::string fromLine(const MediaSnapshot& snap) const;
		std::string countsLine(const MediaSnapshot& snap) const;	// "148 albums · 1,912 songs"
		std::vector<Hint> hintList(const Player&) const;
		// (the file's own small helpers, defined in ui_music.cpp: they name Kit, which is Ui's)
		std::vector<Kit::FilterItem> viewItems() const;		// the views' chips (the counts as far as they're known)
		void accentFirst(Kit::Empty& e, const std::vector<std::string>& lines, const char* word) const;
	};
}
