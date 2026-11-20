// Videos, the rail's section (Page::Videos; docs/redesign/src/screens.js: videos, videos-empty, videos-reading).
// An area of Ui (Ui::Area, docs/notes/ui-areas.md): the films on the cards in one list, filled by itself from the
// folders named Videos and Movies at the top of each card (the index's own rule, docs/redesign/components.md
// "Search and Music: the index"), read through MediaIndex::snapshot().
//
// Three views (L2 / R2, or a chip): All, by name; Folders, the folders directly inside the Videos folders and the
// loose files under them; Not finished, what was started and left, newest first, from the resume store. A folder
// row opens the folder's own list (the videos under it, however deep). A plays from the place — the file's own
// start= comes from resume.tsv as it always does — with the view's videos as the list, as a folder's files play
// with their folder (the spec: Play makes the view the list). No tags are read and no thumbnails made: a name, a
// size, a date, the length from the durations cache, the place from the resume store.
#pragma once
#include "ui.h"
#include "ui_shell.h"
#include "ui_kit.h"
#include "mediaindex.h"

namespace bp
{
	struct Ui::Videos : Ui::Area
	{
		explicit Videos(Ui& u) : Area(u) { u.shell->adopt(Page::Videos); }

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Videos; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		bool pointerDownLate() const override { return true; }
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;

		// the cards' index, handed by main (the worker owns it; a null one draws the reading notice)
		MediaIndex* source = nullptr;
		std::shared_ptr<const MediaSnapshot> snapshot() const;
		uint32_t generation() const { return source ? source->generation() : 0; }

		// ---- the views: 0 All, 1 Folders, 2 Not finished ----
		int view = 0;
		std::string folder;		// the folder list open ("" the views)
		void open(Player& p);		// the rail's way in: the view as it was left
		void viewStep(int dir, Player& p);	// L2 / R2 and the chips: the views, wrapping
		void openFolder(const std::string& path);
		// the list's order (More › Sort, stepped in place): by name (the snapshot's own, as the spec draws All),
		// newest first, biggest first. Not finished keeps the store's own order (the spec's rule for it)
		enum Sort { SORT_NAME = 0, SORT_NEWEST, SORT_BIGGEST, SORTS };
		int sort = SORT_NAME;
		void sortStep(int dir);

		// ---- the rows the view shows ----
		// a video, or a folder inside the Videos folders. `videos` counts what is under a folder row, however deep
		struct Row
		{
			bool folder = false;
			std::string path, name;		// the file's or the folder's, and the name shown (less the extension)
			long long size = 0;
			int64_t mtime = 0;
			int videos = 0;			// a folder row's count
		};
		// built for the view shown, from the snapshot and the resume store, and kept until one of them moves
		const std::vector<Row>& rowsFor(const Player& p) const;
		mutable std::vector<Row> rowsCache;
		mutable std::string rowsKey;
		mutable std::unordered_map<std::string, int> videoWhere;	// the snapshot's videos, for the store's lookups
		mutable uint32_t pathsGen = 0;
		std::vector<Row> buildRows(const Player& p) const;

		// the views' chips: All, Folders, Not finished, with their counts
		std::vector<Kit::FilterItem> viewItems(const Player& p) const;

		// ---- the focus ----
		int cursor = 0, scroll = 0;
		void fitRows(int at, int rows) { scroll = std::clamp(scroll, std::max(0, at - rows + 1), std::max(0, at)); }

		// ---- playing (the play request, as Files' Play all: main opens it and the Session takes it) ----
		void play(const Row& r, const Player& p) const;

		// ---- the More sheet (X): what a video row has ----
		void openMoreSheet(const Row& row, Player& p);

		// ---- a finger ----
		// what it went down on: T_CHIP + i the chips, T_ROW + i a row, T_EMPTY1 / T_EMPTY2 the empty page's keys
		enum { T_NONE = -1, T_CHIP = 0, T_ROW = 10, T_GROUND = 499, T_EMPTY1 = 500, T_EMPTY2 = 501 };
		int touch = T_NONE;
		int touchY = 0, scrollStart = 0;
		bool touchMoved = false;
		Rect emptyKey[2]{};

		// ---- the shell's header (Ui::Shell::head asks this) ----
		ShellHead head(const Player& p) const;
		std::string fromLine(const MediaSnapshot& snap) const;	// "From Videos and Movies on both cards · 86 videos"
		std::vector<Hint> hintList(const Player& p) const;
		// (the file's own small helper, defined in ui_videos.cpp: it names Kit, which is Ui's)
		void accentFirst(Kit::Empty& e, const std::vector<std::string>& lines, const char* word) const;
	};
}
