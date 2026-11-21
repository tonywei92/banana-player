// The Manga reader's sheets and marks (docs/redesign: comic-view, comic-more, manga-end): View (START), More (X), the
// page bookmarks and the top panel's card at a chapter's end. The reading page itself, its keys and its finger are in
// ui_manga.cpp (drawMangaRead, mangaKey, mangaPointer).
#include "ui.h"
#include "ui_internal.h"
#include "ui_settings.h"
#include "ui_manga.h"
#include "ui_reader.h"
#include "ui_kit.h"
#include "manganet.h"
#include <algorithm>
#include <memory>

namespace bp
{
	// ---- View (START) ----

	int Ui::Manga::mangaViewNow() const
	{
		const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
		return st.book ? MV_BOOK : st.fullscreen ? MV_FULL : MV_ONE;
	}

	void Ui::Manga::mangaSetView(int v, Player& p)
	{
		int cur = mangaViewNow();
		if (v < 0 || v >= MV_COUNT || v == cur) return;
		if (cur == MV_BOOK) mangaSetBook(MANGA_BOOK_OFF, p);
		if (v == MV_BOOK)
		{
			// Book stands alone: fullscreen goes (leaving Book later comes back to one screen)
			if (ui.pageReader->readerStatus.fullscreen) mangaFullscreen(false, p);
			mangaSetBook(mangaHoldLast, p);
		}
		else mangaFullscreen(v == MV_FULL, p);
		// one screen or fullscreen is how the next chapter opens too (Book is the series' own)
		if (v != MV_BOOK && p.settings.mangaOpenFull != (v == MV_FULL))
		{
			Settings s = p.settings;
			s.mangaOpenFull = v == MV_FULL;
			p.setSettings(s);
		}
	}

