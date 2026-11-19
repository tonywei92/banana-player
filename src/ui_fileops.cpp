// Files' own file operations on the handheld: Copy, Move, Rename and Delete on a file or folder of the cards. A copy,
// or a move to the other card, runs on a thread of its own (file reads and writes only: it never touches the Player
// or the Ui; the Ui reads its progress each pass), and the panel shows it, with Cancel, until it ends. The copying
// itself is treecopy.h's: measured first, written out of sight, put in place last, so nothing half-copied is left.
#include "ui_internal.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"
#include "ui_docs.h"
#include "ui_manga.h"
#include "ui_kit.h"
#include "treecopy.h"
#include "pagebook.h"
#include "netpath.h"
#include "netstore.h"
#include "xfer.h"
#include <ctime>
#include <strings.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <thread>
#ifdef __linux__
#include <sys/vfs.h>
#endif

namespace bp
{
	// a network job's other kinds (a copy or a move is CLIP_COPY or CLIP_MOVE)
	enum { JOB_RENAME = 10, JOB_DELETE, JOB_FETCH };

	struct Ui::FileJob
	{
		int op = CLIP_COPY;
		std::string src, dest, name;
		// a network end (one or both): xfer.h does it. newName: Rename's; openAfter: a fetched document to open
		bool net = false;
		XferEnd srcEnd, destEnd;
		std::string newName, openAfter;
		// a group Copy / Move (the select bar's): every source, and what landed (for pathMoved), under m
		std::vector<std::string> srcs;
		std::vector<std::pair<std::string, std::string>> moved;
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
			if (j->net)
			{
				std::string err;
				if (j->op == JOB_RENAME) err = xferRename(j->srcEnd, j->newName);
				else if (j->op == JOB_DELETE) err = xferDelete(j->srcEnd, &j->cancel);
				else
				{
					XferProgress pr;
					pr.done = &j->done;
					pr.total = &j->total;
					pr.cancel = &j->cancel;
					pr.current = [j](const std::string& rel) {
						std::lock_guard<std::mutex> l(j->m);
						j->current = rel;
					};
					std::string name = j->name;
					err = xferCopy(j->srcEnd, j->destEnd, name, j->op == CLIP_MOVE, pr);
					std::lock_guard<std::mutex> l(j->m);
					j->name = name;
				}
				if (!err.empty()) fail(err);
				j->finished = true;
				return;
			}
			if (!j->srcs.empty())
			{
				// a group (the select bar's Copy / Move): measured first, then each file written out of sight;
				// a move's originals go only once every copy is complete, and a cancel takes back what was made
				uint64_t total = 0;
				for (const std::string& src : j->srcs)
				{
					TreeInfo info;
					if (!scanTree(src, info))
					{
						fail(info.bad.empty() ? "\"" + nameOf(src) + "\" can't be read" : "\"" + info.bad + "\" in it can't be copied (a link, or unreadable)");
						j->finished = true;
						return;
					}
					total += info.bytes;
				}
				j->total = total;
				struct statvfs vs;
				if (statvfs(j->dest.c_str(), &vs) == 0)
				{
					uint64_t room = (uint64_t)vs.f_bavail * vs.f_frsize, margin = 64ull << 20;
					if (total + margin > room)
					{
						fail("Not enough room: it needs " + sizeText(total) + ", " + sizeText(room > margin ? room - margin : 0) + " is free");
						j->finished = true;
						return;
					}
				}
				std::vector<std::string> made;
				std::string err;
				for (const std::string& src : j->srcs)
				{
					if (j->cancel.load()) break;
					bool folder = isDirPath(src);
					std::string name = freeName(j->dest, nameOf(src), folder);
					if (name.empty()) { err = "There's no free name for \"" + nameOf(src) + "\" in this folder."; break; }
					{
						std::lock_guard<std::mutex> l(j->m);
						j->name = name;
						j->current = name;
					}
					std::string target = j->dest + "/" + name;
					int r = copyTree(src, target, [j](uint64_t add, const std::string& rel) {
						j->done += add;
						{
							std::lock_guard<std::mutex> l(j->m);
							j->current = rel;
						}
						return !j->cancel.load();
					});
					if (r != 0)
					{
						err = r == ECANCELED ? "Cancelled: nothing was changed" : std::string("It couldn't be copied: ") + strerror(r);
						break;
					}
					made.push_back(target);
					std::lock_guard<std::mutex> l(j->m);
					j->moved.push_back({src, target});
				}
				if (!err.empty() || j->cancel.load())
				{
					// nothing half-copied stays: what was made goes, and a move's originals were never touched
					for (const std::string& target : made) removeTree(target);
					std::lock_guard<std::mutex> l(j->m);
					j->moved.clear();
					if (err.empty()) fail("Cancelled: nothing was changed");
					else fail(err);
				}
				else if (j->op == Ui::CLIP_MOVE)
				{
					for (const std::string& src : j->srcs) removeTree(src);
				}
				j->finished = true;
				return;
			}
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

