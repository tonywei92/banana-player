// The redesign's shared frame for every section page (docs/redesign/components.md "Frame"; screens.js's shell()):
// the rail at the left, the header and the hint bar. The screen agents draw their own bodies in lay::shell().body;
// this is the one place the frame is drawn and the rail is stepped. No page has a bar at its foot any more (the
// owner, 2026-11-19): what plays is reached from its player, Home's card, Quick settings' Now playing block, and the
// rail's Now playing key.
#pragma once
#include "ui.h"
#include <functional>
#include <set>

namespace bp
{
	// What a page tells the shell about its header. The page supplies this and draws its body inside
	// lay::shell().body; until a page is rebuilt on the kit the shell bridges its old full-panel drawing into the
	// body box (see beginBody/endBody). `right` is the header's right slot (badges, a group's neighbours), drawn from
	// `right` leftwards and returning its left edge; the title ends left of what it leaves. Search is the rail's key.
	struct Ui::ShellHead
	{
		std::string title, crumb;
		bool back = true, more = true;
		std::vector<Hint> hints;
		std::function<int(int right, int cy)> right;
		int rightW = 0;	// the room `right` needs, so the title ends before it
	};

	// An area in shape (Ui::Area) but with no page of its own: Browse and Settings keep their keys and touches
	// (docs/notes/ui-areas.md, "PageReader and Manga"), and Ui asks the shell for the frame before drawing them. It is
	// not listed in Ui::areas, so `Ui::area()` still names the page's own area.
	struct Ui::Shell : Ui::Area
	{
		explicit Shell(Ui& u) : Area(u) {}

		// the spec's sections, in the rail's order (Music and Books are not built yet: cells() leaves them out)
		enum class Section { Home, Files, Music, Playlists, Books, Extensions, Network, Settings };
		struct Cell
		{
			Section section;
			const Glyph* glyph;
			const char* label;
		};

		bool page(Page pg) const;	// the shared frame's pages: Browse, ExtSection and Settings
		std::vector<Cell> cells(const Player& p) const;	// the rail's sections (Extensions always, Manga is a row in it)
		Section section(const Player& p) const;	// the one the page shown belongs to (Settings for Page::Settings)
		lay::Shell layout(const Player& p) const;	// lay::shell()
		ShellHead head(const Player& p) const;	// this step's head: title, crumb, hints; later the pages' own

		// the rail's Now playing key's hold (600 ms: the More sheet of what plays), timed while the frame shows
		void tick(Player& p) override;

		// the entry point: the frame for `section`, then nothing of the body (the page draws that itself)
		void draw(Section section, const ShellHead& head, const Player& p);

		// a section's page, from any page or a page's own B (the Extensions page's B is Home): L/R's own step and a
		// tap on the rail both come here
		void openSection(Section s, Player& p);

		// The old bodies draw the whole panel (their own header, hints and 640-wide rows). beginBody renders that
		// into a texture; endBody copies the 0,54..450 part into layout().body and remembers the mapping so
		// mapPoint() can send the page's touches where they were drawn. A page rebuilt on the kit calls adopt(pg)
		// once (in its area's constructor) and draws directly in the body box: no texture, no mapping. The browser's
		// rebuilt views (Home, All recent, Files, Playlists) share Page::Browse with the pages still on the old
		// drawing (Network, the forms), so adoptBrowse() says the browser is rebuilt and
		// adopted(p) tells its views from the rest.
		bool beginBody(const Player& p);
		void endBody(const Player& p);
		bool mapPoint(const Player& p, int& x, int& y) const;
		void adopt(Page pg);	// the page's body is drawn in layout().body (a screen rebuilt on the kit)
		void adoptBrowse();	// the browser's rebuilt views: Home, All recent, Files, Playlists
		bool adopted(Page pg) const;
		bool adopted(const Player& p) const;	// the page shown draws its body itself (Browse: only its rebuilt views)

		// frame keys and touches (Ui calls them while page() is true): L/R step the sections (Search is passed by), L3
		// and the rail's Search key open Search's page, the rail's cells open a section, back is B, More is X
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		std::string signature(const Player& p) const override;
		// (no pages of its own: the page's area keeps its dispatch; these are never asked)
		bool has(Page) const override { return false; }
		void draw(const Player&) override {}

	private:
		enum { F_BACK, F_MORE, F_STATUS, F_NP, F_SEARCH, F_RAIL = 100 };
		int pressedFrame = -1;	// F_* or F_RAIL + the rail cell under a finger

		int railScroll = 0;	// the sections' offset, moved by a finger drag when they don't fit
		bool railDrag = false, railMoved = false;
		int railDownY = 0, railScrollAt = 0;
		Uint32 npHoldAt = 0;	// a finger on the rail's Now playing key: held, its More sheet
		mutable lay::Box statusBox{0, 0, 0, 0};	// the header's status cluster, as last drawn (one touch target)
		void search(Player& p);	// Search's one place: a note until the page is built
		int activeIndex(const std::vector<Cell>& cells, Section at) const;
		bool frameRects(const Player& p, const ShellHead& h, lay::Box& back, lay::Box& more) const;
		void drawHeader(const ShellHead& h, const Player& p);

		std::set<Page> bodyPages;	// pages whose body is drawn in the shell's box (empty: the old full-panel bodies)
		bool browseBuilt = false;	// the browser's rebuilt views are drawn in the shell's box
		SDL_Texture* bodyTex = nullptr;
		int bodyTexW = 0, bodyTexH = 0;
		bool bodyTargetOn = false;
		struct Bridge
		{
			bool on = false;
			int x = 0, y = 0, w = 0, h = 0;	// in panel units
		} bridge;
		Bridge bridgeFor(const Player& p) const;
	};
}
