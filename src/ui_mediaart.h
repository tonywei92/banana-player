// Pictures and facts for the media sections that the bottom panel's art cache (Ui::artTex: textures of songs' and
// books' covers) doesn't hold: the videos' list thumbnails, top-screen frames and info (VideoArtWorker, kept on the
// card: videocache.h), and the top screen's song and book covers as pixels (the Preview pane is composed in software,
// so it needs the RGBA, not a texture; CoverWorker decodes them, its "home:pane|" answers come here).
//
// Every ask is answered later (an SDL event, main hands the results in); nothing here reads a file. Bounded: 64 list
// thumbnails (textures of 128 x 72), 3 top-screen frames and 4 pane covers (a few MB), the least recently used going
// first. Not an area: Videos' rows and Ui::topPane both use it.
#pragma once
#include "ui.h"
#include "previewpane.h"
#include "videoartworker.h"
#include <memory>
#include <unordered_map>

namespace bp
{
	struct Ui::MediaArt
	{
		explicit MediaArt(Ui& u) : ui(u) {}
		~MediaArt();
		Ui& ui;
		VideoArtWorker* worker = nullptr;	// handed by main (never destroyed)
		int serial = 0;	// bumped by each answer that changes what shows: the pages' signature()s hold it

		// ---- a video ----
		// the list's thumbnail: 0 not yet (asked), 1 ready (drawThumb draws it), 2 none for good. Asking counts as
		// drawn (a smooth list's row key asks every frame but draws the row once)
		int thumbState(const std::string& path);
		void drawThumb(const std::string& path, Rect r);
		// the same filling `r` at its own aspect, the middle kept (Home's square Recent tiles and Continue card)
		void drawThumbCrop(const std::string& path, Rect r);
		// the cached info (null: not known yet; asked)
		const VideoMeta* meta(const std::string& path);
		// the top screen's frame (null: not yet, or none: frameNone)
		std::shared_ptr<const PaneImage> frame(const std::string& path);
		bool frameNone(const std::string& path) const;
		void videosReady(std::vector<VideoArtWorker::Result> results);

		// ---- the top pane's covers (CoverWorker) ----
		// a song's album art (square) or a book's cover, as pixels for the pane: null while it decodes or when there
		// is none (coverNone says which)
		std::shared_ptr<const PaneImage> paneCover(const std::string& path, bool book);
		bool coverNone(const std::string& path, bool book) const;
		static bool isPaneCoverKey(const std::string& key);	// a CoverWorker answer of these (main routes it here)
		void paneCoverReady(CoverWorker::Result& r);

	private:
		struct Video
		{
			bool asked = false, largeAsked = false, none = false;
			bool metaKnown = false;
			VideoMeta meta;
			SDL_Texture* tex = nullptr;
			uint64_t texUsed = 0;
			std::shared_ptr<const PaneImage> large;
			uint64_t largeUsed = 0;
		};
		std::unordered_map<std::string, Video> videos;
		struct Cover
		{
			bool asked = false, none = false;
			std::shared_ptr<const PaneImage> img;
			uint64_t used = 0;
		};
		std::unordered_map<std::string, Cover> covers;	// by the worker's key
		uint64_t clock = 0;
		void ask(const std::string& path, bool large);
		void trim();
		static std::string coverKey(const std::string& path, bool book);
	};
}
