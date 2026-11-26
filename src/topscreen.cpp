// The top panel's own state and what main asks of it (topscreen.h). The bodies here were main's: the reader and text
// viewer, the frame taken from mpv under them, the level card over them, and the panel's drawing and screenshots.
#include "topscreen.h"
#include "epub.h"
#include "framewatch.h"
#include "javaext.h"
#include "levelcard.h"
#include "manganet.h"
#include "player.h"
#include "previewpane.h"
#include "supportqr.h"
#include "textsource.h"
#include "theme.h"
#include "ui.h"
#include "ui_docs.h"
#include "ui_sleep.h"
#include "ui_glyphs.h"
#include "ui_manga.h"
#include "ui_reader.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <strings.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace bp
{
	TopScreen::TopScreen(SDL_Window* win, SDL_GLContext ctx, std::string fonts)
		: top(win), gl(ctx), fontsDir(std::move(fonts)), evReader(SDL_RegisterEvents(1))
	{
	}

	bool TopScreen::onTop(const Ui& ui) const
	{
		return (reader.isOpen() || textReader.isOpen() || ui.aboutQrUp()) && !ui.topShowsPlayer();
	}

	void TopScreen::drainUnder(Player& player, Player& photos, bool readerOnTop)
	{
		// (and under the Theme row's preview pane, which covers whatever plays)
		if ((!photos.playing() && !readerOnTop && !paneUp && !topBlack) || !player.playing() || !player.frameDue()) return;
		SDL_GL_MakeCurrent(top, gl);
		if (!sinkFbo)
		{
			using Gen = void (*)(int, unsigned*);
			using Bind = void (*)(unsigned, unsigned);
			using TexImage = void (*)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
			using FbTex = void (*)(unsigned, unsigned, unsigned, unsigned, int);
			auto genFb = (Gen)SDL_GL_GetProcAddress("glGenFramebuffers");
			auto bindFb = (Bind)SDL_GL_GetProcAddress("glBindFramebuffer");
			auto genTex = (Gen)SDL_GL_GetProcAddress("glGenTextures");
			auto bindTex = (Bind)SDL_GL_GetProcAddress("glBindTexture");
			auto texImage = (TexImage)SDL_GL_GetProcAddress("glTexImage2D");
			auto fbTex = (FbTex)SDL_GL_GetProcAddress("glFramebufferTexture2D");
			if (!genFb || !bindFb || !genTex || !bindTex || !texImage || !fbTex) return;
			unsigned tex = 0;
			genTex(1, &tex);
			bindTex(0x0DE1 /* GL_TEXTURE_2D */, tex);
			texImage(0x0DE1, 0, 0x1908 /* GL_RGBA */, 16, 16, 0, 0x1908, 0x1401 /* GL_UNSIGNED_BYTE */, nullptr);
			bindTex(0x0DE1, 0);
			genFb(1, &sinkFbo);
			bindFb(0x8D40 /* GL_FRAMEBUFFER */, sinkFbo);
			fbTex(0x8D40, 0x8CE0 /* GL_COLOR_ATTACHMENT0 */, 0x0DE1, tex, 0);
			bindFb(0x8D40, 0);
		}
		// unseen: taken, not drawn (MPV_RENDER_PARAM_SKIP_RENDERING), and not waited for; a slow take is logged (it
		// would show as a stutter in the reader or a photo's pan while music plays)
		Uint32 t0 = SDL_GetTicks();
		player.render(16, 16, sinkFbo, true);
		player.reportSwap();
		Uint32 took = SDL_GetTicks() - t0;
		if (took > 8 && SDL_GetTicks() - slowLogged > 5000)
		{
			slowLogged = SDL_GetTicks();
			fprintf(stderr, "timing: a frame under the pages took %u ms to take\n", took);
		}
	}

	SDL_Surface* TopScreen::capture(Player& shown, bool readerOnTop, bool qr)
	{
		SDL_GL_MakeCurrent(top, gl);
		int w, h;
		SDL_GL_GetDrawableSize(top, &w, &h);
		using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void*);
		auto readPixels = (ReadPixels)SDL_GL_GetProcAddress("glReadPixels");
		if (!readPixels) return nullptr;
		if (qr && drawQr(w, h)) {}	// About's support QR, large
		else if (paneUp && paneReady) drawPaneNow(w, h);	// the Preview pane
		else if (reader.isOpen() && readerOnTop) reader.draw(w, h);	// a manga chapter's pages
		else if (textReader.isOpen() && readerOnTop) textReader.draw(w, h);	// a document's text
		else if (shown.nativeShown() && nativeUp) nativeFrame(shown, w, h, true, true);	// the native visualizer and mpv's layer over it
		else if (shown.groundClear()) groundFrame(shown, w, h);	// a song's picture before its visualizer is up
		else
		{
			shown.render(w, h);	// a video, a photo, or the icon with nothing playing
			if (bgLeaving && bgUp && bgReady) drawBackdropPages(w, h, true);	// (a song just stopped: its page, part way out)
		}
		std::vector<unsigned char> rgba((size_t)w * h * 4);
		readPixels(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, rgba.data());
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
		if (s)
			for (int y = 0; y < h; y++)	// GL's rows go up
				memcpy((unsigned char*)s->pixels + (size_t)y * s->pitch, &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
		SDL_GL_SwapWindow(top);
		shown.reportSwap();
		return s;
	}

	bool TopScreen::drawQr(int w, int h)
	{
		SDL_GL_MakeCurrent(top, gl);
		if (!qrReady)
		{
#ifdef BP_GLES
			qrReady = qrRender.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, true);
#else
			qrReady = qrRender.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, false);
#endif
			qrRender.setUploadAll(true);	// (two tiles: up in the frame that shows it)
		}
		if (!qrReady) return false;
		if (!qrUp || qrW != w || qrH != h)
		{
			// a new panel size (or the first time): the picture at its pixels, so the code lands on whole ones
			SupportQrImage img = composeSupportQr(fontsDir, w, h);
			qrW = w;
			qrH = h;	// (a failure is not retried every pass: refresh asks again)
			if (!img.ok()) return false;
			qrRender.addPage(0, img.w, img.h, std::make_shared<const std::vector<uint8_t>>(std::move(img.rgba)));
			qrUp = true;
		}
		qrRender.draw({{0, 0, 0, (float)w, (float)h}}, w, h);
		return true;
	}

	bool TopScreen::drawPane(const PaneSpec& spec, const std::string& key, int w, int h)
	{
		SDL_GL_MakeCurrent(top, gl);
		if (!paneReady)
		{
#ifdef BP_GLES
			paneReady = paneRender.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, true);
#else
			paneReady = paneRender.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, false);