	bool Ui::Browse::fileOpsOn(int row) const
	{
		// a file or folder inside a card's folder (not a place itself, not Home's, Recent's or the network's)
		if (row < 0 || row >= (int)browsePaths.size() || row >= (int)browseRows.size()) return false;
		if (browseDir.empty() || pseudoDir(browseDir) || (isNetPath(browseDir) && !netOpsHere())) return false;
		int id = browseRows[row].id;
		if (isNetPath(browseDir)) return (id == 1 || id == 2) && netParent(browsePaths[row]) == browseDir;
		return (id == 1 || id == 2) && browsePaths[row] != ".." && browsePaths[row].rfind(browseDir + "/", 0) == 0;
	}

	bool Ui::Browse::netOpsHere() const
	{
		NetPath np;
		if (!isNetPath(browseDir) || !parseNetPath(browseDir, np) || !ui.netStore) return false;
		const NetServer* sv = ui.netStore->find(np.server);
		return sv && (sv->type != NetType::Smb || np.path != "/");
	}

	bool Ui::Browse::netReadOnlySaid(const std::string& path, Player& p)
	{
		if (!isNetPath(path) || netWritable) return false;
		ui.alert("This folder is read only", "This account can\xE2\x80\x99t change anything in this network folder: you can copy from it, not into it.");
		return true;
	}

	void Ui::Browse::startNetJob(int op, const std::string& src, const std::string& dest, const std::string& name, Player& p)
	{
		auto end = [&](const std::string& path, XferEnd& e) {
			e = XferEnd();
			if (!isNetPath(path)) { e.path = path; return true; }
			NetPath np;
			const NetServer* sv = ui.netStore && parseNetPath(path, np) ? ui.netStore->find(np.server) : nullptr;
			if (!sv) return false;
			e.net = true;
			e.server = *sv;
			e.password = ui.netStore->password(*sv);
			e.path = np.path;
			return true;
		};
		auto j = std::make_shared<FileJob>();
		j->op = op;
		j->net = true;
		j->src = src;
		j->dest = dest;
		j->name = name;
		if (!end(src, j->srcEnd) || (!dest.empty() && !end(dest, j->destEnd))) { ui.alert("This connection is gone", "It was deleted. Add it again in Network.");; return; }
		ui.fileJob = j;
		ui.fileJobPressed = -1;
		fprintf(stderr, "files: network job %d %s -> %s/%s\n", op, src.c_str(), dest.c_str(), name.c_str());
		FileJob* raw = j.get();
		j->t = std::thread([raw] { runFileJob(raw); });
	}

	void Ui::Browse::netOpenCopy(const std::string& path, Player& p)
	{
		// the copy kept by the file's address: opened again at once when it's still there whole
		std::string name = nameOf(netDecode(path));
		uint32_t h = 2166136261u;
		for (unsigned char c : path) { h ^= c; h *= 16777619u; }
		char hex[12];
		snprintf(hex, sizeof hex, "%08x", h);
		std::string root = ui.stateDirPath + "/netdocs", dir = root + "/" + hex, local = dir + "/" + name;
		struct stat st;
		auto size = netSizes.find(path);
		if (stat(local.c_str(), &st) == 0 && size != netSizes.end() && size->second == (int64_t)st.st_size)
		{
			utimes(dir.c_str(), nullptr);
			ui.openFile(local, p);
			return;
		}
		if (ui.stateDirPath.empty()) { ui.alert("Can\xE2\x80\x99t open it", "There\xE2\x80\x99s no folder on the card to copy it to.");; return; }
		removeTree(dir);
		mkdir(root.c_str(), 0755);
		mkdir(dir.c_str(), 0755);
		// the copies of what was read before go, the oldest first, till this one fits beside them in 2 GB; a file of 2 GB
		// or more is let in alone (everything else goes), so the folder is never over 2 GB but for that one file
		const uint64_t cap = 2ull << 30;
		uint64_t coming = size != netSizes.end() && size->second > 0 ? (uint64_t)size->second : 0;
		trimCacheFolder(root, coming < cap ? cap - coming : 0, dir);
		startNetJob(JOB_FETCH, path, dir, name, p);
		if (ui.fileJob) ui.fileJob->openAfter = local;
	}

