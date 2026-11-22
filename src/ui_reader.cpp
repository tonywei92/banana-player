// The top panel's page reader, the bottom panel's side (ui_reader.h): the seek and the zoom, Try again, fullscreen's
// pages, the pages grid and the dragged lists, shared by Files' PDF/comic viewer (Ui::Docs) and the Manga reader.
#include "ui_reader.h"
#include "ui_kit.h"
#include "ui_smoothlist.h"
#include "ui_internal.h"
#include "ui_manga.h"
#include "ui_docs.h"
#include "textreader.h"
#include "manga_ui_logic.h"
#include "pagebook.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	namespace
	{
		// the photo viewer's steps (FEATURES.md, "Looking at a photo"): 1.0 is the fit, and the reader draws its pages
		// from the same geometry, so Up/Down and the View sheet's Zoom row walk the same ladder
		// small steps where reading zooms most (just past the fit), wider ones further in
		constexpr int MANGA_ZOOMS[] = {100, 110, 125, 140, 160, 180, 200, 225, 250, 300, 350, 400, 500, 600, 700, 800};
		constexpr int MANGA_ZOOM_STEPS = (int)(sizeof MANGA_ZOOMS / sizeof MANGA_ZOOMS[0]);
	}

	bool Ui::PageReader::takeMangaRead(MangaReadRequest& out)
	{
		if (mangaRead.path.empty()) return false;
		out = std::move(mangaRead);
		mangaRead = {};
		out.book = ui.manga->mangaBookFor(out.seriesId);
		out.within = ui.manga->mangaWithinFor(out.chapterId, out.page);
		return true;
	}

	void Ui::PageReader::mangaSeek(int page)
	{
		int pages = readerStatus.pages;
		if (pages <= 0) return;
		page = std::clamp(page, 0, pages - 1);
		if (page == mangaSliderPage) return;
		mangaSliderPage = page;
		readerCmds.push_back({ReaderCmd::Seek, (double)page, (int64_t)SDL_GetTicks()});
	}

	void Ui::PageReader::mangaZoomTo(double z)
	{
		mangaZoomAsked = (int)(z * 100 + 0.5);
		readerCmds.push_back({ReaderCmd::Zoom, z, (int64_t)SDL_GetTicks()});
	}

	void Ui::PageReader::mangaZoomStep(int dir)
	{
		// the nearest step at or above what the keys last asked (the reader's own status lags a pass behind);
		// one further up or down from there, ends held
		int pct = mangaZoomAsked > 0 ? mangaZoomAsked : 100;
		int i = 0;
		while (i < MANGA_ZOOM_STEPS - 1 && MANGA_ZOOMS[i] < pct) i++;
		i = std::clamp(i + (dir > 0 ? 1 : -1), 0, MANGA_ZOOM_STEPS - 1);
		mangaZoomTo(MANGA_ZOOMS[i] / 100.0);
	}

	void Ui::PageReader::mangaZoomToggle()
	{
		// as the photo viewer's A: twice the fit, or back to it from any zoom
		mangaZoomTo(mangaZoomAsked >= 200 ? 1.0 : 2.0);
	}

	void Ui::PageReader::drawMangaRetry(bool full)
	{
		Rect r = mangaRetryRect(full);
		Kit::Key key;
		key.label = "Try again";
		key.key = "A";
		key.tone = Kit::Tone::Primary;
		ui.kit->key(r, key);
		ui.drawTextMid("A page didn\xE2\x80\x99t load", F_I12, full ? TEXT : DIM, W / 2, r.y + r.h + 18, 0, 2);
	}

	void Ui::PageReader::mfFree()
	{
		for (auto& t : mfTex)
			for (SDL_Texture* x : t.second.tiles) SDL_DestroyTexture(x);
		mfTex.clear();
		mfWaiting = 0;
	}

	void Ui::PageReader::drawMangaFull(const Player& p)
	{
		// the pages under the hinge: main gives each page's box (fractions of the panel) and its pixels; they become
		// this renderer's textures, one page a frame at most so a turn never stalls the panel
		Uint32 now = SDL_GetTicks();
		if (ui.manga->mangaCtrlUntil && now >= ui.manga->mangaCtrlUntil) ui.manga->mangaCtrlUntil = 0;	// the timer's end: the controls go
		if (ui.manga->mangaToastUntil && now >= ui.manga->mangaToastUntil) ui.manga->mangaToastUntil = ui.manga->mangaBookToast = 0;
		if (readerBottom.gen != mfGen) { mfFree(); mfGen = readerBottom.gen; }
		// a new page: its number shows a moment
		if (readerStatus.page != mfLastPage) { mfLastPage = readerStatus.page; mfTagUntil = now + 1500; }
		SDL_SetRenderDrawColor(ui.ren, 0, 0, 0, 255);
		SDL_RenderClear(ui.ren);
		int pw = (int)std::lround(W * sx), ph = (int)std::lround(H * sy);
		bool made = false;
		int waiting = 0;
		for (auto& b : readerBottom.boxes)
		{
			SDL_Rect r{(int)std::lround(b.x * pw), (int)std::lround(b.y * ph), (int)std::lround(b.w * pw), (int)std::lround(b.h * ph)};
			auto it = mfTex.find(b.page);
			// the page's pixels changed (turned by L3 or Book, decoded again wider): made again from the new ones,
			// the old texture drawn until then
			if (it != mfTex.end() && !made)
				for (auto& px : readerBottom.pages)
					if (px.page == b.page && px.rgba && px.rgba.get() != it->second.src)
					{
						for (SDL_Texture* x : it->second.tiles) SDL_DestroyTexture(x);
						mfTex.erase(it);
						it = mfTex.end();
						break;
					}
			if (it == mfTex.end() && !made)
			{
				for (auto& px : readerBottom.pages)
				{
					if (px.page != b.page || !px.rgba || px.w <= 0 || px.h <= 0) continue;
					FullTex ft;
					ft.w = px.w;
					ft.h = px.h;
					ft.src = px.rgba.get();
					for (int y = 0; y < px.h; y += 2048)
					{
						int th = std::min(2048, px.h - y);
						SDL_Texture* t = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, px.w, th);
						if (!t) break;
						// filtered like the top panel's pages (GL_LINEAR): SDL's default, nearest, drops whole rows
						// and columns when a page is shrunk to the panel, and its lines and letters go jagged (#6)
						SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
						SDL_UpdateTexture(t, nullptr, px.rgba->data() + (size_t)y * px.w * 4, px.w * 4);
						ft.tiles.push_back(t);
					}
					it = mfTex.emplace(b.page, std::move(ft)).first;
					made = true;
					break;
				}
			}
			if (it == mfTex.end())
			{
				// not decoded yet, or its turn to be made comes next frame: its place, dark in every theme (TOP_TRACK)
				SDL_SetRenderDrawColor(ui.ren, theme::TOP_TRACK.r, theme::TOP_TRACK.g, theme::TOP_TRACK.b, 255);
				SDL_RenderFillRect(ui.ren, &r);
				waiting++;
				continue;
			}
			FullTex& ft = it->second;
			ft.used = now;
			// the reading filter and Extra dim, as the top panel's pages have them
			float fr, fg, fb;
			docFilterTint(readFilterNow, fr, fg, fb, readDimNow);
			for (SDL_Texture* tt : ft.tiles) SDL_SetTextureColorMod(tt, (Uint8)(fr * 255), (Uint8)(fg * 255), (Uint8)(fb * 255));
			for (size_t k = 0; k < ft.tiles.size(); k++)
			{
				int y0 = (int)k * 2048, th = std::min(2048, ft.h - y0);
				SDL_Rect d{r.x, r.y + (int)std::lround((double)r.h * y0 / ft.h), r.w, (int)std::lround((double)r.h * th / ft.h) + 1};
				if (d.y + d.h < 0 || d.y > ph) continue;
				SDL_RenderCopy(ui.ren, ft.tiles[k], nullptr, &d);
			}
		}
		mfWaiting = waiting;
		// at most 8 pages kept here: the ones not drawn longest go
		// (Book keeps 4: a turn's slide shows the screen left and the new one together, two whole pages each)
		while (mfTex.size() > (readerStatus.book ? 4u : 2u))	// (each a page's textures on the GPU, a long page's tens of MB: the memory is the system's)
		{
			auto old = mfTex.begin();
			for (auto i = mfTex.begin(); i != mfTex.end(); ++i)
				if (i->second.used < old->second.used) old = i;
			for (SDL_Texture* x : old->second.tiles) SDL_DestroyTexture(x);
			mfTex.erase(old);
		}
		// Book: the page numbers turned as the pages are, at the foot of the spread as held (the panel's left edge
		// held top-left, its right edge held top-right); at the chapter's end they stay, with what comes next
		if (readerStatus.book && readerStatus.pages > 0)
		{
			if (now < mfTagUntil || readerStatus.atEnd) ui.manga->drawMangaBookTag();
		}
		// a thin page tag for a moment after a change
		else if (now < mfTagUntil && readerStatus.pages > 0)
		{
			// (the chapter first, quiet as the numbers: "Ch. 1 · 12 / 30"; a comic from Files has none)
			std::string ch = ui.page == Page::MangaRead ? ui.manga->mangaReadChapterTag() : "";
			std::string tag = (ch.empty() ? "" : ch + " \xC2\xB7 ") + std::to_string(readerStatus.page + 1) + " / " + std::to_string(readerStatus.pages);
			ui.kit->pill(W - 12, H - 23, tag, Kit::Pill::Fact);
		}
		if (readerStatus.failedHere) drawMangaRetry(true);
		// the controls, while they show (over the tag), then the first open's toast over both
		if (now < ui.manga->mangaCtrlUntil) ui.manga->drawMangaControls(p);
		if (now < ui.manga->mangaToastUntil) ui.manga->drawMangaToast();
	}

	Ui::Rect Ui::PageReader::fullNavKey(int id, bool chapters)
	{
		lay::ReaderFullNav l = lay::readerFullNav(chapters);
		if (id >= FN_CH_BACK && id <= FN_CH_ON) return l.step[id - FN_CH_BACK];
		if (id >= FN_PAGES && id <= FN_LEAVE) return l.row[id - FN_PAGES];
		return {0, 0, 0, 0};
	}

	int Ui::PageReader::fullNavKeyAt(int x, int y, bool chapters)
	{
		for (int id = 0; id < FN_KEYS; id++)
			if (fullNavKey(id, chapters).hit(x, y)) return id;
		return -1;
	}

	void Ui::PageReader::drawFullNav(const FullNav& n)
	{
		lay::ReaderFullNav l = lay::readerFullNav(n.chapters);
		Kit& k = *ui.kit;
		// the ground lets the page show through (the keys on it are solid: they are read and pressed)
		SDL_Color ground = BG;
		ground.a = 224;
		ui.fillRound(l.sheet, ground, R_DIALOG, ROUND_TL | ROUND_TR);
		bool held = ui.slideHeld(n.slider);
		int pages = readerStatus.pages, at = held ? (int)ui.slide.value : readerStatus.page;
		ui.drawTextMid(n.title, F_P16, TEXT, l.title.x, l.title.y + l.title.h / 2, l.title.w);
		if (pages > 0)
			ui.drawTextMid((n.chapter.empty() ? "" : n.chapter + " \xC2\xB7 ") + "Page " + std::to_string(at + 1) + " of " + std::to_string(pages), F_SMALL, DIM, l.page.x + l.page.w, l.page.y + l.page.h / 2, l.page.w, 1);
		k.progress(l.track, ui.slideShare(n.slider, 0, pages > 1 ? at / (double)(pages - 1) : 0), {}, held, held ? "Page " + std::to_string(at + 1) : "");
		static const Glyph* const GLYPHS[FN_KEYS] = {&G_PREV, &G_LEFT, &G_RIGHT, &G_NEXT, &G_BLOCKS, &G_EYE, &G_FULL};
		static const char* const LABELS[FN_KEYS] = {"Chapter", "Page", "Page", "Chapter", "Pages", "View", "One screen"};
		static const char* const CAPS[FN_KEYS] = {"L2", "L", "R", "R2", "Y", "START", "SELECT"};
		for (int id = 0; id < FN_KEYS; id++)
		{
			Rect r = fullNavKey(id, n.chapters);
			if (r.w <= 0) continue;
			Kit::Key key;
			key.glyph = GLYPHS[id];
			key.label = LABELS[id];
			key.key = n.caps ? CAPS[id] : "";
			key.off = n.off[id];
			key.pressed = n.pressed == id;
			k.key(r, key);
		}
	}

	void Ui::PageReader::drawFullToast(const std::string& text)
	{
		int tw = (int)std::lround(ui.textWidth(text, 0) / sx) + 32;
		ui.fillRound({W / 2 - tw / 2, H / 2 - 22, tw, 44}, SURFACE, R_BUTTON);
		ui.drawTextMid(text, 0, TEXT, W / 2, H / 2, 0, 2);
	}

	std::vector<int> Ui::PageReader::marksNow() const
	{
		return ui.page == Page::MangaRead ? ui.manga->mangaMarksHere() : ui.docs->pagesMarksHere();
	}

	std::vector<std::string> Ui::PageReader::mangaPagesTabLabels() const
	{
		return {"Pages " + std::to_string(readerStatus.pages), "Bookmarks " + std::to_string(pageMarks.size())};
	}

	void Ui::PageReader::mpCentre()
	{
		int row = mangaPagesCursor / MPG_COLS;
		int rows = (mpCount() + MPG_COLS - 1) / MPG_COLS;
		Rect view = mangaPagesView();
		int shown = view.h / MPG_ROW_H;
		mangaPagesScroll = std::clamp(row - shown / 2, 0, std::max(0, rows - shown));
		pagesList.sc.setExtent((double)rows * MPG_ROW_H, view.h);
		pagesList.sc.jump((double)mangaPagesScroll * MPG_ROW_H);
		pagesMoving = false;
		mangaPagesTouch = false;
		mangaPagesPressed = -1;
	}

	void Ui::PageReader::mangaPagesOpen(Player& p)
	{
		(void)p;
		if (!readerStatus.open || readerStatus.pages <= 0) return;
		mangaPages = true;
		mangaPagesTab = 0;
		pageMarks = marksNow();
		mangaPagesCursor = mangaPagesHere = std::clamp(readerStatus.page, 0, readerStatus.pages - 1);
		mpCentre();
		mpMade = 0;
	}

	void Ui::PageReader::mangaPagesTabSet(int tab)
	{
		tab = tab ? 1 : 0;
		if (tab == mangaPagesTab) return;
		// the cursor stays on its page: on Bookmarks, the first one at or after it (the last, past them all)
		int page = mpPage(mangaPagesCursor);
		mangaPagesTab = tab;
		if (!tab) mangaPagesCursor = std::clamp(page, 0, std::max(0, readerStatus.pages - 1));
		else
		{
			int at = (int)(std::lower_bound(pageMarks.begin(), pageMarks.end(), page) - pageMarks.begin());
			mangaPagesCursor = std::clamp(at, 0, std::max(0, (int)pageMarks.size() - 1));
		}
		mpCentre();
		if (mpCount() > 0 && mpPage(mangaPagesCursor) != page) mangaSeek(mpPage(mangaPagesCursor));
	}

	void Ui::PageReader::mangaPagesClose(Player& p)
	{
		(void)p;
		mangaPages = false;
		mangaPagesTouch = false;
		mangaPagesPressed = -1;
		mpAsk(-1, -1);
	}

	void Ui::PageReader::mpAsk(int from, int to)
	{
		if (from == mpAskFrom && to == mpAskTo) return;
		mpAskFrom = from;
		mpAskTo = to;
		readerCmds.push_back({ReaderCmd::Thumbs, (double)from, (int64_t)SDL_GetTicks(), (double)to});
	}

	void Ui::PageReader::mpFree()
	{
		for (auto& t : mpTex)
			if (t.second.tex) SDL_DestroyTexture(t.second.tex);
		mpTex.clear();
		mpMade = 0;
	}

	bool Ui::PageReader::mangaPagesKey(Button b, Player& p)
	{
		if (readerStatus.pages <= 0) { mangaPagesClose(p); return false; }
		// L / R: the other tab
		if (b == Button::L || b == Button::R) { mangaPagesTabSet(!mangaPagesTab); return false; }
		// (slots from here: every page, or the bookmarks alone; none bookmarked leaves only the way out)
		int pages = mpCount();
		if (pages <= 0)
		{
			if (b == Button::B || b == Button::Y) mangaPagesClose(p);
			return false;
		}
		int cur = mangaPagesCursor;
		int row = cur / MPG_COLS, col = cur % MPG_COLS;
		int rows = (pages + MPG_COLS - 1) / MPG_COLS;
		const int SCREEN_ROWS = std::max(1, mangaPagesView().h / MPG_ROW_H);
		switch (b)
		{
		case Button::Left: if (col > 0) cur--; break;
		case Button::Right: if (col + 1 < MPG_COLS && cur + 1 < pages) cur++; break;
		case Button::Up: if (row > 0) cur -= MPG_COLS; break;
		case Button::Down: if (row + 1 < rows) cur = std::min(cur + MPG_COLS, pages - 1); break;
		// a screenful of rows back or on, the column kept
		case Button::L2: cur = std::max(col, cur - SCREEN_ROWS * MPG_COLS); break;
		case Button::R2: if (row + 1 < rows) cur = std::min(cur + SCREEN_ROWS * MPG_COLS, pages - 1); break;
		case Button::A:
			// jump to the page under the cursor and close the grid
			mangaSeek(mpPage(cur));
			mangaPagesClose(p);
			return false;
		case Button::B:
		case Button::Y:
			mangaPagesClose(p);
			return false;
		default: break;
		}
		if (cur != mangaPagesCursor)
		{
			mangaPagesCursor = cur;
			// the top panel follows the cursor: a Seek, as the slider sends
			mangaSeek(mpPage(cur));
			// keep the cursor's row in view (a short glide)
			pagesList.reveal(cur / MPG_COLS, mangaPagesView(), MPG_ROW_H, rows);
		}
		return false;
	}

	bool Ui::PageReader::mangaPagesPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (readerStatus.pages <= 0) { mangaPagesClose(p); return false; }
		int pages = mpCount();
		// the thumbnail under a point, -1 for none (in the grid's view, at its offset now)
		auto thumbAt = [&](int px, int py) {
			int r = pagesList.rowAt(px, py);
			if (r < 0) return -1;
			for (int i = r * MPG_COLS; i < pages && i < (r + 1) * MPG_COLS; i++)
				if (mangaPageCell(i).cell.hit(px, py)) return i;
			return -1;
		};
		const float fy = pt.y * H;	// (by the fraction: the grid follows the panel's pixels)
		if (pt.kind == Pointer::Down)
		{
			mangaPagesTouch = false;
			mangaPagesMoved = false;
			mangaPagesPressed = -1;
			// above the sheet: it closes, as B (the touch goes no further)
			if (!lay::pagesSheet().sheet.box.hit(x, y))
			{
				mangaPagesClose(p);
				ui.swallowTouch = true;
				return false;
			}
			// a tab: the other list
			{
				auto tabs = ui.kit->tabRects(lay::pagesTabs(lay::pagesSheet()), mangaPagesTabLabels());
				for (int i = 0; i < (int)tabs.size(); i++)
					if (tabs[i].hit(x, y)) { mangaPagesTabSet(i); return false; }
			}
			// anywhere on the grid, a thumbnail too: a drag or a tap, told apart when the finger lifts (a thumbnail
			// opening on the touch left nothing to start a drag from: the grid is all thumbnails)
			if (mangaPagesView().hit(x, y))
			{
				mangaPagesTouch = true;
				mangaPagesCaught = pagesList.press(fy);
				mangaPagesPressed = mangaPagesCaught ? -1 : thumbAt(x, y);
			}
			return false;
		}
		if (pt.kind == Pointer::Move && mangaPagesTouch)
		{
			// past the scroller's slop it's a drag: the grid follows the finger by the pixel (the cursor is put
			// among the rows shown when it comes to rest: drawMangaPages)
			if (pagesList.move(fy))
			{
				mangaPagesMoved = true;
				mangaPagesPressed = -1;
			}
			return false;
		}
		if (pt.kind == Pointer::Up && mangaPagesTouch)
		{
			mangaPagesTouch = false;
			pagesList.lift();
			// a tap (no drag) that lifts on the thumbnail it touched: that page, and the grid closes
			int i = mangaPagesPressed;
			mangaPagesPressed = -1;
			if (!mangaPagesMoved && !mangaPagesCaught && i >= 0 && thumbAt(x, y) == i)
			{
				mangaSeek(mpPage(i));
				mangaPagesClose(p);
			}
			return false;
		}
		return false;
	}

	SDL_Texture* Ui::PageReader::mpThumb(int i)
	{
		auto it = mpTex.find(i);
		if (it == mpTex.end() && mpMade < 1)
		{
			// made from the RGBA main handed us, one a frame so a turn never stalls the panel
			for (auto& th : readerThumbs.pages)
			{
				if (th.page != i || !th.rgba || th.w <= 0 || th.h <= 0) continue;
				SDL_Texture* t = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, th.w, th.h);
				if (t)
				{
					SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);	// scaled to its tile: smooth, not jagged
					SDL_UpdateTexture(t, nullptr, th.rgba->data(), th.w * 4);
					SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
				}
				mpTex[i] = {t, th.w, th.h, SDL_GetTicks()};
				it = mpTex.find(i);
				mpMade++;
				break;
			}
		}
		if (it == mpTex.end()) return nullptr;
		it->second.used = SDL_GetTicks();	// (asked by a row's key: drawn now, whether its slot is drawn again or not)
		return it->second.tex;
	}

	void Ui::PageReader::drawMangaPages(const Player& p)
	{
		if (readerStatus.pages <= 0) { mangaPagesClose(const_cast<Player&>(p)); return; }
		int pages = mpCount();	// (slots: every page, or the bookmarks alone)
		// the chapter changed: the old textures go
		if (readerThumbs.gen != mpGen) { mpFree(); mpGen = readerThumbs.gen; mpAskFrom = mpAskTo = -1; }
		// the sheet over the reader: its two tabs with their counts, where the reader was at its right
		Kit& k = *ui.kit;
		lay::PagesSheet ps = lay::pagesSheet();
		k.sheetBack(ps.sheet.box, ps.sheet.grab);
		k.tabs(lay::pagesTabs(ps), mangaPagesTabLabels(), mangaPagesTab);
		{
			Rect hd = ps.sheet.head;
			ui.drawTextMid("You are on page " + std::to_string(mangaPagesHere + 1), F_I12, DIM, hd.x + hd.w, hd.y + hd.h / 2, 200, 1);
		}
		if (pages <= 0)
		{
			// Bookmarks with none: where one is made
			Rect v = ps.view;
			ui.drawGlyphMid(G_BOOKMARK, v.x + v.w / 2, v.y + v.h / 2 - 34, DIM, 2);
			ui.drawTextMid("No bookmarks in this one yet", F_P16, TEXT, v.x + v.w / 2, v.y + v.h / 2 + 4, v.w, 2);
			ui.drawTextMid("While reading: X \xC2\xB7 More, then Bookmark this page", F_I12, DIM, v.x + v.w / 2, v.y + v.h / 2 + 30, v.w, 2);
			k.hints({{"L/R", "Tab"}, {"Y/B", "Close"}});
			return;
		}
		// the grid: thumbnails with numbered places for pages not decoded yet, a row of them a row of a smooth
		// list. At most one texture made a frame (mpThumb), so a turn never stalls the panel
		mpMade = 0;
		int rows = (pages + MPG_COLS - 1) / MPG_COLS;
		Rect view = ps.view;
		int shown = view.h / MPG_ROW_H;
		pagesList.sc.setExtent((double)rows * MPG_ROW_H, view.h);
		// a motion over (a fling, a drag let go): the cursor among the whole rows shown, and the top panel there
		bool moving = pagesList.moving() || pagesList.dragging();
		if (pagesMoving && !moving)
		{
			double off = pagesList.sc.offset();
			int first = std::clamp((int)std::ceil((off - 2) / MPG_ROW_H), 0, rows - 1);
			int last = std::clamp((int)std::floor((off + view.h + 2) / MPG_ROW_H) - 1, first, rows - 1);
			int row = std::clamp(mangaPagesCursor / MPG_COLS, first, last);
			if (row != mangaPagesCursor / MPG_COLS)
			{
				mangaPagesCursor = std::clamp(row * MPG_COLS + mangaPagesCursor % MPG_COLS, 0, pages - 1);
				mangaSeek(mpPage(mangaPagesCursor));
			}
		}
		pagesMoving = moving;
		{
			// the reader makes thumbnails only of pages it decoded: the rows in view, and one each side, are asked
			// for here, so a page the view was never near has its picture too
			double off = pagesList.sc.offset();
			int first = std::max(0, (int)std::floor(off / MPG_ROW_H) - 1);
			int last = std::min(rows - 1, (int)std::floor((off + view.h) / MPG_ROW_H) + 1);
			mpAsk(mpPage(first * MPG_COLS), mpPage(std::min(pages - 1, (last + 1) * MPG_COLS - 1)));
		}
		SmoothList::Spec spec;
		spec.view = view;
		spec.rowH = MPG_ROW_H;
		spec.count = rows;
		spec.key = [this, pages, cursor = mangaPagesCursor, here = mangaPagesHere](int r) {
			std::string key = mangaPagesTab ? "b" : "p";
			for (int i = r * MPG_COLS; i < pages && i < (r + 1) * MPG_COLS; i++)
			{
				int page = mpPage(i);
				key += mpThumb(page) ? (i == cursor ? "T" : "t") : (i == cursor ? "N" : "n");
				key += std::to_string(page) + (page == here ? "h" : "") + (std::binary_search(pageMarks.begin(), pageMarks.end(), page) ? "m" : "");
			}
			return key;
		};
		spec.draw = [this, pages, cursor = mangaPagesCursor, here = mangaPagesHere](int r, Rect box) {
			Kit& k = *ui.kit;
			ui.fill(box, SURFACE);	// (the slot is the sheet's ground)
			for (int i = r * MPG_COLS; i < pages && i < (r + 1) * MPG_COLS; i++)
			{
				lay::PagesCell c = lay::pagesCell(box, i % MPG_COLS);
				bool at = i == cursor;
				int page = mpPage(i);
				// the page's place: the thumbnail fitted inside it (aspect kept), or its number while it isn't decoded
				Rect t = c.thumb;
				ui.fillRound(t, BUTTON, theme::R_GRIP);
				auto it = mpTex.find(page);
				if (it != mpTex.end() && it->second.tex && it->second.w > 0 && it->second.h > 0)
				{
					double aspect = (double)it->second.w / it->second.h;
					int dw = t.w, dh = t.h;
					if (aspect > (double)t.w / t.h) dh = (int)std::lround(t.w / aspect);
					else dw = (int)std::lround(t.h * aspect);
					SDL_Rect dst{(int)std::lround((t.x + (t.w - dw) / 2) * sx), (int)std::lround((t.y + (t.h - dh) / 2) * sy),
						(int)std::lround(dw * sx), (int)std::lround(dh * sy)};
					SDL_RenderCopy(ui.ren, it->second.tex, nullptr, &dst);
				}
				// the cursor: the accent outline 2 clear of the picture
				if (at) k.ring({t.x - 4, t.y - 4, t.w + 8, t.h + 8}, theme::R_GRIP + 2, ACCENT);
				if (page == here) k.pill(t.x + 3 + k.pillW("Here"), t.y + 13, "Here", Kit::Pill::Fact);
				// a bookmarked page: the mark at its top right corner
				if (std::binary_search(pageMarks.begin(), pageMarks.end(), page)) ui.drawGlyphMid(G_BOOKMARK, t.x + t.w - 11, t.y + 9, ACCENT);
				ui.drawTextMid(std::to_string(page + 1), F_I12, at ? ACCENT : DIM, c.number.x + c.number.w / 2, c.number.y + c.number.h / 2, 0, 2);
			}
		};
		pagesList.draw(std::move(spec));
		mangaPagesScroll = std::clamp((int)std::ceil((pagesList.sc.offset() - 2) / MPG_ROW_H), 0, std::max(0, rows - shown));
		// at most 60 thumbnails kept: the ones not drawn longest go (a 200-page chapter keeps 60 at a time)
		while (mpTex.size() > 60)
		{
			auto old = mpTex.begin();
			for (auto i = mpTex.begin(); i != mpTex.end(); ++i)
				if (i->second.used < old->second.used) old = i;
			if (old->second.tex) SDL_DestroyTexture(old->second.tex);
			mpTex.erase(old);
		}
		k.hints({{"D-PAD", "Move"}, {"A", "Go"}, {"L2/R2", "A screenful"}, {"L/R", "Tab"}, {"Y/B", "Close"}});
	}

	void Ui::PageReader::FollowList::follow(Rect view, int rowH, int n, int cols, int& scroll, int* cursor)
	{
		SmoothList& l = list;
		cols = std::max(1, cols);
		rowH = std::max(1, rowH);
		const int rows = (std::max(0, n) + cols - 1) / cols;
		l.sc.setExtent((double)rows * rowH, view.h);
		const int crow = cursor && *cursor >= 0 && *cursor < n ? *cursor / cols : -1;
		const bool fresh = l.takeFresh();
		const bool cursorMoved = cursor && *cursor != cursorWas;
		if (scroll != synced)
		{
			// the page set its scroll: its keys keeping the cursor in sight (a glide there), or a place of its own (a
			// list opened, one put back where it was: there at once)
			if (crow >= 0 && cursorMoved && synced >= 0 && !(scroll == 0 && crow == 0)) l.reveal(crow, view, rowH, rows, !fresh);
			else l.sc.jump((double)scroll * rowH);
		}
		else if (crow >= 0 && (cursorMoved || fresh)) l.reveal(crow, view, rowH, rows, cursorMoved && !fresh);
		// a motion over (a fling, a drag let go): the cursor among the whole rows shown, its column kept
		const bool moving = l.moving() || l.dragging();
		if (wasMoving && !moving && crow >= 0 && rows > 0)
		{
			const double off = l.sc.offset();
			int first = std::clamp((int)std::ceil((off - 2) / rowH), 0, rows - 1);
			int last = std::clamp((int)std::floor((off + view.h + 2) / rowH) - 1, first, rows - 1);
			int r = std::clamp(crow, first, last);
			if (r != crow) *cursor = std::min(n - 1, r * cols + *cursor % cols);
		}
		wasMoving = moving;
		if (cursor) cursorWas = *cursor;
	}

	bool Ui::PageReader::FollowList::sync(int& scroll)
	{
		const SmoothList::Spec& sp = list.spec();
		const int rowH = std::max(1, sp.rowH);
		const int fit = std::max(1, sp.view.h / rowH);
		int first = (int)std::ceil((list.sc.offset() - 2) / rowH);
		first = std::clamp(first, 0, std::max(0, sp.count - fit));
		bool changed = first != synced && synced >= 0;
		scroll = synced = first;
		return changed;
	}

	void Ui::PageReader::mangaListSmooth(SmoothList& list, int x, int y, int item, std::function<void(int)> tap)
	{
		mlist = MangaListDrag();
		mlist.on = true;
		mlist.downX = x;
		mlist.downY = y;
		mlist.smooth = &list;
		mlist.caught = list.press((float)y);
		mlist.item = mlist.caught ? -1 : item;
		mlist.tap = std::move(tap);
	}

	bool Ui::PageReader::mangaListPointer(const Pointer& pt, int x, int y)
	{
		if (!mlist.on || !mlist.smooth) return false;
		// the content follows the finger past the scroller's slop, and glides on from the lift
		if (pt.kind == Pointer::Move)
		{
			if (mlist.smooth->move(pt.y * Ui::H)) mlist.moved = true;
			// a finger that slid sideways off its row changed its mind: no tap
			if (!mlist.moved && std::abs(x - mlist.downX) > MG_LIST_SLOP) mlist.item = -1;
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			// (moved out first: the tap may open a page that starts another list's finger)
			MangaListDrag d = std::move(mlist);
			mlist = MangaListDrag();
			d.smooth->lift();
			if (!d.moved && !d.caught && d.item >= 0 && d.tap && std::abs(x - d.downX) <= MG_LIST_SLOP) d.tap(d.item);
		}
		return false;
	}
}
