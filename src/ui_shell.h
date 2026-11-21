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
	// lay::shell().body. `right` is the header's right slot (badges, a group's neighbours), drawn from
	// `right` leftwards and returning its left edge; the title ends left of what it leaves. Search is the rail's key.
	struct Ui::ShellHead
	{
		std::string title, crumb;
		bool back = true, more = true;
		bool cancel = false;	// the back key is a cross (adding files to a playlist: it leaves at once)
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

		// the spec's sections, in the rail's order (docs/redesign/components.md, Section rail): Home, Files, Videos,
		// Music, Books, Extensions, Network, then Settings pinned at the foot. Playlists is not one: it is Music's
		// third view (L2 / R2 or its chip)
		enum class Section { Home, Files, Videos, Music, Books, Extensions, Settings };
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
		ShellHead head(const Player& p) const;	// this step's head: title, crumb, hints; a section's home has no back

		// the rail's Now playing key's hold (600 ms: the More sheet of what plays), timed while the frame shows
		void tick(Player& p) override;

		// the entry point: the frame for `section`, then nothing of the body (the page draws that itself)
		void draw(Section section, const ShellHead& head, const Player& p);

		// a section's page, from any page or a page's own B (the Extensions page's B is Home): L/R's own step and a
		// tap on the rail both come here
		void openSection(Section s, Player& p);
		// a section's home: its first page (Home, Files' places or Network, Videos' and Music's views, Books, the
		// Extensions list, Settings' tiles as a section). There the header has no back key and B does nothing
		bool atSectionHome(const Player& p) const;
		// the rail's tap on the section shown (the tab bar's rule): inside it, back to its home at the top; on its
		// home, its list glides to the top
		void toSectionHome(Section s, Player& p);

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
		ShellHead pageHead(const Player& p) const;	// what the page says of its header (head() less a home's back)
		int activeIndex(const std::vector<Cell>& cells, Section at) const;
		bool frameRects(const Player& p, const ShellHead& h, lay::Box& back, lay::Box& more) const;
		void drawHeader(const ShellHead& h, const Player& p);
	};
}
