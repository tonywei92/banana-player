// The cards' media index: the folders, the walk, the grouping and the query (see mediaindex.h, and
// docs/redesign/components.md › "Search and Music: the index"). File-system reads live here too — the walk is the
// one part that must touch the card, so it is this file's, driven from whatever thread called it (the worker's).
#include "mediaindex.h"
#include "browser.h"
#include "pagebook.h"
#include "playlist.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <set>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		std::string lowerAscii(std::string s)
		{
			for (auto& c : s) c = (char)tolower((unsigned char)c);
			return s;
		}

		bool isDir(const std::string& p)
		{
			struct stat st;
			return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}

		// the second card is in when something is mounted there, as the browser's places (browser.cpp)
		bool mounted(const std::string& p)
		{
			std::ifstream m("/proc/mounts");
			std::string dev, at;
			while (m >> dev >> at)
			{
				if (at == p) return true;
				m.ignore(4096, '\n');
			}
			return false;
		}

		// the folder's own name (the path's last part)
		std::string baseName(const std::string& p)
		{
			return p.substr(p.rfind('/') + 1);
		}

		// the name a row shows: the file's own, less its extension
		std::string baseNameLessExt(const std::string& path)
		{
			std::string n = path.substr(path.rfind('/') + 1);
			size_t dot = n.rfind('.');
			if (dot != std::string::npos && dot > 0) n.resize(dot);
			return n;
		}

		// the folder the file sits in, by its own name (an untagged file's album)
		std::string folderName(const std::string& path)
		{
			std::string p = parentFolder(path);
			return p.substr(p.rfind('/') + 1);
		}

		// ---- foldText: the tables

		struct FoldRange
		{
			uint32_t from, to;		// inclusive
			const char* plain;		// the letters it stands for (the Latin base, mostly)
		};

		// Latin-1's own accents and Latin Extended-A (0xC0–0x17F), and the Vietnamese block (0x1EA0–0x1EF9), each
		// range one base letter or digraph; everything else in between is rare enough to match itself
		const FoldRange FOLD[] = {
			{0xC0, 0xC5, "a"}, {0xC6, 0xC6, "ae"}, {0xC7, 0xC7, "c"}, {0xC8, 0xCB, "e"}, {0xCC, 0xCF, "i"},
			{0xD0, 0xD0, "d"}, {0xD1, 0xD1, "n"}, {0xD2, 0xD6, "o"}, {0xD8, 0xD8, "o"}, {0xD9, 0xDC, "u"},
			{0xDD, 0xDD, "y"}, {0xDE, 0xDE, "th"}, {0xDF, 0xDF, "ss"},
			{0xE0, 0xE5, "a"}, {0xE6, 0xE6, "ae"}, {0xE7, 0xE7, "c"}, {0xE8, 0xEB, "e"}, {0xEC, 0xEF, "i"},
			{0xF0, 0xF0, "d"}, {0xF1, 0xF1, "n"}, {0xF2, 0xF6, "o"}, {0xF8, 0xF8, "o"}, {0xF9, 0xFC, "u"},
			{0xFD, 0xFF, "y"}, {0xFE, 0xFE, "th"},
			{0x100, 0x105, "a"}, {0x106, 0x10D, "c"}, {0x10E, 0x111, "d"}, {0x112, 0x11B, "e"},
			{0x11C, 0x123, "g"}, {0x124, 0x127, "h"}, {0x128, 0x131, "i"}, {0x132, 0x133, "ij"},
			{0x134, 0x135, "j"}, {0x136, 0x138, "k"}, {0x139, 0x142, "l"}, {0x143, 0x14B, "n"},
			{0x14C, 0x151, "o"}, {0x152, 0x153, "oe"}, {0x154, 0x159, "r"}, {0x15A, 0x161, "s"},
			{0x162, 0x167, "t"}, {0x168, 0x173, "u"}, {0x174, 0x175, "w"}, {0x176, 0x178, "y"},
			{0x179, 0x17E, "z"}, {0x17F, 0x17F, "s"},
			{0x1EA0, 0x1EB9, "a"}, {0x1EBA, 0x1EC7, "e"}, {0x1EC8, 0x1ECD, "i"}, {0x1ECE, 0x1EE3, "o"},
			{0x1EE4, 0x1EF1, "u"}, {0x1EF2, 0x1EF9, "y"},
		};

		// one UTF-8 code point at at: the bytes, or the one byte as it is when it isn't UTF-8 there
		uint32_t decode(const std::string& s, size_t& at)
		{
			unsigned char b0 = (unsigned char)s[at];
			if (b0 < 0x80) { at++; return b0; }
			int extra = b0 >= 0xF0 ? 3 : b0 >= 0xE0 ? 2 : b0 >= 0xC0 ? 1 : -1;
			uint32_t cp = b0 >= 0xF0 ? b0 & 0x07 : b0 >= 0xE0 ? b0 & 0x0F : b0 & 0x1F;
			if (extra < 0 || at + extra >= s.size())
			{
				at++;	// (not UTF-8 here: the byte matches itself)
				return b0;
			}
			for (int i = 1; i <= extra; i++)
			{
				unsigned char b = (unsigned char)s[at + (size_t)i];
				if ((b & 0xC0) != 0x80)
				{
					at++;
					return b0;
				}
				cp = (cp << 6) | (b & 0x3F);
			}
			at += (size_t)extra + 1;
			return cp;
		}

		void appendPlain(std::string& out, uint32_t cp)
		{
			// Cyrillic and Greek lower in their own case; a combining mark (it only decorates the letter before it)
			// goes; anything else stands as itself
			if (cp >= 0x400 && cp <= 0x40F) cp += 0x50;
			else if (cp >= 0x410 && cp <= 0x42F) cp += 0x20;
			else if (cp >= 0x460 && cp <= 0x52F && (cp & 1) == 0) cp++;
			else if (cp == 0x386) cp = 0x3AC;
			else if (cp >= 0x388 && cp <= 0x38A) cp += 0x25;
			else if (cp == 0x38C) cp = 0x3CC;
			else if (cp >= 0x38E && cp <= 0x38F) cp += 0x3F;
			else if (cp >= 0x391 && cp <= 0x3A1) cp += 0x20;
			else if (cp >= 0x3A3 && cp <= 0x3AB) cp += 0x20;
			else if (cp >= 0x300 && cp <= 0x36F) return;
			// back to UTF-8
			if (cp < 0x80) out += (char)cp;
			else if (cp < 0x800)
			{
				out += (char)(0xC0 | (cp >> 6));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000)
			{
				out += (char)(0xE0 | (cp >> 12));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
			else
			{
				out += (char)(0xF0 | (cp >> 18));
				out += (char)(0x80 | ((cp >> 12) & 0x3F));
				out += (char)(0x80 | ((cp >> 6) & 0x3F));
				out += (char)(0x80 | (cp & 0x3F));
			}
		}
	}

	// ---- the folders

	bool isMusicFolderName(const std::string& name)
	{
		std::string n = lowerAscii(name);
		return n == "music" || n == "musics";
	}

	bool isVideosFolderName(const std::string& name)
	{
		std::string n = lowerAscii(name);
		return n == "videos" || n == "movies";
	}

	// the cards as the index sees them, defined below mediaFolders (its own first half, so the two can never disagree)
	static std::vector<std::string> indexCards();

	MediaFolders mediaFolders()
	{
		MediaFolders out;
		std::vector<std::string> cards = indexCards();
		for (auto& card : cards)
		{
			// every such folder at the top, all as one (a card may have Music and Musics, or Videos twice)
			if (DIR* d = opendir(card.c_str()))
			{
				std::string prefix = card == "/" ? "/" : card + "/";
				while (dirent* e = readdir(d))
				{
					if (e->d_name[0] == '.') continue;
					bool dir = e->d_type == DT_DIR;
					if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK) dir = isDir(prefix + e->d_name);
					if (!dir) continue;
					std::string path = prefix + e->d_name;
					if (isMusicFolderName(e->d_name)) out.music.push_back(path);
					else if (isVideosFolderName(e->d_name)) out.videos.push_back(path);
				}
				closedir(d);
			}
		}
		// BananaOS's own, beside the cards' folders (a computer has none of these)
		if (isDir("/data/roms/music")) out.music.push_back("/data/roms/music");
		if (isDir("/data/roms/videos")) out.videos.push_back("/data/roms/videos");
		return out;
	}

	// the cards as the index sees them (mediaFolders' own first half, so the two can never disagree)
	static std::vector<std::string> indexCards()
	{
		std::vector<std::string> cards;
		// BP_INDEX settles the index alone (a test that wants it pointed elsewhere than the browser's places);
		// without it the browser's own test roots stand in, so one variable walks both
		const char* env = getenv("BP_INDEX");
		if (!env || !*env) env = getenv("BP_BROWSE");
		if (env && *env)
		{
			std::string list = env;
			size_t a = 0;
			while (a <= list.size())
			{
				size_t b = list.find(':', a);
				if (b == std::string::npos) b = list.size();
				std::string p = list.substr(a, b - a);
				if (!p.empty() && isDir(p)) cards.push_back(p);
				a = b + 1;
			}
		}
		else if (isDir("/data/banana"))
		{
			cards.push_back("/data");
			if (mounted("/run/sd2")) cards.push_back("/run/sd2");
		}
		return cards;
	}

	std::vector<std::string> mediaCards() { return indexCards(); }

	std::string walkCardName(int card)
	{
		if (card < 0) return "";
		return card == 0 ? "the card" : "the second card";
	}

	std::string mediaFromWords(const std::vector<std::string>& folders)
	{
		if (folders.empty()) return "";
		// one card's folders or both: the folders' card roots (a roms folder is the main card's)
		std::set<std::string> on;
		std::vector<std::string> cards = indexCards();
		for (auto& f : folders)
			for (auto& c : cards)
				if (f == c || f.compare(0, c.size() + 1, c + "/") == 0) { on.insert(c); break; }
		std::string where = on.size() > 1 ? " on both cards" : " on this card";
		// three or more are counted, never cut short ("From 3 folders on both cards"); the Sources sheet lists them
		if (folders.size() >= 3) return std::to_string(folders.size()) + " folders" + where;
		// the kinds the folders' names stand for, in a fixed order, each once ("Musics" is Music)
		static const struct { const char* plain; const char* word; } KIND[] = {
			{"music", "Music"}, {"videos", "Videos"}, {"movies", "Movies"}, {"books", "Books"}, {"ebook", "Ebook"}, {"ebooks", "Ebooks"}};
		bool seen[6] = {};
		for (auto& f : folders)
		{
			std::string n = lowerAscii(baseName(f));
			for (int i = 0; i < 6; i++)
				if (n == KIND[i].plain) { seen[i] = true; break; }
		}
		std::string kinds;
		for (int i = 0; i < 6; i++)
		{
			if (!seen[i]) continue;
			if (!kinds.empty()) kinds += " and ";
			kinds += KIND[i].word;
		}
		if (kinds.empty()) return "";
		return kinds + where;
	}

	std::vector<std::string> libraryFolders(LibKind kind)
	{
		if (kind == LibKind::Books) return bookFolders();
		MediaFolders mf = mediaFolders();
		return kind == LibKind::Music ? mf.music : mf.videos;
	}

	std::string cardWord(const std::string& path)
	{
		std::vector<std::string> cards = indexCards();
		for (size_t i = 0; i < cards.size(); i++)
			if (path == cards[i] || path.compare(0, cards[i].size() + 1, cards[i] + "/") == 0)
				return i == 0 ? "This card" : "Second card";
		return "";
	}

	std::string sourceLabel(const std::string& folder)
	{
		std::vector<std::string> cards = indexCards();
		for (auto& c : cards)
			if (folder.compare(0, c.size() + 1, c + "/") == 0)
			{
				std::string rest = folder.substr(c.size() + 1), out;
				for (char ch : rest)
					out += ch == '/' ? std::string(" / ") : std::string(1, ch);
				return cardWord(folder) + " / " + out;
			}
		return folder;
	}

	std::string mediaEmptyWords(const std::vector<std::string>& folders)
	{
		std::string from = mediaFromWords(folders);
		if (from.empty()) return "";
		size_t at = from.find(" on ");
		std::string kinds = from.substr(0, at), where = from.substr(at);
		bool plural = kinds.find(" and ") != std::string::npos || kinds.find(" folders") != std::string::npos;
		return kinds + (plural ? " are empty" : " is empty") + where;
	}

	int makeLibraryFolders(const std::string& stateDir)
	{
		std::vector<std::string> cards = indexCards();
		if (cards.empty() || stateDir.empty()) return 0;
		std::string marker = stateDir + "/library-folders-made";
		struct stat st;
		if (stat(marker.c_str(), &st) == 0) return 0;
		const std::string& main = cards[0];
		std::string prefix = main == "/" ? "/" : main + "/";
		bool has[3] = {false, false, false};
		if (DIR* d = opendir(main.c_str()))
		{
			while (dirent* e = readdir(d))
			{
				if (e->d_name[0] == '.') continue;
				bool dir = e->d_type == DT_DIR;
				if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK) dir = isDir(prefix + e->d_name);
				if (!dir) continue;
				if (isVideosFolderName(e->d_name)) has[0] = true;
				if (isMusicFolderName(e->d_name)) has[1] = true;
				if (isBookFolderName(e->d_name)) has[2] = true;
			}
			closedir(d);
		}
		else return 0;	// an unreadable card: try again at the next start
		static const char* NAME[3] = {"Videos", "Music", "Books"};
		int made = 0;
		for (int i = 0; i < 3; i++)
		{
			if (has[i]) continue;
			if (mkdir((prefix + NAME[i]).c_str(), 0755) == 0)
			{
				made++;
				fprintf(stderr, "library: made %s%s on the main card (first start)\n", prefix.c_str(), NAME[i]);
			}
			else fprintf(stderr, "library: could not make %s%s: %s\n", prefix.c_str(), NAME[i], strerror(errno));
		}
		if (FILE* f = fopen(marker.c_str(), "w")) { fputs("1\n", f); fclose(f); }
		return made;
	}

	bool isBookFolderName(const std::string& name)
	{
		std::string n = lowerAscii(name);
		// (not Documents: it holds brochures and forms as much as books; Files opens those)
		return n == "books" || n == "ebook" || n == "ebooks";
	}

	std::vector<std::string> bookFolders()
	{
		std::vector<std::string> out;
		for (auto& card : indexCards())
		{
			// every such folder at the top, all as one (a card may have Books and Ebooks)
			if (DIR* d = opendir(card.c_str()))
			{
				std::string prefix = card == "/" ? "/" : card + "/";
				while (dirent* e = readdir(d))
				{
					if (e->d_name[0] == '.') continue;
					bool dir = e->d_type == DT_DIR;
					if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK) dir = isDir(prefix + e->d_name);
					if (dir && isBookFolderName(e->d_name)) out.push_back(prefix + e->d_name);
				}
				closedir(d);
			}
		}
		std::sort(out.begin(), out.end());	// (readdir's order is the file system's)
		return out;
	}

	std::vector<MediaBook> listBooks(const std::vector<std::string>& folders)
	{
		std::vector<MediaBook> out;
		std::vector<std::string> stack(folders.rbegin(), folders.rend());
		while (!stack.empty())
		{
			std::string dir = stack.back();
			stack.pop_back();
			if (DIR* d = opendir(dir.c_str()))
			{
				std::string prefix = dir == "/" ? "/" : dir + "/";
				while (dirent* e = readdir(d))
				{
					if (e->d_name[0] == '.') continue;
					std::string path = prefix + e->d_name;
					bool dir2 = e->d_type == DT_DIR;
					if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK) dir2 = isDir(path);
					if (dir2)
					{
						if (!isHiddenFolder(path)) stack.push_back(path);
						continue;
					}
					if (!isBookName(path)) continue;
					struct stat st;
					if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
					out.push_back({path, (long long)st.st_size, (int64_t)st.st_mtime});
				}
				closedir(d);
			}
		}
		std::sort(out.begin(), out.end(), [](const MediaBook& a, const MediaBook& b) {
			return naturalLess(baseNameLessExt(a.path), baseNameLessExt(b.path));
		});
		return out;
	}

	// ---- the folded word

	std::string foldText(const std::string& s)
	{
		std::string out;
		out.reserve(s.size());
		size_t at = 0;
		while (at < s.size())
		{
			uint32_t cp = decode(s, at);
			if (cp < 0x80)
			{
				out += (char)tolower((unsigned char)cp);
				continue;
			}
			const FoldRange* hit = nullptr;
			for (auto& r : FOLD)
				if (cp >= r.from && cp <= r.to)
				{
					hit = &r;
					break;
				}
			if (hit) out += hit->plain;
			else appendPlain(out, cp);
		}
		return out;
	}

	// ---- the words

	uint32_t MediaWords::add(const std::string& s)
	{
		auto it = where.find(s);
		if (it != where.end()) return it->second;
		strings.push_back(s);
		uint32_t id = (uint32_t)(strings.size() - 1);
		where.emplace(s, id);
		return id;
	}

	// ---- the read API

	void MediaIndex::commit(const std::shared_ptr<const MediaSnapshot>& s)
	{
		std::lock_guard<std::mutex> g(m);
		snap = s;
		gen++;
	}

	std::shared_ptr<const MediaSnapshot> MediaIndex::snapshot() const
	{
		std::lock_guard<std::mutex> g(m);
		return snap;
	}

	uint32_t MediaIndex::generation() const
	{
		std::lock_guard<std::mutex> g(m);
		return gen;
	}

	// ---- the walk

	void internSong(MediaWords& words, MediaSong& s, const std::string& path, const MediaTags& t)
	{
		s.path = path;
		s.title = words.add(!t.title.empty() ? t.title : baseNameLessExt(path));
		s.artist = words.add(t.artist);
		s.albumArtist = words.add(t.albumArtist);
		s.album = words.add(!t.album.empty() ? t.album : folderName(path));
		// the folded twins are words too: an artist's or album's is shared by every song that holds it, so the
		// query never folds a thing bigger than one title
		s.foldTitle = words.add(foldText(words.at(s.title)));
		s.foldArtist = words.add(foldText(words.at(s.artist)));
		s.foldAlbum = words.add(foldText(words.at(s.album)));
		s.track = t.track;
		s.disc = t.disc;
		s.year = t.year;
		s.duration = t.duration > 0 ? (int32_t)(t.duration + 0.5) : -1;
	}

	namespace
	{
		bool walkFolder(const std::string& dir, int card, bool music, const TagReader& tags, MediaTagCache& cache,
			const WalkGate& gate, MediaWalk& out, std::set<std::pair<uint64_t, uint64_t>>& seen, WalkCounters* live)
		{
			// hidden folders, while they are hidden, are not in it
			if (isHiddenFolder(dir)) return true;
			if (live) live->foldersKnown++;
			if (!gate()) return false;
			DIR* d = opendir(dir.c_str());
			if (!d)
			{
				// counted and logged by the caller, never thrown: one card gone is no reason to lose the rest
				out.failed.push_back(dir);
				if (live) live->foldersDone++;
				return true;
			}
			if (live) live->card.store(card);
			std::string prefix = dir == "/" ? "/" : dir + "/";
			bool whole = true;
			while (dirent* e = readdir(d))
			{
				if (e->d_name[0] == '.') continue;
				std::string path = prefix + e->d_name;
				bool isDir = e->d_type == DT_DIR, isFile = e->d_type == DT_REG;
				// (exFAT and some others don't fill d_type: ask, and keep the answer — a kept file's size and
				// mtime are in it already, so the card pays one stat a file, not two)
				struct stat st = {};
				bool stated = false;
				if (e->d_type == DT_UNKNOWN || e->d_type == DT_LNK)
				{
					if (stat(path.c_str(), &st) != 0) continue;
					stated = true;
					isDir = S_ISDIR(st.st_mode);
					isFile = S_ISREG(st.st_mode);
				}
				if (isDir)
				{
					// a symlink's loop (a computer's test roots; a card has no symlinks): a folder already gone
					// into is not gone into again. exFAT reports no inode (0), so nothing is decided by it there
					if (stated && st.st_ino != 0 &&
						!seen.insert({(uint64_t)st.st_dev, (uint64_t)st.st_ino}).second)
						continue;
					if (!walkFolder(path, card, music, tags, cache, gate, out, seen, live))
					{
						whole = false;
						break;
					}
					continue;
				}
				if (!isFile) continue;
				// the extension decides what the player opens it as, so it decides what counts: a picture or a
				// text under Music is never a song, and no file under Videos but a video is a video. The name, not
				// the whole path: a path may start with a dot (a test root), a file never counts by that
				bool video = isVideoFile(e->d_name);
				if (music ? (!video && !isAudioFile(e->d_name)) : !video) continue;
				if (!gate())
				{
					whole = false;
					break;
				}
				if (!stated && stat(path.c_str(), &st) != 0) continue;	// (gone between the listing and now)
				long long size = (long long)st.st_size, mtime = (long long)st.st_mtime;
				if (music)
				{
					MediaTags t;
					if (cache.find(path, size, mtime, t)) out.kept++;
					else
					{
						tags(path, size, mtime, t);
						// a read cut short (a busy card) is not the file's answer: nothing is kept, so it is
						// asked again next pass
						if (!t.slow)
						{
							cache.put(path, size, mtime, t, video);
							out.probed++;
						}
					}
					MediaSong s;
					internSong(out.words, s, path, t);
					// tagged or not, a video in a Music folder is part of Music: with tags it sits in its album
					// and under its artist, without them by its file's name, in its folder's album
					s.video = video;
					s.kb = (uint32_t)std::min<long long>(size / 1000, UINT32_MAX);
					out.songs.push_back(std::move(s));
					if (live) live->songs++;
				}
				else
				{
					MediaVideo v;
					v.path = path;
					v.size = size;
					v.mtime = mtime;
					out.videos.push_back(std::move(v));
					if (live) live->videos++;
				}
			}
			closedir(d);
			if (live) live->foldersDone++;
			return whole;
		}
	}

	bool walkMedia(const MediaFolders& folders, const TagReader& tags, MediaTagCache& cache, const WalkGate& gate,
		MediaWalk& out, WalkCounters* live)
	{
		std::set<std::pair<uint64_t, uint64_t>> seen;
		// the folder's card, for the notice's name: the roots' order (mediaCards()), a folder under one of them
		auto roots = mediaCards();
		auto cardOf = [&](const std::string& folder) {
			for (int i = 0; i < (int)roots.size(); i++)
				if (folder.rfind(roots[i] + "/", 0) == 0) return i;
			return -1;
		};
		for (auto& f : folders.music)
			if (!walkFolder(f, cardOf(f), true, tags, cache, gate, out, seen, live)) return false;
		for (auto& f : folders.videos)
			if (!walkFolder(f, cardOf(f), false, tags, cache, gate, out, seen, live)) return false;
		out.musicFolders = folders.music;
		out.videoFolders = folders.videos;
		if (live) live->card.store(-1);
		return true;
	}

	// ---- the grouping

	std::shared_ptr<const MediaSnapshot> groupMediaSnapshot(MediaWords&& words, std::vector<MediaSong>&& songs,
		std::vector<MediaVideo>&& videos, std::vector<std::string>&& musicFolders, std::vector<std::string>&& videoFolders)
	{
		MediaSnapshot* p = new MediaSnapshot;
		p->words = std::move(words);
		p->musicFolders = std::move(musicFolders);
		p->videoFolders = std::move(videoFolders);
		// the album's key: the album artist's word, else the artist's — pooled words, so the same word is the
		// same index and the sort never compares the same name twice
		auto key = [](const MediaSong& s) { return s.albumArtist ? s.albumArtist : s.artist; };
		std::sort(songs.begin(), songs.end(), [&](const MediaSong& a, const MediaSong& b) {
			uint32_t ka = key(a), kb = key(b);
			// the unknown artist's albums go last, wherever the empty word would sort
			if ((ka == 0) != (kb == 0)) return kb == 0;
			if (ka != kb) return naturalLess(p->words.at(ka), p->words.at(kb));
			if (a.album != b.album) return naturalLess(p->words.at(a.album), p->words.at(b.album));
			// a disc that isn't tagged is the first (most rips are one disc and never say so)
			int32_t da = a.disc ? a.disc : 1, db = b.disc ? b.disc : 1;
			if (da != db) return da < db;
			// a track number says where in the album it sits; one without (a music video, mostly) only says what
			// it is, so those follow the tracks, in name order
			if ((a.track != 0) != (b.track != 0)) return b.track == 0;
			if (a.track != b.track) return a.track < b.track;
			return naturalLess(baseNameLessExt(a.path), baseNameLessExt(b.path));
		});
		p->songs = std::move(songs);
		// the albums: runs of one key, in the order the songs now carry
		std::unordered_map<uint32_t, uint32_t> artistWhere;
		for (size_t i = 0; i < p->songs.size();)
		{
			uint32_t k = key(p->songs[i]);
			uint32_t albumWord = p->songs[i].album;
			MediaAlbum album;
			album.name = albumWord;
			album.albumArtist = k;
			size_t j = i;
			while (j < p->songs.size() && key(p->songs[j]) == k && p->songs[j].album == albumWord)
				album.songs.push_back((uint32_t)j++);
			auto it = artistWhere.find(k);
			uint32_t ai = it != artistWhere.end() ? it->second : (uint32_t)p->artists.size();
			if (it == artistWhere.end())
			{
				artistWhere.emplace(k, ai);
				p->artists.push_back(MediaArtist{k, {}, 0});
			}
			p->artists[ai].albums.push_back((uint32_t)p->albums.size());
			p->artists[ai].songs += (uint32_t)album.songs.size();
			p->albums.push_back(std::move(album));
			i = j;
		}
		// each artist's albums in name order, then the artists themselves (the unknown last), and the albums
		// vector in that same order, so a reader walks artists, albums, songs and finds them each where they sort
		for (auto& a : p->artists)
			std::sort(a.albums.begin(), a.albums.end(), [&](uint32_t x, uint32_t y) {
				return naturalLess(p->words.at(p->albums[x].name), p->words.at(p->albums[y].name));
			});
		std::stable_sort(p->artists.begin(), p->artists.end(), [&](const MediaArtist& a, const MediaArtist& b) {
			if ((a.name == 0) != (b.name == 0)) return b.name == 0;
			return naturalLess(p->words.at(a.name), p->words.at(b.name));
		});
		{
			std::vector<MediaAlbum> ordered;
			std::vector<uint32_t> from(p->albums.size(), 0);	// old place -> new
			for (auto& a : p->artists)
				for (uint32_t old : a.albums)
				{
					from[old] = (uint32_t)ordered.size();
					ordered.push_back(std::move(p->albums[old]));
				}
			for (auto& a : p->artists)
				for (auto& old : a.albums) old = from[old];
			p->albums = std::move(ordered);
		}
		// the videos: their folded names are words (Search matches them pre-folded), then the list by its names
		for (auto& v : videos) v.foldName = p->words.add(foldText(baseNameLessExt(v.path)));
		std::sort(videos.begin(), videos.end(), [&](const MediaVideo& a, const MediaVideo& b) {
			return naturalLess(baseNameLessExt(a.path), baseNameLessExt(b.path));
		});
		p->videos = std::move(videos);
		p->words.seal();
		return std::shared_ptr<const MediaSnapshot>(p);
	}

	// ---- the query

	SearchHits mediaSearch(const MediaSnapshot& snap, const std::vector<std::string>& fileNames,
		const std::string& query)
	{
		SearchHits hits;
		std::string q = foldText(query);
		std::vector<std::string> words;
		for (size_t a = 0; a < q.size();)
		{
			size_t b = q.find_first_of(" \t", a);
			if (b == std::string::npos) b = q.size();
			if (b > a) words.push_back(q.substr(a, b - a));
			a = b + 1;
		}
		if (words.empty()) return hits;
		auto allIn = [&](const std::string& a, const std::string& b, const std::string& c) {
			for (auto& w : words)
				if (a.find(w) == std::string::npos && b.find(w) == std::string::npos && c.find(w) == std::string::npos)
					return false;
			return true;
		};
		auto starts = [&](const std::string& s) { return s.rfind(words[0], 0) == 0; };
		// two passes a kind: what starts with the query first, then what only holds it, each as its list is sorted
		for (int pass = 0; pass < 2; pass++)
		{
			for (uint32_t i = 0; i < snap.songs.size(); i++)
			{
				const MediaSong& s = snap.songs[i];
				const std::string& title = snap.at(s.foldTitle);
				if ((starts(title) ? 0 : 1) != pass) continue;
				if (allIn(title, snap.at(s.foldArtist), snap.at(s.foldAlbum))) hits.songs.push_back(i);
			}
			for (uint32_t i = 0; i < snap.videos.size(); i++)
			{
				const std::string& name = snap.at(snap.videos[i].foldName);
				if ((starts(name) ? 0 : 1) != pass) continue;
				if (allIn(name, "", "")) hits.videos.push_back(i);
			}
			// the callers' lists (Books' and files' names) live a moment, so they fold here, once a query
			for (uint32_t i = 0; i < fileNames.size(); i++)
			{
				std::string name = foldText(baseNameLessExt(fileNames[i]));
				if ((starts(name) ? 0 : 1) != pass) continue;
				if (allIn(name, "", "")) hits.files.push_back(i);
			}
		}
		// a song whose tags don't hold the words but whose file name does ("gaia" in Ultramangaia_ED2_Beat_on_Dream.mp3):
		// after the tag matches. The name's _ - . read as spaces
		for (uint32_t i = 0; i < snap.songs.size(); i++)
		{
			const MediaSong& s = snap.songs[i];
			if (allIn(snap.at(s.foldTitle), snap.at(s.foldArtist), snap.at(s.foldAlbum))) continue;
			std::string n = baseNameLessExt(s.path);
			for (char& c : n)
				if (c == '_' || c == '-' || c == '.') c = ' ';
			std::string all = foldText(n) + " " + snap.at(s.foldTitle) + " " + snap.at(s.foldArtist) + " " + snap.at(s.foldAlbum);
			if (allIn(all, "", "")) hits.songs.push_back(i);
		}
		return hits;
	}
}