	// How the chapter is held and read, one sheet. Layout first (One screen, Fullscreen, Book: the sheet closes when it
	// changes, the screen under it having become something else), then what means something in that layout: Book has
	// Hold and no fit or zoom (a whole page a screen); the others Page fit, Zoom and the Hinge gap (a fullscreen strip's:
	// dimmed with its reason otherwise). UP / DOWN choose a row, LEFT / RIGHT change it at once (the pages follow), A
	// steps it on, START or B close, SELECT is still Fullscreen; a tap on a chip or a stepper's side changes it, outside
	// the sheet closes.
	struct Ui::Manga::ComicView : Ui::Overlay
	{
		enum { ROW_LAYOUT, ROW_HOLD, ROW_MODE, ROW_FIT, ROW_ZOOM, ROW_HINGE, ROW_SWAP, ROW_LIGHT };
		int cursor = ROW_LAYOUT;	// a row's id: it stays on its row as others come and go
		int holdAsked = 0;	// the hold just asked, till the reader's status says it (a pass behind)
		explicit ComicView(Ui& u) : Overlay(u) {}
		const char* name() const override { return "comicview"; }
		Manga& manga() const { return *ui.manga; }
		const PageReader::ReaderStatus& st() const { return ui.pageReader->readerStatus; }
		bool gone() const { return ui.page != Page::MangaRead || !st().open; }
		bool book() const { return st().book != MANGA_BOOK_OFF; }
		bool strip() const { return st().mode == MANGA_STRIP; }
		std::vector<int> rows() const
		{
			if (book()) return {ROW_LAYOUT, ROW_HOLD, ROW_MODE, ROW_SWAP, ROW_LIGHT};
			return {ROW_LAYOUT, ROW_MODE, ROW_FIT, ROW_ZOOM, ROW_HINGE, ROW_SWAP, ROW_LIGHT};
		}
		lay::ListSheet layout() const { return lay::listSheet((int)rows().size()); }
		int hold() const { return holdAsked ? holdAsked : st().book ? st().book : manga().mangaHoldLast; }
		// why a row does nothing now ("": it does)
		std::string offNote(int row) const
		{
			if (row == ROW_FIT && strip()) return "Fits the width";
			if (row == ROW_HINGE && !st().fullscreen) return "Fullscreen only";
			if (row == ROW_HINGE && !strip()) return "Vertical scroll only";
			return "";
		}
		void setLayout(int to, Player& p)
		{
			if (to == manga().mangaViewNow()) return;
			manga().mangaSetView(to, p);
			close();
		}
		void change(int row, int dir, Player& p)
		{
			if (!offNote(row).empty()) return;
			switch (row)
			{
			case ROW_LAYOUT: setLayout(std::clamp(manga().mangaViewNow() + dir, 0, (int)MV_COUNT - 1), p); break;
			case ROW_HOLD:
				// the top screen on the left or the right as held
				holdAsked = hold() == MANGA_BOOK_LEFT ? MANGA_BOOK_RIGHT : MANGA_BOOK_LEFT;
				manga().mangaSetBook(holdAsked, p);
				break;
			case ROW_MODE:
				// (Book reads a page at a time: its two directions only)
				if (book()) manga().mangaSetMode(st().mode == MANGA_LTR ? MANGA_RTL : MANGA_LTR, p);
				else manga().mangaSetMode((st().mode + dir + MANGA_MODES) % MANGA_MODES, p);
				break;
			// the settings themselves, so the next chapter opens the same way
			case ROW_FIT: ui.settingsPage->changeSetting(settingRow("manga-fit"), dir, p); break;
			case ROW_ZOOM: ui.pageReader->mangaZoomStep(dir); break;
			case ROW_HINGE: ui.settingsPage->changeSetting(settingRow("manga-hinge"), dir, p); break;
			case ROW_SWAP: ui.settingsPage->changeSetting(settingRow("manga-swap-lr"), dir, p); break;
			case ROW_LIGHT: close(); ui.openLight(); break;
			default: break;
			}
		}
		bool key(Button b, Player& p) override
		{
			if (gone()) { close(); return false; }
			std::vector<int> r = rows();
			int n = (int)r.size();
			int i = (int)(std::find(r.begin(), r.end(), cursor) - r.begin());
			if (i >= n) i = 0;	// (its row went with the layout: back to the first)
			cursor = r[i];
			switch (b)
			{
			case Button::Up: cursor = r[(i + n - 1) % n]; break;
			case Button::Down: cursor = r[(i + 1) % n]; break;
			case Button::Left: change(cursor, -1, p); break;
			case Button::Right: change(cursor, 1, p); break;
			case Button::A:
				// Layout steps round; the zoom goes to 200% or back, as A on the page
				if (cursor == ROW_LAYOUT) setLayout((manga().mangaViewNow() + 1) % MV_COUNT, p);
				else if (cursor == ROW_ZOOM) ui.pageReader->mangaZoomToggle();
				else change(cursor, 1, p);
				break;
			case Button::B: case Button::Start: close(); break;
			// SELECT under the sheet is still Fullscreen (in Book: Fullscreen, upright): the sheet goes
			case Button::Select: close(); manga().mangaKey(Button::Select, p); break;
			default: break;
			}
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			if (gone()) { close(); return false; }
			if (pt.kind != Pointer::Down) return false;
			lay::ListSheet s = layout();
			if (!s.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
			std::vector<int> r = rows();
			for (int i = 0; i < (int)r.size(); i++)
			{
				Rect box = s.row(i);
				if (!box.hit(x, y)) continue;
				cursor = r[i];
				if (cursor == ROW_LAYOUT)
				{
					Rect seg = lay::viewChoices(box);
					for (int c = 0; c < MV_COUNT; c++)
						if (lay::cell(seg, MV_COUNT, 6, c).hit(x, y)) { setLayout(c, p); break; }
				}
				else if (cursor == ROW_LIGHT) change(cursor, 1, p);
				else
				{
					lay::SettingStepper stp = lay::settingStepper(box);
					if (stp.back.hit(x, y) || stp.fwd.hit(x, y)) change(cursor, stp.back.hit(x, y) ? -1 : 1, p);
				}
				break;
			}
			return false;
		}
		void draw(const Player& p) override
		{
			if (gone()) { close(); return; }
			if (holdAsked && holdAsked == st().book) holdAsked = 0;
			Kit& k = *ui.kit;
			lay::ListSheet s = layout();
			Kit::SheetHead head;
			head.glyph = &G_EYE;
			head.title = "View";
			head.note = "Kept for this series";
			k.sheet(s, head);
			static const char* MODES[MANGA_MODES] = {"Right to left", "Left to right", "Vertical scroll"};
			static const char* FITS[MANGA_FITS] = {"Page", "Width", "Fill"};
			int mode = st().mode >= 0 && st().mode < MANGA_MODES ? st().mode : 0;
			int fit = p.settings.mangaFit >= 0 && p.settings.mangaFit < MANGA_FITS ? p.settings.mangaFit : 0;
			std::vector<int> r = rows();
			if (std::find(r.begin(), r.end(), cursor) == r.end()) cursor = ROW_LAYOUT;
			for (int i = 0; i < (int)r.size(); i++)
			{
				int id = r[i];
				Rect box = s.row(i);
				if (id == ROW_LAYOUT)
				{
					// its label, the three ways to hold the comic as chips
					Kit::Row row;
					row.font = F_P16;
					row.name = "Layout";
					row.focused = cursor == ROW_LAYOUT;
					k.row(box, row);
					k.segment(lay::viewChoices(box), {"One screen", "Fullscreen", "Book"}, manga().mangaViewNow());
					continue;
				}
				Kit::SettingRow sr;
				sr.focused = cursor == id;
				std::string off = offNote(id);
				sr.off = !off.empty();
				switch (id)
				{
				case ROW_HOLD: sr.label = "Hold"; sr.value = hold() == MANGA_BOOK_RIGHT ? "Top screen right" : "Top screen left"; break;
				case ROW_MODE: sr.label = "Reading mode"; sr.value = MODES[mode]; break;
				case ROW_FIT: sr.label = "Page fit"; sr.value = FITS[fit]; break;
				case ROW_ZOOM: sr.label = "Zoom"; sr.value = std::to_string(st().zoomPct > 0 ? st().zoomPct : 100) + "%"; break;
				case ROW_HINGE: sr.label = "Hinge gap"; sr.value = std::to_string(p.settings.mangaHinge) + " px"; break;
				case ROW_SWAP: sr.label = "Swap Left / Right"; sr.value = p.settings.mangaSwapLR ? "On" : "Off"; break;
				case ROW_LIGHT:
				{
					std::string note = ui.lightNote(p);
					sr.label = "Light & filter";
					sr.value = note.empty() ? "Off" : note;
					sr.action = true;
					break;
				}
				default: break;
				}
				if (sr.off) sr.value = off;
				k.settingRow(box, sr);
			}
			k.hints(s.hints, {{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"START/B", "Close"}});
		}
		std::string signature(const Player& p) const override
		{
			char b[96];
			snprintf(b, sizeof b, "%d|%d %d %d %d %d|%d %d %d", cursor, st().mode, (int)st().fullscreen, st().book, st().zoomPct, holdAsked,
				p.settings.mangaHinge, p.settings.mangaFit, (int)p.settings.mangaSwapLR);
			return b;
		}
	};

