// The media sections' pictures and facts the art cache doesn't hold (ui_mediaart.h).
#include "ui_mediaart.h"
#include "ui_internal.h"
#include "ui_manga.h"
#include <cstring>

namespace bp
{
	namespace
	{
		const size_t THUMBS_KEPT = 64, FRAMES_KEPT = 3, COVERS_KEPT = 4;
		// the pane's cover box at the Plus's 1.6 (lay::previewPane: 196 square for an album, 196 x 278 a book, less
		// the edge): the compose scales it to the panel's own pixels
		const int COVER_PX = 314, BOOK_W = 307, BOOK_H = 438;

		std::shared_ptr<const PaneImage> paneImage(PageImage& img)
		{
			auto p = std::make_shared<PaneImage>();
			p->w = img.w;
			p->h = img.h;
			p->rgba = std::move(img.rgba);
			return p;
		}
	}

	Ui::MediaArt::~MediaArt()
	{
		for (auto& v : videos)
			if (v.second.tex) SDL_DestroyTexture(v.second.tex);
	}

	// ---- a video ----

	void Ui::MediaArt::ask(const std::string& path, bool large)
	{
		Video& v = videos[path];
		if (!worker || v.none) return;
		if (large ? v.largeAsked : v.asked) return;
		v.asked = true;
		if (large) v.largeAsked = true;
		worker->want(path, large);
	}

	int Ui::MediaArt::thumbState(const std::string& path)
	{
		auto it = videos.find(path);
		if (it == videos.end() || (!it->second.asked && !it->second.none))
		{
			ask(path, false);
			return 0;
		}
		if (it->second.tex)
		{
			it->second.texUsed = ++clock;
			return 1;
		}
		return it->second.none ? 2 : 0;
	}

	void Ui::MediaArt::drawThumb(const std::string& path, Rect r)
	{
		auto it = videos.find(path);
		if (it == videos.end() || !it->second.tex) return;
		it->second.texUsed = ++clock;
		SDL_Rect d = ui.devRect(r);
		SDL_RenderCopy(ui.ren, it->second.tex, nullptr, &d);
	}

	void Ui::MediaArt::drawThumbCrop(const std::string& path, Rect r)
	{
		auto it = videos.find(path);
		if (it == videos.end() || !it->second.tex || r.w <= 0 || r.h <= 0) return;
		it->second.texUsed = ++clock;
		int tw = 0, th = 0;
		SDL_QueryTexture(it->second.tex, nullptr, nullptr, &tw, &th);
		// the widest middle part of the frame at the box's aspect
		SDL_Rect src{0, 0, tw, th};
		if ((long)tw * r.h > (long)th * r.w) { src.w = (int)((long)th * r.w / r.h); src.x = (tw - src.w) / 2; }
		else { src.h = (int)((long)tw * r.h / r.w); src.y = (th - src.h) / 2; }
		SDL_Rect d = ui.devRect(r);
		SDL_RenderCopy(ui.ren, it->second.tex, &src, &d);
	}

	const VideoMeta* Ui::MediaArt::meta(const std::string& path)
	{
		auto it = videos.find(path);
		if (it != videos.end() && it->second.metaKnown) return &it->second.meta;
		ask(path, false);
		return nullptr;
	}

	std::shared_ptr<const PaneImage> Ui::MediaArt::frame(const std::string& path)
	{
		auto it = videos.find(path);
		if (it != videos.end() && it->second.large)
		{
			it->second.largeUsed = ++clock;
			return it->second.large;
		}
		ask(path, true);
		return nullptr;
	}

	bool Ui::MediaArt::frameNone(const std::string& path) const
	{
		auto it = videos.find(path);
		return it != videos.end() && it->second.none;
	}

