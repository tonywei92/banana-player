#include "ui.h"
#include "manganet.h"
#include "ui_internal.h"
#include <algorithm>

namespace bp
{
	// ---- a source's filter sheet (docs/design/manga.md 5.2.1) ----
	//
	// The definitions come from the source's filters() through the worker (kept per source for the run); the values
	// are the sheet's own and only leave it as the object a Search job sends. X opens it (the page's own action): the grid's Y is
	// Search already and SELECT is Now Playing's. A source without filters has no hint and no sheet.

	namespace
	{
		enum class MFRowKind { Filter, Direction, Reset, Apply };
		struct MFRow { int index = -1; MFRowKind kind = MFRowKind::Filter; };

		std::vector<MFRow> filterRows(const std::vector<MangaFilter>& defs)
		{
			std::vector<MFRow> rows;
			for (int i = 0; i < (int)defs.size(); i++)
			{
				rows.push_back({i, MFRowKind::Filter});
				// a sort's direction is its own row: LEFT/RIGHT on the sort changes the order, this changes asc/desc
				if (defs[i].type == "sort") rows.push_back({i, MFRowKind::Direction});
			}
			rows.push_back({-1, MFRowKind::Reset});
			rows.push_back({-1, MFRowKind::Apply});
			return rows;
		}

		// the definitions (and values) of the level shown: the top, or a group's children
		const std::vector<MangaFilter>* defsAt(const std::vector<MangaFilter>& defs, const std::vector<int>& path,
			size_t at = 0)
		{
			if (at == path.size()) return &defs;
			int i = path[at];
			if (i < 0 || i >= (int)defs.size()) return nullptr;
			return defsAt(defs[i].children, path, at + 1);
		}

		std::vector<MangaFilterValue>* valuesAt(std::vector<MangaFilterValue>& values, const std::vector<int>& path,
			size_t at = 0)
		{
			if (at == path.size()) return &values;
			int i = path[at];
			if (i < 0 || i >= (int)values.size()) return nullptr;
			return valuesAt(values[i].children, path, at + 1);
		}

		void keepInView(int& cursor, int& scroll, int n, int rows)
		{
			if (n <= 0) { cursor = scroll = 0; return; }
			cursor = std::clamp(cursor, 0, n - 1);
			if (rows <= 0) { scroll = 0; return; }
			scroll = std::clamp(scroll, 0, std::max(0, n - rows));
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + rows) scroll = cursor - rows + 1;
		}

