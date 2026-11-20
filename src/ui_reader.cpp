// The top panel's page reader, the bottom panel's side (ui_reader.h): the seek and the zoom, Try again, fullscreen's
// pages, the pages grid and the dragged lists, shared by Files' PDF/comic viewer (Ui::Docs) and the Manga reader.
#include "ui_reader.h"
#include "ui_kit.h"
#include "ui_internal.h"
#include "ui_manga.h"
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
		// from the same geometry, so Up/Down and the Reading menu's Zoom row walk the same ladder
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

	void Ui::PageReader::drawMangaFull()
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
		while (mfTex.size() > 2)	// (each a page's textures on the GPU, a long page's tens of MB: the memory is the system's)
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
			std::string tag = std::to_string(readerStatus.page + 1) + " / " + std::to_string(readerStatus.pages);
			ui.kit->pill(W - 12, H - 23, tag, Kit::Pill::Fact);
		}
		if (readerStatus.failedHere) drawMangaRetry(true);
		// the controls, while they show (over the tag), then the first open's toast over both
		if (now < ui.manga->mangaCtrlUntil) ui.manga->drawMangaControls();
		if (now < ui.manga->mangaToastUntil) ui.manga->drawMangaToast();
	}

	void Ui::PageReader::mangaPagesOpen(Player& p)
	{
		(void)p;
		if (!readerStatus.open || readerStatus.pages <= 0) return;
		mangaPages = true;
		mangaPagesCursor = std::clamp(readerStatus.page, 0, readerStatus.pages - 1);
		// the cursor's row centred in the grid
		int row = mangaPagesCursor / MPG_COLS;
		int rows = (readerStatus.pages + MPG_COLS - 1) / MPG_COLS;
		int shown = (H - MPG_TOP - 44) / MPG_ROW_H;	// 44: the hints footer
		mangaPagesScroll = std::clamp(row - shown / 2, 0, std::max(0, rows - shown));
		mangaPagesTouch = false;
		mangaPagesPressed = -1;
		mpMade = 0;
	}

	void Ui::PageReader::mangaPagesClose(Player& p)
	{
		(void)p;
		mangaPages = false;
		mangaPagesTouch = false;
		mangaPagesPressed = -1;
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
		int pages = readerStatus.pages;
		if (pages <= 0) { mangaPagesClose(p); return false; }
		int cur = mangaPagesCursor;
		int row = cur / MPG_COLS, col = cur % MPG_COLS;
		int rows = (pages + MPG_COLS - 1) / MPG_COLS;
		switch (b)
		{
		case Button::Left: if (col > 0) cur--; break;
		case Button::Right: if (col + 1 < MPG_COLS && cur + 1 < pages) cur++; break;
		case Button::Up: if (row > 0) cur -= MPG_COLS; break;
		case Button::Down: if (row + 1 < rows) cur = std::min(cur + MPG_COLS, pages - 1); break;
		case Button::A:
			// jump to the page under the cursor and close the grid
			mangaSeek(cur);
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
			mangaSeek(cur);
			// keep the cursor's row in view
			row = cur / MPG_COLS;
			int shown = (H - MPG_TOP - 44) / MPG_ROW_H;
			if (row < mangaPagesScroll) mangaPagesScroll = row;
			if (row >= mangaPagesScroll + shown) mangaPagesScroll = row - shown + 1;
		}
		return false;
	}

	bool Ui::PageReader::mangaPagesPointer(const Pointer& pt, int x, int y, Player& p)
	{
		int pages = readerStatus.pages;
		if (pages <= 0) { mangaPagesClose(p); return false; }
		int rows = (pages + MPG_COLS - 1) / MPG_COLS;
		int shown = std::max(1, (H - MPG_TOP - 44) / MPG_ROW_H);
		// the thumbnail under a point, -1 for none
		auto thumbAt = [&](int px, int py) {
			for (int i = mangaPagesScroll * MPG_COLS; i < pages; i++)
			{
				Rect r = mangaPageRect(i);
				if (r.y >= H - 44) break;	// below the hints
				if (r.hit(px, py)) return i;
			}
			return -1;
		};
		if (pt.kind == Pointer::Down)
		{
			mangaPagesTouch = false;
			mangaPagesMoved = false;
			mangaPagesPressed = -1;
			// the back icon: same as B (close the grid)
			if (ui.buttonRect(BACK_ICON).hit(x, y)) { mangaPagesClose(p); return false; }
			// anywhere on the grid, a thumbnail too: a drag or a tap, told apart when the finger lifts (a thumbnail
			// opening on the touch left nothing to start a drag from: the grid is all thumbnails)
			if (y >= MPG_TOP && y < H - 44)
			{
				mangaPagesTouch = true;
				mangaPagesTouchY = mangaPagesTouchStartY = pt.y;
				mangaPagesPressed = thumbAt(x, y);
			}
			return false;
		}
		if (pt.kind == Pointer::Move && mangaPagesTouch)
		{
			// past a small slop it's a drag (pt.y is in panel heights: 0.03 is about 14 px)
			if (!mangaPagesMoved && std::fabs(pt.y - mangaPagesTouchStartY) > 0.03)
			{
				mangaPagesMoved = true;
				mangaPagesPressed = -1;
			}
			if (!mangaPagesMoved) return false;
			// the grid follows the finger, a row per row's height of drag (pt.y is in panel heights). What's under a
			// whole row stays owed (the finger's place moves on only by the rows taken), so a slow drag scrolls too;
			// the small epsilon keeps a drag of exactly one row from rounding down to none
			const double rowH = (double)MPG_ROW_H / H;
			double owed = -(pt.y - mangaPagesTouchY) / rowH;
			int shift = (int)(owed + (owed > 0 ? 1e-6 : -1e-6));
			if (shift != 0)
			{
				int was = mangaPagesScroll;
				mangaPagesScroll = std::clamp(mangaPagesScroll + shift, 0, std::max(0, rows - shown));
				mangaPagesTouchY -= shift * rowH;
				if (mangaPagesScroll != was)
				{
					// the cursor follows if it was left behind
					if (mangaPagesCursor < mangaPagesScroll * MPG_COLS)
						mangaPagesCursor = std::clamp(mangaPagesScroll * MPG_COLS, 0, pages - 1);
					if (mangaPagesCursor >= (mangaPagesScroll + shown) * MPG_COLS)
						mangaPagesCursor = std::clamp((mangaPagesScroll + shown - 1) * MPG_COLS + MPG_COLS - 1, 0, pages - 1);
					mangaSeek(mangaPagesCursor);
				}
			}
			return false;
		}
		if (pt.kind == Pointer::Up && mangaPagesTouch)
		{
			mangaPagesTouch = false;
			// a tap (no drag) that lifts on the thumbnail it touched: that page, and the grid closes
			int i = mangaPagesPressed;
			mangaPagesPressed = -1;
			if (!mangaPagesMoved && i >= 0 && thumbAt(x, y) == i)
			{
				mangaSeek(i);
				mangaPagesClose(p);
			}
			return false;
		}
		return false;
	}

	void Ui::PageReader::drawMangaPages(const Player& p)
	{
		(void)p;
		int pages = readerStatus.pages;
		if (pages <= 0) { mangaPagesClose(const_cast<Player&>(p)); return; }
		// the chapter changed: the old textures go
		if (readerThumbs.gen != mpGen) { mpFree(); mpGen = readerThumbs.gen; }
		ui.drawBackIcon();
		ui.drawTextMid("Pages", 1, TEXT, 72, 24, 540);
		ui.drawTextMid(std::to_string(mangaPagesCursor + 1) + " / " + std::to_string(pages), 0, DIM, 72, 50, 540);
		// the grid: thumbnails with numbered placeholders for pages not decoded yet. at most a few textures per
		// frame so a turn never stalls the panel (the same pattern as drawMangaFull)
		bool made = false;
		int rows = (pages + MPG_COLS - 1) / MPG_COLS;
		int shown = (H - MPG_TOP - 44) / MPG_ROW_H;
		mangaPagesScroll = std::clamp(mangaPagesScroll, 0, std::max(0, rows - shown));
		SDL_Rect clip{0, (int)std::lround(MPG_TOP * sy), (int)std::lround(W * sx),
			(int)std::lround((H - MPG_TOP - 44) * sy)};
		SDL_RenderSetClipRect(ui.ren, &clip);
		for (int i = mangaPagesScroll * MPG_COLS; i < pages; i++)
		{
			Rect r = mangaPageRect(i);
			if (r.y >= H - 44) break;
			bool at = i == mangaPagesCursor;
			// the focused thumbnail's ACCENT edge
			ui.kit->flat(r, Kit::cardFill(SURFACE), R_TILE - 2, at ? ACCENT : Kit::edgeOf(SURFACE));
			// look for the texture, or make one from the RGBA main handed us
			auto it = mpTex.find(i);
			if (it == mpTex.end() && !made)
			{
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
					made = true;	// one per frame
					break;
				}
			}
			if (it != mpTex.end() && it->second.tex)
			{
				// the thumbnail drawn inside the box, aspect kept (centred)
				int bw = r.w - 4, bh = r.h - 24;	// 24: room for the page number below
				int sw = it->second.w, sh = it->second.h;
				int dw = bw, dh = bh;
				if (sw > 0 && sh > 0)
				{
					double aspect = (double)sw / sh;
					if (aspect > (double)bw / bh) { dw = bw; dh = (int)(bw / aspect + 0.5); }
					else { dh = bh; dw = (int)(bh * aspect + 0.5); }
				}
				SDL_Rect dst{(int)std::lround((r.x + 2 + (bw - dw) / 2) * sx), (int)std::lround((r.y + 2 + (bh - dh) / 2) * sy),
					(int)std::lround(dw * sx), (int)std::lround(dh * sy)};
				SDL_RenderCopy(ui.ren, it->second.tex, nullptr, &dst);
				it->second.used = SDL_GetTicks();
			}
			else
			{
				// not decoded yet: a numbered placeholder
				std::string num = std::to_string(i + 1);
				ui.drawTextMid(num, 0, at ? ACCENT : DIM, r.x + r.w / 2, r.y + r.h / 2, 0, 2);
				continue;	// the number is the placeholder: none again underneath
			}
			// the page number under the thumbnail, inside the box's bottom strip (the 24 kept for it above)
			ui.drawTextMid(std::to_string(i + 1), 0, at ? ACCENT : DIM, r.x + r.w / 2, r.y + r.h - 11, 0, 2);
		}
		SDL_RenderSetClipRect(ui.ren, nullptr);
		// the scrollbar
		ui.drawScrollBar(MPG_TOP, shown * MPG_ROW_H - 4, mangaPagesScroll, shown, rows);
		// at most 60 thumbnails kept: the ones not drawn longest go (a 200-page chapter keeps 60 at a time)
		while (mpTex.size() > 60)
		{
			auto old = mpTex.begin();
			for (auto i = mpTex.begin(); i != mpTex.end(); ++i)
				if (i->second.used < old->second.used) old = i;
			if (old->second.tex) SDL_DestroyTexture(old->second.tex);
			mpTex.erase(old);
		}
		ui.drawHints({{"D-PAD", "Move"}, {"A", "Go to page"}, {"B", "Close"}});
	}

	void Ui::PageReader::mangaListBegin(int x, int y, int scroll, int rowH, int item, std::function<int(int)> scrollTo,
		std::function<void(int)> tap)
	{
		mlist = MangaListDrag();
		mlist.on = true;
		mlist.downX = x;
		mlist.downY = mlist.fromY = y;
		mlist.fromScroll = scroll;
		mlist.rowH = std::max(1, rowH);
		mlist.item = item;
		mlist.scrollTo = std::move(scrollTo);
		mlist.tap = std::move(tap);
	}

	bool Ui::PageReader::mangaListPointer(const Pointer& pt, int x, int y)
	{
		if (!mlist.on) return false;
		if (pt.kind == Pointer::Move)
		{
			// past the slop (as the pages grid's, about 0.03 of the panel) it's a drag, and the lift is no tap
			if (!mlist.moved && std::abs(y - mlist.downY) > MG_LIST_SLOP) mlist.moved = true;
			// a finger that slid sideways off its cover changed its mind: no tap (and no scroll: the lists go up and down)
			if (!mlist.moved && std::abs(x - mlist.downX) > MG_LIST_SLOP) mlist.item = -1;
			if (!mlist.moved || !mlist.scrollTo) return false;
			// counted from where the finger went down, so a slow drag loses nothing to rounding move by move
			int want = mlist.fromScroll + (int)std::lround((mlist.fromY - y) / (double)mlist.rowH);
			int took = mlist.scrollTo(want);
			// stopped at an end: the count starts again here, so the way back moves the list at once
			if (took != want)
			{
				mlist.fromScroll = took;
				mlist.fromY = y;
			}
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			// (moved out first: the tap may open a page that starts another list's finger)
			MangaListDrag d = std::move(mlist);
			mlist = MangaListDrag();
			if (!d.moved && d.item >= 0 && d.tap && std::abs(x - d.downX) <= MG_LIST_SLOP &&
				std::abs(y - d.downY) <= MG_LIST_SLOP)
				d.tap(d.item);
		}
		return false;
	}
}