	void Ui::Manga::mangaOpenView()
	{
		if (!ui.pageReader->readerStatus.open) return;
		ui.pushOverlay(std::make_unique<ComicView>(ui));
	}

	// ---- More (X) ----

	bool Ui::Manga::mangaReadIsRead() const
	{
		const ChapterProgress* pr = mangaStore ? mangaStore->progress(ui.pageReader->mangaReadChapterId) : nullptr;
		return pr && pr->read;
	}

	void Ui::Manga::mangaReadMarkRead()
	{
		if (!mangaStore) return;
		mangaStore->markRead(ui.pageReader->mangaReadSeriesId, {ui.pageReader->mangaReadChapterId}, !mangaReadIsRead());
		mangaSave();
	}

	bool Ui::Manga::mangaReadCanGet(std::string& note) const
	{
		const std::string& sid = ui.pageReader->mangaReadSeriesId;
		note.clear();
		// only a source's chapter is fetched to the card: a series read from the card is there already
		if (isServerId(sid)) { note = "a server\xE2\x80\x99s"; return false; }
		if (!isSourceId(sid)) { note = "on the card"; return false; }
		int i = mangaReadChapterIndex();
		if (i < 0 || i >= (int)mangaReadSrcChapters.size()) return false;
		std::string full, key;
		splitSourceId(sid, full, key);
		switch (mangaDlMarkFor(full, sid, mangaReadSrcChapters[i]).kind)
		{
		case MangaDlMark::None: return true;
		case MangaDlMark::OnCard: note = "on the card"; return false;
		case MangaDlMark::Failed: note = "failed: see Downloads"; return false;
		default: note = "queued"; return false;
		}
	}

