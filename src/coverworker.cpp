#include "coverworker.h"
#include "bookcoverfile.h"
#include "pagebook.h"
#include "pagedecode.h"
#include "web/probe.h"
#include <algorithm>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		constexpr size_t kMaxWaiting = 200;	// (a scroll through a big library asks for more than it ever shows)

		// the page a cover comes from: a picture on the card, or one entry of an archive ("archive.cbz#entry")
		PageImage coverPage(const std::string& source, int w)
		{
			size_t at = source.find('#');
			if (at == std::string::npos) return decodePageFile(source, w);
			// (any book: a CBZ's or CBR's entry, a PDF's page number; a PDF's cover is drawn at the tile's width)
			std::string err;
			auto book = openPageBook(source.substr(0, at), err);
			PageImage p;
			if (!book)
			{
				p.error = err;
				return p;
			}
			const auto& names = book->pages();
			// "archive.cbz#": its first page, whatever it's called (the library lists archives without opening them)
			auto it = at + 1 == source.size() ? names.begin() : std::find(names.begin(), names.end(), source.substr(at + 1));
			if (it == names.end())
			{
				p.error = "No such page";
				return p;
			}
			return bookPage(*book, (int)(it - names.begin()), w);
		}

		// a song's art: the attached picture, else cover.* beside the file, decoded at the card's width
		PageImage songPage(const std::string& path, int w)
		{
			std::string bytes, mime;
			PageImage p;
			if (!probeCover(path, bytes, mime)) return p;	// (error left empty: nothing to draw is the answer)
			return decodePage((const uint8_t*)bytes.data(), bytes.size(), w);
		}

		// a book's cover page (Books' shelf, ui_books.h; docs/redesign "books"): a comic's first page, a PDF's,
		// an EPUB's own cover image (else its first picture). A TXT has none — the drawn sleeve is its answer
		PageImage bookCover(const std::string& path, int w)
		{
			BookCover c;
			if (!findBookCover(path, w, c)) return {};
			if (c.drawn.ok()) return c.drawn;
			return decodePage(c.bytes.data(), c.bytes.size(), w);
		}

		// ---- Match album art's cache (artbackdrop.h: <dir>/artbg/<fnv64>.bg) ----
		// The worker thread alone is here: a read (the file's date bumped, the oldest evicted past the cap), or
		// a decode and the pure maths, then the write. Nothing of this touches Ui or Player.
		std::vector<uint8_t> readCacheFile(const std::string& path)
		{
			std::vector<uint8_t> out;
			FILE* f = fopen(path.c_str(), "rb");
			if (!f) return out;
			char buf[8192];
			size_t n = 0;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
			fclose(f);
			return out;
		}

		void writeCacheFile(const std::string& path, const std::vector<uint8_t>& bytes)
		{
			FILE* f = fopen(path.c_str(), "wb");
			if (!f) return;
			fwrite(bytes.data(), 1, bytes.size(), f);
			fclose(f);
		}

		// a file's date says when it was last used: bumped on a read, so the eviction's "oldest first" means
		// "least recently shown", not "least recently added"
		void touchCacheFile(const std::string& path)
		{
			struct stat st;
			if (stat(path.c_str(), &st) != 0) return;
			struct timeval times[2] = {};
			times[0].tv_sec = st.st_atime;
			times[1].tv_sec = time(nullptr);
			utimes(path.c_str(), times);
		}

		void evictCache(const std::string& dir)
		{
			DIR* d = opendir(dir.c_str());
			if (!d) return;
			std::vector<artbg::CacheFile> files;
			std::vector<std::string> names;
			while (dirent* e = readdir(d))
			{
				std::string name = e->d_name;
				if (name.size() != 20 || name.substr(16) != ".bg") continue;	// (only our own 16-hex names)
				struct stat st;
				if (stat((dir + "/" + name).c_str(), &st) != 0) continue;
				files.push_back({name, (size_t)st.st_size, (uint64_t)st.st_mtime * 1000});
				names.push_back(name);
			}
			closedir(d);
			for (size_t i : artbg::evict(files)) unlink((dir + "/" + names[i]).c_str());
		}

		// The tile filled, never stretched: a picture wider than the tile's shape (decoded at the tile's width it
		// would be too short) was decoded wider by the caller, and its middle columns are kept.
		// A manga cover keeps its top rows (where a cover puts its title; the bottom of a chapter's first page is not
		// part of a cover); album art is cropped from its middle, since a square sleeve in a wide card must show its
		// centre, not its top.
		CoverWorker::Result cropped(const std::string& key, const PageImage& img, int w, int h, bool centre)
		{
			CoverWorker::Result r;
			r.key = key;
			if (!img.ok()) return r;
			r.w = std::min(img.w, w);
			r.h = std::min(img.h, h);
			int x0 = (img.w - r.w) / 2;
			int y0 = centre ? (img.h - r.h) / 2 : 0;
			r.rgba.resize((size_t)r.w * (size_t)r.h * 4);
			for (int y = 0; y < r.h; y++)
				std::memcpy(r.rgba.data() + (size_t)y * r.w * 4, img.rgba.data() + ((size_t)(y + y0) * img.w + x0) * 4,
					(size_t)r.w * 4);
			r.ok = true;
			return r;
		}
	}

	void CoverWorker::want(const std::string& key, const std::string& source, int w, int h)
	{
		ask(key, source, w, h, false);
	}

	void CoverWorker::wantSong(const std::string& key, const std::string& path, int w, int h)
	{
		ask(key, path, w, h, true);
	}

	void CoverWorker::wantBackdrop(const std::string& key, const std::string& path)
	{
		ask(key, path, artbg::W, artbg::H, true, true);
	}

	void CoverWorker::wantBook(const std::string& key, const std::string& path, int w, int h)
	{
		ask(key, path, w, h, false, false, true);
	}

	void CoverWorker::ask(const std::string& key, const std::string& source, int w, int h, bool song, bool backdrop,
		bool book)
	{
		std::lock_guard<std::mutex> g(m);
		if (stopping) return;
		// the same cover asked again (a redraw, a scroll back) is the same picture: moved to the front, it is decoded
		// next rather than twice
		items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& i) { return i.key == key; }), items.end());
		items.push_back({key, source, std::max(1, w), std::max(1, h), song, backdrop, book});
		if (items.size() > kMaxWaiting) items.erase(items.begin(), items.begin() + (items.size() - kMaxWaiting));
		if (!started)
		{
			started = true;
			thread = std::thread([this] { run(); });
			thread.detach();
		}
		cv.notify_one();
	}

	void CoverWorker::cancel()
	{
		std::lock_guard<std::mutex> g(m);
		// Home's cards may be waiting for a song's art while the Manga app is opened: only Manga's requests are its
		// own to drop (a decode may still be Home's, and its answer is routed by key)
		const size_t n = std::strlen(HOME_COVER_KEY);
		items.erase(std::remove_if(items.begin(), items.end(), [n](const Item& i) { return i.key.compare(0, n, HOME_COVER_KEY) != 0; }),
			items.end());
	}

	std::vector<CoverWorker::Result> CoverWorker::take()
	{
		std::lock_guard<std::mutex> g(m);
		signalled = false;
		std::vector<Result> out;
		out.swap(results);
		return out;
	}

	void CoverWorker::stop()
	{
		std::lock_guard<std::mutex> g(m);
		stopping = true;
		items.clear();
		cv.notify_one();
	}

	void CoverWorker::run()
	{
		for (;;)
		{
			Item it;
			{
				std::unique_lock<std::mutex> g(m);
				cv.wait(g, [&] { return !stopping && !items.empty(); });
				it = std::move(items.back());
				items.pop_back();
			}
			// a book's cover: its cover page (the source's own format decides which), kept from the top as a
			// manga cover is — a book's title sits at a cover's head
			if (it.book)
			{
				Result r = cropped(it.key, bookCover(it.source, it.w), it.w, it.h, false);
				bool signal = false;
				{
					std::lock_guard<std::mutex> g(m);
					if (stopping) continue;	// (a stop at quit: nothing more is handed out, and the loop parks)
					results.push_back(std::move(r));
					signal = !signalled;
					signalled = true;
				}
				if (signal) ready();
				continue;
			}
			// a song's backdrop: the cache by the art's own bytes, else the decode and the pure maths; one
			// answer, no tile of it
			if (it.backdrop)
			{
				Result r;
				r.key = it.key;
				r.backdrop = true;
				std::string bytes, mime;
				if (probeCover(it.source, bytes, mime) && !bytes.empty())
				{
					r.ok = true;
					r.artHash = artbg::fnv64((const uint8_t*)bytes.data(), bytes.size());
					std::string dir = stateDir.empty() ? std::string() : stateDir + "/artbg";
					std::string file = dir.empty() ? std::string() : dir + "/" + artbg::cacheName(r.artHash);
					bool fromCache = false;
					if (!file.empty())
					{
						if (std::vector<uint8_t> data = readCacheFile(file); !data.empty() &&
							artbg::decode(data.data(), data.size(), r.art))
						{
							touchCacheFile(file);
							fromCache = true;
						}
					}
					if (!fromCache)
					{
						// decoded at twice the picture's width: the crop to 4:3 and the box filter down have
						// enough to work on, and the decode stays a few milliseconds
						PageImage img = decodePage((const uint8_t*)bytes.data(), bytes.size(), 2 * artbg::W);
						if (img.ok()) r.art = artbg::artBackdrop(img.rgba.data(), img.w, img.h);
						if (!dir.empty())
						{
							if (std::vector<uint8_t> out = artbg::encode(r.art); !out.empty()) writeCacheFile(file, out);
						}
					}
					if (!dir.empty()) evictCache(dir);
				}
				bool signal = false;
				{
					std::lock_guard<std::mutex> g(m);
					if (stopping) continue;	// (a stop at quit: nothing more is handed out, and the loop parks)
					results.push_back(std::move(r));
					signal = !signalled;
					signalled = true;
				}
				if (signal) ready();
				continue;
			}
			auto page = [&](int width) { return it.song ? songPage(it.source, width) : coverPage(it.source, width); };
			PageImage img = page(it.w);
			// too short for the tile at its width: decoded again as wide as it must be to cover the tile's height
			if (img.ok() && img.h < it.h && img.h > 0)
			{
				int wider = (int)(((int64_t)it.w * it.h + img.h - 1) / img.h);
				if (wider > it.w && wider <= 4 * it.w)
				{
					PageImage again = page(wider);
					if (again.ok()) img = std::move(again);
				}
			}
			Result r = cropped(it.key, img, it.w, it.h, it.song);
			bool signal = false;
			{
				std::lock_guard<std::mutex> g(m);
				if (stopping) continue;	// (a stop at quit: nothing more is handed out, and the loop parks)
				results.push_back(std::move(r));
				signal = !signalled;
				signalled = true;
			}
			if (signal) ready();
		}
	}
}
