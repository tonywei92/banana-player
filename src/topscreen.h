#pragma once
// The top panel: its window and GL context, and what main draws on it besides mpv (the manga reader, Files' text
// viewer, the frame taken from mpv under their pages, its screenshots). main.cpp keeps the loop and the panels'
// decisions; the panel's own state lives here so the loop reads it by name.
#include "reader.h"
#include "readerfetch.h"
#include "textreader.h"
#include "viznative.h"
#include <SDL.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace bp
{
	class MangaNet;
	class Player;
	struct Settings;
	struct Ui;
	struct PaneSpec;

	struct TopScreen
	{
		TopScreen(SDL_Window* win, SDL_GLContext ctx, std::string fonts);

		// a reader open, or About's support QR up: the Ui's own picture on the top panel; not while the player's own
		// pages show (they show what plays)
		bool onTop(const Ui& ui) const;
		// What plays under a photo or a reader's pages: a song's graph makes frames still (it carries the sound), and
		// mpv waits for each to be taken. They're taken here into a tiny target with rendering skipped (nothing drawn,
		// no GL time), so the photo or the comic isn't drawn again 30 times a second and the bottom panel keeps the GPU
		void drainUnder(Player& player, Player& photos, bool readerOnTop);
		// the top panel's pixels (RGBA, top row first), the frame drawn again for it: screenshots
		SDL_Surface* capture(Player& shown, bool readerOnTop, bool qr);
		// the level card (levelcard.h: one picture, the same on every screen) over a reader's pages or the Theme row's
		// preview pane, mpv's own being under them; barTop: the bar's whole (levelTop), 0 for the label alone (the reader's zoom)
		void cardOnReader(const std::string& label, int percent, bool bar, int barTop, int ms);
		// the manga reader and Files' text viewer: what the bottom panel asked for, its commands, what they show back
		void syncReaders(Ui& ui, Player& player, MangaNet* mangaNet, const std::string& stateDir, Uint32 now, bool woke, bool haveUi,
			const std::function<void(const std::string&, int, int)>& showLevelCard, std::atomic<bool>& renderPending);
		// a frame on the top panel: a reader's pages, or mpv's picture (what plays, a photo, the icon)
		void draw(Player& player, Player& photos, Player& shown, const Ui& ui, Uint32 now, bool refresh, Uint32 levelUntil,
			std::atomic<bool>& renderPending);
		SDL_Window* top;
		SDL_GLContext gl;
		std::string fontsDir;
		unsigned sinkFbo = 0;	// the tiny target what plays is taken into under a photo or the pages
		// the manga reader: its pages on the top panel while a chapter is open (Ui asks for it: takeMangaRead)
		MangaReader reader;
		uint64_t zoomShownGen = 0;	// the chapter the zoom card follows (a new chapter opens at 100%: no card for that)
		// Files' text viewer (TXT, EPUB): its rows on the top panel while a document is open (Ui asks: takeTextRead)
		TextReader textReader;
		bool textReady = false;
		ReaderFetch readerFetch;	// a URL chapter's pages, kept coming while reader is open
		bool readerReady = false;
		Uint32 evReader = 0;	// a page decoded: the loop wakes and takes it
		bool topBlack = false, topBlackDrawn = false;	// Top screen off (music): the panel black, drawn once
		Uint32 topOffFrom = 0;
		std::string topOffPath;
		bool topDark = false;	// the top panel cleared while there's no video
		int topDraws = 0;
		bool logNextTop = false;
		// About's support QR, large (Ui::aboutQrUp): the panel's own picture while it is up, composed at its pixels
		// (supportqr.h) and drawn 1:1 through its own renderer, so the code's edges stay the file's
		PageRenderer qrRender;
		bool qrReady = false, qrUp = false;
		int qrW = 0, qrH = 0;
		// the Preview pane (previewpane.h: Ui::topPane says which) while nothing plays: the panel's own picture as the
		// QR's is, composed again only when its key (with the theme) or the panel's size changes
		PageRenderer paneRender;
		bool paneReady = false, paneUp = false;
		std::string paneKey;
		int paneW = 0, paneH = 0;
		CardOverlay paneCard;	// the level card over the pane (mpv's is under it): cardOnReader puts it there
		bool paneCardNew = false;
		// Match album art's backdrop, the song's while it plays (docs/redesign "Match album art"; Ui holds the
		// answer): its own GL pages as the QR's are, the picture with the theme's veil baked per row — composed
		// again only when the answer, the theme or the fade changes, never per frame. Page 1 is what plays; page
		// 0 what it fades from, kept only while the cross-fade runs
		PageRenderer bgRender;
		bool bgReady = false, bgUp = false, bgFading = false;
		uint64_t bgPageOf = 0;
		int bgPageTheme = -1;
		float bgFadeStep = 1;	// the cross-fade's place (1: over), read as the pages draw
		// the native visualizers (src/topviz.cpp): the style's levels and holds, when it last drew
		VizStyleState nativeState;
		Uint32 nativeAt = 0;
		bool nativeUp = false;
		// what each frame costs (the 5 s viz-native draw line: the handheld's log says where the time goes)
		struct NativeCost
		{
			uint64_t since = 0;	// (performance counter) the window's start
			int frames = 0, mpvRenders = 0, logs = 0;
			double mpvMs = 0, buildMs = 0, glMs = 0, swapMs = 0, swapMax = 0, gpuMs = 0, gapMax = 0;
			long verts = 0, draws = 0, fillPx = 0, blendPx = 0;
			int vertsMax = 0;
		} nativeCost;
		uint64_t nativeLast = 0;	// (performance counter) the last frame drawn
		uint64_t nativeNext = 0;	// (performance counter) when the next frame is due
		static constexpr double NATIVE_EARLY_MS = 1.5;	// a frame drawn this early counts as due (the loop's wake is ms-grained)
		bool nativeLayerDirty = false;	// mpv has a new picture for the layer
		bool nativeActive = false;	// the last pass drew (or would have drawn) the native style, not a reader's pages
		// the loop's wait: ms till the native style's next frame is due, -1 when none is (not up, paused)
		int nativeWaitMs(Player& shown);
		// mpv's picture alone on the theme's ground while its own ground is clear (a song's visualizer not up yet)
		void groundFrame(Player& shown, int w, int h);
		// the song's backdrop pages composed from Ui's answer (its hash, the theme, the fade's state say when);
		// dropped when the song ends, the switch goes off or High contrast is set
		// (both: true when the pages changed, so a still panel — Album art only — is drawn again)
		bool composeBackdrop(const Ui& ui, const Player& shown);
		bool dropBackdropPages();
		// the pages into the panel (the context current): the fade's pair while one runs; false when they
		// could not be drawn (no GL program), so the theme's ground is cleared instead
		bool drawBackdropPages(int w, int h);
		// the newer page's bytes again for the native styles' ground ink (topviz.cpp: groundTexture), with a serial
		// that moves when they change
		std::shared_ptr<const std::vector<uint8_t>> bgGroundPage;
		uint64_t bgGroundSerial = 0;

	private:
		// the QR drawn 1:1 into the top window (the context made current here); false when it couldn't be composed
		bool drawQr(int w, int h);
		// the pane drawn 1:1 (composed first when it changed); false when it couldn't be
		bool drawPane(const PaneSpec& spec, const std::string& key, int w, int h);
		void drawPaneNow(int w, int h);	// the composed pane and the card over it
		// the native visualizer's frame: drawn when the tap has new bars or mpv a new frame, then swapped (topviz.cpp)
		void drawNative(Player& shown, std::atomic<bool>& renderPending, bool refresh);
		// a frame, not swapped (screenshots too): the ground, the shapes (withShapes), mpv's layer over them (rendered
		// again when mpvNew or it has none yet)
		void nativeFrame(Player& shown, int w, int h, bool mpvNew, bool withShapes);
		double nativePeriod(const Player& shown) const;	// ms between frames at the Visualizer frame rate setting
		// the text viewer's look (Settings: the Text viewer's rows)
		TextReader::Style textStyle(const Settings& s) const;
		Uint32 slowLogged = 0;	// when a take under the pages was last logged (once per 5 s)
	};
}
