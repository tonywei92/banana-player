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
		// the shell's pages this step builds: the browser (Home, Files, Playlists, Network, All recent), the
		// Extensions section's list and Settings. The redesigned bodies of these replace theirs one at a time;
		// Manga's pages keep their own frame (their tabs are the top of the panel) until they are rebuilt too.
		return pg == Page::Browse || pg == Page::ExtSection || pg == Page::Settings;
	}

	std::vector<Ui::Shell::Cell> Ui::Shell::cells(const Player& p) const
	{
		// The spec's order: Home, Files, Music, Playlists, Books, Extensions, Network. Music is a later step, Books has
		// no page in the app yet (books are files in Files). Extensions is one cell for every extension that is on
		// (Manga reader is a row of its page), and it is always in the rail: opening it starts and stops nothing.
		(void)p;
		return {
			{Section::Home, &G_HOME, "Home"},
			{Section::Files, &G_FOLDER, "Files"},
			{Section::Playlists, &G_PLAYLIST, "Playlists"},
			{Section::Extensions, &G_BLOCKS, "Extensions"},
			{Section::Network, &G_NETWORK, "Network"},
		};
	}

	Ui::Shell::Section Ui::Shell::section(const Player& p) const
	{
		if (ui.page == Page::Settings) return Section::Settings;
		// Manga is a page of Extensions: its own pages and Manage extensions keep the section's cell lit
		if (ui.page == Page::ExtSection || ui.page == Page::Manga || ui.page == Page::MangaSeries || ui.page == Page::MangaRead ||
			ui.page == Page::MangaSettings || ui.page == Page::MangaSources || ui.page == Page::Extensions)
			return Section::Extensions;
		if (ui.page != Page::Browse) return Section::Home;
		const std::string& dir = ui.browse->browseDir;
		if (dir == HOMEDIR || dir == RECENT) return Section::Home;
		if (dir == PLROOT || isPlDir(dir)) return Section::Playlists;
		if (dir == NETROOT || dir == FORMDIR || dir == FINDNET || isNetPath(dir)) return Section::Network;
		return Section::Files;
	}

	lay::Shell Ui::Shell::layout(const Player& p) const
	{
		return lay::shell(ui.miniBarShown(p));
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
		if (ui.page == Page::ExtSection)
		{
			// the Extensions section (docs/redesign: ext-section, ext-section-video, ext-section-empty): a list that
			// starts nothing; A opens a row, B goes Home, L/R step the sections, L3 is Search. START is Settings, and
			// the rail's START cap says so: no hint bar repeats it
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
					"Power", "Web", "Extensions", "Updates", "About"};
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
				: dir == FINDNET ? "On your network" : baseName(dir);
			h.crumb = dir == NETROOT ? "Home" : std::string("Network");
			h.hints = {{"A", "Open"}, {"X", "More"}, {"Y", "Add"}, {"B", "Up"}, {"L/R", "Sections"}};
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
		// Search is a later step. This is its one place: L3 and the rail's Search key both come here; the page it
		// opens (or the sheet it puts over the list) replaces this note, and lights the key as that page shows.
		searchUp = true;
		ui.alert("Search", "Search is coming in a later step.");
		(void)p;
	}

	void Ui::Shell::openSection(Section s, Player& p)
	{
		if (s == Section::Music || s == Section::Books) return;	// (not in the rail yet)
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
		// another section's page is the browser's (the Extensions page sets its own, in openExtensionsSection)
		if (ui.page != Page::Browse && s != Section::Extensions) ui.page = Page::Browse;
		switch (s)
		{
		case Section::Home:
			if (!ui.browse->atHome()) ui.browse->enterFolder(HOMEDIR, p);
			break;
		case Section::Files: ui.browse->openSection(TAB_LOCAL, p); break;
		case Section::Playlists: ui.browse->openSection(TAB_PLAYLISTS, p); break;
		case Section::Network: ui.browse->openSection(TAB_NETWORK, p); break;
		// Extensions: its list, or Manga again where it was left (and never opened by the rail itself, so arriving
		// here starts and stops nothing)
		case Section::Extensions: ui.manga->openExtensionsSection(p); break;
		default: break;
		}
	}

	bool Ui::Shell::key(Button b, Player& p)
	{
		if (!page(ui.page)) return false;
		const Browse& br = *ui.browse;
		bool modal = br.playlists->pickerOpen || br.playlists->selMode != SEL_NONE || br.playlists->addPending.serial;
		// All recent's tabs keep a key: L2 / R2 (the spec moves them there, L / R being sections everywhere in the shell)
		if ((b == Button::L2 || b == Button::R2) && ui.page == Page::Browse && br.browseDir == RECENT)
		{
			if (p.settings.keepHistory && br.homeView->recentCounts[0])
				br.homeView->stepRecentTab(b == Button::R2 ? 1 : -1, p);
			return true;
		}
		// Search by the keys (L3), while browsing, on the Extensions page or on Settings: a note for now (not while a
		// form, a selection or the picker has the page)
		if (b == Button::L3 && (ui.page == Page::Settings || ui.page == Page::ExtSection || (ui.page == Page::Browse && !modal)))
		{
			search(p);
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
				bool hit = false;
				if (id >= F_RAIL)
				{
					int at = id - F_RAIL;
					hit = at >= 0 && at < (int)cs.size() ? lay::railAt(r, x, y) == at : lay::railAt(r, x, y) == (int)r.cell.size();
				}
				else if (id == F_SEARCH)
					hit = r.search.hit(x, y);
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
					// (the cell past the sections is Settings, pinned at the foot: not one of cs)
					else openSection(id - F_RAIL < (int)cs.size() ? cs[id - F_RAIL].section : Section::Settings, p);
				}
			}
			return true;
		}
		pressedFrame = -1;
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
			// a finger on the sections (a cell or the gap) may drag them; a lift without a move is the tap
			if (r.view.hit(x, y))
			{
				railDrag = true;
				railDownY = y;
				railScrollAt = railScroll;
			}
			return true;
		}
		// the header: back and More; nothing else there is pressed
		if (y < s.header.y + s.header.h)
		{
			ShellHead h = head(p);
			lay::Box back, more;
			frameRects(p, h, back, more);
			if (h.back && lay::Box{back.x - 12, 0, back.w + 24, 56}.hit(x, y)) pressedFrame = F_BACK;
			else if (h.more && lay::Box{more.x - 12, 0, more.w + 24, 56}.hit(x, y)) pressedFrame = F_MORE;
			return true;
		}
		return false;
	}

	std::string Ui::Shell::signature(const Player& p) const
	{
		if (!page(ui.page)) return "";
		std::vector<Cell> cs = cells(p);
		int at = activeIndex(cs, section(p));
		// (no cell carries a warning: opening a section starts and stops nothing; the Extensions page's row says
		// "Stops the video" and asks first)
		// the note Search puts up is all it shows today: the key is lit while it is
		if (searchUp && !ui.findOverlay("alert")) searchUp = false;
		return "|sh" + std::to_string((int)section(p)) + "." + std::to_string(at) + "." + std::to_string(pressedFrame) +
			"." + std::to_string(railScroll) + (searchUp ? "S" : "") + std::to_string(ui.miniBarShown(p));
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
		st.playing = ui.markW(p) && ui.markLive(p);
		st.tick = (int)(SDL_GetTicks() / 166);
		if (ui.incognito) st.badges.push_back({&G_INCOGNITO, ""});
		if (ui.webIndicatorShown(p)) st.badges.push_back({&G_WEB, ""});
		int left = k.status(more.x - lay::SP8, cy, st);
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

	void Ui::Shell::drawMini(const lay::Shell& s, const Player& p)
	{
		if (s.mini.w <= 0 || !ui.miniBarShown(p)) return;
		const Player& bp = ui.barPlayer(p);
		bool live = bp.playing() && !bp.playingPhoto();
		double pos = live ? ui.shownPosition(bp) : 0, dur = live ? bp.duration : 0;
		lay::MiniBar l = lay::miniBar(s.mini);
		Kit::Mini m;
		m.kind = live && bp.playingAudio() ? &G_NOTE : &G_FILM;
		m.title = live ? (!bp.title.empty() ? bp.title : entryTitle(bp.currentPath())) : "";
		m.time = live ? formatTime(pos) + " / " + formatTime(dur) : "stopped at " + formatTime(pos);
		m.paused = live && bp.paused;
		m.err = live && !bp.error.empty();
		m.pressed = ui.pressedBtn == MINI_BAR;
		m.markPressed = ui.pressedBtn == MINI_PP;
		m.progress = dur > 0 && pos >= 0 ? std::clamp(pos / dur, 0.0, 1.0) : 0;
		// the same state pills Home's Now playing card shows (only what is away from normal); with them the bar
		// drops the words "Now playing"
		if (live)
			for (auto& s : ui.kit->statePills(bp)) m.pills.push_back({s, Kit::Pill::Live});
		if (ui.playHoldMini && ui.playHoldAt)
			m.hold = std::min(1.0, (SDL_GetTicks() - ui.playHoldAt) / (double)PLAY_HOLD_MS);
		m.key = bp.settings.buttonHotkeys && ui.selectToPlayer() ? "SELECT" : "";
		ui.kit->miniBar(l, m);
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
		if (searchUp && !ui.findOverlay("alert")) searchUp = false;
		KitNew::rail(k, r, items, settings, activeIndex(cs, at), pressedFrame >= F_RAIL ? pressedFrame - F_RAIL : -1,
			searchUp, pressedFrame == F_SEARCH);
		drawHeader(h, p);
		drawMini(s, p);
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
		// the browser's rebuilt views draw in the body box; Network and the forms still draw the old full panel and
		// keep the bridge
		if (ui.page != Page::Browse) return adopted(ui.page);
		if (!browseBuilt) return false;
		const Browse& b = *ui.browse;
		return b.atHome() || b.browseDir == RECENT || b.filesPage() || b.browseDir == PLROOT || isPlDir(b.browseDir);
	}

	Ui::Shell::Bridge Ui::Shell::bridgeFor(const Player& p) const
	{
		Bridge b;
		lay::Shell s = layout(p);
		// the old page is 640 x 480 with its own header (0..54) and hint bar (450..480): the body is the part between,
		// scaled uniformly into the shell's body (centred, so nothing is cut)
		const int sw = 640, sh = 396;
		double sc = std::min(s.body.w / (double)sw, s.body.h / (double)sh);
		b.w = std::max(1, (int)std::lround(sw * sc));
		b.h = std::max(1, (int)std::lround(sh * sc));
		b.x = s.body.x + (s.body.w - b.w) / 2;
		b.y = s.body.y + (s.body.h - b.h) / 2;
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

	bool Ui::Shell::mapPoint(const Player& p, int& x, int& y) const
	{
		if (adopted(p) || !bridge.on) return false;
		Bridge b = bridgeFor(p);
		if (x < b.x || y < b.y || x >= b.x + b.w || y >= b.y + b.h) return false;
		x = (x - b.x) * 640 / b.w;
		y = 54 + (y - b.y) * 396 / b.h;
		return true;
	}

	Ui::Rect Ui::miniBox(const Player& p) const
	{
		if (shell && shell->page(page)) return shell->layout(p).mini;
		return MINI_BOX;
	}

	int Ui::miniMarkRight(const Player& p) const
	{
		if (shell && shell->page(page))
		{
			lay::MiniBar m = lay::miniBar(shell->layout(p).mini);
			return m.mark.x + m.mark.w;
		}
		return MINI_BOX.x + 44;
	}
}
