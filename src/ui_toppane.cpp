// Which Preview pane the top panel shows while nothing plays (previewpane.h; docs/redesign/components.md, Preview
// pane): it follows the bottom panel's cursor, so every shell section tells what is under the cursor on the top screen.
// Only text, read from state the pages already hold: nothing is read from the disk but a file's size, and the pane is
// composed again only when its key changes (topscreen.cpp).
#include "ui.h"
#include "format.h"
#include "previewpane.h"
#include "ui_books.h"
#include "ui_browse.h"
#include "ui_home.h"
#include "ui_internal.h"
#include "ui_music.h"
#include "ui_search.h"
#include "ui_settings.h"
#include "ui_videos.h"
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		const char* const MID = " \xC2\xB7 ";
		const char* const CRUMB = " \xE2\x80\xBA ";

		std::string noExt(const std::string& name)
		{
			size_t dot = name.rfind('.');
			return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
		}

		std::string upper(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
			return s;
		}
	}

	bool Ui::topPane(PaneSpec& out, const Player& p, bool idle) const
	{
		if (topPaneGallery(out)) return true;
		if (page == Page::Settings && settingsPage && settingsPage->topPane(out)) return true;
		return idle && idlePane(out, p);
	}

	bool Ui::idlePane(PaneSpec& out, const Player& p) const
	{
		out = PaneSpec{};
		auto keys = [&](std::vector<std::pair<std::string, std::string>> k) { out.keys = std::move(k); };
		auto plural = [](size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); };

		if (page == Page::Settings && settingsPage)
		{
			std::vector<int> rows = settingsPage->settingRows();
			out.key = "set|" + std::to_string(setGroup) + "|" + std::to_string(cursor);
			out.label = std::string("Settings") + (setGroup >= 0 ? CRUMB + std::string(GROUP_NAMES[setGroup]) : "");
			if (setGroup >= 0 && cursor >= 0 && cursor < (int)rows.size())
			{
				const SettingDef* d = rowDef(rows[cursor]);
				out.title = d ? d->label : GROUP_NAMES[setGroup];
				out.text = settingsPage->settingHelp(rows[cursor]);
			}
			else
			{
				out.title = setGroup >= 0 ? GROUP_NAMES[setGroup] : "Settings";
				out.text = "Open a group to see what each setting does here.";
			}
			keys({{"Left/Right", "Change"}, {"B", "Back"}});
			return true;
		}
		if (page == Page::Search && search)
		{
			out.key = "search|" + search->query;
			out.label = "Search library";
			out.title = search->query.empty() ? "Search" : "\xE2\x80\x9C" + search->query + "\xE2\x80\x9D";
			out.text = search->query.empty() ? "Find a song, a video or a book in your library by its name." : "Music, Videos and Books: L / R choose a kind.";
			keys({{"B", "Close"}});
			return true;
		}
		if (page == Page::ExtSection || page == Page::Extensions)
		{
			out.key = std::string("ext|") + (page == Page::Extensions ? "m" : "s");
			out.label = "Extensions";
			out.title = page == Page::Extensions ? "Manage extensions" : "Extensions";
			out.text = "An extension adds to Banana Player. Nothing of one shows or runs until you turn it on.";
			keys({{"A", "Open"}, {"B", "Back"}});
			return true;
		}
		if (page == Page::Videos && videos)
		{
			auto snap = videos->snapshot();
			const auto& rows = videos->rowsFor(p);
			out.label = "Videos";
			std::string from = snap ? videos->fromLine(*snap) : "";
			if (videos->cursor >= 0 && videos->cursor < (int)rows.size())
			{
				const auto& r = rows[videos->cursor];
				out.key = "vid|" + r.path;
				out.label = r.folder ? "Folder" : "Video";
				out.title = r.name;
				if (r.folder) out.rows = {{"Holds", plural(r.videos, "video", "videos")}};
				else out.rows = {{"Size", formatBytes(r.size)}};
				out.rows.push_back({"In", baseName(r.path.substr(0, r.path.rfind('/')))});
				out.text = from;
				keys({{"A", "Play"}, {"X", "More"}});
				return true;
			}
			out.key = "vid|" + from;
			out.title = "Videos";
			out.text = from.empty() ? "Put videos in a folder named Videos or Movies on a card." : from;
			return true;
		}
		if (page == Page::Music && music)
		{
			auto snap = music->snapshot();
			out.label = "Music";
			out.title = "Music";
			if (snap)
			{
				std::string from = mediaFromWords(snap->musicFolders);
				out.text = from.empty() ? "Put music in a folder named Music on a card." : "From " + from + MID + plural(snap->albums.size(), "album", "albums");
				int ai = music->albumAt;
				if (ai < 0 && music->view == 0) ai = music->cursor;
				if (ai >= 0 && ai < (int)snap->albums.size())
				{
					const MediaAlbum& a = snap->albums[ai];
					out.cover = true;
					out.label = "Album";
					out.title = snap->at(a.name);
					out.text = a.albumArtist ? snap->at(a.albumArtist) : "";
					out.rows = {{"Songs", std::to_string(a.songs.size())}};
					out.key = "alb|" + std::to_string(ai) + "|" + out.title;
					keys({{"A", "Open"}, {"X", "More"}});
					return true;
				}
				int ri = music->artistAt >= 0 ? music->artistAt : music->view == 1 ? music->cursor : -1;
				if (ri >= 0 && ri < (int)snap->artists.size())
				{
					const MediaArtist& ar = snap->artists[ri];
					out.label = "Artist";
					out.title = snap->at(ar.name);
					out.rows = {{"Albums", std::to_string(ar.albums.size())}, {"Songs", std::to_string(ar.songs)}};
					out.key = "art|" + std::to_string(ri) + "|" + out.title;
					keys({{"A", "Open"}, {"X", "More"}});
					return true;
				}
			}
			out.key = "music|" + out.text;
			return true;
		}
		if (page == Page::Books && books)
		{
			books->build();
			size_t strip = std::min<size_t>(5, books->reading.size());
			const Ui::Books::Shelf* b = nullptr;
			if (books->cursor >= 0 && (size_t)books->cursor < strip) b = &books->reading[books->cursor];
			else if (books->cursor >= (int)strip && (size_t)(books->cursor - strip) < books->shelf.size()) b = &books->shelf[books->cursor - strip];
			if (b)
			{
				out.cover = true;
				out.label = "Book";
				out.title = b->name;
				if (b->progress >= 0) out.rows = {{"Progress", std::to_string((int)(b->progress * 100 + 0.5)) + "%"}};
				out.text = upper(baseName(b->path).substr(baseName(b->path).rfind('.') == std::string::npos ? 0 : baseName(b->path).rfind('.') + 1));
				out.key = "book|" + b->path;
				keys({{"A", "Read"}, {"X", "More"}});
				return true;
			}
			out.key = "books|" + books->fromWords;
			out.label = "Books";
			out.title = "Books";
			out.text = books->fromWords.empty() ? "Put books in a folder named Books on a card." : "From " + books->fromWords + MID + plural(books->shelf.size(), "book", "books");
			return true;
		}
		if (page == Page::Browse && browse)
		{
			if (browse->atHome())
			{
				Home& h = *browse->homeView;
				if (h.recent.empty())
				{
					out.key = "welcome";
					out.welcome = true;
					out.label = "Welcome";
					out.title = "Banana Player";
					out.text = "Videos, music, photos and books from your cards, on two screens.";
					out.rows = {{"1. Play what is on the card", "Files lists every folder."},
						{"2. Send files from your phone", "Web dashboard: scan the code, then upload."},
						{"3. Play from a computer", "Network finds shared folders on your Wi-Fi."}};
					keys({{"L2/R2", "Sections"}, {"START", "Settings"}, {"F", "Quick settings"}});
					return true;
				}
				out.label = "Home";
				if (h.homeZone == Home::HOME_CARDS && h.homeStrip >= 0 && h.homeStrip < h.stripCards())
				{
					const auto& r = h.recent[h.homeStrip];
					out.key = "home|" + r.path + "|" + r.at;
					out.label = "Recent";
					out.title = r.name;
					out.text = r.sub;
					out.rows = {{"Where", r.where}};
					if (!r.at.empty()) out.rows.push_back({"Left at", r.at});
					keys({{"A", "Play"}, {"X", "More"}, {"L3", "Search"}});
					return true;
				}
				out.key = "home|" + std::to_string(h.stripCards());
				out.title = "Banana Player";
				out.text = "What you played is under Recent. The sections are in the rail, one L or R away.";
				out.rows = {{"Recent", std::to_string(h.stripCards())}};
				keys({{"L2/R2", "Sections"}, {"L3", "Search"}});
				return true;
			}
			std::string sel = browseSelection();
			out.label = browse->filesTitle();
			if (browse->filesPage() && !sel.empty() && sel != "..")
			{
				out.key = "file|" + sel;
				bool folder = browse->folderRow(cursor);
				std::string name = baseName(sel);
				out.title = name;
				struct stat st;
				if (!folder && !pseudoDir(sel) && !isNetPath(sel) && stat(sel.c_str(), &st) == 0) out.rows.push_back({"Size", formatBytes(st.st_size)});
				out.rows.insert(out.rows.begin(), {"Kind", folder ? "Folder" : upper(name.rfind('.') == std::string::npos ? "File" : name.substr(name.rfind('.') + 1))});
				out.rows.push_back({"In", browse->filesCrumb()});
				keys({{"A", folder ? "Open" : "Play"}, {"X", "More"}, {"Y", "Select"}});
				return true;
			}
			out.key = "browse|" + browse->browseDir + "|" + sel;
			out.title = sel.empty() || sel == ".." ? browse->filesTitle() : baseName(sel);
			out.text = browse->filesCrumb();
			keys({{"A", "Open"}, {"B", "Back"}});
			return true;
		}
		// every other page (Info, About, the Web page, Manga's): the plain pane
		out.key = "idle";
		out.label = "Banana Player";
		out.title = "Nothing playing";
		out.text = "Pick a video or a song on the bottom screen.";
		return true;
	}
}