	void Ui::MediaArt::videosReady(std::vector<VideoArtWorker::Result> results)
	{
		for (auto& r : results)
		{
			Video& v = videos[r.path];
			if (r.metaOk)
			{
				v.meta = std::move(r.meta);
				v.metaKnown = true;
			}
			if (r.noPicture) v.none = true;
			if (r.small.ok() && !v.tex)
			{
				// the row's corners (5 units on the 64-wide thumb: the texture is twice that) cut into the pixels
				roundCorners(r.small.rgba, r.small.w, r.small.h, 5 * r.small.w / lay::VIDEO_THUMB_W);
				v.tex = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, r.small.w, r.small.h);
				if (v.tex)
				{
					SDL_SetTextureScaleMode(v.tex, SDL_ScaleModeLinear);
					SDL_UpdateTexture(v.tex, nullptr, r.small.rgba.data(), r.small.w * 4);
					SDL_SetTextureBlendMode(v.tex, SDL_BLENDMODE_BLEND);
					v.texUsed = ++clock;
				}
			}
			// (an answer with no frame — none, or one the card won't decode — leaves it asked: not asked again)
			if (r.largeAsked && r.large.ok())
			{
				v.large = paneImage(r.large);
				v.largeUsed = ++clock;
			}
		}
		serial++;
		trim();
	}

	void Ui::MediaArt::trim()
	{
		// the least recently used thumbnails' textures and frames' pixels go (their entries stay: the info is small)
		auto drop = [&](size_t kept, auto has, auto used, auto free) {
			for (;;)
			{
				size_t n = 0;
				Video* oldest = nullptr;
				for (auto& e : videos)
				{
					if (!has(e.second)) continue;
					n++;
					if (!oldest || used(e.second) < used(*oldest)) oldest = &e.second;
				}
				if (n <= kept || !oldest) return;
				free(*oldest);
			}
		};
		drop(THUMBS_KEPT, [](Video& v) { return v.tex != nullptr; }, [](Video& v) { return v.texUsed; }, [](Video& v) {
			SDL_DestroyTexture(v.tex);
			v.tex = nullptr;
			v.asked = false;	// (asked again when it shows again: a cache read)
		});
		drop(FRAMES_KEPT, [](Video& v) { return v.large != nullptr; }, [](Video& v) { return v.largeUsed; }, [](Video& v) {
			v.large.reset();
			v.largeAsked = false;
		});
	}

	// ---- the top pane's covers ----

	std::string Ui::MediaArt::coverKey(const std::string& path, bool book)
	{
		return std::string(HOME_COVER_KEY) + "pane|" + (book ? "book|" : "song|") + path;
	}

	bool Ui::MediaArt::isPaneCoverKey(const std::string& key)
	{
		return key.compare(0, std::strlen(HOME_COVER_KEY) + 5, std::string(HOME_COVER_KEY) + "pane|") == 0;
	}

	std::shared_ptr<const PaneImage> Ui::MediaArt::paneCover(const std::string& path, bool book)
	{
		if (path.empty()) return nullptr;
		std::string key = coverKey(path, book);
		Cover& c = covers[key];
		if (c.img)
		{
			c.used = ++clock;
			return c.img;
		}
		CoverWorker* cw = ui.manga ? ui.manga->coverWorker : nullptr;
		if (c.asked || c.none || !cw) return nullptr;
		c.asked = true;
		if (book) cw->wantBook(key, path, BOOK_W, BOOK_H);
		else cw->wantSong(key, path, COVER_PX, COVER_PX);
		return nullptr;
	}

	bool Ui::MediaArt::coverNone(const std::string& path, bool book) const
	{
		auto it = covers.find(coverKey(path, book));
		return it != covers.end() && it->second.none;
	}

	void Ui::MediaArt::paneCoverReady(CoverWorker::Result& r)
	{
		Cover& c = covers[r.key];
		c.asked = false;
		if (!r.ok)
		{
			c.none = true;
			serial++;
			return;
		}
		auto p = std::make_shared<PaneImage>();
		p->w = r.w;
		p->h = r.h;
		p->rgba = std::move(r.rgba);
		c.img = std::move(p);
		c.used = ++clock;
		serial++;
		// the least recently shown go (a pane shows one at a time)
		for (;;)
		{
			size_t n = 0;
			Cover* oldest = nullptr;
			for (auto& e : covers)
			{
				if (!e.second.img) continue;
				n++;
				if (!oldest || e.second.used < oldest->used) oldest = &e.second;
			}
			if (n <= COVERS_KEPT || !oldest) break;
			oldest->img.reset();
		}
	}
}
