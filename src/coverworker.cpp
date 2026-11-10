#include "coverworker.h"
#include "cbz.h"
#include "pagedecode.h"
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
			Cbz cbz;
			if (!cbz.open(source.substr(0, at)))
			{
				PageImage p;
				p.error = cbz.error();
				return p;
			}
			std::vector<uint8_t> bytes;
			if (!cbz.read(source.substr(at + 1), bytes))
			{
				PageImage p;
				p.error = cbz.error();
				return p;
			}
			return decodePage(bytes.data(), bytes.size(), w);
		}

		// the top of the page, where a cover puts its title, at the tile's aspect: a whole page is much taller, and the
		// bottom of a chapter's first page is not part of a cover
		CoverWorker::Result cropped(const std::string& key, const PageImage& img, int h)
		{
			CoverWorker::Result r;
			r.key = key;
			if (!img.ok()) return r;
			r.w = img.w;
			r.h = std::min(img.h, h);
			r.rgba.resize((size_t)r.w * (size_t)r.h * 4);
			std::memcpy(r.rgba.data(), img.rgba.data(), r.rgba.size());
			r.ok = true;
			return r;
		}
	}

	void CoverWorker::want(const std::string& key, const std::string& source, int w, int h)
	{
		std::lock_guard<std::mutex> g(m);
		if (stopping) return;
		// the same cover asked again (a redraw, a scroll back) is the same picture: moved to the front, it is decoded
		// next rather than twice
		items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& i) { return i.key == key; }), items.end());
		items.push_back({key, source, std::max(1, w), std::max(1, h)});
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
		items.clear();
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
			Result r = cropped(it.key, coverPage(it.source, it.w), it.h);
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