#endif
			paneRender.setUploadAll(true);	// (all its tiles up in the frame that shows it)
		}
		if (!paneReady) return false;
		if (!paneUp || key != paneKey || paneW != w || paneH != h)
		{
			Uint32 t0 = SDL_GetTicks();
			PaneSpec withIcon;
			const PaneSpec* use = &spec;
			if (spec.welcome)
			{
				// the app's icon (assets/icons/app.png beside the fonts), decoded at the panel's 84 units, kept
				int px = std::max(1, (int)std::lround(84.0 * h / 480));
				if (welcomeIconPx != px)
				{
					PageImage img = decodePageFile(fontsDir + "/../icons/app.png", px);
					welcomeIcon = PaneImage{};
					if (img.ok())
					{
						welcomeIcon.w = img.w;
						welcomeIcon.h = img.h;
						welcomeIcon.rgba = img.rgba;
					}
					welcomeIconPx = px;
				}
				withIcon = spec;
				withIcon.icon = welcomeIcon;
				use = &withIcon;
			}
			PaneImage img = composePane(fontsDir, w, h, *use);
			// (a failure isn't tried again every pass: the key is taken; the 2 s refresh or a change asks again)
			bool first = !paneUp;
			paneKey = key;
			paneW = w;
			paneH = h;
			if (!img.ok()) return false;
			paneRender.addPage(0, img.w, img.h, std::make_shared<const std::vector<uint8_t>>(std::move(img.rgba)));
			paneUp = true;
			// in the log: the pane up and each change (a top panel stuck on it would show here)
			fprintf(stderr, "top: preview pane %s %s %dx%d in %u ms\n", first ? "up" : "now", key.c_str(), w, h, SDL_GetTicks() - t0);
		}
		drawPaneNow(w, h);
		return true;
	}

	void TopScreen::drawPaneNow(int w, int h)
	{
		// the pane, and the level card over it while one is up (mpv's own would be under it)
		std::vector<PageRenderer::Placed> placed{{0, 0, 0, (float)w, (float)h}};
		paneCard.place(paneRender, placed, w, h, SDL_GetTicks());
		paneRender.draw(placed, w, h);
	}

	bool TopScreen::composeBackdrop(const Ui& ui, const Player& shown)
	{
		// composed again when the answer, the theme or the fade's state moves (a fade's pages are two: what it
		// fades from, and what plays at the fade's own alpha — the alpha is per draw, not per compose)
		uint64_t hash = ui.backdropHash();
		int theme = theme::currentTheme();
		bool fading = ui.backdropFading() && ui.backdropFadeAnswered();
		if (bgUp && hash == bgPageOf && theme == bgPageTheme && fading == bgFading) return false;
		// the fade just ended: what it faded from goes (the answer's page stays alone)
		if (bgUp && !fading && bgFading)
		{
			SDL_GL_MakeCurrent(top, gl);
			bgRender.keepOnly({1});
			bgFading = false;
			if (hash == bgPageOf && theme == bgPageTheme) return true;
		}
		if (!bgReady)
		{
			SDL_GL_MakeCurrent(top, gl);
	#ifdef BP_GLES
			bgReady = bgRender.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, true);
	#else
			bgReady = bgRender.init([](const char* n) { return SDL_GL_GetProcAddress(n); }, false);
	#endif
			if (bgReady)
			{
				bgRender.setUploadAll(true);	// (small pages: up in the frame that shows them)
				fprintf(stderr, "backdrop: the top panel composes the song's ground\n");
			}
		}
		if (!bgReady) return false;
		artbg::ThemeColours th;
		th.bg = theme::current.BG;
		th.surface = theme::current.SURFACE;
		th.text = theme::current.TEXT;
		th.dimOn = theme::current.DIM_ON;
		th.accent = theme::current.ACCENT;
		auto add = [&](int page, const artbg::Art& art) {
			auto bytes = std::make_shared<const std::vector<uint8_t>>(artbg::composePage(art, artbg::backdropFor(th, art), artbg::W, 480, artbg::Half::Upper));
			if (!bytes->empty()) bgRender.addPage(page, artbg::W, 480, bytes);
			if (page == 1)
			{
				bgGroundPage = bytes;
				bgGroundSerial++;
			}
		};
		if (fading) add(0, ui.backdropFadeArt());
		add(1, ui.backdropArt());
		SDL_GL_MakeCurrent(top, gl);
		if (fading) bgRender.keepOnly({0, 1});
		else bgRender.keepOnly({1});
		bgUp = true;
		bgFading = fading;
		bgPageOf = hash;
		bgPageTheme = theme;
		return true;
	}

	bool TopScreen::keepLeavingPage()
	{
		// the song's own page is page 1; a cross-fade that was running loses the page it came from
		if (!bgFading) return false;
		SDL_GL_MakeCurrent(top, gl);
		bgRender.keepOnly({1});
		bgFading = false;
		return true;
	}

	bool TopScreen::dropBackdropPages()
	{
		if (!bgUp && !bgFading && bgPageOf == 0) return false;
		SDL_GL_MakeCurrent(top, gl);
		bgRender.keepOnly({});
		bgRender.flushDead();
		bgUp = false;
		bgFading = false;
		bgPageOf = 0;
		bgPageTheme = -1;
		return true;
	}

	void TopScreen::cardOnReader(const std::string& label, int percent, bool bar, int barTop, int ms)
	{
		int tw = 0, th = 0;
		SDL_GL_GetDrawableSize(top, &tw, &th);
		cardImageOnReader(drawLevelCard(fontsDir, th, label, percent, barTop, levelIcon(label)), ms);
	}

	void TopScreen::cardImageOnReader(const LevelCardImage& c, int ms)
	{
		SDL_GL_MakeCurrent(top, gl);
		int64_t until = (int64_t)SDL_GetTicks() + ms;
		if (c.ok() && paneUp)
		{
			paneCard.show(paneRender, c.rgba, c.w, c.h, until);
			paneCardNew = true;
		}
		else if (c.ok() && reader.isOpen()) reader.showOverlay(c.rgba, c.w, c.h, until);
		else if (c.ok()) textReader.showOverlay(c.rgba, c.w, c.h, until);
	}

	void TopScreen::notesOnReader(Ui& ui, const Player& player, Uint32 now)
	{
		int tw = 0, th = 0;
		SDL_GL_GetDrawableSize(top, &tw, &th);
		auto show = [&](const LevelCardImage& c, int ms) {
			if (!c.ok()) return;
			SDL_GL_MakeCurrent(top, gl);
			reader.showNote(c.rgba, c.w, c.h, (int64_t)now + ms);
		};
		Ui::PageReader::TopNote& note = ui.pageReader->topNote;
		if (note.ms > 0)
		{
			show(drawNoteCard(fontsDir, th, {G_BOOKMARK_ROWS, G_BOOKMARK.h}, note.line1, note.line2, false), note.ms);
			fprintf(stderr, "reader: note (%s)\n", note.line1.c_str());
			note = {};
		}
		// the chapter's end: once each time the view comes to it (a new chapter starts over), its pages in
		if (reader.chapterGen() != endCardGen)
		{
			endCardGen = reader.chapterGen();
			endCardShown = false;
		}
		bool atEnd = reader.atEnd() && !reader.loading();
		if (!reader.atEnd()) endCardShown = false;
		else if (atEnd && !endCardShown)
		{
			endCardShown = true;
			Ui::Manga::EndCard e;
			if (ui.manga->mangaEndCard(e, player))
			{
				show(drawEndCard(fontsDir, th, e.label, e.title, e.line), 4000);
				fprintf(stderr, "reader: end card (%s / %s)\n", e.title.c_str(), e.line.c_str());
			}
		}
	}

	TextReader::Style TopScreen::textStyle(const Settings& s) const
	{
		TextReader::Style st;
		st.font = s.txtFont;
		st.size = s.txtSize;
		st.spacing = s.txtSpacing;
		st.margins = s.txtMargins;
		st.theme = txtPage(s);
		st.filter = s.txtFilter;
		st.dim = s.readDim;
		return st;
	}

	void TopScreen::syncReaders(Ui& ui, Player& player, MangaNet* mangaNet, const std::string& stateDir, Uint32 now, bool woke,
		bool haveUi, const std::function<void(const std::string&, int, int)>& showLevelCard, std::atomic<bool>& renderPending)
	{
		Ui::PageReader::MangaReadRequest rq;
		if (ui.pageReader->takeMangaRead(rq))
		{
			readerFetch.end();
			SDL_GL_MakeCurrent(top, gl);
#ifdef BP_GLES
			if (!readerReady) readerReady = reader.init(true, evReader);
#else
			if (!readerReady) readerReady = reader.init(false, evReader);
#endif
			int w, h;
			SDL_GL_GetDrawableSize(top, &w, &h);
			ui.pageReader->readerStatus = {};
			if (!readerReady) ui.pageReader->readerStatus.error = "The reader couldn't start";
			else if (!rq.pageUrls.empty())
			{
				// a source's or a server's chapter: the pages come through MangaNet into the Ui's own page
				// cache (its Clear cache empties the same folder)
				if (stateDir.empty()) ui.pageReader->readerStatus.error = "No state folder for the chapter's pages";
				else if (!mangaNet || !player.settings.mangaOn)
					ui.pageReader->readerStatus.error = "The manga reader's network side is off";
				else
				{
					mangaNet->start();
					PageCache& cache = ui.manga->mangaPageCacheRef();
					uint64_t limit = mangaCacheBytes(player.settings.mangaCacheSize);
					if (!cache.open(stateDir + "/manga/cache", limit))
						ui.pageReader->readerStatus.error = "The page cache couldn't be opened";
					else if (!reader.open(rq.pageUrls, rq.pageHeaders, cache, rq.path, rq.page, w, h, rq.mode,
								 rq.fit))
						ui.pageReader->readerStatus.error = reader.error();
					else
						readerFetch.begin(mangaNet, &cache, &reader, rq.pageUrls, rq.pageHeaders, limit);
				}
			}
			else if (!reader.open(rq.path, rq.archive, rq.page, w, h, rq.mode, rq.fit))
				ui.pageReader->readerStatus.error = reader.error();
			// the series' own Book (or off), each chapter: it turns the pages as they are decoded
			if (reader.isOpen()) reader.command(MangaReader::Book, rq.book, (int64_t)now);
			// back to the place inside the page it was left at (Vertical scroll: a PDF's tall page)
			if (reader.isOpen() && rq.within > 0) reader.scrollWithin(rq.page, rq.within);
		}
		for (auto& c : ui.pageReader->readerCmds)
		{
			if (c.kind == Ui::PageReader::ReaderCmd::Close)
			{
				readerFetch.end();
				SDL_GL_MakeCurrent(top, gl);	// (close frees the pages' textures in this context)
				reader.close();
			}
			else reader.command((int)c.kind, c.v, c.ms, c.v2, c.v3);
		}
		ui.pageReader->readerCmds.clear();
		// Files' text viewer: a document asked for, the bottom panel's commands, where it is told back
		{
			Ui::Docs& docs = *ui.docs;
			Ui::Docs::TextReadRequest tq;
			if (docs.takeTextRead(tq))
			{
				SDL_GL_MakeCurrent(top, gl);
#ifdef BP_GLES
				if (!textReady) textReady = textReader.init(true, fontsDir);
#else
				if (!textReady) textReady = textReader.init(false, fontsDir);
#endif
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				std::string err, title = docs.textStatus.title;
				bool epub = tq.path.size() > 5 && strcasecmp(tq.path.c_str() + tq.path.size() - 5, ".epub") == 0;
				auto src = !textReady ? nullptr : epub ? openEpubText(tq.path, err) : openTextFile(tq.path, err);
				docs.textStatus = {};
				docs.textStatus.title = title;
				docs.textEncoding.clear();
				if (!textReady) docs.textStatus.error = "The text viewer couldn't start";
				else if (!src) docs.textStatus.error = err;
				else if (textReader.setBook(player.settings.txtBook), false) {}
				else if (Uint32 t0 = SDL_GetTicks(); !textReader.open(std::move(src), tq.place, w, h, textStyle(player.settings)) ||
					(fprintf(stderr, "text: open took %u ms\n", SDL_GetTicks() - t0), false))
					docs.textStatus.error = "The text viewer couldn't open it";
				else
				{
					// (the contents once: an EPUB's is read from its navigation document)
					for (auto& c : textReader.contents()) docs.textStatus.contents.push_back({c.title, {c.id, c.depth}});
					if (!textReader.title().empty()) docs.textStatus.title = textReader.title();
					fprintf(stderr, "text: opened %s (%s), %zu in the contents\n", tq.path.c_str(),
						textReader.encoding().empty() ? "book" : textReader.encoding().c_str(), docs.textStatus.contents.size());
				}
			}
			if (docs.textCloseAsked)
			{
				docs.textCloseAsked = false;
				SDL_GL_MakeCurrent(top, gl);	// (its textures are freed in this context)
				textReader.close();
				docs.textStatus = {};
				renderPending = true;	// (the top panel shows what plays, or the icon, again)
				topDark = false;
			}
			if (textReader.isOpen())
			{
				textReader.setStyle(textStyle(player.settings));
				textReader.setBook(player.settings.txtBook);
				textReader.setTurnFold(player.settings.txtTurn == 0);
				textReader.setHighlight(docs.docHighlight());
				if (docs.textEncodingAsked)
				{
					docs.textEncodingAsked = false;
					textReader.setEncoding(docs.textEncoding);
				}
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				// (a finger's moves come as shares of the panel: the top's pixels here)
				for (auto& c : docs.textCmds)
				{
					Uint32 t0 = SDL_GetTicks();
					textReader.command(c.kind, c.kind == TextReader::Scroll || c.kind == TextReader::Fling ? c.v * h : c.v, c.ms);
					// (a jump's cost: the text found around its place, nothing else)
					if (c.kind == TextReader::MatchStep) docs.textStatus.matchStep = textReader.matchStepped() ? 1 : -1;
					if (c.kind == TextReader::Jump || c.kind == TextReader::JumpTo || c.kind == TextReader::JumpToLast)
						fprintf(stderr, "text: jump to %.1f%% in %u ms\n", textReader.fraction() * 100, SDL_GetTicks() - t0);
				}
				docs.textStatus.open = true;
				docs.textStatus.fraction = textReader.fraction();
				docs.textStatus.place = textReader.place();
				docs.textStatus.atEnd = textReader.atEnd();
				docs.textStatus.encoding = textReader.encoding();
				docs.textStatus.snippet = textReader.snippet();
				// Book: the bottom panel's page
				docs.textStatus.book = textReader.book();
				docs.textStatus.turnDir = textReader.turnDir();
				docs.textStatus.turnAt = textReader.turnAt();
				docs.textStatus.rightOnTop = textReader.rightOnTop();
				docs.textStatus.turnMs = TextReader::turnMs();
				docs.textStatus.noticeDir = textReader.lastTurnDir();
				docs.textStatus.noticeAt = textReader.lastTurnAt();
				docs.textStatus.bottomOld = textReader.bottomBefore();
				if (textReader.bottomGen() != docs.textStatus.bottomGen)
				{
					docs.textStatus.bottomGen = textReader.bottomGen();
					docs.textStatus.bottom = textReader.bottomPage(docs.textStatus.bottomW, docs.textStatus.bottomH);
				}
				if (!textReader.error().empty()) docs.textStatus.error = textReader.error();
			}
			docs.textCmds.clear();
		}
		// a song's visualizer rests while the pages cover the top panel (nobody sees it; its frames cost CPU)
		player.setTopCovered(onTop(ui));
		// and while the manga engine starts (its ~25 s of Java start-up wants the cores the graph would take)
		player.setEngineLoading(ui.manga->mangaNet && ui.manga->mangaNet->javaExt().state() == JavaExtHost::State::Starting);
		// a URL chapter's pages: requests reordered and the free slots filled (the answers themselves come
		// through MangaNet's fetch owner above)
		if (reader.isOpen()) reader.tick(now);
		if (reader.isOpen()) readerFetch.pass();
		if (reader.isOpen())
		{
			// the row set while a chapter is open reaches the reader too (the fit comes as its own command)
			reader.setPreload(player.settings.mangaPreload);
			reader.setBookSpread(player.settings.mangaSpread, player.settings.mangaHinge);
			reader.setBookDirection(mangaBookDirection(player.settings.mangaMode));
			reader.setTurnAnimation(player.settings.mangaTurnAnim);
			// a document of Files: its reading filter, and its pan area's page and box
			{
				int f = ui.docs->docPagesFilter(player);
				// (the Manga reader's own filter; either's Extra dim)
				float r, g, b;
				docFilterTint(f >= 0 ? f : player.settings.mangaFilter, r, g, b, player.settings.readDim);
				reader.setTint(r, g, b);
				Ui::Docs::DocPan dp;
				if (f >= 0 && reader.panBox(dp.page, dp.x0, dp.y0, dp.x1, dp.y1))
				{
					int pw = 0, ph = 0;
					dp.rgba = reader.pixels(dp.page, pw, ph);
					dp.w = pw;
					dp.h = ph;
				}
				ui.docs->docPan = std::move(dp);
			}
			ui.pageReader->readerStatus.open = true;
			ui.pageReader->readerStatus.loading = reader.loading();
			ui.pageReader->readerStatus.error = reader.error();
			ui.pageReader->readerStatus.failedHere = reader.failedInView();
			ui.pageReader->readerStatus.atEnd = reader.atEnd();
			ui.pageReader->readerStatus.atStart = reader.atStart() && !reader.loading();
			ui.pageReader->readerStatus.page = reader.page();
			ui.pageReader->readerStatus.within = (float)reader.within();
			ui.pageReader->readerStatus.pages = reader.pages();
			ui.pageReader->readerStatus.mode = reader.mode();
			ui.pageReader->readerStatus.modeSet = reader.modeSet();
			// a zoom changed (keys, the menu, a double-tap): its percent on the pages a moment, fading out
			if (reader.zoomPct() != ui.pageReader->readerStatus.zoomPct && reader.chapterGen() == zoomShownGen && haveUi)
			{
				showLevelCard("Zoom", reader.zoomPct(), 1200);
			}
			zoomShownGen = reader.chapterGen();
			if (haveUi) notesOnReader(ui, player, now);
			ui.pageReader->readerStatus.zoomPct = reader.zoomPct();
			ui.pageReader->readerStatus.fullscreen = reader.fullscreen();
			ui.pageReader->readerStatus.rotation = reader.rotation();
			ui.pageReader->readerStatus.book = reader.book();
			ui.pageReader->readerStatus.bookCount = reader.bookCount();
			// fullscreen: the bottom panel's share of the view, as fractions of the panel, and the pixels it needs
			Ui::PageReader::ReaderBottom rb;
			rb.on = reader.bothPanels();	// fullscreen, or Book (a page on each panel)
			rb.gen = reader.chapterGen();
			if (rb.on)
			{
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				for (auto& b : reader.bottomBoxes())
				{
					rb.boxes.push_back({b.page, b.x / w, b.y / h, b.w / w, b.h / h, reader.pageWaiting(b.page)});
					int pw = 0, ph = 0;
					if (auto px = reader.pixels(b.page, pw, ph)) rb.pages.push_back({b.page, pw, ph, px});
				}
			}
			ui.pageReader->readerBottom = std::move(rb);
			// the pages grid's thumbnails: every page the reader has decoded, ~96 px wide RGBA.
			// always filled while the reader is open (the Ui only uses them when the grid shows);
			// shared_ptr copies are cheap, and absent pages are placeholders in the grid
			Ui::PageReader::ReaderThumbs rt;
			rt.on = true;
			rt.gen = reader.chapterGen();
			for (int pi = 0; pi < reader.pages(); pi++)
			{
				int tw = 0, th = 0;
				if (auto tp = reader.thumbPixels(pi, tw, th))
					rt.pages.push_back({pi, tw, th, tp});
			}
			ui.pageReader->readerThumbs = std::move(rt);
		}
		else
		{
			readerFetch.end();
			ui.pageReader->readerStatus.open = false;
			ui.pageReader->readerBottom = {};
			ui.pageReader->readerThumbs = {};
		}
		if (woke) reader.redrawSoon();
	}

	void TopScreen::draw(Player& player, Player& photos, Player& shown, const Ui& ui, Uint32 now, bool refresh, Uint32 levelUntil,
		std::atomic<bool>& renderPending)
	{
		// the native visualizer gone (another style, a pause of the song's display, the song's end): mpv's picture
		// is drawn again at once, not at its next frame
		nativeActive = false;	// (set again below if this pass draws the native style: the loop's wait follows it)
		if (nativeUp && !shown.nativeShown())
		{
			nativeUp = false;
			renderPending = true;
		}
		// About's support QR up: the panel is that picture (down again: its texture goes, in this context)
		bool qrNow = ui.aboutQrUp();
		if (!qrNow && qrUp)
		{
			qrUp = false;
			qrW = qrH = 0;	// (shown again: composed again)
			SDL_GL_MakeCurrent(top, gl);
			qrRender.keepOnly({});
			qrRender.flushDead();
		}
		// the Preview pane, whatever plays (the Theme row's: the one help that takes the top screen from a picture,
		// docs/redesign Themes › Where it is changed): a video or a song goes on under it, its frames taken unseen
		// (drainUnder), its sound on. Not while a reader has the panel. Down again, mpv's picture is drawn once more
		PaneSpec pane;
		bool idle = !shown.playing() && !photos.playing() && !player.topCardUp();
		// the State card (Playing in your browser, Loading, Buffering) is the pane too, over what the cursor would show
		const PaneSpec* stateCard = player.topState() ? player.topState() : photos.topState();
		bool free = !qrNow && !reader.isOpen() && !textReader.isOpen();
		bool paneNow = free && (stateCard ? (pane = *stateCard, true) : ui.topPane(pane, player, idle));
		if (!paneNow && paneUp)
		{
			paneUp = false;
			paneKey.clear();
			paneCard = CardOverlay{};
			SDL_GL_MakeCurrent(top, gl);
			paneRender.keepOnly({});
			paneRender.flushDead();
			topDark = false;
			renderPending = true;	// (what plays: its picture at once, not at its next frame)
			fprintf(stderr, "top: preview pane dropped%s\n", player.playing() || photos.playing() ? ": the picture of what plays is back" : "");
		}
		// Match album art's backdrop, the song's while it plays: composed again only when the answer, the theme
		// or the fade moves (whatever else has the panel — the QR, the pane, a reader — draws over it), dropped
		// with the song's end, the switch off or High contrast
		// A song that stopped: its last page stays up over mpv's idle picture while it fades out (the bottom panel's
		// half second, Ui::BG_FADE_MS); one textured quad a frame, at an alpha falling to nothing
		bgLeaving = !ui.backdropOn(shown) && ui.backdropLeaving() && ui.backdropFadeAnswered() && bgUp;
		if (ui.backdropOn(shown) ? composeBackdrop(ui, shown) : bgLeaving ? keepLeavingPage() : dropBackdropPages())
			renderPending = true;	// (a still panel — Album art only, paused — shows it now, not at mpv's next frame)
		// the cross-fade's place this pass (bgFadeStep, as the pages draw it): over, unless one is running; a leave
		// runs it the other way, from over to gone
		if (bgLeaving)
			bgFadeStep = 1.0f - (float)std::min(1.0, (double)(SDL_GetTicks() - ui.backdropLeaveAt()) / Ui::BG_FADE_MS);
		else
			bgFadeStep = ui.backdropFading() && ui.backdropFadeAnswered()
				? (float)std::min(1.0, (double)(SDL_GetTicks() - ui.backdropFadeAt()) / Ui::BG_FADE_MS) : 1.0f;
		// black, drawn once, mpv's frames taken unseen (drainUnder); the backlight is untouched
		bool sleepBlack = false;
		auto drawBlack = [&](const char* why) {
			renderPending = false;
			if (topBlackDrawn) return;
			using Clear = void (*)(float, float, float, float);
			using Cl = void (*)(unsigned);
			auto color = (Clear)SDL_GL_GetProcAddress("glClearColor");
			auto clear = (Cl)SDL_GL_GetProcAddress("glClear");
			SDL_GL_MakeCurrent(top, gl);
			if (color && clear)
			{
				color(0, 0, 0, 1);
				clear(0x4000 /* GL_COLOR_BUFFER_BIT */);
				SDL_GL_SwapWindow(top);
			}
			topBlackDrawn = true;
			fprintf(stderr, "top: black (%s)\n", why);
		};
		// Top screen off (music): a song playing, no press or touch for the delay (a new song counts from its start)
		{
			int secs = screenOffSeconds(shown.settings.topScreenOff);
			if (shown.currentPath() != topOffPath)
			{
				topOffPath = shown.currentPath();
				topOffFrom = now;
			}
			bool black = secs > 0 && !qrNow && !paneNow && !reader.isOpen() && !textReader.isOpen() && shown.playingAudio() &&
				shown.playing() && !shown.paused && !photos.playing() && now - std::max(ui.lastInputAt(), topOffFrom) >= (Uint32)secs * 1000;
			// the sleep timer ran out: dark whatever the panel showed, until a key or a touch (ui_sleep.h)
			sleepBlack = ui.sleep && ui.sleep->dark();
			black = black || sleepBlack;
			if (black != topBlack)
			{
				topBlack = black;
				topBlackDrawn = false;
				if (!black)
				{
					topDark = false;
					renderPending = true;	// (what plays: drawn at once)
					refresh = true;
				}
			}
		}
		if (sleepBlack) drawBlack("the sleep timer");
		else if (qrNow)
		{
			// About's support QR, large: the panel's own picture, 1:1 (a still: drawn again when it changes, and
			// with the 2 s refresh, so a failed compose gets another try)
			renderPending = false;
			int w, h;
			SDL_GL_GetDrawableSize(top, &w, &h);
			if ((qrW != w || qrH != h || refresh) && drawQr(w, h)) SDL_GL_SwapWindow(top);
		}
		else if (paneNow)
		{
			// a still, as the QR: drawn when its key (the theme in it) or the size changes, and with the 2 s refresh
			renderPending = false;
			int w, h;
			SDL_GL_GetDrawableSize(top, &w, &h);
			std::string key = pane.key + "|" + theme::themeId(theme::currentTheme());
			if (!paneUp && (player.playing() || photos.playing()))
				fprintf(stderr, "top: preview pane over what plays (%s): it goes on under it\n", player.playing() ? player.currentPath().c_str() : "a photo");
			// the level card over it: up, fading, or just down (its texture let go)
			bool ended = false, cardNeed = paneCard.frame(SDL_GetTicks(), ended) || paneCardNew;
			paneCardNew = false;
			if (ended && paneUp) paneRender.keepOnly({0});
			if ((key != paneKey || paneW != w || paneH != h || refresh || cardNeed) && drawPane(pane, key, w, h)) SDL_GL_SwapWindow(top);
		}
		else if (reader.isOpen() && onTop(ui))
		{
			// the pages, not mpv's picture (what plays goes on under them: drainUnder)
			renderPending = false;
			bool pageFrame;
			{
				FrameWatchScope fw("reader frame");
				pageFrame = reader.frame(now);
			}
			if (pageFrame || refresh)
			{
				SDL_GL_MakeCurrent(top, gl);
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				{
					FrameWatchScope fw("reader draw");
					reader.draw(w, h);
				}
				SDL_GL_SwapWindow(top);
			}
		}
		else if (textReader.isOpen() && onTop(ui))
		{
			// a document's text, not mpv's picture (what plays goes on under it: drainUnder)
			renderPending = false;
			if (textReader.frame(now) || refresh)
			{
				SDL_GL_MakeCurrent(top, gl);
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				textReader.draw(w, h);
				SDL_GL_SwapWindow(top);
			}
		}
		// Top screen off (music): black, drawn once, mpv's frames taken unseen (drainUnder); the backlight is untouched
		else if (topBlack) drawBlack("Top screen off");
		// the native visualizer (Mirror bars on a local song): the player's own bars over mpv's picture, at the tap's
		// frame rate (src/topviz.cpp)
		else if (shown.nativeShown() && !photos.playing())
		{
			nativeActive = true;
			drawNative(shown, renderPending, refresh);
		}
		// (a photo is a still: mpv puts the level card on it later than the key's redraw, and asks for no frame
		// after, so it's drawn again while the card is up, as with nothing playing)
		else if (renderPending.exchange(false) || refresh || (bgUp && bgFading) || bgLeaving ||
			(!shown.playing() && (!topDark || levelUntil)) || (photos.playing() && levelUntil))
		{
			SDL_GL_MakeCurrent(top, gl);
			int w, h;
			SDL_GL_GetDrawableSize(top, &w, &h);
			if (!shown.playing())
			{
				// nothing playing: mpv draws its black with the icon (and a level card if one is up); its ground still
				// clear for a moment after a song's visualizer: on the theme's ground
				int r = 0;
				if (player.groundClear()) groundFrame(player, w, h);
				else
				{
					r = player.render(w, h);
					if (bgLeaving && bgUp && bgReady) drawBackdropPages(w, h, true);	// the song's page, fading out over the idle picture
				}
				if (r < 0 && topDraws < 5) fprintf(stderr, "top: mpv couldn't draw (%d) at %u ms\n", r, SDL_GetTicks());
				SDL_GL_SwapWindow(top);
				player.reportSwap();
				topDark = true;
				// in the log: the top panel drawn with nothing playing (the first time, and after a level card),
				// to find where a black top panel comes from
				if (topDraws++ == 0 || logNextTop)
					fprintf(stderr, "top: drawn idle %dx%d at %u ms (draw %d)%s\n", w, h, SDL_GetTicks(), topDraws,
						logNextTop ? " with the level card" : "");
				logNextTop = false;
			}
			// a new frame, or the same one again (paused, or a refresh); a photo's over what plays. The backdrop's
			// cross-fade steps here too (mpv's picture needs no new frame for it: the layer it is composed from)
			else if (shown.frameDue() || refresh || (bgUp && bgFading))
			{
				if (shown.groundClear()) groundFrame(shown, w, h);	// (a song's visualizer not up yet)
				else shown.render(w, h);
				SDL_GL_SwapWindow(top);
				shown.reportSwap();
			}
		}
	}
}