		const Ui::Glyph* filterGlyph(const MangaFilter& def, const MangaFilterValue& value)
		{
			if (def.type == "group") return &G_FOLDER;
			if (def.type == "text") return &G_PENCIL;
			if (def.type == "check") return value.check ? &G_CHECK : &G_BOX;
			if (def.type == "multi")
			{
				for (int m : value.multi) if (m) return &G_CHECK;
				return &G_BOX;
			}
			if (def.type == "tri")
			{
				for (int t : value.tri) if (t) return &G_CHECK;
				return &G_BOX;
			}
			return nullptr;
		}
	}

	Ui::MangaFilterSession* Ui::mangaFilterSession()
	{
		auto it = mangaFilters.find(mangaSrcId);
		return it == mangaFilters.end() ? nullptr : &it->second;
	}

	const Ui::MangaFilterSession* Ui::mangaFilterSession() const
	{
		auto it = mangaFilters.find(mangaSrcId);
		return it == mangaFilters.end() ? nullptr : &it->second;
	}

	void Ui::mangaFilterAsk()
	{
		// once a run per source: a source without filters() answers "[]" and is remembered as such
		if (mangaSrcId.empty()) return;
		if (MangaJava::isSourceId(mangaSrcId)) { mangaJavaFiltersAsk(); return; }
		MangaFilterSession& f = mangaFilters[mangaSrcId];
		if (f.known || f.loading || f.serial) return;
		if (!mangaNet || !mangaNet->started()) return;
		MangaWorker::Job job;
		job.sourceId = mangaSrcId;
		job.scriptPath = mangaSrcFile;
		job.kind = MangaWorker::Job::Filters;
		f.serial = mangaNet->worker().submit(job);
		f.loading = f.serial != 0;
		if (!f.serial) f.error = "Couldn\xE2\x80\x99t ask the source";
		mangaFilterLocal++;
	}

	bool Ui::mangaFilterAvailable() const
	{
		const MangaFilterSession* f = mangaFilterSession();
		return f && f->known && !f->defs.empty();
	}

	bool Ui::mangaFilterShown() const
	{
		const MangaFilterSession* f = mangaFilterSession();
		return f && f->open;
	}

	std::string Ui::mangaSourceFiltersJson() const
	{
		if (MangaJava::isSourceId(mangaSrcId)) return mangaJavaFiltersJson();
		const MangaFilterSession* f = mangaFilterSession();
		if (!f || !f->known || f->defs.empty()) return "{}";
		return mangaFilterValuesJson(f->defs, f->values);
	}

	void Ui::mangaFilterShow(Player& p)
	{
		(void)p;
		if (!mangaFilterAvailable()) return;
		MangaFilterSession& f = mangaFilters[mangaSrcId];
		f.open = true;
		f.path.clear();
		f.cursor = f.scroll = 0;
		f.checkAt = -1;
		f.checkCursor = f.checkScroll = 0;
		f.textAt = -1;
		mangaFilterLocal++;
	}

	void Ui::mangaFilterClose()
	{
		MangaFilterSession* f = mangaFilterSession();
		if (!f || !f->open) return;
		f->open = false;
		f->path.clear();
		f->cursor = f->scroll = 0;
		f->checkAt = -1;
		f->checkCursor = f->checkScroll = 0;
		f->textAt = -1;
		mangaFilterLocal++;
	}

	void Ui::mangaFilterTextTyped(const std::string& text)
	{
		MangaFilterSession* f = mangaFilterSession();
		if (!f) return;
		const std::vector<MangaFilter>* defs = defsAt(f->defs, f->path);
		std::vector<MangaFilterValue>* values = valuesAt(f->values, f->path);
		int i = f->textAt;
		f->textAt = -1;
		if (defs && values && i >= 0 && i < (int)defs->size() && i < (int)values->size() && (*defs)[i].type == "text")
			(*values)[i].text = text;
		mangaFilterLocal++;
	}

	bool Ui::mangaFilterKey(Button b, Player& p)
	{
		MangaFilterSession* f = mangaFilterSession();
		if (!f || !f->open) return false;
		const std::vector<MangaFilter>* defs = defsAt(f->defs, f->path);
		std::vector<MangaFilterValue>* values = valuesAt(f->values, f->path);
		if (!defs || !values)
		{
			mangaFilterClose();
			return false;
		}

		// a multi/tri checklist first: A cycles it, B returns to the filters
		if (f->checkAt >= 0)
		{
			if (f->checkAt >= (int)defs->size()) { f->checkAt = -1; mangaFilterLocal++; return false; }
			const MangaFilter& d = (*defs)[f->checkAt];
			int n = (int)d.options.size();
			if (n <= 0) { f->checkAt = -1; mangaFilterLocal++; return false; }
			keepInView(f->checkCursor, f->checkScroll, n, MF_ROWS);
			switch (b)
			{
			case Button::Up: f->checkCursor = (f->checkCursor + n - 1) % n; break;
			case Button::Down: f->checkCursor = (f->checkCursor + 1) % n; break;
			case Button::A:
			{
				MangaFilterValue& v = (*values)[f->checkAt];
				if (d.type == "multi")
				{
					if ((int)v.multi.size() < n) v.multi.resize(n, 0);
					v.multi[f->checkCursor] = v.multi[f->checkCursor] ? 0 : 1;
				}
				else
				{
					if ((int)v.tri.size() < n) v.tri.resize(n, 0);
					v.tri[f->checkCursor] = (v.tri[f->checkCursor] + 1) % 3;	// off -> include -> exclude
				}
				break;
			}
			case Button::B: f->checkAt = -1; f->checkCursor = f->checkScroll = 0; break;
			default: return false;
			}
			keepInView(f->checkCursor, f->checkScroll, n, MF_ROWS);
			mangaFilterLocal++;
			return false;
		}

		std::vector<MFRow> rows = filterRows(*defs);
		int n = (int)rows.size();
		keepInView(f->cursor, f->scroll, n, MF_ROWS);
		if (b == Button::B)
		{
			// inside a group B steps back to the level above; at the top it closes the sheet
			if (!f->path.empty())
			{
				f->path.pop_back();
				f->cursor = f->scroll = 0;
				mangaFilterLocal++;
			}
			else mangaFilterClose();
			return false;
		}
		if (b == Button::Up || b == Button::Down)
		{
			f->cursor = (f->cursor + (b == Button::Down ? 1 : n - 1) + n) % n;
			keepInView(f->cursor, f->scroll, n, MF_ROWS);
			mangaFilterLocal++;
			return false;
		}
		if (b != Button::A && b != Button::Left && b != Button::Right) return false;
		const MFRow& row = rows[f->cursor];
		if (row.kind == MFRowKind::Reset)
		{
			if (b == Button::A)
			{
				f->values = mangaFilterDefaults(f->defs);
				mangaFilterLocal++;
			}
			return false;
		}
		if (row.kind == MFRowKind::Apply)
		{
			if (b != Button::A) return false;
			mangaFilterClose();
			// a filter is a search: page 1, an empty query, the values the sheet holds
			mangaSrcQuery.clear();
			mangaSrcTab = SRC_SEARCH;
			mangaSrcItems.clear();
			mangaSrcCursor = mangaSrcScroll = mangaSrcPage = 0;
			mangaSrcMore = false;
			mangaSrcError.clear();
			mangaSourceLoad(1);
			return false;
		}
		const MangaFilter& d = (*defs)[row.index];
		MangaFilterValue& v = (*values)[row.index];
		if (row.kind == MFRowKind::Direction)
		{
			if (b == Button::Left || b == Button::Right || b == Button::A)
			{
				v.sortAsc = !v.sortAsc;
				mangaFilterLocal++;
			}
			return false;
		}
		if (b == Button::Left || b == Button::Right)
		{
			// a select or sort steps through its options; the direction has the row after it
			if ((d.type == "select" || d.type == "sort") && !d.options.empty())
			{
				int at = 0;
				const std::string& cur = d.type == "select" ? v.select : v.sortBy;
				for (int i = 0; i < (int)d.options.size(); i++)
					if (d.options[i].id == cur) { at = i; break; }
				int m = (int)d.options.size();
				at = (at + (b == Button::Right ? 1 : m - 1) + m) % m;
				if (d.type == "select") v.select = d.options[at].id;
				else v.sortBy = d.options[at].id;
				mangaFilterLocal++;
			}
			return false;
		}
		// A
		if (d.type == "check")
		{
			v.check = !v.check;
			mangaFilterLocal++;
		}
		else if (d.type == "multi" || d.type == "tri")
		{
			f->checkAt = row.index;
			f->checkCursor = f->checkScroll = 0;
			mangaFilterLocal++;
		}
		else if (d.type == "text")
		{
			f->textAt = row.index;
			mangaFilterLocal++;
			openKeyboard(MANGA_FILTER_FIELD, v.text);
		}
		else if (d.type == "group")
		{
			f->path.push_back(row.index);
			f->cursor = f->scroll = 0;
			mangaFilterLocal++;
		}
		return false;
	}

	bool Ui::mangaFilterPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Down) return false;
		MangaFilterSession* f = mangaFilterSession();
		if (!f || !f->open) return false;
		if (!mangaFilterBox().hit(x, y))
		{
			mangaFilterClose();
			return false;
		}
		const std::vector<MangaFilter>* defs = defsAt(f->defs, f->path);
		if (!defs) return false;
		if (f->checkAt >= 0)
		{
			if (f->checkAt >= (int)defs->size()) return false;
			int n = (int)(*defs)[f->checkAt].options.size();
			int scroll = f->checkScroll;
			keepInView(f->checkCursor, scroll, n, MF_ROWS);
			for (int i = scroll; i < n && i < scroll + MF_ROWS; i++)
				if (mangaFilterRowRect(i - scroll).hit(x, y))
				{
					f->checkCursor = i;
					mangaFilterKey(Button::A, p);
					return false;
				}
			return false;
		}
		std::vector<MFRow> rows = filterRows(*defs);
		int n = (int)rows.size();
		int scroll = f->scroll;
		keepInView(f->cursor, scroll, n, MF_ROWS);
		for (int i = scroll; i < n && i < scroll + MF_ROWS; i++)
			if (mangaFilterRowRect(i - scroll).hit(x, y))
			{
				f->cursor = i;
				mangaFilterLocal++;
				mangaFilterKey(Button::A, p);
				return false;
			}
		return false;
	}

	void Ui::drawMangaFilters(const Player& p)
	{
		(void)p;
		MangaFilterSession* f = mangaFilterSession();
		if (!f || !f->open) return;
		const std::vector<MangaFilter>* defs = defsAt(f->defs, f->path);
		// the values are reached along the same path as the definitions
		const std::vector<MangaFilterValue>* levelValues = nullptr;
		{
			const std::vector<MangaFilterValue>* v = &f->values;
			for (size_t k = 0; k < f->path.size() && v; k++)
			{
				int i = f->path[k];
				if (i < 0 || i >= (int)v->size()) { v = nullptr; break; }
				v = &(*v)[i].children;
			}
			levelValues = v;
		}
		if (!defs || !levelValues || levelValues->size() != defs->size())
		{
			mangaFilterClose();
			return;
		}

		std::string title = "Filters";
		const std::vector<MangaFilter>* walk = &f->defs;
		for (int i : f->path)
		{
			if (i < 0 || i >= (int)walk->size()) break;
			title = (*walk)[i].name;
			walk = &(*walk)[i].children;
		}
		if (f->checkAt >= 0 && f->checkAt < (int)defs->size()) title = (*defs)[f->checkAt].name;
		drawDialog(mangaFilterBox(), title);
		// a scroll bar inside the sheet's right edge (drawScrollBar's own place is outside a dialog)
		auto bar = [&](int first, int shown, int total) {
			if (total <= shown || shown <= 0) return;
			int height = MF_ROWS * MF_ROW_H - 8;
			fillRound({584, MF_ROW_TOP, 4, height}, TRACK, 2);
			int h = std::max(16, height * shown / total);
			int y = MF_ROW_TOP + (height - h) * first / std::max(1, total - shown);
			fillRound({584, y, 4, h}, DIM, 2);
		};

		if (f->checkAt >= 0 && f->checkAt < (int)defs->size())
		{
			const MangaFilter& d = (*defs)[f->checkAt];
			const MangaFilterValue& v = (*levelValues)[f->checkAt];
			int n = (int)d.options.size();
			int cursor = f->checkCursor, scroll = f->checkScroll;
			keepInView(cursor, scroll, n, MF_ROWS);
			for (int i = scroll; i < n && i < scroll + MF_ROWS; i++)
			{
				const Glyph* g = nullptr;
				std::string note;
				SDL_Color noteInk{0, 0, 0, 0};
				if (d.type == "multi")
				{
					bool on = i < (int)v.multi.size() && v.multi[i];
					g = on ? &G_CHECK : &G_BOX;
					if (on) note = "On";
				}
				else
				{
					int t = i < (int)v.tri.size() ? v.tri[i] : 0;
					g = t == 1 ? &G_CHECK : t == 2 ? &G_CROSS : &G_BOX;
					if (t == 1) note = "Include";
					else if (t == 2) { note = "Exclude"; noteInk = DANGER; }
				}
				drawDialogRow(mangaFilterRowRect(i - scroll), g, d.options[i].name, i == cursor, false, TEXT, note, noteInk);
			}
			bar(scroll, MF_ROWS, n);
			drawHints({{"A", d.type == "tri" ? "Off / In / Ex" : "Check"}, {"B", "Back"}});
			return;
		}

		std::vector<MFRow> rows = filterRows(*defs);
		int n = (int)rows.size();
		int cursor = f->cursor, scroll = f->scroll;
		keepInView(cursor, scroll, n, MF_ROWS);
		for (int i = scroll; i < n && i < scroll + MF_ROWS; i++)
		{
			const MFRow& row = rows[i];
			bool at = i == cursor;
			if (row.kind == MFRowKind::Reset)
			{
				drawDialogRow(mangaFilterRowRect(i - scroll), &G_REFRESH, "Reset", at, false, TEXT, "Defaults",
					SDL_Color{0, 0, 0, 0});
				continue;
			}
			if (row.kind == MFRowKind::Apply)
			{
				drawDialogRow(mangaFilterRowRect(i - scroll), &G_CHECK, "Apply", at, false, TEXT, "Page 1",
					SDL_Color{0, 0, 0, 0});
				continue;
			}
			const MangaFilter& d = (*defs)[row.index];
			const MangaFilterValue& v = (*levelValues)[row.index];
			if (row.kind == MFRowKind::Direction)
			{
				drawDialogRow(mangaFilterRowRect(i - scroll), v.sortAsc ? &G_UP : &G_DOWN, "Direction", at, false, TEXT,
					v.sortAsc ? "Ascending" : "Descending", SDL_Color{0, 0, 0, 0});
				continue;
			}
			drawDialogRow(mangaFilterRowRect(i - scroll), filterGlyph(d, v), d.name, at, false, TEXT,
				mangaFilterSummary(d, v), SDL_Color{0, 0, 0, 0});
		}
		bar(scroll, MF_ROWS, n);
		drawHints({{"A", "Choose"}, {"LEFT/RIGHT", "Change"}, {"B", "Back"}});
	}
}
