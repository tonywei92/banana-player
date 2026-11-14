// Files' own file operations on the handheld: Copy, Move, Rename and Delete on a file or folder of the cards. A copy,
// or a move to the other card, runs on a thread of its own (file reads and writes only: it never touches the Player
// or the Ui; the Ui reads its progress each pass), and the panel shows it, with Cancel, until it ends. The copying
// itself is treecopy.h's: measured first, written out of sight, put in place last, so nothing half-copied is left.
#include "ui_internal.h"
#include "treecopy.h"
#include "pagebook.h"
#include <ctime>
#include <strings.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <sys/statvfs.h>
#include <thread>
#ifdef __linux__
#include <sys/vfs.h>
#endif

namespace bp
{
	struct Ui::FileJob
	{
		int op = CLIP_COPY;
		std::string src, dest, name;
		std::atomic<uint64_t> done{0}, total{0};
		std::atomic<bool> cancel{false}, finished{false};
		std::mutex m;
		std::string current, error;	// under m
		std::thread t;
		~FileJob()
		{
			if (t.joinable()) t.join();
		}
	};

	namespace
	{
		std::string nameOf(const std::string& path)
		{
			size_t s = path.rfind('/');
			return s == std::string::npos ? path : path.substr(s + 1);
		}

		std::string sizeText(uint64_t b)
		{
			char out[32];
			if (b >= (1ull << 30)) snprintf(out, sizeof out, "%.1f GB", b / 1073741824.0);
			else if (b >= (1ull << 20)) snprintf(out, sizeof out, "%.1f MB", b / 1048576.0);
			else if (b >= 1024) snprintf(out, sizeof out, "%llu KB", (unsigned long long)(b / 1024));
			else snprintf(out, sizeof out, "%llu bytes", (unsigned long long)b);
			return out;
		}

		bool isDirPath(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}

		bool sameDevice(const std::string& a, const std::string& b)
		{
			struct stat sa, sb;
			return stat(a.c_str(), &sa) == 0 && stat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev;
		}

		// a free name in dir for name: itself, else "name (2).ext", "name (3).ext"... (a folder's number at its end)
		std::string freeName(const std::string& dir, const std::string& name, bool folder)
		{
			struct stat st;
			if (stat((dir + "/" + name).c_str(), &st) != 0) return name;
			size_t dot = folder ? std::string::npos : name.rfind('.');
			if (dot == 0) dot = std::string::npos;
			std::string base = dot == std::string::npos ? name : name.substr(0, dot), ext = dot == std::string::npos ? "" : name.substr(dot);
			for (int n = 2; n < 1000; n++)
			{
				std::string c = base + " (" + std::to_string(n) + ")" + ext;
				if (stat((dir + "/" + c).c_str(), &st) != 0) return c;
			}
			return "";
		}

	}