	void Ui::Browse::fileOpsAct(int what, const std::string& path, Player& p)
	{
		std::string name = nameOf(path);
		bool net = isNetPath(path);
		if (net) name = netBaseName(path);
		// a network folder the account can't write to: only Copy (and Info) are for it
		if (net && (what == CTX_F_MOVE || what == CTX_F_RENAME || what == CTX_F_DELETE) && netReadOnlySaid(path, p)) return;
		// (what plays can't be moved, renamed or deleted from under it)
		bool playing = p.playing() && (p.currentPath() == path || p.currentPath().rfind(path + "/", 0) == 0);
		switch (what)
		{
		case CTX_F_COPY:
		case CTX_F_MOVE:
			if (what == CTX_F_MOVE && playing) { ui.alert("It\xE2\x80\x99s playing", "Stop it first, then try again."); return; }
			clipOp = what == CTX_F_COPY ? CLIP_COPY : CLIP_MOVE;
			clipPath = path;
			clipPaths.clear();
			ui.ctxOpen = false;
			p.osd(std::string(clipOp == CLIP_COPY ? "Copy" : "Move") + ": open the folder it goes in, then SELECT", 2500);
			return;
		case CTX_F_RENAME:
			if (playing) { ui.alert("It\xE2\x80\x99s playing", "Stop it first, then try again."); return; }
			ui.ctxOpen = false;
			fileRenamePath = path;
			fileNameNote.clear();
			ui.openKeyboard(FILE_NAME, name);	// (a network name decoded: what the server shows)
			return;
		case CTX_F_DELETE:
			if (playing) { ui.alert("It\xE2\x80\x99s playing", "Stop it first, then try again."); return; }
			// twice: it's gone for good
			if (!ui.ctxArmed) { ui.ctxArmed = true; return; }
			ui.ctxOpen = false;
			if (net) { startNetJob(JOB_DELETE, path, "", name, p); return; }
			removeTree(path);
			{
				struct stat st;
				if (lstat(path.c_str(), &st) == 0) ui.alert("Couldn\xE2\x80\x99t delete it", "\"" + name + "\" (or something in it) couldn\xE2\x80\x99t be deleted. Is the card read only?");
				else p.osd("Deleted \"" + name + "\"", 2000);
			}
			{
				int c = ui.cursor, s = ui.scroll;
				enterFolder(browseDir, p);
				ui.cursor = std::min(c, std::max(0, (int)browsePaths.size() - 1));
				ui.scroll = s;
				fitList();
			}
			return;
		default: return;
		}
	}

	void Ui::Browse::fileRenameDone(Player& p)
	{
		fileNameSubmit = false;
		std::string name = trimmed(ui.kbEdit.text);
		bool net = isNetPath(fileRenamePath);
		std::string dir = net ? netParent(fileRenamePath) : parentFolder(fileRenamePath);
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
		if (name == (net ? netBaseName(fileRenamePath) : nameOf(fileRenamePath)))
		{
			ui.kbOpen = false;
			ui.kbPressed = -1;
			return;
		}
		// a network one: on the job's thread (the server answers when it does), the list read again after
		if (net)
		{
			for (const std::string& o : browsePaths)
				if (o == netJoin(dir, name)) { fileNameNote = "Something here already has that name"; return; }
			ui.kbOpen = false;
			ui.kbPressed = -1;
			fileNameNote.clear();
			startNetJob(JOB_RENAME, fileRenamePath, dir, name, p);
			if (ui.fileJob) ui.fileJob->newName = name;
			return;
		}
		int r = renameNoReplace(fileRenamePath, dir + "/" + name);
		if (r == 0) ui.pathMoved(fileRenamePath, dir + "/" + name, p);
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
		ui.kbOpen = false;
		ui.kbPressed = -1;
		fileNameNote.clear();
		enterFolder(browseDir, p);
		for (size_t i = 0; i < browsePaths.size(); i++)
			if (browsePaths[i] == dir + "/" + name) ui.cursor = (int)i;
		fitList();
	}

	// ---- the clip: what Copy or Move holds while you go to its folder ----

	bool Ui::Browse::clipCanPaste() const
	{
		if (clipOp == CLIP_NONE || ui.page != Page::Browse || browseDir.empty()) return false;
		if (isNetPath(browseDir)) return netOpsHere() && netWritable && !netLoading;
		return !pseudoDir(browseDir) && isDirPath(browseDir);
	}

	bool Ui::Browse::clipKey(Button b, Player& p)
	{
		if (clipOp == CLIP_NONE || ui.page != Page::Browse || ui.ctxOpen || ui.kbOpen) return false;
		if (b == Button::Select && clipCanPaste()) { filePaste(p); return true; }
		if (b == Button::X)
		{
			clipOp = CLIP_NONE;
			clipPath.clear();
			clipPaths.clear();
			p.osd("Let go", 1000);
			return true;
		}
		return false;
	}

