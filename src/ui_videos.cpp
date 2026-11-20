// Videos (ui_videos.h): the cards' films in one list, three views of them and a folder's own list. Everything is
// drawn inside lay::shell().body (the shell draws the rail, the header and the hints); the touches test the same
// Rects the drawing asked for.
#include "ui_videos.h"
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "mediaworker.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include <sys/stat.h>
#include <unordered_set>

namespace bp
{
	namespace
	{
		// Videos' sorts (More › Sort, stepped in place): the labels the row's value shows
		const char* SORT_LABELS[Ui::Videos::SORTS] = {"Name", "Newest first", "Biggest first"};

		// (the file-scope helpers name what they use in full: Ui's own names are private to it, and its nested
		// types need the Ui:: prefix here)
		lay::Box videosInner()
		{
			lay::Shell s = lay::shell();
			return {s.body.x + lay::SP12, s.body.y, s.body.w - 2 * lay::SP12, s.body.h};
		}

		// the file's name as the row shows it (less its extension)
		std::string rowName(const std::string& path)
		{
			std::string n = path.substr(path.rfind('/') + 1);
			size_t dot = n.rfind('.');
			if (dot != std::string::npos && dot > 0) n.resize(dot);
			return n;
		}

		// the pass's counters, quantized for a page's signature: the bar's step (a folder), and the counts'
		// (every 25 files, so a long card's read redraws about once a second and not a pass a file)
		int progressStep(const MediaIndexWorker::Progress& pr)
		{
			return pr.foldersDone + 1000 * pr.foldersKnown + pr.songs / 25 + pr.videos / 25;
		}

		// "12 Nov" (the date column the mock-up's row carries)
		std::string rowDate(int64_t mtime)
		{
			time_t t = (time_t)mtime;
			struct tm tmv;
			if (!mtime || !localtime_r(&t, &tmv)) return "";
			char out[32];
			if (strftime(out, sizeof out, "%d %b", &tmv) == 0) return "";
			return out;
		}

		// the empty page's sentence
		const char* EMPTY_VIDEOS_LINE = "Put your videos in a folder named Videos or Movies on either card. "
			"They are listed here by name, with your place in each.";
	}

	std::shared_ptr<const MediaSnapshot> Ui::Videos::snapshot() const
	{
		return source ? source->snapshot() : nullptr;
	}

	// ---- the views, and a folder's list ----

	void Ui::Videos::open(Player&) { ui.page = Page::Videos; }

	void Ui::Videos::viewStep(int dir, Player&)
	{
		// L2 / R2 and the chips: the three views, wrapping (All · Folders · Not finished)
		view = ((view + dir) % 3 + 3) % 3;
		folder.clear();
		cursor = scroll = 0;
	}

	void Ui::Videos::openFolder(const std::string& path)
	{
		folder = path;
		cursor = scroll = 0;
	}