	void Ui::Manga::mangaReadGet(Player& p)
	{
		std::string note;
		if (!mangaReadCanGet(note)) return;
		const std::string sid = ui.pageReader->mangaReadSeriesId;
		std::string full, key;
		splitSourceId(sid, full, key);
		const MangaSrcRow* row = mangaSrcRowFor(full);
		if (!row) { p.osd("Its source isn\xE2\x80\x99t installed now", 2000); return; }
		int added = mangaDlQueueChapters(full, row->file, sid, mangaTitle(sid), {mangaReadSrcChapters[mangaReadChapterIndex()]});
		ui.pageReader->topNote = {added ? "Download queued" : "Already here or queued", "", 1800};
	}

	void Ui::Manga::mangaReadInfo()
	{
		const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
		const std::string& sid = ui.pageReader->mangaReadSeriesId;
		const std::string& cid = ui.pageReader->mangaReadChapterId;
		InfoGroup g;
		g.title = "This chapter";
		std::string series = mangaTitle(sid);
		if (!series.empty()) g.rows.push_back({"Series", series});
		g.rows.push_back({"Chapter", ui.pageReader->mangaReadChapterTitle});
		int ci = mangaReadChapterIndex(), total = mangaReadChapterTotal();
		if (ci >= 0 && total > 0) g.rows.push_back({"In the series", std::to_string(ci + 1) + " of " + std::to_string(total)});
		if (st.pages > 0) g.rows.push_back({"Page", std::to_string(st.page + 1) + " of " + std::to_string(st.pages)});
		g.rows.push_back({"Bookmarks", std::to_string(mangaMarksHere().size())});
		g.rows.push_back({"Read", mangaReadIsRead() ? "Yes" : "Not yet"});
		std::string from = mangaSourceLabel(sid);
		if (!from.empty()) g.rows.push_back({"Source", from});
		else if (!isSourceId(sid) && !isServerId(sid)) g.rows.push_back({"On", cid});
		ui.openInfoGroups(series.empty() ? ui.pageReader->mangaReadChapterTitle : series, {g});
	}