	bool Ui::Browse::clipPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (clipOp == CLIP_NONE || ui.page != Page::Browse || ui.ctxOpen || ui.kbOpen) return false;
		lay::FilesClip l = lay::filesClipBar(lay::shell().body);
		if (!l.box.hit(x, y)) return false;
		if (pt.kind == Pointer::Up)
		{
			if (l.key[1].hit(x, y)) clipKey(Button::X, p);
			else if (l.key[0].hit(x, y) && clipCanPaste()) filePaste(p);
		}
		return true;
	}

	void Ui::Browse::filePaste(Player& p)
	{
		std::string dest = browseDir;
		std::vector<std::string> srcs = clipPaths.empty() ? std::vector<std::string>{clipPath} : clipPaths;
		if (srcs.empty() || srcs.front().empty()) return;
		auto letGo = [this] { clipOp = CLIP_NONE; clipPath.clear(); clipPaths.clear(); };
		if (cardOut(dest)) { ui.alert("The second card is out", "Put it back in, then try again.");; return; }
		bool group = srcs.size() > 1;
		if (group && (isNetPath(dest) || isNetPath(srcs.front())))
		{
			ui.alert("One at a time", "A server takes one file at a time: pick them one by one.");
			return;
		}
		// a move of what is already here is nothing; the others still go
		if (clipOp == CLIP_MOVE)
			srcs.erase(std::remove_if(srcs.begin(), srcs.end(), [&](const std::string& s) { return parentFolder(s) == dest || (isNetPath(s) && netParent(s) == dest); }), srcs.end());
		if (srcs.empty()) { p.osd("It's already here", 1500); letGo(); return; }
		for (const std::string& src : srcs)
		{
			bool folder = isDirPath(src);
			struct stat st;
			if (!isNetPath(src) && lstat(src.c_str(), &st) != 0)
			{
				ui.alert("It isn\xE2\x80\x99t there any more", "It was moved or deleted meanwhile.");
				letGo();
				return;
			}
			if (folder && (dest == src || dest.rfind(src + "/", 0) == 0)) { ui.alert("Can\xE2\x80\x99t put it there", "A folder can\xE2\x80\x99t go into itself.");; return; }
			if (clipOp == CLIP_MOVE && p.playing() && (p.currentPath() == src || p.currentPath().rfind(src + "/", 0) == 0))
			{
				ui.alert("It\xE2\x80\x99s playing", "Stop it first, then try again.");
				return;
			}
		}
		std::string src = srcs.front();
		// a network folder at one end (or both), one file: xfer.h's job
		if (isNetPath(src) || isNetPath(dest))
		{
			bool srcNet = isNetPath(src);
			int op = clipOp;
			letGo();
			startNetJob(op, src, dest, srcNet ? netBaseName(src) : nameOf(src), p);
			return;
		}
		bool folder = isDirPath(src);
		auto j = std::make_shared<FileJob>();
		j->op = clipOp;
		j->src = src;
		j->dest = dest;
		j->srcs = group ? srcs : std::vector<std::string>();
		j->name = freeName(dest, nameOf(src), folder);	// (a copy in its own folder: "name (2)")
		if (j->name.empty()) { ui.alert("Can\xE2\x80\x99t put it here", "There\xE2\x80\x99s no free name for it in this folder.");; return; }
		letGo();
		ui.fileJob = j;
		ui.fileJobPressed = -1;
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
		// a network job's own ends: a fetched document opened, a rename or a delete said, the folder listed again
		if (j->net && j->op >= JOB_RENAME)
		{
			fprintf(stderr, "files: network job %d on %s: %s\n", j->op, j->src.c_str(), err.empty() ? "done" : err.c_str());
			if (j->op == JOB_FETCH)
			{
				if (err.empty()) openFile(j->openAfter, p);
				else if (err.rfind("Cancelled", 0) == 0) p.osd("Cancelled", 1200);
				else alert("Couldn\xE2\x80\x99t open it", err);
				return;
			}
			std::string renamed = j->op == JOB_RENAME ? netJoin(j->dest, j->newName) : "";
			if (err.empty() && j->op == JOB_RENAME) pathMoved(j->src, renamed, p);
			if (!err.empty()) alert(j->op == JOB_RENAME ? "Couldn\xE2\x80\x99t rename it" : "Couldn\xE2\x80\x99t delete it", err);
			else p.osd(j->op == JOB_RENAME ? "Renamed to \"" + j->newName + "\"" : "Deleted \"" + j->name + "\"", 2500);
			if (page == Page::Browse && isNetPath(browse->browseDir))
			{
				int c = cursor, s = scroll;
				browse->netSelect = renamed;
				browse->enterFolder(browse->browseDir, p);
				if (renamed.empty()) { cursor = c; scroll = s; }
			}
			return;
		}
		bool copy = j->op == CLIP_COPY;
		if (!j->srcs.empty())
		{
			// a group: each landing follows its place, a playlist or a document
			std::vector<std::pair<std::string, std::string>> moved;
			{
				std::lock_guard<std::mutex> l(j->m);
				moved.swap(j->moved);
			}
			for (auto& m : moved) pathMoved(m.first, m.second, p);
			fprintf(stderr, "files: %s %zu files: %s\n", copy ? "copy" : "move", j->srcs.size(), err.empty() ? "done" : err.c_str());
			if (err.empty()) p.osd(std::string(copy ? "Copied " : "Moved ") + std::to_string(moved.size()) + (moved.size() == 1 ? " file" : " files"), 2500);
			else if (err.rfind("Cancelled", 0) == 0) p.osd(err, 2000);
			else alert(copy ? "Couldn\xE2\x80\x99t copy them" : "Couldn\xE2\x80\x99t move them", err);
			if (page == Page::Browse && browse->browseDir == j->dest)
			{
				std::string landed = moved.empty() ? std::string() : moved.front().second;
				browse->enterFolder(browse->browseDir, p);
				for (size_t i = 0; i < browse->browsePaths.size() && !landed.empty(); i++)
					if (browse->browsePaths[i] == landed) cursor = (int)i;
				browse->fitList();
			}
			return;
		}
		fprintf(stderr, "files: %s %s: %s\n", copy ? "copy" : "move", j->name.c_str(), err.empty() ? "done" : err.c_str());
		// a move: what was kept of it goes along (a copy is a new file: nothing follows it)
		std::string landed = isNetPath(j->dest) ? netJoin(j->dest, j->name) : j->dest + "/" + j->name;
		if (!copy && err.empty()) pathMoved(j->src, landed, p);
		if (err.empty()) p.osd(std::string(copy ? "Copied \"" : "Moved \"") + j->name + "\"", 2500);
		else if (err.rfind("Cancelled", 0) == 0) p.osd(err, 2000);
		else alert(copy ? "Couldn\xE2\x80\x99t copy it" : "Couldn\xE2\x80\x99t move it", err);
		// the folder listed again, the cursor on what came
		if (page == Page::Browse && browse->browseDir == j->dest)
		{
			if (isNetPath(browse->browseDir)) browse->netSelect = landed;	// (listed again on the network's worker: the cursor then)
			browse->enterFolder(browse->browseDir, p);
			for (size_t i = 0; i < browse->browsePaths.size(); i++)
				if (browse->browsePaths[i] == landed) cursor = (int)i;
			browse->fitList();
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
		Rect key = lay::busySheet().key;
		if (pt.kind == Pointer::Down) fileJobPressed = key.hit(x, y) ? 0 : -1;
		else if (pt.kind == Pointer::Up)
		{
			if (fileJobPressed == 0 && key.hit(x, y)) fileJob->cancel = true;
			fileJobPressed = -1;
		}
		return false;
	}

	void Ui::drawFileJob()
	{
		if (!fileJob) return;
		FileJob& j = *fileJob;
		bool copy = j.op == CLIP_COPY;
		std::string jname, cur;
		{
			std::lock_guard<std::mutex> l(j.m);
			jname = j.name;
			cur = j.current;
		}
		const char* verb = j.op == JOB_RENAME ? "Renaming" : j.op == JOB_DELETE ? "Deleting" : j.op == JOB_FETCH ? "Opening" : copy ? "Copying" : "Moving";
		std::string title = verb;
		if ((j.op == CLIP_COPY || j.op == CLIP_MOVE) && !j.dest.empty()) title += " to " + (isNetPath(j.dest) ? std::string("the server") : baseName(j.dest));
		if (!j.srcs.empty()) title += " \xC2\xB7 " + std::to_string(j.srcs.size()) + (j.srcs.size() == 1 ? " file" : " files");
		uint64_t done = j.done, total = j.total;
		double share = total ? std::min(1.0, done / (double)total) : -1;
		std::string left = total ? sizeText(done) + " of " + sizeText(total) + " \xC2\xB7 " + std::to_string((int)std::lround(share * 100)) + "%"
			: j.op == JOB_RENAME || j.op == JOB_DELETE ? "Asking the server\xE2\x80\xA6" : "Measuring\xE2\x80\xA6";
		if (j.op == JOB_FETCH)
		{
			const NetServer& sv = j.srcEnd.server;
			std::string from = !sv.host.empty() ? sv.host : sv.name;
			left = "Getting it from " + (from.empty() ? std::string("the server") : from) + "\xE2\x80\xA6" +
				(total ? "  \xC2\xB7  " + std::to_string((int)std::lround(share * 100)) + "%" : std::string());
		}
		Kit::BusySpec b;
		b.title = title;
		b.file = j.srcs.empty() || j.op == JOB_FETCH ? (j.op == JOB_FETCH ? jname : (cur.empty() ? jname : cur)) : (cur.empty() ? jname : cur);
		b.left = left;
		b.share = share;
		b.glyph = j.op == JOB_RENAME ? &G_PENCIL : j.op == JOB_DELETE ? &G_TRASH : j.op == JOB_FETCH ? &G_DOWNLOAD : copy ? &G_PAGE : &G_MOVE;
		b.tick = (int)(SDL_GetTicks() / 110 % 8);
		b.key = j.cancel ? "Stopping\xE2\x80\xA6" : "Cancel";
		b.cap = "B";
		b.pressed = fileJobPressed == 0;
		kit->busySheet(lay::busySheet(), b);
	}

	void Ui::Browse::drawClipBar()
	{
		if (clipOp == CLIP_NONE || ui.page != Page::Browse) return;
		// the redesign's clip bar (a sheet with a yellow edge): what is held and its size, Put here and Cancel
		std::vector<std::string> paths = clipPaths.empty() ? std::vector<std::string>{clipPath} : clipPaths;
		uint64_t bytes = 0;
		for (const std::string& p : paths) bytes += (uint64_t)std::max<long long>(0, fileSize(p));
		bool can = clipCanPaste();
		lay::FilesClip l = lay::filesClipBar(lay::shell().body);
		Kit::Clip c;
		c.what = paths.size() == 1 ? std::string(clipOp == CLIP_COPY ? "Copy" : "Move") + " \"" + nameOf(paths.front()) + "\""
			: std::to_string(paths.size()) + " files held";
		c.size = bytes ? sizeText(bytes) : "";
		c.put = {clipOp == CLIP_COPY ? &G_PAGE : &G_MOVE, clipOp == CLIP_COPY ? "Copy here" : "Move here", "SELECT", Kit::Tone::Normal, false, !can, true};
		c.cancel = {nullptr, "Cancel", "X", Kit::Tone::Normal, false, false, false};
		lay::ClipBar cb;
		cb.box = l.box;
		cb.text = l.text;
		cb.key[0] = l.key[0];
		cb.key[1] = l.key[1];
		ui.kit->clipBar(cb, c);
	}

	std::string Ui::fileJobSignature() const
	{
		if (!fileJob) return "";
		std::lock_guard<std::mutex> l(fileJob->m);
		uint64_t t = fileJob->total, d = fileJob->done;
		return "|job" + std::to_string(t ? d * 200 / t : 0) + " " + std::to_string(fileJobPressed) + (fileJob->cancel ? "c" : "") +
			fileJob->current;
	}

	// ---- the delete question: the redesign's Confirm sheet (docs/redesign: delete) ----

	struct Ui::Browse::FileDeleteAsk : Ui::Overlay
	{
		std::vector<std::string> paths;
		int focus = 0, pressed = -1;
		FileDeleteAsk(Ui& u, std::vector<std::string> at) : Overlay(u), paths(std::move(at)) {}
		const char* name() const override { return "filedelete"; }

		std::string title() const
		{
			if (paths.size() == 1) return "Delete \"" + nameOf(paths.front()) + "\"?";
			return "Delete " + std::to_string(paths.size()) + " files?";
		}
		std::string text() const
		{
			uint64_t bytes = 0;
			for (const std::string& path : paths) bytes += (uint64_t)std::max<long long>(0, fileSize(path));
			std::string s = paths.size() == 1 ? "It is removed from the card for good." : "They are removed from the card for good.";
			if (bytes > 0) s += " " + sizeText(bytes) + ".";
			s += " This can\xE2\x80\x99t be undone.";
			return s;
		}
		std::vector<std::string> lines() const { return ui.kit->sheetText(text()); }
		void act(Player& p)
		{
			for (const std::string& path : paths)
				if (p.playing() && (p.currentPath() == path || p.currentPath().rfind(path + "/", 0) == 0))
				{
					close();
					ui.alert("It\xE2\x80\x99s playing", "Stop it first, then try again.");
					return;
				}
			int done = 0;
			for (const std::string& path : paths)
			{
				removeTree(path);
				struct stat st;
				if (lstat(path.c_str(), &st) != 0) done++;
			}
			close();
			ui.browse->filesSelEnd(p);
			if (ui.page == Page::Browse && ui.browse->filesPage()) ui.browse->filesRefresh(p);
			if (done == (int)paths.size())
				p.osd("Deleted " + std::to_string(done) + (done == 1 ? " file" : " files"), 2000);
			else if (done == 0) ui.alert("Couldn\xE2\x80\x99t delete it", "Is the card read only?");
			else ui.alert("Not all of it was deleted", std::to_string(done) + " of " + std::to_string(paths.size()) + " were removed.");
		}
		bool key(Button b, Player& p) override
		{
			switch (b)
			{
			case Button::Left: case Button::Right: focus = 1 - focus; break;
			case Button::A:
				if (focus == 1) act(p);
				else close();
				break;
			case Button::B: close(); break;
			default: break;
			}
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			lay::ConfirmSheet l = lay::confirmSheet((int)lines().size(), false);
			if (pt.kind == Pointer::Down)
			{
				pressed = -1;
				if (!l.sheet.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
				if (l.safe.hit(x, y)) pressed = 0;
				else if (l.action.hit(x, y)) pressed = 1;
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int at = pressed;
			pressed = -1;
			if (at == 0 && l.safe.hit(x, y)) { close(); return false; }
			if (at == 1 && l.action.hit(x, y)) { focus = 1; act(p); }
			return false;
		}
		void draw(const Player&) override
		{
			auto ls = lines();
			lay::ConfirmSheet l = lay::confirmSheet((int)ls.size(), false);
			Kit::ConfirmSpec c;
			c.title = title();
			c.text = text();
			c.safe = "Cancel";
			c.action = "Delete";
			c.danger = true;
			c.focus = focus;
			c.pressed = pressed;
			ui.kit->confirmSheet(l, c, ls);
		}
		std::string signature(const Player&) const override
		{
			return std::to_string(focus) + "," + std::to_string(pressed) + "," + std::to_string(paths.size()) + title();
		}
	};

	void Ui::Browse::filesDeleteAsk(std::vector<std::string> paths, Player& p)
	{
		if (paths.empty()) return;
		for (const std::string& path : paths)
			if (p.playing() && (p.currentPath() == path || p.currentPath().rfind(path + "/", 0) == 0))
			{
				ui.alert("It\xE2\x80\x99s playing", "Stop it first, then try again.");
				return;
			}
		ui.pushOverlay(std::make_unique<FileDeleteAsk>(ui, std::move(paths)));
	}

	// ---- Info: one scrolling page (docs/redesign: info) ----

	struct Ui::Browse::FileInfoSheet : Ui::Overlay
	{
		int scroll = 0, pressed = -1, touchY = 0, startScroll = 0;
		bool scrolled = false;
		explicit FileInfoSheet(Ui& u) : Overlay(u) {}
		const char* name() const override { return "fileinfo"; }
		int total() const { return 1 + (int)ui.infoRows.size(); }
		int shown() const { return lay::fileInfoShown(lay::shell().body); }
		void clampScroll() { scroll = std::clamp(scroll, 0, std::max(0, total() - shown())); }

		bool key(Button b, Player& p) override
		{
			(void)p;
			switch (b)
			{
			case Button::Up: scroll--; break;
			case Button::Down: scroll++; break;
			case Button::Left: scroll -= shown(); break;
			case Button::Right: scroll += shown(); break;
			case Button::A: case Button::B: close(); break;
			default: break;
			}
			clampScroll();
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			(void)p;
			lay::Shell s = lay::shell();
			lay::FilesHeader h = lay::filesHeader(s.header);
			if (pt.kind == Pointer::Down)
			{
				pressed = -1;
				scrolled = false;
				touchY = y;
				startScroll = scroll;
				if (h.backHit.hit(x, y)) pressed = 200;
				return false;
			}
			if (pt.kind == Pointer::Move)
			{
				if (std::abs(y - touchY) > 12)
				{
					scrolled = true;
					scroll = startScroll + (int)std::lround((touchY - y) / (double)lay::FILE_INFO_ROW);
					clampScroll();
				}
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int at = pressed;
			pressed = -1;
			if (at == 200 && h.backHit.hit(x, y)) close();
			else if (!scrolled && at < 0) close();
			return false;
		}
		void draw(const Player&) override
		{
			lay::Shell s = lay::shell();
			Kit& k = *ui.kit;
			ui.fill({s.main.x, 0, s.main.w, H}, BG);
			lay::FilesHeader h = lay::filesHeader(s.header);
			k.iconKey(h.back, G_BACK, pressed == 200, 2);
			int cy = h.title.y + h.title.h / 2;
			ui.drawTextMid("Info", F_P28, TEXT, h.title.x, cy, std::max(40, h.title.w - 10));
			std::string crumb = ui.infoTitle;
			if (k.textW(crumb, F_I12) > h.crumb.w) crumb = ui.elideLeft(crumb, F_I12, h.crumb.w);
			ui.drawTextMid(crumb, F_I12, DIM, h.crumb.x, h.crumb.y + h.crumb.h / 2, h.crumb.w);
			// one group heading, then the rows; scrolled
			int shown = lay::fileInfoShown(s.body);
			for (int k2 = 0; k2 < shown; k2++)
			{
				int line = scroll + k2;
				if (line >= total()) break;
				lay::Box r = lay::fileInfoRow(s.body, k2);
				int ry = r.y + r.h / 2;
				if (line == 0)
				{
					ui.drawTextMid("File", F_I11B, ACCENT, r.x, ry, r.w);
					continue;
				}
				const auto& row = ui.infoRows[line - 1];
				ui.drawTextMid(row.first, F_I12, DIM, r.x, ry, 120);
				ui.drawTextMid(row.second, F_I12, TEXT, r.x + 132, ry, std::max(1, r.w - 132));
			}
			k.hints({{"UP/DOWN", "Scroll"}, {"A/B", "Back"}});
		}
		std::string signature(const Player&) const override
		{
			return std::to_string(scroll) + "," + std::to_string(pressed) + "|" + ui.infoTitle + std::to_string(total());
		}
	};

	void Ui::openFileInfo(const std::string& path, const std::string& title, std::vector<std::pair<std::string, std::string>> extra)
	{
		infoRows.clear();
		if (isNetPath(path))
		{
			// a network file or folder: what its folder's listing said
			std::string name = netBaseName(path);
			infoTitle = title.empty() ? name : title;
			bool folder = browse->netSizes.find(path) == browse->netSizes.end();
			std::string kind = folder ? "Folder" : isVideoFile(name) ? "Video" : isAudioFile(name) ? "Song" : isPhotoFile(name) ? "Photo"
				: isComicName(name) ? "Comic" : isPdfName(name) ? "PDF" : isDocumentName(name) ? (strcasecmp(name.c_str() + name.size() - 5, ".epub") == 0 ? "Book (EPUB)" : "Text") : "File";
			infoRows.push_back({"Name", name});
			infoRows.push_back({"Kind", kind});
			if (!folder && browse->netSizes[path] >= 0) infoRows.push_back({"Size", sizeText((uint64_t)browse->netSizes[path])});
			for (auto& e : extra) infoRows.push_back(e);
			infoRows.push_back({"Where", whereOf(netParent(path))});
			infoRows.push_back({"Access", browse->netWritable ? "Read and write" : "Read only (for this account)"});
			NetPath np;
			if (parseNetPath(path, np)) infoRows.push_back({"Path", np.path});
			pushOverlay(std::make_unique<Browse::FileInfoSheet>(*this));
			return;
		}
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
		pushOverlay(std::make_unique<Browse::FileInfoSheet>(*this));
	}

	void Ui::drawFileInfo()
	{
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

	void Ui::pathMoved(const std::string& from, const std::string& to, Player& p)
	{
		if (from.empty() || to.empty() || from == to) return;
		int moved = 0;
		p.pathsMoved(from, to);
		// playlists and Liked (the edits waiting first, so none is written over the old paths)
		if (browse->playlists->plStore)
		{
			browse->playlists->plFlush(p);
			for (const auto& info : browse->playlists->plStore->list())
			{
				Playlist pl;
				int skipped = 0;
				if (!browse->playlists->plStore->load(info.file, pl, skipped)) continue;
				bool any = false;
				for (auto& e : pl.entries)
				{
					std::string n;
					if (movedPath(e.path, from, to, n)) { e.path = n; any = true; }
				}
				if (any && browse->playlists->plStore->save(pl)) moved++;
			}
		}
		// documents: their places and bookmarks
		docs->pathMoved(from, to);
		// the manga reader's series of the card ("local:<folder or file>") and their chapters' places
		if (!manga->mangaStore)
		{
			manga->mangaStore = std::make_unique<MangaStore>();
			manga->mangaStore->load(stateDirPath);
		}
		for (const std::string& id : manga->mangaStore->seriesIds())
		{
			std::string n;
			if (id.rfind("local:", 0) != 0 || !movedPath(id.substr(6), from, to, n)) continue;
			std::unordered_map<std::string, std::string> chapters;
			for (const ChapterProgress& c : manga->mangaStore->progressOf(id))
			{
				std::string cn;
				if (movedPath(c.chapterId, from, to, cn)) chapters[c.chapterId] = cn;
			}
			manga->mangaStore->moveSeries(id, "local:" + n);
			if (!chapters.empty()) manga->mangaStore->moveChapters(chapters);
			moved++;
		}
		manga->mangaCacheValid = false;
		manga->mangaSave();
		fprintf(stderr, "files: %s moved to %s: its places, playlists and series follow (%d stores)\n", from.c_str(), to.c_str(), moved);
		// Home's Recent shows the new names
		if (browse->browseDir == HOMEDIR || browse->browseDir == RECENT) browse->homeView->recentEdited(p);
	}
}
