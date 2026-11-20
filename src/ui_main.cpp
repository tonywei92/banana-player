#include "ui.h"
#include "session_host.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include "theme.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#ifdef BP_WEB
#include "web/netif.h"
#include "web/server.h"
#include <qrcodegen.hpp>
#endif
#include "ui_internal.h"
#include "ui_kit.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"
#include "ui_sound.h"
#include "ui_shell.h"

namespace bp
{
	bool Ui::pageIsOut() const { return SessionHost::get().pageIsOutput(); }

	int Ui::takeOverHint(const Player& p) const
	{
		// (as tick() takes over: the player with no menu over it)
		(void)p;
		return page == Page::Main ? 1 : 2;
	}

	// (the Session's: a page the output of the file held here, its place by posNow, its play state the session's wish)
	bool Ui::predicting(const Player& p) const
	{
		return pageIsOut() && p.loaded && p.duration > 0 && p.currentPath() == SessionHost::get().file();
	}

	bool Ui::pageDrives(const Player& p) const
	{
		return pageIsOut() && webClients() > 0 && p.playing() && !p.playingPhoto() && (p.paused || p.loading());
	}

	double Ui::shownPosition(const Player& p) const
	{
		if (!predicting(p)) return p.position;
		return std::clamp(posNow(SessionHost::get().session(), SDL_GetTicks()), 0.0, p.duration);
	}

	int Ui::shownChapter(const Player& p) const
	{
		if (!predicting(p)) return p.currentChapter();
		double t = shownPosition(p);
		int c = -1;
		for (int i = 0; i < (int)p.chapters.size() && p.chapters[i].time <= t; i++) c = i;
		return c;
	}

	void Ui::noteSeek(const Player& p, double t)
	{
		// a seek made here while a page plays it: the session's Seek, which the page follows
		if (predicting(p)) SessionHost::get().seekLive(std::clamp(t, 0.0, p.duration), false);
	}

	bool Ui::pagePlaying() const { return SessionHost::get().session().playing; }

	void Ui::toggleBrowser(Player& p)
	{
		(void)p;
		// the session's Play / Pause, from what it wishes now (quick presses each flip the last one)
		SessionHost& sh = SessionHost::get();
		sh.playLive(!sh.session().playing);
	}

	void Ui::takeOver(Player& p)
	{
		// the handheld the output (the Session's SetOutput: here at the page's place, with its tracks) and playing
		SessionHost::get().takeOver();
		p.osd("Playing on the handheld");
	}

	void Ui::release(Button b, Player& p)
	{
		if (Area* a = area()) a->release(b, p);
		if (b != Button::A || !playHoldAt || playHoldTouch) return;
		playHoldAt = 0;	// let go before PLAY_HOLD_MS (tick() takes over at it, and clears this)
		if (pageDrives(p)) toggleBrowser(p);
	}

	bool Ui::repeats(Button b, const Player& p) const
	{
		// a photo: on to the next, and zooming, while held
		if (page == Page::Main && p.playingPhoto())
			return b == Button::Left || b == Button::Right || b == Button::L || b == Button::R || b == Button::Up || b == Button::Down;
		if (b == Button::L2 || b == Button::R2) return false;
		if (b == Button::Left || b == Button::Right) return true;
		if (b == Button::L || b == Button::R) return false;	// (the browser's tabs)
		// (the player's face: UP / DOWN are the volume; with a sheet, a menu or a picker over it they move the cursor
		// one a press, as before)
		if (page == Page::Main)
			return !topOverlay() && !ctxOpen && !kbOpen && !browse->playlists->pickerOpen && (b == Button::Up || b == Button::Down);
		return b == Button::Up || b == Button::Down;
	}

	int Ui::repeatMs(Button b, const Player& p) const
	{
		// the player's volume: eight a second, as a volume key held does
		if (page == Page::Main && !p.playingPhoto() && (b == Button::Up || b == Button::Down)) return 125;
		return 150;
	}

	// a touch button: what its button does, whether the buttons themselves are on or not
	bool Ui::pressAnyway(Button b, Player& p)
	{
		bool keep = p.settings.buttonHotkeys;
		p.settings.buttonHotkeys = true;
		bool r = press(b, p);
		p.settings.buttonHotkeys = keep;
		return r;
	}

