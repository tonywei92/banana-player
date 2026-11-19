#include "coverworker.h"
#include "pagebook.h"
#include "pagedecode.h"
#include "web/probe.h"
#include <algorithm>
#include <cstring>

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

	void CoverWorker::ask(const std::string& key, const std::string& source, int w, int h, bool song)
	{
		std::lock_guard<std::mutex> g(m);
		if (stopping) return;
		// the same cover asked again (a redraw, a scroll back) is the same picture: moved to the front, it is decoded
		// next rather than twice
		items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& i) { return i.key == key; }), items.end());
		items.push_back({key, source, std::max(1, w), std::max(1, h), song});
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
