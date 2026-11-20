// The redesign's shared frame (ui_shell.h): the rail, the header, the hint bar and the mini bar for every section
// page, and the one step of it that is a key (L / R through the sections). The bodies stay their areas' until they
// are rebuilt on the kit; beginBody/endBody bridge one into the body box meanwhile.
#include "ui_shell.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_browse.h"
#include "ui_home.h"
#include "ui_playlists.h"
#include "ui_settings.h"
#include "ui_manga.h"
#include "ui_search.h"
#include "ui_music.h"
#include "ui_videos.h"
#include "ui_books.h"
#include "format.h"
#include "netpath.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	namespace
	{
		const char* const DOT = " \xE2\x80\xBA ";	// the crumb's separator (" › ")
	}

	bool Ui::Shell::page(Page pg) const
	{
		// the shell's pages: the browser (Home, Files, Playlists, Network, All recent), the Extensions section's
		// list, Settings, Search, the library sections the index fills (Music, Videos, Books), and Manga's pages
		// (Manage extensions, the four tabs, a series, Sources, Manga settings) but the reader's, which keeps its
		// own frame (a comic's page needs the whole panel).
		return pg == Page::Browse || pg == Page::ExtSection || pg == Page::Settings || pg == Page::Search ||
			pg == Page::Music || pg == Page::Videos || pg == Page::Books || (ui.manga && ui.manga->shellPage(pg));
	}

	std::vector<Ui::Shell::Cell> Ui::Shell::cells(const Player& p) const
	{
		// The spec's order: Home, Files, Videos, Music, Books, Extensions, Network — seven sections, all in
		// sight (the cells go 45 tall; lay::rail). Videos and Music are filled by themselves from the folders named
		// Videos/Movies and Music on each card; Books from the doc places and the Books and Documents folders.
		// Extensions is one cell for every extension that is on (Manga reader is a row of its page), and it is
		// always in the rail: opening it starts and stops nothing.
		(void)p;
		return {
			{Section::Home, &G_HOME, "Home"},
			{Section::Files, &G_FOLDER, "Files"},
			{Section::Videos, &G_FILM, "Videos"},
			{Section::Music, &G_NOTE, "Music"},
			{Section::Books, &G_BOOK, "Books"},
			{Section::Extensions, &G_BLOCKS, "Extensions"},
			{Section::Network, &G_NETWORK, "Network"},
		};
	}

	Ui::Shell::Section Ui::Shell::section(const Player& p) const
	{
		if (ui.page == Page::Settings) return Section::Settings;
		// the library sections, filled by themselves from the cards (Music's third view is the browser's own
		// Playlists page, drawn under Music)
		if (ui.page == Page::Music) return Section::Music;
		if (ui.page == Page::Videos) return Section::Videos;
		if (ui.page == Page::Books) return Section::Books;
		// Manga is a page of Extensions: its own pages and Manage extensions keep the section's cell lit
		if (ui.page == Page::ExtSection || ui.page == Page::Manga || ui.page == Page::MangaSeries || ui.page == Page::MangaRead ||
			ui.page == Page::MangaSettings || ui.page == Page::MangaSources || ui.page == Page::Extensions)
			return Section::Extensions;
		if (ui.page != Page::Browse) return Section::Home;
		const std::string& dir = ui.browse->browseDir;
		if (dir == HOMEDIR || dir == RECENT) return Section::Home;
		if (dir == PLROOT || isPlDir(dir)) return Section::Music;		// Playlists: Music's third view
		if (dir == NETROOT || dir == FORMDIR || dir == FINDNET || isNetPath(dir)) return Section::Network;
		return Section::Files;
	}

	lay::Shell Ui::Shell::layout(const Player& p) const
	{
		(void)p;
		return lay::shell();
	}

	int Ui::Shell::activeIndex(const std::vector<Cell>& cs, Section at) const
	{
		for (size_t i = 0; i < cs.size(); i++)
			if (cs[i].section == at) return (int)i;
		return at == Section::Settings ? (int)cs.size() : -1;
	}

	Ui::ShellHead Ui::Shell::head(const Player& p) const
	{
		ShellHead h;
		h.back = ui.backIconShown(p);
		h.more = ui.page != Page::Settings;
		// the library sections: their own areas say what their header shows (title, crumb, hints)
		if (ui.page == Page::Music) return ui.music->head(p);
		if (ui.page == Page::Videos) return ui.videos->head(p);
		if (ui.page == Page::Books) return ui.books->head(p);
		// Manga's pages (Manage extensions, the app's tabs and pages): the area says
		if (ui.page != Page::ExtSection && ui.manga->shellPage(ui.page)) return ui.manga->head(p);
		if (ui.page == Page::ExtSection)
		{
			// the Extensions section (docs/redesign: ext-section, ext-section-video, ext-section-empty): a list that
			// starts nothing; A opens a row, B goes Home, L/R step the sections, L3 is Search. START is Settings
			// everywhere: no hint bar repeats it
			h.title = "Extensions";
			h.crumb = p.settings.mangaOn ? "1 on" : "None on";
			h.back = false;
			h.more = false;
			h.hints = p.settings.mangaOn
				? std::vector<Hint>{{"A", "Open"}, {"B", "Home"}, {"L/R", "Sections"}, {"L3", "Search"}}
				: std::vector<Hint>{{"A", "Manage extensions"}, {"B", "Home"}, {"L/R", "Sections"}, {"L3", "Search"}};
			return h;
		}
		if (ui.page == Page::Settings)
		{
			// Settings is START's page, opened as a section: its own title, crumb and hints; a group also has its
			// neighbours in the right slot ("L2 Playback · Music R2", the spec's settings-group)
			h.back = true;
			h.more = false;
			h.title = ui.settingsPage->title();
			h.crumb = ui.settingsPage->crumb();
			h.hints = ui.settingsPage->hints(p);
			if (ui.setGroup >= 0)
			{
				int prev = (ui.setGroup + GROUPS - 1) % GROUPS, next = (ui.setGroup + 1) % GROUPS;
				static const char* const SHORT_GROUP[] = {"Playback", "Subtitles", "Music", "Photos", "Reading", "Files",
					"Power", "Web", "Updates", "About"};
				std::string prevName = SHORT_GROUP[prev], nextName = SHORT_GROUP[next];
				Kit& k = *ui.kit;
				h.rightW = k.capsW("L2") + 6 + k.textW(prevName, F_I12) + 14 + k.textW(nextName, F_I12) + 6 + k.capsW("R2");
				h.right = [this, prevName, nextName](int right, int cy) {
					Kit& k = *ui.kit;
					int r = right, w = k.capsW("R2");
					k.caps("R2", r - w, cy, ACCENT, BG);
					r -= w + 6;
					ui.drawTextMid(nextName, F_I12, DIM, r, cy, 0, 1);
					r -= k.textW(nextName, F_I12) + 14;
					ui.drawTextMid(prevName, F_I12, DIM, r, cy, 0, 1);
					r -= k.textW(prevName, F_I12) + 6;
					w = k.capsW("L2");
					k.caps("L2", r - w, cy, ACCENT, BG);
					return r - w;
				};
			}
			return h;
		}
		// Search: its own title, crumb (the query and how many it found) and hints; it is a tool, not a section
		if (ui.page == Page::Search) return ui.search->head(p);
		const Browse& b = *ui.browse;
		const std::string& dir = b.browseDir;
		// the rebuilt views say what their header shows (title, crumb, hints, back, More); the rest keep the generic
		// values until their screens are rebuilt too
		if (b.atHome() || dir == RECENT)
		{
			h.title = b.homeView->title();
			h.crumb = b.homeView->crumb(p);
			h.hints = b.homeView->hintList(p);
			return h;
		}
		if (b.filesPage())
		{
			h.title = b.fileSel ? (b.fileSelCount() ? std::to_string(b.fileSelCount()) + " selected" : std::string("Select"))
				: b.filesTitle();
			h.crumb = b.filesCrumb();
			h.hints = b.filesHints(p);
			h.back = true;
			h.more = !b.fileSel;	// (the old header hid More in select mode; X still opens it)
			if (b.filesReadOnly())
			{
				// the read-only badge sits in the right slot, left of the status cluster (as the old header had it)
				Kit& k = *ui.kit;
				h.rightW = lay::badgeW(k.textW(lay::caps("Read only"), F_I11B), true);
				h.right = [this](int right, int cy) {
					return right - ui.kit->badge(right, cy, {&G_LOCK, "Read only"});
				};
			}
			return h;
		}
		if (dir == PLROOT || isPlDir(dir))
		{
			h.title = b.playlists->plTitle(p);
			h.crumb = b.playlists->plCrumb(p);
			h.hints = b.playlists->plHints(p);
			h.back = true;
			return h;
		}
		if (dir == NETROOT || dir == FORMDIR || dir == FINDNET || isNetPath(dir))
		{
			h.title = dir == NETROOT ? "Network" : dir == FORMDIR ? (b.formId.empty() ? "Add connection" : "Edit connection")
				: dir == FINDNET ? "Find on the network" : baseName(dir);
			int servers = 0;
			for (auto& row : b.browseRows) servers += row.id == 4;
			h.crumb = dir == NETROOT ? (servers ? std::to_string(servers) + (servers == 1 ? " connection" : " connections")
				: std::string("No connections")) : std::string("Network");
			h.more = dir == NETROOT;
			if (dir == NETROOT && b.emptyAction())
				h.hints = {{"A", "Find on the network"}, {"Y", "Add connection"}, {"B", "Home"}, {"L/R", "Sections"}};
			else if (dir == NETROOT) h.hints = {{"A", "Open"}, {"X", "More"}, {"Y", "Add"}, {"B", "Home"}, {"L/R", "Sections"}};
			else if (dir == FORMDIR) h.hints = {{"A", "Edit"}, {"LEFT/RIGHT", "Change"}, {"Y", "Test"}, {"START", "Save"}, {"B", "Cancel"}};
			else if (dir == FINDNET) h.hints = {{"A", "Add this one"}, {"B", "Back"}};
			else h.hints = {{"A", "Open"}, {"X", "More"}, {"Y", "Select"}, {"B", "Up"}, {"L/R", "Sections"}};
		}
		else
		{
			// Files, and the folders under it: the folder's name, the way there under it ("Files › Videos › Movies")
			h.title = dir.empty() ? "Files" : baseName(dir);
			std::string where = b.browseWhere();
			h.crumb = dir.empty() ? "Your cards" : std::string("Files") + DOT + where;
			h.hints = {{"A", "Open"}, {"B", "Up"}, {"X", "More"}, {"Y", "Select"}, {"L/R", "Sections"}};
		}
		return h;
	}

	// ---- the frame's keys and touches ----

	void Ui::Shell::search(Player& p)
	{
		// Search's one way in: L3 and the rail's Search key both come here. The page keeps its last query (a second
		// press edits it); opening it from anywhere remembers where B goes back to.
		ui.search->open(p);
	}

	void Ui::Shell::openSection(Section s, Player& p)
	{
		if (s == Section::Settings)
		{
			// the rail and L/R open Settings as a section: B goes Home (not back to where START would)
			ui.openSettings(p, false);
			return;
		}
		// where the Extensions section was left (Manga, where you were; its list): the rail's rule
		if (ui.manga) ui.manga->sectionLeft(ui.page);
		// leaving Settings: it becomes the browser, whichever way it was opened (a section page now shows)
		ui.settingsOver = false;
		if (ui.page == Page::Settings) ui.page = Page::Browse;
		// the library sections are the areas' own pages (the browser's is the only one left to set by hand)
		switch (s)
		{
		case Section::Music: ui.music->open(p); return;
		case Section::Videos: ui.videos->open(p); return;
		case Section::Books: ui.books->open(p); return;
		default: break;
		}
		// another section's page is the browser's (the Extensions page sets its own, in openExtensionsSection)
		if (ui.page != Page::Browse && s != Section::Extensions) ui.page = Page::Browse;
		switch (s)
		{
		case Section::Home:
			if (!ui.browse->atHome()) ui.browse->enterFolder(HOMEDIR, p);
			break;
		case Section::Files: ui.browse->openSection(TAB_LOCAL, p); break;
		case Section::Network: ui.browse->openSection(TAB_NETWORK, p); break;
		// Extensions: its list, or Manga again where it was left (and never opened by the rail itself, so arriving
		// here starts and stops nothing)
		case Section::Extensions: ui.manga->openExtensionsSection(p); break;
		default: break;
		}
	}

	void Ui::Shell::tick(Player& p)
	{
		// a finger on the rail's Now playing key: held 600 ms, the More sheet of what plays (as the mini bar's was);
		// the lift that follows chooses nothing
		if (pressedFrame != F_NP || !npHoldAt) return;
		if (SDL_GetTicks() - npHoldAt < 600) return;
		npHoldAt = 0;
		pressedFrame = -1;
		ui.swallowTouch = true;
		ui.openNowMenu(p);
	}

	bool Ui::Shell::key(Button b, Player& p)
	{
		if (!page(ui.page)) return false;
		// a Manga sheet, menu or the keyboard has the keys (it is drawn over the frame, as it always was)
		if (ui.manga->shellPage(ui.page) && ui.manga->shellModal()) return false;
		const Browse& br = *ui.browse;
		bool modal = br.playlists->pickerOpen || br.playlists->selMode != SEL_NONE || br.playlists->addPending.serial;
		// All recent's tabs keep a key: L2 / R2 (the spec moves them there, L / R being sections everywhere in the shell)
		if ((b == Button::L2 || b == Button::R2) && ui.page == Page::Browse && br.browseDir == RECENT)
		{
			if (p.settings.keepHistory && br.homeView->recentCounts[0])
				br.homeView->stepRecentTab(b == Button::R2 ? 1 : -1, p);
			return true;
		}
		// Search by the keys (L3), while browsing, on the Extensions page, on Settings or on a library page (the
		// sections the index fills): a note for now (not while a form, a selection or the picker has the page)
		if (b == Button::L3 && (ui.page == Page::Settings || ui.manga->shellPage(ui.page) || ui.page == Page::Music ||
			ui.page == Page::Videos || ui.page == Page::Books || (ui.page == Page::Browse && !modal)))
		{
			search(p);
			return true;
		}
		// Search keeps every other key: LEFT / RIGHT move its caret (or its page), SELECT its keyboard's layer.
		// It is a tool: L / R do not step the sections under it.
		if (ui.page == Page::Search) return false;
		// SELECT is the rail's Now playing key's key: what plays, from any of the shell's pages (a copy or move, a
		// selection and the picker keep theirs)
		if (b == Button::Select && ui.nowPlayable(p) && !modal && !(ui.page == Page::Browse && br.clipOp != CLIP_NONE))
		{
			ui.goNowPlaying(p);
			return true;
		}
		if (b != Button::L && b != Button::R) return false;
		// an open form's LEFT / RIGHT step its choices, a select mode and the picker keep theirs: the section step does
		// not take those keys away
		if (ui.page == Page::Browse && (modal || br.browseDir == FORMDIR || br.browseDir == FINDNET))
			return false;
		std::vector<Cell> cs = cells(p);
		if (cs.empty()) return false;
		int n = (int)cs.size() + 1;	// + Settings, pinned at the foot and in the ring
		int at = activeIndex(cs, section(p));
		int next = ((at + (b == Button::R ? 1 : -1)) % n + n) % n;
		openSection(next == (int)cs.size() ? Section::Settings : cs[next].section, p);
		return true;
	}

	bool Ui::Shell::frameRects(const Player& p, const ShellHead& h, lay::Box& back, lay::Box& more) const
	{
		lay::Shell s = layout(p);
		back = {s.main.x + 10, 3, 56, 48};
		more = {lay::PANEL_W - 10 - 56, 3, 56, 48};
		// (the status cluster's width is laid out by Kit::status; frameRects needs only the two keys' boxes)
		(void)h;
		return true;
	}

	bool Ui::Shell::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (!page(ui.page)) return false;
		// a Manga sheet over the frame takes the finger, the rail and the header under it included
		if (ui.manga->shellPage(ui.page) && ui.manga->shellModal()) return false;
		lay::Shell s = layout(p);
		std::vector<Cell> cs = cells(p);
		lay::Rail r = lay::rail((int)cs.size(), lay::PANEL_H, railScroll);
		if (pt.kind != Pointer::Down)
		{
			// a finger that went down on the rail's sections: it scrolls them while it moves; a tap opens one
			if (railDrag)
			{
				if (pt.kind == Pointer::Move)
				{
					if (!railMoved && std::abs(y - railDownY) > 6) railMoved = true;
					if (railMoved)
					{
						railScroll = std::clamp(railScrollAt - (y - railDownY), 0, r.maxScroll);
						pressedFrame = -1;
					}
					return true;
				}
				bool moved = railMoved;
				railDrag = railMoved = false;
				if (moved)
				{
					pressedFrame = -1;
					return true;
				}
			}
			if (pressedFrame < 0) return false;
			int id = pressedFrame;
			if (pt.kind == Pointer::Up)
			{
				pressedFrame = -1;
				if (id == F_NP) npHoldAt = 0;
				bool hit = false;
				if (id >= F_RAIL)
				{
					int at = id - F_RAIL;
					hit = at >= 0 && at < (int)cs.size() ? lay::railAt(r, x, y) == at : lay::railAt(r, x, y) == (int)r.cell.size();
				}
				else if (id == F_SEARCH)
					hit = r.search.hit(x, y);
				else if (id == F_NP)
					hit = r.np.hit(x, y) && ui.nowPlayable(p);
				else if (id == F_STATUS)
					hit = statusBox.hit(x, y);
				else
				{
					ShellHead h = head(p);
					lay::Box back, more;
					frameRects(p, h, back, more);
					hit = (id == F_BACK && h.back && lay::Box{back.x - 12, 0, back.w + 24, 56}.hit(x, y)) ||
						(id == F_MORE && h.more && lay::Box{more.x - 12, 0, more.w + 24, 56}.hit(x, y));
				}
				if (hit)
				{
					if (id == F_BACK) ui.act(BACK_ICON, p);
					else if (id == F_MORE) ui.press(Button::X, p);
					else if (id == F_SEARCH) search(p);
					else if (id == F_NP) ui.goNowPlaying(p);
					else if (id == F_STATUS) ui.toggleQuick(p);
					// (the cell past the sections is Settings, pinned at the foot: not one of cs)
					else openSection(id - F_RAIL < (int)cs.size() ? cs[id - F_RAIL].section : Section::Settings, p);
				}
			}
			return true;
		}
		pressedFrame = -1;
		npHoldAt = 0;
		railDrag = railMoved = false;
		// the rail: Search at the top, a cell opens its section, Settings at the foot; the blank parts take the touch
		// (nothing of the page is under the strip). Its sections also take a drag (a tap opens one)
		if (x < lay::RAIL_W)
		{
			if (r.search.hit(x, y))
			{
				pressedFrame = F_SEARCH;
				return true;
			}
			int at = lay::railAt(r, x, y);
			if (at >= 0)
			{
				pressedFrame = F_RAIL + at;
				fprintf(stderr, "touch: shell rail %s\n", at < (int)cs.size() ? cs[at].label : "Settings");
			}
			// the Now playing key, pinned above Settings: a tap opens the player, a hold its More sheet; while
			// nothing plays its slot is empty and presses nothing
			if (r.np.hit(x, y))
			{
				if (ui.nowPlayable(p))
				{
					pressedFrame = F_NP;
					npHoldAt = SDL_GetTicks();
					fprintf(stderr, "touch: shell rail Now playing\n");
				}
				return true;
			}
			// a finger on the sections (a cell or the gap) may drag them; a lift without a move is the tap
			if (r.view.hit(x, y))
			{
				railDrag = true;
				railDownY = y;
				railScrollAt = railScroll;
			}
			return true;
		}
		// the header: back, More and the status cluster (one touch target: F opens Quick settings); nothing else
		// there is pressed
		if (y < s.header.y + s.header.h)
		{
			ShellHead h = head(p);
			lay::Box back, more;
			frameRects(p, h, back, more);
			if (h.back && lay::Box{back.x - 12, 0, back.w + 24, 56}.hit(x, y)) pressedFrame = F_BACK;
			else if (h.more && lay::Box{more.x - 12, 0, more.w + 24, 56}.hit(x, y)) pressedFrame = F_MORE;
			else if (statusBox.w > 0 && statusBox.hit(x, y)) pressedFrame = F_STATUS;
			return true;
		}
		return false;
	}

	std::string Ui::Shell::signature(const Player& p) const
	{
		if (!page(ui.page)) return "";
		std::vector<Cell> cs = cells(p);
		// Search is a tool, not a section: no cell is lit on its page, only its own key
		int at = ui.page == Page::Search ? -1 : activeIndex(cs, section(p));
		// (no cell carries a warning: opening a section starts and stops nothing; the Extensions page's row says
		// "Stops the video" and asks first)
		const Player& bp = ui.barPlayer(p);
		bool live = bp.playing() && !bp.playingPhoto();
		// the rail's Now playing key: its art (a song's arrives: artSerial; a video shows its kind mark), its
		// bars (moving while it plays) and its pause mark
		std::string np = live ? bp.artPlaceholderKey() : "";
		if (live && !bp.paused) np += "." + std::to_string(SDL_GetTicks() / 166);
		if (live) np += "|" + std::string(bp.playingAudio() ? "a" : "v") + std::to_string(ui.artSerial);
		return "|sh" + std::to_string((int)section(p)) + "." + std::to_string(at) + "." + std::to_string(pressedFrame) +
			"." + std::to_string(railScroll) + (ui.page == Page::Search ? "S" : "") + "|np" + (live ? "1" : "0") +
			(live && bp.paused ? "p" : "") + np;
	}

	// ---- the frame's drawing ----

	void Ui::Shell::drawHeader(const ShellHead& h, const Player& p)
	{
		Kit& k = *ui.kit;
		lay::Box back, more;
		frameRects(p, h, back, more);
		// the header's two ask for the quiet look: a glyph alone in Clean, the same touch area (rule 5)
		if (h.back) k.iconKey(back, G_BACK, pressedFrame == F_BACK, 3, true);
		if (h.more) k.iconKey(more, G_MORE, pressedFrame == F_MORE, 2, true);
		int cy = 27;
		Kit::Status st;
		st.clock = ui.clockText(p);
		st.battery = ui.battery.percent;
		st.charging = ui.battery.charging;
		st.playing = false;	// (the rail's Now playing key shows the bars on a shell page: none in the header, the owner)
		st.tick = (int)(SDL_GetTicks() / 166);
		if (ui.incognito) st.badges.push_back({&G_INCOGNITO, ""});
		if (ui.webIndicatorShown(p)) st.badges.push_back({&G_WEB, ""});
		int left = k.status(more.x - lay::SP8, cy, st);
		// the cluster is one touch target (F opens Quick settings): its mark, Wi-Fi, battery and clock
		statusBox = left < more.x - lay::SP8 ? lay::Box{left - 8, 0, more.x - lay::SP8 - (left - 8), 54} : lay::Box{0, 0, 0, 0};
		// the right slot (a badge, Settings' neighbours) keeps 8 before the status cluster, so the pair beside the
		// clock never touches it
		if (h.right) left = h.right(left - lay::SP8, cy);
		int x = h.back ? back.x + back.w + 10 : lay::RAIL_W + 16;
		int right = left - 14;	// the title ends before the status cluster (Search is the rail's key)
		if (right <= x + 20) return;
		if (!h.title.empty()) ui.drawTextMid(h.title, F_NORMAL, TEXT, x, cy - 7, right - x);
		if (!h.crumb.empty())
		{
			// a folder's path keeps its end, a name its start
			std::string crumb = h.crumb;
			if (right - x < (int)std::lround(ui.textWidth(crumb, F_I12) / sx)) crumb = ui.elideLeft(crumb, F_I12, right - x);
			ui.drawTextMid(crumb, F_I12, DIM, x, cy + 12, right - x);
		}
	}

	void Ui::Shell::draw(Section at, const ShellHead& h, const Player& p)
	{
		lay::Shell s = layout(p);
		Kit& k = *ui.kit;
		// the header's ground (the body is the page's; the rail covers its own 78 either way)
		ui.fill({s.main.x, 0, s.main.w, lay::SHELL_HEAD}, BG);
		std::vector<Cell> cs = cells(p);
		lay::Rail r = lay::rail((int)cs.size(), lay::PANEL_H, railScroll);
		std::vector<KitNew::RailItem> items;
		for (auto& c : cs)
		{
			KitNew::RailItem it;
			it.glyph = c.glyph;
			it.label = c.label;
			items.push_back(it);
		}
		KitNew::RailItem settings{&G_GEAR, "Settings", false};
		const bool searchLit = ui.page == Page::Search;	// Search shows: its key is filled (a tool, not a section)
		const Player& bp = ui.barPlayer(p);
		bool live = bp.playing() && !bp.playingPhoto();
		// the key's art, drawn by Ui: a song's real cover (its placeholder until the worker answers), a
		// video's kind mark — the same box the block and Home's cards show
		std::function<void(Rect)> npPicture;
		if (live) npPicture = [this, &bp](Rect r) { ui.drawNowArt(bp, r, 5); };
		KitNew::rail(k, r, items, settings, ui.page == Page::Search ? -1 : activeIndex(cs, at),
			pressedFrame >= F_RAIL ? pressedFrame - F_RAIL : pressedFrame == F_NP ? KitNew::NP_PRESSED : -1,
			searchLit, pressedFrame == F_SEARCH, live, live && bp.paused, npPicture);
		drawHeader(h, p);
		k.hints(s.hints, h.hints);
	}

	// ---- the old bodies, in the body box, until their screens are rebuilt ----

	void Ui::Shell::adopt(Page pg)
	{
		bodyPages.insert(pg);
	}

	void Ui::Shell::adoptBrowse()
	{
		browseBuilt = true;
	}

	bool Ui::Shell::adopted(Page pg) const
	{
		return bodyPages.count(pg) != 0;
	}

	bool Ui::Shell::adopted(const Player& p) const
	{
		// Manga's Library and History are drawn on the kit under its tabs (its other tabs are still bridged)
		if (ui.manga->kitBody()) return true;
		// the browser's rebuilt views draw in the body box; Network and the forms still draw the old full panel and
		// keep the bridge
		if (ui.page != Page::Browse) return adopted(ui.page);
		if (!browseBuilt) return false;
		const Browse& b = *ui.browse;
		return b.atHome() || b.browseDir == RECENT || b.filesPage() || b.browseDir == PLROOT || isPlDir(b.browseDir) ||
			b.browseDir == NETROOT || b.browseDir == FORMDIR || b.browseDir == FINDNET;
	}

	Ui::Shell::Bridge Ui::Shell::bridgeFor(const Player& p) const
	{
		Bridge b;
		lay::Shell s = layout(p);
		// the old page is 640 x 480 with its own header (0..54) and hint bar (450..480): the body is the part between,
		// scaled uniformly into the shell's body (centred, so nothing is cut). Manga's pages still bridged (a series,
		// a source's and a server's pages, Sources, Manga settings) keep their top at the body's, so their lists start
		// there; its four tabs are the kit's and never bridged
		const int sw = 640, sh = 396;
		double sc = std::min(s.body.w / (double)sw, s.body.h / (double)sh);
		b.w = std::max(1, (int)std::lround(sw * sc));
		b.h = std::max(1, (int)std::lround(sh * sc));
		b.x = s.body.x + (s.body.w - b.w) / 2;
		b.y = ui.manga->shellPage(ui.page) ? s.body.y : s.body.y + (s.body.h - b.h) / 2;
		b.on = !adopted(p);
		return b;
	}

	bool Ui::Shell::beginBody(const Player& p)
	{
		bridge = bridgeFor(p);
		if (!bridge.on) return false;
		int ow, oh;
		SDL_GetRendererOutputSize(ui.ren, &ow, &oh);
		if (ow <= 0 || oh <= 0) return false;
		if (!bodyTex || ow != bodyTexW || oh != bodyTexH)
		{
			if (bodyTex) SDL_DestroyTexture(bodyTex);
			bodyTex = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow, oh);
			bodyTexW = bodyTex ? ow : 0;
			bodyTexH = bodyTex ? oh : 0;
			static bool logged = false;
			if (!bodyTex && !logged)
			{
				logged = true;
				fprintf(stderr, "shell: no render target for a page's body (%s): drawn whole\n", SDL_GetError());
			}
		}
		if (!bodyTex) return false;
		if (SDL_SetRenderTarget(ui.ren, bodyTex) != 0)
		{
			bridge.on = false;
			return false;
		}
		// the page's ground (the old page did not fill it: renderPage cleared the window's own BG before this)
		SDL_SetRenderDrawBlendMode(ui.ren, SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(ui.ren, BG.r, BG.g, BG.b, 255);
		SDL_RenderClear(ui.ren);
		bodyTargetOn = true;
		return true;
	}

	void Ui::Shell::endBody(const Player& p)
	{
		if (!bodyTargetOn) return;
		bodyTargetOn = false;
		SDL_SetRenderTarget(ui.ren, nullptr);
		SDL_RenderSetViewport(ui.ren, nullptr);
		SDL_RenderSetClipRect(ui.ren, nullptr);
		Bridge b = bridge;
		// the source is the old page's body between its header and its hint bar, in the renderer's pixels
		SDL_Rect src{0, (int)std::lround(54 * sy), (int)std::lround(640 * sx), (int)std::lround(396 * sy)};
		SDL_Rect dst{(int)std::lround(b.x * sx), (int)std::lround(b.y * sy), (int)std::lround(b.w * sx), (int)std::lround(b.h * sy)};
		SDL_SetTextureBlendMode(bodyTex, SDL_BLENDMODE_BLEND);
		SDL_RenderCopy(ui.ren, bodyTex, &src, &dst);
		(void)p;
	}

	bool Ui::Shell::mapPoint(const Player& p, int& x, int& y, bool any) const
	{
		if (adopted(p) || !bridge.on) return false;
		Bridge b = bridgeFor(p);
		if (!any && (x < b.x || y < b.y || x >= b.x + b.w || y >= b.y + b.h)) return false;
		x = (int)std::floor((x - b.x) * 640.0 / b.w);
		y = 54 + (int)std::floor((y - b.y) * 396.0 / b.h);
		return true;
	}
}