	std::string Ui::modeLabel(const Player& p) const
	{
		if (!p.settings.shuffle) return p.settings.loop == LOOP_OFF ? "In order" : loopLabel(p.settings.loop);
		return p.settings.loop == LOOP_ALL ? "Shuffle" : p.settings.loop == LOOP_ONE ? "Shuffle \xC2\xB7 Loop one" : "Shuffle \xC2\xB7 Once through";
	}

	bool Ui::photoKey(Button b, Player& p)
	{
		flickAt = 0;
		const Player::PhotoView& v = p.photo();
		switch (b)
		{
		case Button::A:
		{
			// Fit and 2x, as a double-tap: zoomed in, out; else in on the middle
			bool zoomed = v.zoom > 1.001;
			photoZoomTo(zoomed ? 1 : 2, v.panX, v.panY, p);
			return true;
		}
		case Button::Left: case Button::L: if (!p.prev()) p.osd("This is the first one"); return true;
		case Button::Right: case Button::R: if (!p.next()) p.osd("This is the last one"); return true;
		case Button::Up: case Button::Down: photoZoomTo(stepZoom(v.zoom, b == Button::Up ? 1 : -1), v.panX, v.panY, p); return true;
		case Button::L3: p.rotatePhoto(); p.osd("Turned " + std::to_string(p.photo().rotate) + "°", 800); return true;
		case Button::X: case Button::Y: return true;	// (X opens More, above, before this)
		default: return false;	// B and START: as over a video
		}
	}

	void Ui::photoZoomTo(double zoom, double panX, double panY, Player& p)
	{
		p.setPhotoView(zoom, panX, panY);
		p.osd("Zoom " + std::to_string((int)std::lround(p.photo().zoom * 100)) + "%", 800);
	}

	Ui::Rect Ui::thumbRect(const Player& p) const
	{
		Rect pan = panRect(p);
		double a = p.photoWidth() > 0 ? p.photoWidth() / (double)p.photoHeight() : 4.0 / 3;
		int w = pan.w, h = (int)std::lround(pan.w / a);
		if (h > pan.h)
		{
			h = pan.h;
			w = (int)std::lround(pan.h * a);
		}
		return {pan.x + (pan.w - w) / 2, pan.y + (pan.h - h) / 2, w, h};
	}

	bool Ui::photoPointer(const Pointer& pt, int x, int y, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		PhotoScale sc = p.photoScaleNow();
		const Player::PhotoView& v = p.photo();
		int dx = x - panDownX, dy = y - panDownY;
		if (pt.kind == Pointer::Move)
		{
			if (std::abs(dx) > 8 || std::abs(dy) > 8) panMoved = true;
			if (!panMoved) return false;
			// Drag moves the photo: it goes with the finger, the pan area standing for the top panel. The box: it
			// goes with the finger over the small photo (its centre is minus the pan, in shares of the photo as
			// drawn here), the top panel showing what's in it
			Rect tr = thumbRect(p);
			if (p.settings.dragBox) p.setPhotoView(v.zoom, panStartX - dx / (double)tr.w, panStartY - dy / (double)tr.h);
			else p.setPhotoView(v.zoom, panStartX + dx / (double)PAN_W / sc.x, panStartY + dy / (double)PAN_H / sc.y);
			// its speed, smoothed over the last moves, for a flick
			Uint32 dt = std::max<Uint32>(1, now - lastAt);
			flickVX = 0.6 * (x - lastX) / (double)dt + 0.4 * flickVX;
			flickVY = 0.6 * (y - lastY) / (double)dt + 0.4 * flickVY;
			lastX = x;
			lastY = y;
			lastAt = now;
			return false;
		}
		if (pt.kind != Pointer::Up) return false;
		panTouch = false;
		if (panMoved)
		{
			// let go while moving: it glides on
			if (now - lastAt < 80 && std::hypot(flickVX, flickVY) > 0.3) flickAt = now;
			return false;
		}
		Rect tr = thumbRect(p);
		double u = std::clamp((x - tr.x) / (double)tr.w, 0.0, 1.0) - 0.5, w = std::clamp((y - tr.y) / (double)tr.h, 0.0, 1.0) - 0.5;
		if (tapAt && now - tapAt < 350 && std::abs(x - tapX) < 40 && std::abs(y - tapY) < 40)
		{
			// a double-tap: in to 2x on that spot, or (zoomed) back out
			tapAt = 0;
			bool zoomed = v.zoom > 1.001;
			photoZoomTo(zoomed ? 1 : 2, zoomed ? 0 : -u, zoomed ? 0 : -w, p);
			return false;
		}
		tapAt = now;
		tapX = x;
		tapY = y;
		// a tap on the thumbnail, zoomed in: the view there
		if ((sc.x > 1.001 || sc.y > 1.001) && tr.hit(x, y)) p.setPhotoView(v.zoom, -u, -w);
		return false;
	}