	// the job itself, on its thread: only the job's own fields are written
	void Ui::runFileJob(FileJob* j)
	{
			auto fail = [&](const std::string& e) {
				std::lock_guard<std::mutex> l(j->m);
				j->error = e;
			};
			std::string target = j->dest + "/" + j->name;
			bool folder = isDirPath(j->src);
			// a move on one card: a rename, at once
			if (j->op == Ui::CLIP_MOVE && sameDevice(j->src, j->dest))
			{
				int r = renameNoReplace(j->src, target);
				if (r != 0) fail(std::string("It couldn't be moved: ") + strerror(r));
				j->finished = true;
				return;
			}
			TreeInfo info;
			if (!scanTree(j->src, info))
			{
				fail(info.bad.empty() ? "It can't be read" : "\"" + info.bad + "\" in it can't be copied (a link, or unreadable)");
				j->finished = true;
				return;
			}
			j->total = info.bytes;
			// room for it, with a little to spare (a card filled to its last byte misbehaves)
			struct statvfs vs;
			if (statvfs(j->dest.c_str(), &vs) == 0)
			{
				uint64_t room = (uint64_t)vs.f_bavail * vs.f_frsize, margin = 64ull << 20;
				if (info.bytes + margin > room)
				{
					fail("Not enough room: it needs " + sizeText(info.bytes) + ", " + sizeText(room > margin ? room - margin : 0) + " is free");
					j->finished = true;
					return;
				}
			}
#ifdef __linux__
			// a FAT card holds no file of 4 GB or more
			struct statfs fs;
			if (info.biggest >= (4ull << 30) && statfs(j->dest.c_str(), &fs) == 0 && fs.f_type == 0x4d44 /* MSDOS_SUPER_MAGIC */)
			{
				fail("That card (FAT) can't hold a file of 4 GB or more");
				j->finished = true;
				return;
			}
#endif
			// (tests: BP_COPY_SLOW_MS a piece, so a scripted run sees the progress)
			static const int slow = getenv("BP_COPY_SLOW_MS") ? atoi(getenv("BP_COPY_SLOW_MS")) : 0;
			int r = copyTree(j->src, target, [j](uint64_t add, const std::string& rel) {
				if (slow > 0) std::this_thread::sleep_for(std::chrono::milliseconds(slow));
				j->done += add;
				{
					std::lock_guard<std::mutex> l(j->m);
					j->current = rel;
				}
				return !j->cancel.load();
			});
			if (r == ECANCELED) fail("Cancelled: nothing was changed");
			else if (r != 0) fail(std::string("It couldn't be copied: ") + strerror(r));
			else if (j->op == Ui::CLIP_MOVE) removeTree(j->src);	// (the copy is on the card first)
			(void)folder;
			j->finished = true;
	}

	bool Ui::fileOpsOn(int row) const
	{
		// a file or folder inside a card's folder (not a place itself, not Home's, Recent's or the network's)
		if (row < 0 || row >= (int)browsePaths.size() || row >= (int)browseRows.size()) return false;
		if (browseDir.empty() || pseudoDir(browseDir) || isNetPath(browseDir)) return false;
		int id = browseRows[row].id;
		return (id == 1 || id == 2) && browsePaths[row] != ".." && browsePaths[row].rfind(browseDir + "/", 0) == 0;
	}

	void Ui::fileOpsAct(int what, const std::string& path, Player& p)
	{
		std::string name = nameOf(path);
		// (what plays can't be moved, renamed or deleted from under it)
		bool playing = p.playing() && (p.currentPath() == path || p.currentPath().rfind(path + "/", 0) == 0);
		switch (what)
		{
		case CTX_F_COPY:
		case CTX_F_MOVE:
			if (what == CTX_F_MOVE && playing) { p.osd("It's playing: stop it first", 2000); return; }
			clipOp = what == CTX_F_COPY ? CLIP_COPY : CLIP_MOVE;
			clipPath = path;
			ctxOpen = false;
			p.osd(std::string(clipOp == CLIP_COPY ? "Copy" : "Move") + ": open the folder it goes in, then SELECT", 2500);
			return;
		case CTX_F_RENAME:
			if (playing) { p.osd("It's playing: stop it first", 2000); return; }
			ctxOpen = false;
			fileRenamePath = path;
			fileNameNote.clear();
			openKeyboard(FILE_NAME, name);
			return;
		case CTX_F_DELETE:
			if (playing) { p.osd("It's playing: stop it first", 2000); return; }
			// twice: it's gone for good
			if (!ctxArmed) { ctxArmed = true; return; }
			ctxOpen = false;
			removeTree(path);
			{
				struct stat st;
				p.osd(lstat(path.c_str(), &st) == 0 ? "\"" + name + "\" couldn't be deleted" : "Deleted \"" + name + "\"", 2000);
			}
			{
				int c = cursor, s = scroll;
				enterFolder(browseDir, p);
				cursor = std::min(c, std::max(0, (int)browsePaths.size() - 1));
				scroll = s;
				fitList();
			}
			return;
		default: return;
		}
	}