	std::vector<Ui::Videos::Row> Ui::Videos::buildRows(const Player& p) const
	{
		auto snap = snapshot();
		std::vector<Row> out;
		if (!snap) return out;
		// the file rows' order for the sorts the name order isn't (More › Sort): newest first, biggest first
		auto orderFiles = [&](std::vector<Row>& rows) {
			if (sort == SORT_NEWEST)
				std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
					return a.mtime != b.mtime ? a.mtime > b.mtime : naturalLess(a.name, b.name);
				});
			else if (sort == SORT_BIGGEST)
				std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
					return a.size != b.size ? a.size > b.size : naturalLess(a.name, b.name);
				});
		};
		if (!folder.empty())
		{
			// the folder's own list: everything under it, however deep, in the snapshot's (name) order
			std::string prefix = folder + "/";
			for (auto& v : snap->videos)
				if (v.path.compare(0, prefix.size(), prefix) == 0)
					out.push_back({false, v.path, rowName(v.path), v.size, v.mtime, 0});
			orderFiles(out);
			return out;
		}
		if (view == 2)
		{
			// what was started and left, newest first, and worth coming back to (the store's own order); the
			// snapshot's videos as the map the store's paths are told apart by
			if (pathsGen != generation())
			{
				pathsGen = generation();
				videoWhere.clear();
				for (int i = 0; i < (int)snap->videos.size(); i++) videoWhere[snap->videos[i].path] = i;
			}
			for (auto& e : p.resumeStore().all())
			{
				auto it = videoWhere.find(e.path);
				if (it == videoWhere.end() || e.position < 0 || !worthResuming(e.position, e.duration)) continue;
				const MediaVideo& v = snap->videos[it->second];
				out.push_back({false, v.path, rowName(v.path), v.size, v.mtime, 0});
			}
			return out;
		}
		if (view == 0)
		{
			// every video, by name (the snapshot's own order) or as the sort asks
			for (auto& v : snap->videos) out.push_back({false, v.path, rowName(v.path), v.size, v.mtime, 0});
			orderFiles(out);
			return out;
		}
		// Folders: the folders directly inside the Videos folders (each with how much is under it, however deep),
		// then the loose files directly inside them; both as the snapshot's own (name) order keeps them
		std::map<std::string, int> inside;		// a child folder of a Videos folder -> its videos
		std::vector<Row> loose;
		for (auto& v : snap->videos)
		{
			std::string parent = parentFolder(v.path);
			bool atTop = false;
			for (auto& root : snap->videoFolders)
				if (parent == root) { atTop = true; break; }
			if (atTop)
			{
				loose.push_back({false, v.path, rowName(v.path), v.size, v.mtime, 0});
				continue;
			}
			for (auto& root : snap->videoFolders)
			{
				std::string prefix = root + "/";
				if (parent.compare(0, prefix.size(), prefix) != 0) continue;
				std::string rest = parent.substr(prefix.size());
				size_t slash = rest.find('/');
				std::string child = slash == std::string::npos ? rest : rest.substr(0, slash);
				if (!child.empty()) inside[prefix + child]++;
				break;
			}
		}
		for (auto& [path, count] : inside) out.push_back({true, path, rowName(path), 0, 0, count});
		orderFiles(loose);
		out.insert(out.end(), loose.begin(), loose.end());
		return out;
	}

	const std::vector<Ui::Videos::Row>& Ui::Videos::rowsFor(const Player& p) const
	{
		// kept until the index moves, the view, the sort or what plays does (a place in the store moved with it)
		char key[96];
		snprintf(key, sizeof key, "%u|%d|%d|%s|%s", generation(), view, sort, folder.c_str(), p.currentPath().c_str());
		if (rowsKey == key) return rowsCache;
		rowsKey = key;
		rowsCache = buildRows(p);
		return rowsCache;
	}

	std::vector<Ui::Kit::FilterItem> Ui::Videos::viewItems(const Player& p) const
	{
		auto snap = snapshot();
		int all = -1, folders = -1, unfinished = -1;
		if (snap && generation() > 0)
		{
			all = (int)snap->videos.size();
			// the folders directly inside the Videos folders, and the store's unfinished ones among the list's
			std::map<std::string, int> inside;
			std::unordered_set<std::string> paths;
			for (auto& v : snap->videos)
			{
				paths.insert(v.path);
				std::string parent = parentFolder(v.path);
				bool atTop = false;
				for (auto& root : snap->videoFolders)
					if (parent == root) { atTop = true; break; }
				if (atTop) continue;
				for (auto& root : snap->videoFolders)
				{
					std::string prefix = root + "/";
					if (parent.compare(0, prefix.size(), prefix) != 0) continue;
					std::string rest = parent.substr(prefix.size());
					size_t slash = rest.find('/');
					std::string child = slash == std::string::npos ? rest : rest.substr(0, slash);
					if (!child.empty()) inside[prefix + child]++;
					break;
				}
			}
			folders = (int)inside.size();
			unfinished = 0;
			for (auto& e : p.resumeStore().all())
				if (e.position >= 0 && worthResuming(e.position, e.duration) && paths.count(e.path)) unfinished++;
		}
		return {{"All", all}, {"Folders", folders}, {"Not finished", unfinished}};
	}

	// ---- playing ----

	void Ui::Videos::play(const Row& r, const Player& p) const
	{
		// the view's own videos as the list (a folder's list: the folder's; Folders: the loose files under the
		// Videos folders), and the file's own place in it — its start= comes from resume.tsv as it always does
		std::vector<std::string> paths;
		int index = -1;
		for (auto& row : rowsFor(p))
		{
			if (row.folder) continue;
			paths.push_back(row.path);
			if (row.path == r.path) index = (int)paths.size() - 1;
		}
		if (index < 0)
		{
			paths = {r.path};
			index = 0;
		}
		ui.playRequest = {};
		ui.playRequest.paths = paths;
		ui.playRequest.index = index;
		ui.playRequest.source.kind = SourceKind::Folder;
		ui.playRequest.source.title = folder.empty() ? "Videos" : baseName(folder);
	}

	// ---- the head (the shell asks) ----

	std::string Ui::Videos::fromLine(const MediaSnapshot& snap) const
	{
		std::string from = mediaFromWords(snap.videoFolders);
		if (from.empty()) return "";
		return "From " + from + " \xC2\xB7 " + std::to_string(snap.videos.size()) +
			(snap.videos.size() == 1 ? " video" : " videos");
	}

	std::vector<Ui::Hint> Ui::Videos::hintList(const Player&) const
	{
		if (!folder.empty()) return {{"A", "Play"}, {"X", "More"}, {"\xE2\x97\x80/\xE2\x96\xB6", "Page"}, {"B", "Videos"}};
		return {{"A", "Play / open"}, {"X", "More"}, {"L2/R2", "View"}, {"\xE2\x97\x80/\xE2\x96\xB6", "Page"}, {"B", "Home"}};
	}

	void Ui::Videos::accentFirst(Kit::Empty& e, const std::vector<std::string>& lines, const char* word) const
	{
		// the word a sentence of the empty state carries in ACCENT (the folders an empty page names)
		for (size_t i = 0; i < lines.size(); i++)
		{
			size_t at = lines[i].find(word);
			if (at != std::string::npos)
			{
				e.accent.push_back({(int)i, (int)at, (int)strlen(word)});
				return;
			}
		}
	}

	Ui::ShellHead Ui::Videos::head(const Player& p) const
	{
		ShellHead h;
		h.back = ui.backIconShown(p);
		h.more = true;
		h.hints = hintList(p);
		auto snap = snapshot();
		if (!folder.empty())
		{
			h.title = baseName(folder);
			h.crumb = "Videos \xE2\x80\xBA " + baseName(folder);
			return h;
		}
		h.title = "Videos";
		if (!snap || generation() == 0)
		{
			// the first read's live line, as the mock-up draws it: where from is known at once, how far is the
			// pass's own counter ("From Videos and Movies on both cards · 31 videos so far")
			h.crumb = "Reading the cards";
			h.more = false;
			if (ui.indexWorker)
			{
				auto pr = ui.indexWorker->progress();
				if (pr.reading)
				{
					MediaFolders mf = mediaFolders();
					std::string from = mediaFromWords(mf.videos);
					h.crumb = (from.empty() ? std::string("Reading the cards") : "From " + from) + " \xC2\xB7 " +
						std::to_string(pr.videos) + (pr.videos == 1 ? " video so far" : " videos so far");
				}
			}
			return h;
		}
		if (snap->videoFolders.empty()) { h.crumb = "No Videos or Movies folder on either card"; h.more = false; }
		else if (snap->videos.empty()) { h.crumb = "Videos is empty"; h.more = false; }
		else h.crumb = fromLine(*snap);
		return h;
	}

	// ---- the More sheet (X): what a video row has ----

	void Ui::Videos::openMoreSheet(const Row& row, Player&)
	{
		if (row.folder) return;
		std::string path = row.path, label = row.name;
		// asked for its list each time it draws, so a Like row says what it is now
		ui.openMore([this, path, label](const Player&) {
			MoreSpec m;
			m.glyph = &G_FILM;
			m.title = label;
			auto* store = ui.browse->playlists->plStore.get();
			bool likable = store && PlaylistStore::likable(path);
			bool liked = likable && store->isLiked(path);
			m.rows.push_back({&G_PLAY, "Play from the start", "", "", [this, path](Player& p) {
				if (p.playing() && !p.playingPhoto() && p.currentPath() == path)
				{
					p.seekFraction(0);
					if (!ui.pageIsOut()) p.unpause();
					else ui.noteSeek(p, 0);
					ui.showPlayer();
				}
				else
				{
					p.forgetPosition(path);
					ui.openFile(path, p);
				}
				return false;
			}});
			if (store)
				m.rows.push_back({&G_PLUS, "Add to playlist\xE2\x80\xA6", "", "", [this, path, label](Player& p) {
					ui.browse->playlists->openAddOne(path, label, -1, p);
					return false;
				}});
			if (likable)
				m.rows.push_back({liked ? &G_HEART : &G_HEART_O, liked ? "Unlike" : "Like", "", "",
					[this, path, label, liked](Player& p) {
						bool was = ui.browse->playlists->plStore && ui.browse->playlists->plStore->isLiked(path);
						if (ui.setLikedPath(p, path, label, -1, !was) != was) ui.likeCard(p, !was, label);
						return false;
					}, false, false, liked, true});
			m.rows.push_back({&G_FOLDER, "Show in folder", "", "", [this, path](Player& p) {
				ui.page = Page::Browse;
				ui.showInFolder(path, p);
				return false;
			}});
			m.rows.push_back({&G_INFO, "Info", "", "", [this, path](Player& p) { ui.openFileInfo(path, ""); return false; }});
			// the file's own rows, as Files' More has them (ui_fileops.cpp's fileOpsAct): the clipboard's flow is
			// Files' (Copy: open the folder it goes in, then SELECT), Delete asks as it does there
			m.rows.push_back({&G_PAGE, "Copy\xE2\x80\xA6", "", "", [this, path](Player& p) {
				ui.browse->fileOpsAct(CTX_F_COPY, path, p);
				return false;
			}});
			m.rows.push_back({&G_MOVE, "Move\xE2\x80\xA6", "", "", [this, path](Player& p) {
				ui.browse->fileOpsAct(CTX_F_MOVE, path, p);
				return false;
			}});
			m.rows.push_back({&G_PENCIL, "Rename\xE2\x80\xA6", "", "", [this, path](Player& p) {
				ui.browse->fileOpsAct(CTX_F_RENAME, path, p);
				return false;
			}});
			m.rows.push_back({&G_TRASH, "Delete", "", "", [this, path](Player& p) {
				std::vector<std::string> one{path};
				ui.browse->filesDeleteAsk(one, p);
				return false;
			}, true});
			// the list's order, stepped in place (A cycles it); Not finished keeps the store's own newest-first
			if (folder.empty() && view != 2)
				m.rows.push_back({&G_DOWN, "Sort", "", "", [this](Player&) {
					sortStep(1);
					return false;
				}, false, false, false, true, SORT_LABELS[sort]});
			return m;
		});
	}

	void Ui::Videos::sortStep(int dir)
	{
		sort = ((sort + dir) % SORTS + SORTS) % SORTS;
		cursor = scroll = 0;
	}

	// ---- the keys ----

	bool Ui::Videos::key(Button b, Player& p)
	{
		Rect inner = videosInner();
		const auto& rows = rowsFor(p);
		int n = (int)rows.size();
		// the views' step (L2 / R2), not a folder's own list (the mock-up gives that none)
		if ((b == Button::L2 || b == Button::R2) && folder.empty())
		{
			viewStep(b == Button::R2 ? 1 : -1, p);
			return false;
		}
		bool reading = !snapshot() || generation() == 0;
		bool empty = !reading && snapshot()->videos.empty() && folder.empty();
		// an empty page (no Videos folder, or none with a video in it): its two keys, and B Home
		if (empty)
		{
			switch (b)
			{
			case Button::A: ui.page = Page::Browse; ui.browse->openSection(TAB_LOCAL, p); break;
			case Button::Y: ui.openWeb(); break;
			case Button::B: ui.page = Page::Browse; ui.browse->enterFolder(HOMEDIR, p); break;
			default: break;
			}
			return false;
		}
		int rowsShown = std::max(1, (inner.h - (lay::CHIP_STRIP + lay::SP8)) / lay::VIDEO_ROW_H);
		if (!folder.empty()) rowsShown = std::max(1, inner.h / lay::VIDEO_ROW_H);
		switch (b)
		{
		case Button::Up: if (n) cursor = (cursor + n - 1) % n; break;
		case Button::Down: if (n) cursor = (cursor + 1) % n; break;
		case Button::Left: cursor = std::max(0, cursor - rowsShown); break;
		case Button::Right: cursor = std::min(std::max(0, n - 1), cursor + rowsShown); break;
		case Button::A:
			if (cursor >= 0 && cursor < n)
			{
				if (rows[cursor].folder) openFolder(rows[cursor].path);
				else play(rows[cursor], p);
			}
			break;
		case Button::X:
			if (cursor >= 0 && cursor < n) openMoreSheet(rows[cursor], p);
			break;
		case Button::B:
			if (!folder.empty()) { folder.clear(); cursor = scroll = 0; }
			else { ui.page = Page::Browse; ui.browse->enterFolder(HOMEDIR, p); }
			break;
		default: break;
		}
		fitRows(cursor, rowsShown);
		return false;
	}

	// ---- a finger ----

	bool Ui::Videos::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		Rect inner = videosInner();
		const auto& rows = rowsFor(p);
		int n = (int)rows.size();
		bool reading = !snapshot() || generation() == 0;
		bool empty = !reading && snapshot()->videos.empty() && folder.empty();
		int rowsShown = std::max(1, (inner.h - (lay::CHIP_STRIP + lay::SP8)) / lay::VIDEO_ROW_H);
		if (!folder.empty()) rowsShown = std::max(1, inner.h / lay::VIDEO_ROW_H);
		int top = inner.y;
		if (folder.empty()) top = inner.y + lay::LINE_H + lay::SP8 + lay::CHIP_STRIP + lay::SP8;
		if (pt.kind != Pointer::Down)
		{
			if (touch == T_NONE) return false;
			if (pt.kind == Pointer::Move)
			{
				if (!touchMoved && std::abs(y - touchY) > 6) touchMoved = true;
				if (touchMoved) scroll = lay::dragScroll(scrollStart, touchY, y, lay::VIDEO_ROW_H, 0, n, rowsShown);
				return false;
			}
			int t = touch;
			bool moved = touchMoved;
			touch = T_NONE;
			touchMoved = false;
			if (moved || t == T_NONE) return false;
			switch (t)
			{
			case T_EMPTY1:
				if (empty && emptyKey[0].hit(x, y)) { ui.page = Page::Browse; ui.browse->openSection(TAB_LOCAL, p); }
				return false;
			case T_EMPTY2:
				if (empty && emptyKey[1].hit(x, y)) ui.openWeb();
				return false;
			default: break;
			}
			if (t == T_GROUND) return false;		// the ground: a drag was its only meaning
			if (t >= T_CHIP && t < T_CHIP + 3)
			{
				if (folder.empty())
				{
					view = t - T_CHIP;
					cursor = scroll = 0;
				}
				return false;
			}
			if (t >= T_ROW && t - T_ROW < n)
			{
				Rect r{inner.x, top + (t - T_ROW - scroll) * lay::VIDEO_ROW_H, inner.w, lay::VIDEO_ROW_H};
				if (r.hit(x, y))
				{
					cursor = t - T_ROW;
					if (rows[cursor].folder) openFolder(rows[cursor].path);
					else play(rows[cursor], p);
				}
			}
			return false;
		}
		// the down: what is under the finger (the same Rects the drawing asked for)
		touch = T_NONE;
		touchMoved = false;
		touchY = y;
		scrollStart = scroll;
		if (empty)
		{
			std::vector<std::string> lines = ui.wrapText(EMPTY_VIDEOS_LINE, F_I14, 400, 99);
			lay::EmptyState e = lay::emptyState(inner, (int)lines.size(), 2);
			emptyKey[0] = e.key[0];
			emptyKey[1] = e.key[1];
			if (e.key[0].hit(x, y)) touch = T_EMPTY1;
			else if (e.key[1].hit(x, y)) touch = T_EMPTY2;
			return false;
		}
		if (reading) return false;
		if (folder.empty())
		{
			Kit& k = *ui.kit;
			lay::MusicHeader mh = lay::musicHeader(inner, k.filterWidths(viewItems(p)));
			int chip = lay::filterAt(mh.chips, x, y);
			if (chip >= 0) { touch = T_CHIP + chip; return false; }
		}
		for (int i = 0; i < rowsShown && scroll + i < n; i++)
		{
			Rect r{inner.x, top + i * lay::VIDEO_ROW_H, inner.w, lay::VIDEO_ROW_H};
			if (r.hit(x, y))
			{
				touch = T_ROW + scroll + i;
				cursor = scroll + i;
				return false;
			}
		}
		touch = T_GROUND;		// the list's own ground: a drag still scrolls it
		return false;
	}

	// ---- the drawing ----

	void Ui::Videos::draw(const Player& p)
	{
		auto snap = snapshot();
		Rect inner = videosInner();
		Kit& k = *ui.kit;
		bool reading = !snap || generation() == 0;
		bool empty = !reading && snap->videos.empty() && folder.empty();
		const auto& rows = rowsFor(p);
		int n = (int)rows.size();
		int rowsShown = std::max(1, (inner.h - (lay::CHIP_STRIP + lay::SP8)) / lay::VIDEO_ROW_H);
		int top = inner.y;
		if (folder.empty())
		{
			// the views: the line under the title, the chips, then the rows
			std::vector<Kit::FilterItem> views = viewItems(p);
			lay::MusicHeader mh = lay::musicHeader(inner, k.filterWidths(views));
			if (empty)
			{
				std::vector<std::string> lines = ui.wrapText(EMPTY_VIDEOS_LINE, F_I14, 400, 99);
				lay::EmptyState e = lay::emptyState(inner, (int)lines.size(), 2);
				emptyKey[0] = e.key[0];
				emptyKey[1] = e.key[1];
				Kit::Empty v;
				v.glyph = &G_FILM;
				v.title = "No videos here yet";
				v.lines = lines;
				accentFirst(v, lines, "Videos");
				accentFirst(v, lines, "Movies");
				v.key1 = "Browse Files";
				v.glyph1 = &G_FOLDER;
				v.key2 = "Web dashboard";
				v.glyph2 = &G_WEB;
				v.pressed1 = touch == T_EMPTY1 && !touchMoved;
				v.pressed2 = touch == T_EMPTY2 && !touchMoved;
				v.hotkeys = p.settings.buttonHotkeys;
				k.emptyState(e, v);
				return;
			}
			// the view chips (the "From …" line is the header's crumb, once drawn here as well)
			k.filters(mh.chips, views, view, -1,
				touch >= T_CHIP && touch < T_CHIP + 3 && !touchMoved ? touch - T_CHIP : -1);
			if (reading)
			{
				// the first read, as the spec draws it (videos-reading): which card, how far, a bar. Quicker than
				// Music (no tags), so the folders are the measure
				lay::Notice l = lay::notice({inner.x, mh.strip.y + lay::CHIP_STRIP + lay::SP8, inner.w, 0},
					true, true, 0);
				Kit::NoticeRow row;
				row.tone = Kit::NoticeTone::Working;
				row.glyph = &G_REFRESH;
				auto pr = ui.indexWorker ? ui.indexWorker->progress() : MediaIndexWorker::Progress{};
				std::string card = walkCardName(pr.card);
				row.text = card.empty() ? "Reading the Videos folders" : "Reading Videos on " + card;
				row.second = pr.foldersKnown > 0
					? std::to_string(std::min(pr.foldersDone, pr.foldersKnown)) + " of " + std::to_string(pr.foldersKnown) +
						" folders \xC2\xB7 the list lands when it's done"
					: "the list lands when it's done";
				row.share = pr.foldersKnown > 0 ? (double)pr.foldersDone / pr.foldersKnown : 0;
				k.notice(l, row);
				return;
			}
			top = mh.strip.y + lay::CHIP_STRIP + lay::SP8;
		}
		else
		{
			rowsShown = std::max(1, inner.h / lay::VIDEO_ROW_H);
		}
		// the rows (a video's place and length read here, from the stores that hold them, keyed by the path)
		fitRows(cursor, rowsShown);
		for (int i = 0; i < rowsShown && scroll + i < n; i++)
		{
			const Row& r = rows[scroll + i];
			int at = scroll + i;
			if (r.folder)
			{
				Kit::Folder f;
				f.name = r.name;
				f.second = std::to_string(r.videos) + (r.videos == 1 ? " video" : " videos");
				f.focused = at == cursor;
				f.pressed = touch == T_ROW + at && !touchMoved;
				Rect box{inner.x, top + i * lay::VIDEO_ROW_H, inner.w, lay::VIDEO_ROW_H};
				k.folderRow(lay::folderRow(box), f);
				continue;
			}
			Kit::Video v;
			v.name = r.name;
			const ResumeEntry* e = p.resumeStore().find(r.path);
			double secs = -1;
			cachedDuration(r.path, secs);
			if (e && e->position >= 0)
			{
				// "at 6:12 / 9:56" and the bar under it
				double dur = e->duration > 0 ? e->duration : secs;
				v.note = formatTime(e->position) + " / " + (dur > 0 ? formatTime(dur) : "--:--");
				v.resume = dur > 0 ? std::clamp(e->position / dur, 0.0, 1.0) : 0;
			}
			else if (e && e->position < 0 && e->duration > 0)
			{
				v.note = formatTime(e->duration);
				v.done = true;		// played through (or as good as): the tick, no place kept
			}
			else if (secs > 0) v.note = formatTime(secs);
			v.second = baseName(parentFolder(r.path)) + (r.size > 0 ? " \xC2\xB7 " + formatBytes(r.size) : "") +
				(r.mtime ? " \xC2\xB7 " + rowDate(r.mtime) : "");
			// the row that plays: the Playing pill (paused too), as the Playlists rows and Music's cells mark theirs
			bool here = p.playing() && !p.playingPhoto() && p.currentPath() == r.path;
			v.playing = here && !p.paused;
			v.paused = here && p.paused;
			v.focused = at == cursor;
			v.pressed = touch == T_ROW + at && !touchMoved;
			Rect box{inner.x, top + i * lay::VIDEO_ROW_H, inner.w, lay::VIDEO_ROW_H};
			k.videoRow(lay::videoRow(box), v);
		}
	}

	std::string Ui::Videos::signature(const Player& p) const
	{
		// everything the drawing shows that Ui::signature doesn't already hold (the playing file and its pause are
		// its base; the resume store is read at the row, and what plays moved it — the rows' key holds that), and
		// the reading notice's step, so the bar moves while the first pass walks
		char buf[96];
		snprintf(buf, sizeof buf, "|vd%d.%d.%s.%d.%d.%d.%u", view, cursor, folder.c_str(), scroll, touch,
			(int)rowsFor(p).size(), generation());
		std::string s = buf;
		if (!snapshot() || generation() == 0)
		{
			s += "R";
			if (ui.indexWorker)
			{
				auto pr = ui.indexWorker->progress();
				s += std::to_string(progressStep(pr));
			}
		}
		return s;
	}
}