	Ui::Rect Ui::buttonRect(int id) const
	{
		if (id == BUSY_CANCEL) { lay::Box k = lay::busySheet().key; return {k.x, k.y, k.w, k.h}; }
		if (id == WEB_NEWPIN) return {296, 384, 152, 52};
		if (id == WEB_OFF) return {464, 384, 152, 52};
		if (id >= PHOTO_BTN) return {0, 0, 0, 0};	// photoBtn's: they move with the pan area
		if (id == QUIT_BTN) return {468, 428, 148, 44};	// (a photo's Close)
		if (id == EMPTY_BTN) return page == Page::Browse && browse->browseDir == NETROOT ? Rect{160, 300, 320, 52} : Rect{200, 300, 240, 52};	// (Find on the network: a longer label)
		// the Settings pages' kit header: the key is a little wider than a tab bar's "<" (10,3,56,48)
		if (id == BACK_ICON) return page == Page::Settings || page == Page::About ? Rect{10, 3, 56, 48} : Rect{8, 8, 48, 48};
		return {0, 0, 0, 0};	// (the player screen's keys are its own: PlayerPage::rect)
	}

	void Ui::drawPhoto(const Player& p)
	{
		// (the photo viewer's keys are drawn with drawBox, not the kit's key: Classic whatever the style, rule 7)
		Kit::ClassicHere classic;
		std::string name = p.currentPath().substr(p.currentPath().rfind('/') + 1);
		drawHeader(!p.title.empty() ? p.title : !p.error.empty() ? name : "Opening...", p);
		Rect pan = panRect(p);
		fill(pan, SCREEN);
		PhotoScale sc = p.photoScaleNow();
		const Player::PhotoView& v = p.photo();
		if (!p.error.empty())
		{
			drawText("This photo can't be shown:", 0, DANGER, pan.x + 12, pan.y + 12, pan.w - 24);
			drawText(p.error, 0, DANGER, pan.x + 12, pan.y + 40, pan.w - 24);
		}
		else if (p.thumbW > 0)
		{
			if (photoTexId != p.thumbId)
			{
				if (photoTex) SDL_DestroyTexture(photoTex);
				photoTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STATIC, p.thumbW, p.thumbH);
				if (photoTex) SDL_UpdateTexture(photoTex, nullptr, p.thumb.data(), p.thumbW * 4);
				photoTexId = p.thumbId;
			}
			// the thumbnail as the photo is shown: turned about its middle, in the box it fills turned
			Rect tr = thumbRect(p);
			int turn = p.thumbTurn();
			bool across = turn % 180 == 90;
			float cx = (tr.x + tr.w / 2.f) * sx, cy = (tr.y + tr.h / 2.f) * sy;
			float w = (across ? tr.h * sy : tr.w * sx), h = (across ? tr.w * sx : tr.h * sy);
			SDL_Rect dst{(int)std::lround(cx - w / 2), (int)std::lround(cy - h / 2), (int)std::lround(w), (int)std::lround(h)};
			if (photoTex) SDL_RenderCopyEx(ren, photoTex, nullptr, &dst, turn, nullptr, SDL_FLIP_NONE);
			// the part on the top panel, boxed (all of it shown: no box)
			if (sc.x > 1.001 || sc.y > 1.001)
			{
				double hx = std::min(0.5, 0.5 / sc.x), hy = std::min(0.5, 0.5 / sc.y);
				int bx = tr.x + (int)std::lround(tr.w * (0.5 - v.panX - hx)), by = tr.y + (int)std::lround(tr.h * (0.5 - v.panY - hy));
				int bw = std::max(4, (int)std::lround(tr.w * 2 * hx)), bh = std::max(4, (int)std::lround(tr.h * 2 * hy));
				fill({bx, by, bw, 2}, ACCENT);
				fill({bx, by + bh - 2, bw, 2}, ACCENT);
				fill({bx, by, 2, bh}, ACCENT);
				fill({bx + bw - 2, by, 2, bh}, ACCENT);
			}
		}
		else if (p.thumbPending()) drawText("Loading" + std::string(SDL_GetTicks() / 400 % 4, '.'), 1, DIM, pan.x + pan.w / 2, pan.y + pan.h / 2 - 12, 0, 2);
		else drawText("No preview", 1, DIM, pan.x + pan.w / 2, pan.y + pan.h / 2 - 12, 0, 2);