	void Ui::fileRenameDone(Player& p)
	{
		fileNameSubmit = false;
		std::string name = trimmed(kbEdit.text);
		std::string dir = parentFolder(fileRenamePath);
		if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos)
		{
			fileNameNote = "Type a name (no /)";
			return;
		}
		if (name[0] == '.')
		{
			fileNameNote = "A name starting with a dot would be hidden";
			return;
		}
		if (name == nameOf(fileRenamePath))
		{
			kbOpen = false;
			kbPressed = -1;
			return;
		}
		int r = renameNoReplace(fileRenamePath, dir + "/" + name);
		if (r == EEXIST)
		{
			fileNameNote = "Something here already has that name";
			return;
		}
		if (r != 0)
		{
			fileNameNote = std::string("It couldn't be renamed: ") + strerror(r);
			return;
		}
		kbOpen = false;
		kbPressed = -1;
		fileNameNote.clear();
		enterFolder(browseDir, p);
		for (size_t i = 0; i < browsePaths.size(); i++)
			if (browsePaths[i] == dir + "/" + name) cursor = (int)i;
		fitList();
	}

	// ---- the clip: what Copy or Move holds while you go to its folder ----

	bool Ui::clipCanPaste() const
	{
		return clipOp != CLIP_NONE && page == Page::Browse && !browseDir.empty() && !pseudoDir(browseDir) &&
			!isNetPath(browseDir) && isDirPath(browseDir);
	}

	bool Ui::clipKey(Button b, Player& p)
	{
		if (clipOp == CLIP_NONE || page != Page::Browse || inMenu || ctxOpen || kbOpen) return false;
		if (b == Button::Select && clipCanPaste()) { filePaste(p); return true; }
		if (b == Button::X)
		{
			clipOp = CLIP_NONE;
			clipPath.clear();
			p.osd("Let go", 1000);
			return true;
		}
		return false;
	}

	bool Ui::clipPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (clipOp == CLIP_NONE || page != Page::Browse || inMenu || ctxOpen || kbOpen) return false;
		if (!clipBtn(0).hit(x, y) && !clipBtn(1).hit(x, y)) return false;
		if (pt.kind == Pointer::Up)
		{
			if (clipBtn(0).hit(x, y)) clipKey(Button::X, p);
			else if (clipCanPaste()) filePaste(p);
		}
		return true;
	}

	void Ui::filePaste(Player& p)
	{
		std::string src = clipPath, dest = browseDir;
		bool folder = isDirPath(src);
		struct stat st;
		if (lstat(src.c_str(), &st) != 0) { p.osd("It isn't there any more", 2000); clipOp = CLIP_NONE; return; }
		if (folder && (dest == src || dest.rfind(src + "/", 0) == 0)) { p.osd("A folder can't go into itself", 2000); return; }
		if (clipOp == CLIP_MOVE && parentFolder(src) == dest) { p.osd("It's already here", 1500); clipOp = CLIP_NONE; return; }
		if (clipOp == CLIP_MOVE && p.playing() && (p.currentPath() == src || p.currentPath().rfind(src + "/", 0) == 0))
		{
			p.osd("It's playing: stop it first", 2000);
			return;
		}
		auto j = std::make_shared<FileJob>();
		j->op = clipOp;
		j->src = src;
		j->dest = dest;
		j->name = freeName(dest, nameOf(src), folder);	// (a copy in its own folder: "name (2)")
		if (j->name.empty()) { p.osd("No free name for it here", 2000); return; }
		clipOp = CLIP_NONE;
		clipPath.clear();
		fileJob = j;
		fileJobPressed = -1;
		fprintf(stderr, "files: %s %s -> %s/%s\n", j->op == CLIP_COPY ? "copy" : "move", src.c_str(), dest.c_str(), j->name.c_str());
		FileJob* raw = j.get();
		j->t = std::thread([raw] { runFileJob(raw); });
	}

	void Ui::fileJobTick(Player& p)
	{
		if (!fileJob || !fileJob->finished) return;
		auto j = fileJob;
		fileJob.reset();
		if (j->t.joinable()) j->t.join();
		std::string err;
		{
			std::lock_guard<std::mutex> l(j->m);
			err = j->error;
		}
		bool copy = j->op == CLIP_COPY;
		fprintf(stderr, "files: %s %s: %s\n", copy ? "copy" : "move", j->name.c_str(), err.empty() ? "done" : err.c_str());
		p.osd(err.empty() ? std::string(copy ? "Copied \"" : "Moved \"") + j->name + "\"" : err, 2500);
		// the folder listed again, the cursor on what came
		if (page == Page::Browse && browseDir == j->dest)
		{
			enterFolder(browseDir, p);
			for (size_t i = 0; i < browsePaths.size(); i++)
				if (browsePaths[i] == j->dest + "/" + j->name) cursor = (int)i;
			fitList();
		}
	}

	bool Ui::fileJobKey(Button b)
	{
		// nothing else while it runs: B (or A on Cancel) asks it to stop, and it stops after the piece it's writing
		if (b == Button::B || b == Button::A) fileJob->cancel = true;
		return false;
	}

	bool Ui::fileJobPointer(const Pointer& pt, int x, int y)
	{
		if (pt.kind == Pointer::Down) fileJobPressed = fileJobCancelRect().hit(x, y) ? 0 : -1;
		else if (pt.kind == Pointer::Up)
		{
			if (fileJobPressed == 0 && fileJobCancelRect().hit(x, y)) fileJob->cancel = true;
			fileJobPressed = -1;
		}
		return false;
	}

	void Ui::drawFileJob()
	{
		if (!fileJob) return;
		FileJob& j = *fileJob;
		// over the whole panel: nothing under it can be used until it ends
		SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
		fill({0, 0, W, H}, SDL_Color{0, 0, 0, 200});
		Rect box{60, 110, W - 120, 270};
		drawBox(box, SURFACE, R_DIALOG);
		bool copy = j.op == CLIP_COPY;
		drawTextMid(std::string(copy ? "Copying" : "Moving") + " \"" + j.name + "\"", 1, TEXT, W / 2, 146, box.w - 40, 2);
		uint64_t done = j.done, total = j.total;
		double f = total ? std::min(1.0, done / (double)total) : 0;
		Rect track{box.x + 30, 186, box.w - 60, 14};
		drawLevel(track, (int)std::lround(track.w * f), TRACK, ACCENT);
		std::string amount = total ? std::to_string((int)std::lround(f * 100)) + "%  \xC2\xB7  " + sizeText(done) + " of " + sizeText(total)
			: "Measuring\xE2\x80\xA6";
		drawTextMid(amount, 0, TEXT, W / 2, 222, box.w - 40, 2);
		std::string cur;
		{
			std::lock_guard<std::mutex> l(j.m);
			cur = j.current;
		}
		drawTextMid(cur.empty() ? " " : cur, 0, DIM, W / 2, 252, box.w - 40, 2);
		Rect c = fileJobCancelRect();
		bool stopping = j.cancel;
		drawBox(c, fileJobPressed == 0 ? PRESSED : BUTTON, R_BUTTON);
		drawTextMid(stopping ? "Stopping\xE2\x80\xA6" : "Cancel (B)", 1, stopping ? DIM : TEXT, c.x + c.w / 2, c.y + c.h / 2, c.w - 10, 2);
	}

	void Ui::drawClipBar()
	{
		if (clipOp == CLIP_NONE || page != Page::Browse || inMenu) return;
		// along the foot: what's held, and Cancel / Paste here
		fill({0, 376, W, H - 376}, BG);
		fill({0, 376, W, 2}, RULE);
		std::string what = std::string(clipOp == CLIP_COPY ? "Copy" : "Move") + " \"" + nameOf(clipPath) + "\"";
		drawTextMid(what, 0, TEXT, 24, 400, W - 48);
		bool can = clipCanPaste();
		const char* labels[2] = {"Cancel (X)", clipOp == CLIP_COPY ? "Copy here (SELECT)" : "Move here (SELECT)"};
		for (int i = 0; i < 2; i++)
		{
			Rect b = clipBtn(i);
			drawBox(b, BUTTON, R_BUTTON);
			drawTextMid(labels[i], 0, i == 1 && !can ? DIM : TEXT, b.x + b.w / 2, b.y + b.h / 2, b.w - 10, 2);
		}
	}

	std::string Ui::fileJobSignature() const
	{
		if (!fileJob) return "";
		std::lock_guard<std::mutex> l(fileJob->m);
		uint64_t t = fileJob->total, d = fileJob->done;
		return "|job" + std::to_string(t ? d * 200 / t : 0) + " " + std::to_string(fileJobPressed) + (fileJob->cancel ? "c" : "") +
			fileJob->current;
	}

	// ---- Info ----

	void Ui::openFileInfo(const std::string& path, const std::string& title, std::vector<std::pair<std::string, std::string>> extra)
	{
		infoRows.clear();
		infoTitle = title.empty() ? nameOf(path) : title;
		struct stat st;
		bool there = stat(path.c_str(), &st) == 0;
		bool folder = there && S_ISDIR(st.st_mode);
		std::string name = nameOf(path);
		std::string kind = folder ? "Folder" : isVideoFile(name) ? "Video" : isAudioFile(name) ? "Song" : isPhotoFile(name) ? "Photo"
			: isComicName(name) ? "Comic" : isPdfName(name) ? "PDF" : isDocumentName(name) ? (strcasecmp(name.c_str() + name.size() - 5, ".epub") == 0 ? "Book (EPUB)" : "Text")
			: "File";
		size_t dot = name.rfind('.');
		if (!folder && kind == "File" && dot != std::string::npos && dot > 0) kind += " (." + name.substr(dot + 1) + ")";
		infoRows.push_back({"Name", name});
		infoRows.push_back({"Kind", kind});
		if (!there) infoRows.push_back({"Size", "It isn't there any more"});
		else if (folder)
		{
			// (everything in it counted; a big folder takes a moment, on the card)
			TreeInfo t;
			scanTree(path, t);
			infoRows.push_back({"Size", sizeText(t.bytes) + "  \xC2\xB7  " + std::to_string(t.files) + (t.files == 1 ? " file" : " files")});
		}
		else infoRows.push_back({"Size", sizeText((uint64_t)st.st_size)});
		double secs = 0;
		if (!folder && (isVideoFile(name) || isAudioFile(name)) && cachedDuration(path, secs) && secs > 0)
			infoRows.push_back({"Length", formatTime(secs)});
		for (auto& e : extra) infoRows.push_back(e);
		infoRows.push_back({"Where", whereOf(parentFolder(path))});
		if (there)
		{
			char when[64];
			struct tm tmv;
			time_t mt = st.st_mtime;
			localtime_r(&mt, &tmv);
			strftime(when, sizeof when, "%d %b %Y, %H:%M", &tmv);
			infoRows.push_back({"Modified", when});
		}
		infoRows.push_back({"Path", path});
		infoOpen = true;
	}

	void Ui::drawFileInfo()
	{
		if (!infoOpen) return;
		Rect box{32, 40, 576, 388};
		drawDialog(box, infoTitle);
		int y = box.y + 74;
		for (auto& r : infoRows)
		{
			drawTextMid(r.first, 0, DIM, box.x + 28, y, 110);
			// the path may be long: two lines, its middle kept out
			if (r.first == "Path")
			{
				auto lines = wrapText(r.second, 0, box.w - 168, 3);
				for (size_t i = 0; i < lines.size(); i++) drawTextMid(lines[i], 0, TEXT, box.x + 144, y + (int)i * 22, box.w - 168);
				y += 22 * (int)std::max<size_t>(1, lines.size()) + 12;
				continue;
			}
			drawTextMid(r.second, 0, TEXT, box.x + 144, y, box.w - 168);
			y += 36;
		}
		drawHints({{"A", "Close"}, {"B", "Close"}});
	}
}