	void Ui::Manga::mangaOpenReadMore()
	{
		if (!ui.pageReader->readerStatus.open) return;
		std::string chapter = ui.pageReader->mangaReadChapterId;
		ui.openMore([this, chapter](const Player&) {
			MoreSpec m;
			// (the chapter closed or changed under it: nothing left to say)
			if (ui.page != Page::MangaRead || ui.pageReader->mangaReadChapterId != chapter) return m;
			const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
			std::string series = mangaTitle(ui.pageReader->mangaReadSeriesId);
			m.glyph = &G_COMIC;
			m.title = series.empty() ? ui.pageReader->mangaReadChapterTitle : series;
			if (!series.empty()) m.note = ui.pageReader->mangaReadChapterTitle;
			if (st.pages > 0)
				m.note += std::string(m.note.empty() ? "" : " \xC2\xB7 ") + "page " + std::to_string(st.page + 1) + " of " + std::to_string(st.pages);
			// this chapter, at the left
			MoreItem read;
			read.glyph = &G_CHECK;
			read.label = mangaReadIsRead() ? "Mark unread" : "Mark read";
			read.act = [this](Player&) { mangaReadMarkRead(); return false; };
			MoreItem get;
			get.glyph = &G_DOWNLOAD;
			get.label = "Get this chapter";
			get.off = !mangaReadCanGet(get.note);
			get.act = [this](Player& p) { mangaReadGet(p); return false; };
			MoreItem mark;
			mark.glyph = &G_BOOKMARK;
			mark.lit = mangaPageMarked();
			mark.label = mark.lit ? "Remove this bookmark" : "Bookmark this page";
			mark.note = "page " + std::to_string(st.page + 1);
			mark.act = [this](Player& p) { mangaBookmarkToggle(p); return false; };
			m.rows = {read, get, mark};
			// the series and the app, at the right
			MoreItem info;
			info.glyph = &G_INFO;
			info.label = "Info";
			info.act = [this](Player&) { mangaReadInfo(); return false; };
			MoreItem chapters;
			chapters.glyph = &G_BLOCKS;
			chapters.label = "Chapters";
			chapters.key = "B";
			chapters.act = [this](Player&) { mangaCloseReader(); return false; };
			MoreItem settings;
			settings.glyph = &G_GEAR;
			settings.label = "Manga settings";
			settings.act = [this](Player&) { openMangaSettings(); return false; };	// (B there comes back to the page)
			ui.moreTwoColumns(m, {info, chapters, settings});
			return m;
		}, "comicmore");
	}

	// ---- page bookmarks ----

	std::vector<int> Ui::Manga::mangaMarksHere() const
	{
		return mangaStore ? mangaStore->bookmarks(ui.pageReader->mangaReadChapterId) : std::vector<int>();
	}

	bool Ui::Manga::mangaPageMarked() const
	{
		const ChapterProgress* pr = mangaStore ? mangaStore->progress(ui.pageReader->mangaReadChapterId) : nullptr;
		return pr && std::binary_search(pr->marks.begin(), pr->marks.end(), ui.pageReader->readerStatus.page);
	}

	void Ui::Manga::mangaBookmarkToggle(Player& p)
	{
		(void)p;
		const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
		if (!mangaStore || !st.open || st.pages <= 0) return;
		bool had = mangaPageMarked();
		bool on = mangaStore->toggleBookmark(ui.pageReader->mangaReadSeriesId, ui.pageReader->mangaReadChapterId, st.page);
		mangaSave();
		// said over the page on the top panel (the reader's own note: mpv's is under the pages)
		std::string page = "Page " + std::to_string(st.page + 1);
		if (on) ui.pageReader->topNote = {page + " bookmarked", "Y \xC2\xB7 Pages lists the bookmarks", 2400};
		else if (had) ui.pageReader->topNote = {"Bookmark removed", page, 1500};
		else ui.pageReader->topNote = {"No room for another bookmark", "This chapter has " + std::to_string(MangaStore::MAX_MARKS), 2200};
	}

	// ---- the chapter's end, on the top panel ----

	bool Ui::Manga::mangaEndCard(EndCard& out, const Player& p) const
	{
		const PageReader::ReaderStatus& st = ui.pageReader->readerStatus;
		// (Book says it on its own page tag, sideways: an upright card would lie across the spread)
		if (ui.page != Page::MangaRead || mangaReadWaiting || !st.open || !st.atEnd || st.book || st.pages <= 0) return false;
		int ci = mangaReadChapterIndex();
		bool next = ci >= 0 && ci + 1 < mangaReadChapterTotal();
		out.label = ui.pageReader->mangaReadChapterTitle + " \xC2\xB7 " + std::to_string(st.pages) + " of " + std::to_string(st.pages);
		if (next)
		{
			std::string name = mangaReadChapterName(ci + 1);
			out.title = "End of chapter";
			out.line = std::string("R2 or ") + (p.settings.mangaSwapLR ? "LEFT" : "RIGHT") + " for " + (name.empty() ? "the next chapter" : name);
		}
		else
		{
			out.title = "You\xE2\x80\x99re caught up";
			out.line = "That was the last chapter.";
		}
		return true;
	}
}