		// under the pan area: the zoom and the size; how to use it
		std::string zoom = "Zoom " + std::to_string((int)std::lround(v.zoom * 100)) + "%";
		if (p.photoWidth() > 0) zoom += "   " + std::to_string(p.photoWidth()) + " x " + std::to_string(p.photoHeight());
		drawText(zoom, 0, TEXT, pan.x, pan.y + pan.h + 10, pan.w);
		drawText(maxPan(sc.x) > 0 || maxPan(sc.y) > 0 ? (p.settings.dragBox ? "Drag the box · double-tap: zoom out" : "Drag to move · double-tap: zoom out")
			: "Double-tap: zoom in",
			0, DIM, pan.x, pan.y + pan.h + 36, pan.w);

		bool hints = p.settings.buttonHotkeys;
		struct { const char* label; const char* hint; bool off; } btns[PHOTO_BTNS] = {
			{"Prev", "L", p.index() == 0},
			{"Next", "R", p.index() + 1 >= p.count()},
			{"Zoom -", "DOWN", v.zoom <= 1.001},
			{"Zoom +", "UP", v.zoom >= MAX_ZOOM - 0.001},
			{"Rotate", "L3", false},
			{v.fill ? "Fill" : "Fit", "", false},
			{"Info", "", false},
			{"Settings", "START", false},
		};
		for (int i = 0; i < PHOTO_BTNS; i++)
		{
			Rect b = photoBtn(i, p);
			Kit::Key key;
			key.label = btns[i].label;
			key.key = hints ? btns[i].hint : "";
			key.off = btns[i].off;
			key.pressed = pressedBtn == PHOTO_BTN + i;
			kit->key(b, key);
		}
		if (p.count() > 1)
		{
			Rect last = photoBtn(PHOTO_BTNS - 1, p);
			drawText(std::to_string(p.index() + 1) + " / " + std::to_string(p.count()), 0, DIM, last.x + last.w, last.y + last.h + 12, 0, 1);
		}

		drawLeave(p);
		// along the bottom, beside it: what A does here
		if (hints) drawTextMid("A", 0, ACCENT, 24, 450), drawTextMid("Zoom to 2x, or the whole photo", 0, DIM, 24 + (int)std::lround(textWidth("A", 0) / sx) + 7, 450, 400);
	}

	void Ui::drawLeave(const Player& p)
	{
		// a photo's Close, as B (a song's or a video's Stop is in X's More sheet: PlayerPage::moreSpec)
		Kit::Key key;
		key.glyph = &G_CROSS;
		key.label = "Close";
		key.key = p.settings.buttonHotkeys ? "B" : "";
		key.pressed = pressedBtn == QUIT_BTN;
		kit->key(buttonRect(QUIT_BTN), key);
	}

	bool Ui::photoDown(int x, int y, Player& p)
	{
		if (panRect(p).hit(x, y))
		{
			panTouch = true;
			panMoved = false;
			flickAt = 0;
			flickVX = flickVY = 0;
			panDownX = lastX = x;
			panDownY = lastY = y;
			lastAt = SDL_GetTicks();
			panStartX = p.photo().panX;
			panStartY = p.photo().panY;
			return false;
		}
		for (int i = 0; i < PHOTO_BTNS; i++)
			if (photoBtn(i, p).hit(x, y)) pressedBtn = PHOTO_BTN + i;
		if (buttonRect(QUIT_BTN).hit(x, y)) pressedBtn = QUIT_BTN;
		return false;
	}
}
