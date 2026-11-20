// Where Videos, Music and Books read from (docs/redesign: videos-more, videos-from, music-from, books-from). The three
// More sheets end in the same right column, "Where ... come from" and "Web dashboard"; the first opens the Sources
// sheet: each folder read now as card / folder with its count (A opens it in Files, one that holds nothing is dim),
// the dashboard's address, and the naming rule. Built on the More sheet's rows, so the kit's pieces are the same.
#include "ui_kit.h"
#include "ui_browse.h"
#include "ui_music.h"
#include "ui_videos.h"

namespace bp
{
	namespace
	{
		struct Words
		{
			const char* title;		// the sheet and the More row
			const char* things;		// "video": a count's noun
			const char* rule1;		// the naming rule, in two dim lines
			const char* rule2;
		};
		const Words& wordsOf(LibKind kind)
		{
			static const Words V{"Where videos come from", "video", "Folders named Videos or Movies, any capitals,", "at the top of a card. Elsewhere: Files, Search."};
			static const Words M{"Where music comes from", "song", "Folders named Music or Musics, any capitals,", "at the top of a card. Elsewhere: Files, Search."};
			static const Words B{"Where books come from", "book", "Folders named Books, Ebook or Ebooks, any capitals,", "at the top of a card. Elsewhere: Files, Search."};
			return kind == LibKind::Videos ? V : kind == LibKind::Music ? M : B;
		}

		std::string countOf(int n, const char* noun)
		{
			return std::to_string(n) + " " + noun + (n == 1 ? "" : "s");
		}

		// how many of the section's files lie under each folder (paths start with the folder and a slash)
		std::vector<int> countsUnder(LibKind kind, const std::vector<std::string>& folders, const MediaSnapshot* snap)
		{
			std::vector<int> n(folders.size(), 0);
			auto under = [&](const std::string& path) {
				for (size_t i = 0; i < folders.size(); i++)
					if (path.compare(0, folders[i].size() + 1, folders[i] + "/") == 0) { n[i]++; return; }
			};
			if (kind == LibKind::Books)
			{
				for (size_t i = 0; i < folders.size(); i++) n[i] = (int)listBooks({folders[i]}).size();
			}
			else if (snap && kind == LibKind::Videos)
				for (auto& v : snap->videos) under(v.path);
			else if (snap)
				for (auto& s : snap->songs) under(s.path);
			return n;
		}
	}

	Ui::MoreItem Ui::webDashboardRow(const Player& p)
	{
		MoreItem i;
		i.glyph = &G_WEB;
		i.label = "Web dashboard";
		// what it says about the dashboard: its address, "Off" (the page opens on its switch), or no Wi-Fi
		std::string addr = webAddress();
		i.note = !p.settings.webDashboard ? "Off" : addr.empty() ? "No Wi-Fi" : addr;
		i.act = [this](Player&) { openWeb(); return false; };
		return i;
	}

	void Ui::moreTwoColumns(MoreSpec& m, std::vector<MoreItem> section)
	{
		// the More sheet's rows run across, so the two lists are woven line by line; a short one is padded with blanks
		std::vector<MoreItem> left = std::move(m.rows), woven;
		size_t lines = std::max(left.size(), section.size());
		for (size_t i = 0; i < lines; i++)
		{
			MoreItem blank;
			blank.off = true;
			woven.push_back(i < left.size() ? left[i] : blank);
			woven.push_back(i < section.size() ? section[i] : blank);
		}
		m.rows = std::move(woven);
		m.cols = 2;
	}

	std::vector<Ui::MoreItem> Ui::sourceMoreRows(LibKind kind, const Player& p)
	{
		std::vector<MoreItem> rows;
		MoreItem from;
		from.glyph = &G_FOLDER;
		from.label = wordsOf(kind).title;
		// (no count note: the half-width column would cut the name; the sheet it opens says how many)
		from.act = [this, kind](Player&) { openSources(kind); return false; };
		rows.push_back(from);
		rows.push_back(webDashboardRow(p));
		return rows;
	}

	void Ui::openSources(LibKind kind)
	{
		std::vector<std::string> folders = libraryFolders(kind);
		std::shared_ptr<const MediaSnapshot> snap = kind == LibKind::Videos ? videos->snapshot() : kind == LibKind::Music ? music->snapshot() : nullptr;
		std::vector<int> counts = countsUnder(kind, folders, snap.get());
		int total = 0;
		for (int c : counts) total += c;
		const Words& w0 = wordsOf(kind);
		std::string sum = folders.empty() ? "no folder yet"
			: (folders.size() == 1 ? std::string("1 folder") : std::to_string(folders.size()) + " folders") + " \xC2\xB7 " + countOf(total, w0.things);
		openMore([this, kind, folders, counts, sum](const Player& p) {
			const Words& w = wordsOf(kind);
			MoreSpec m;
			m.glyph = &G_FOLDER;
			m.title = w.title;
			m.note = sum;
			// five folders at most: the sheet holds eight rows, and a card with more has bigger things to sort out
			for (size_t i = 0; i < folders.size() && i < 5; i++)
			{
				MoreItem r;
				r.glyph = &G_FOLDER;
				r.label = sourceLabel(folders[i]);
				r.note = counts[i] == 0 ? "empty" : countOf(counts[i], w.things);
				r.dim = counts[i] == 0;
				std::string path = folders[i];
				r.act = [this, path](Player& pl) {
					page = Page::Browse;
					browse->setSection(TAB_LOCAL);
					browse->enterFolder(path, pl);
					return false;
				};
				m.rows.push_back(r);
			}
			m.rows.push_back(webDashboardRow(p));
			for (const char* line : {w.rule1, w.rule2})
			{
				MoreItem r;
				r.label = line;
				r.off = true;
				m.rows.push_back(r);
			}
			return m;
		}, "sources");
	}
}
