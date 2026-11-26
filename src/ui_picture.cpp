// A video's Picture sheet (docs/redesign/src/screens.js "video-picture", "video-look"): the size (Fit, Fill,
// Stretch), the look (Off, Sharpen, Soften, Scanlines, S-Video, Retro) with its Strength, whether the look is for
// every video or only this one, and Adjust (a page of it). The film on the top screen is the preview: a choice
// applies at once (Player::setLook, mpv's vf sent async), and holding Y shows it without the look.
#include "ui_kit.h"
#include "ui_glyphs.h"
#include "aspect.h"
#include "picturelook.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	namespace
	{
		SDL_Color blend(SDL_Color a, SDL_Color b, int percent)
		{
			auto m = [percent](Uint8 x, Uint8 y) { return (Uint8)((x * (100 - percent) + y * percent) / 100); };
			return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255};
		}

		const char* const SIZE_NAMES[lay::PICTURE_SIZES] = {"Fit", "Fill", "Stretch"};
		const char* const ADJUST_NAMES[4] = {"Brightness", "Contrast", "Saturation", "Gamma"};
	}

	struct Ui::PictureSheet : Ui::Overlay
	{
		int cursor = 0;
		int press = -1;			// the item a finger is down on (-1 none)
		bool comparing = false;	// Y held: the film without its look
		int globalAtOpen = LOOK_OFF;	// every video's look as the sheet opened ("Only this video" gives it back)

		PictureSheet(Ui& u, const Player& p) : Overlay(u)
		{
			globalAtOpen = p.settings.pictureLook;
			// opened on the look in use (Off too): what the sheet is mostly for
			cursor = layout().lookRows ? layout().lookAt(p.lookInUse().look) : (int)p.aspect;
		}
		const char* name() const override { return "picture"; }

			// a tile's sample: a small scene (a sky, a sun, a hill) in the theme's colours, the look drawn over it as the
			// film gets it. Not the film itself: the top screen is the real preview
			static void lookSample(Ui& ui, Rect r, int look)
			{
				SDL_Color sky = TRACK, sun = ACCENT, hill = DIM_ON;
				if (look == LOOK_RETRO)
				{
					// warm and faded: everything drawn toward the accent's warmth and away from its depth
					sky = blend(TRACK, ACCENT, 30);
					sun = blend(ACCENT, TEXT, 30);
					hill = blend(DIM_ON, ACCENT, 35);
				}
				ui.fillRound(r, sky, 5);
				Rect disc{r.x + r.w - 17, r.y + 5, 10, 10};
				if (look == LOOK_SOFTEN) ui.fillRound({disc.x - 3, disc.y - 3, disc.w + 6, disc.h + 6}, blend(sky, sun, 45), 8);
				// S-Video: the colour runs sideways past the sun's crisp edge (chroma bleed)
				if (look == LOOK_SVIDEO) ui.fillRound({disc.x - 6, disc.y + 1, disc.w + 12, disc.h - 2}, blend(sky, sun, 40), 4);
				ui.fillRound(disc, sun, 5);
				Rect ground{r.x, r.y + r.h - 10, r.w, 10};
				ui.fillRound(ground, hill, 5, ROUND_BL | ROUND_BR);
				ui.fillRound({r.x + 4, r.y + r.h - 16, 16, 10}, hill, 5, ROUND_TL | ROUND_TR);
				if (look == LOOK_SHARPEN)
				{
					// a crisp edge: a light line along the hill's top and the sun's
					ui.fill({r.x + 4, r.y + r.h - 17, 16, 1}, TEXT);
					ui.fill({r.x, r.y + r.h - 11, r.w, 1}, TEXT);
					ui.fill({disc.x + 2, disc.y - 1, disc.w - 4, 1}, TEXT);
				}
				if (look == LOOK_SCANLINES)
					for (int y = r.y + 1; y < r.y + r.h - 1; y += 3) ui.fill({r.x + 1, y, r.w - 2, 1}, BG);
				// and a CRT's lines over it, lighter than Scanlines'
				if (look == LOOK_SVIDEO)
					for (int y = r.y + 1; y < r.y + r.h - 1; y += 3) ui.fill({r.x + 1, y, r.w - 2, 1}, blend(BG, sky, 50));
			}

		static lay::PicturePage layout() { return lay::picturePage(PICTURE_LOOKS, PICTURE_LOOKS_ENABLED); }
		static bool gone(const Player& p, const Ui& ui)
		{
			return !p.playing() || p.playingPhoto() || p.playingAudio() || ui.page != Page::Main;
		}
		static bool strengthOn(const Player& p) { return p.lookInUse().look != LOOK_OFF; }

		// ---- the choices ----
		void setStrength(int s, Player& p)
		{
			Player::LookInUse l = p.lookInUse();
			s = clampLookStrength(s);
			if (l.look == LOOK_OFF || s == l.strength) return;
			p.setLook(l.look, s, l.onlyThis);
		}
		void setOnlyThis(bool only, Player& p)
		{
			Player::LookInUse l = p.lookInUse();
			if (only == l.onlyThis) return;
			p.setLook(l.look, l.strength, only);
			// the look just chosen was meant for this film alone: every video's goes back to what it was
			if (only && p.settings.pictureLook != globalAtOpen)
			{
				Settings s = p.settings;
				s.pictureLook = globalAtOpen;
				p.setSettings(s);
			}
		}
		void endCompare(Player& p)
		{
			if (!comparing) return;
			comparing = false;
			p.compareLook(false);
		}
		void leave(Player& p)
		{
			endCompare(p);
			close();
		}
		void choose(int i, Player& p)
		{
			lay::PicturePage d = layout();
			cursor = i;
			if (i < lay::PICTURE_SIZES) { p.setAspect((AspectMode)i); return; }
			if (i < d.strengthAt())
			{
				int look = i - lay::PICTURE_SIZES;
				// each look keeps its own strength (every video's); this film's own starts from it too
				p.setLook(look, p.settings.lookStrength[look], p.lookInUse().onlyThis);
				return;
			}
			if (i == d.useForAt()) { setOnlyThis(!p.lookInUse().onlyThis, p); return; }
			if (i == d.adjustAt())
			{
				endCompare(p);
				// a page of this sheet: it stays under Adjust, and B (or its <) comes back here
				ui.openAdjust([this] { close(); });
			}
		}
		static SliderRange strengthRange() { return {LOOK_STRENGTH_MIN, LOOK_STRENGTH_MAX, LOOK_STRENGTH_STEP}; }

		// ---- keys and touches ----
		bool key(Button b, Player& p) override
		{
			if (gone(p, ui)) { leave(p); return false; }
			lay::PicturePage d = layout();
			const bool sideways = b == Button::Left || b == Button::Right;
			if (sideways && cursor == d.strengthAt())
			{
				setStrength(stepLookStrength(p.lookInUse().strength, b == Button::Right ? 1 : -1), p);
				return false;
			}
			if (sideways && cursor == d.useForAt())
			{
				setOnlyThis(b == Button::Right, p);
				return false;
			}
			switch (b)
			{
			case Button::Up: cursor = lay::pictureMove(d, cursor, lay::Dir::Up, strengthOn(p)); break;
			case Button::Down: cursor = lay::pictureMove(d, cursor, lay::Dir::Down, strengthOn(p)); break;
			case Button::Left: cursor = lay::pictureMove(d, cursor, lay::Dir::Left, strengthOn(p)); break;
			case Button::Right: cursor = lay::pictureMove(d, cursor, lay::Dir::Right, strengthOn(p)); break;
			case Button::A: choose(cursor, p); break;
			// held: the film without its look, until let go (release)
			case Button::Y:
				if (strengthOn(p) && !comparing) { comparing = true; p.compareLook(true); }
				break;
			case Button::Select:
			case Button::B: leave(p); break;
			default: break;
			}
			// (the look set Off under the cursor on Strength: on to Use for)
			if (cursor == d.strengthAt() && !strengthOn(p)) cursor = d.useForAt();
			return false;
		}
		void release(Button b, Player& p) override
		{
			if (b == Button::Y) endCompare(p);
		}

		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			if (ui.touchIsLocked()) return false;
			if (gone(p, ui)) { leave(p); return false; }
			lay::PicturePage d = layout();
			if (pt.kind == Pointer::Down)
			{
				press = -1;
				if (!d.sheet.box.hit(x, y)) { leave(p); ui.swallowTouch = true; return false; }
				int at = lay::pictureAt(d, x, y);
				if (d.lookRows && at == d.strengthAt())
				{
					if (!strengthOn(p)) return false;
					cursor = at;
					// the app's one slider (docs/notes/sliders.md): a tap puts it there, a drag moves it on by the
					// finger's travel, kept till the lift
					ui.slideGrab(SL_PICTURE, 0, {d.track, strengthRange(), false, 0, (double)p.lookInUse().strength}, x, y, p,
						[this](double v, bool, Player& p) { setStrength((int)std::lround(v), p); }, this);
					return false;
				}
				press = at;
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int at = press;
			press = -1;
			// let go off what it was pressed on: nothing (the finger changed its mind)
			if (at < 0 || lay::pictureAt(d, x, y) != at) return false;
			if (at == d.useForAt())
			{
				cursor = at;
				if (d.useAll.hit(x, y)) setOnlyThis(false, p);
				else if (d.useOne.hit(x, y)) setOnlyThis(true, p);
				return false;
			}
			choose(at, p);
			return false;
		}

		// ---- drawing ----
		static std::string adjustNote(const Player& p)
		{
			std::string out;
			for (int i = 0; i < 4; i++)
			{
				int v = (int)std::lround(p.videoAdjust(i));
				if (!v) continue;
				if (!out.empty()) out += ", ";
				out += std::string(ADJUST_NAMES[i]) + (v > 0 ? " +" : " ") + std::to_string(v);
			}
			return out.empty() ? "As the film is" : out;
		}
		std::string headNote(const Player& p) const
		{
			Player::LookInUse l = p.lookInUse();
			if (l.look == LOOK_OFF) return "";
			if (comparing) return "Off while Y is held";
			return std::string(pictureLookLabel(l.look)) + " \xC2\xB7 " + std::to_string(l.strength) + "%";
		}

		void draw(const Player& p) override
		{
			if (gone(p, ui)) { close(); return; }
			Kit& k = *ui.kit;
			lay::PicturePage d = layout();
			Player::LookInUse l = p.lookInUse();
			k.sheet(d.sheet, {&G_PICTURE, "Picture", headNote(p), false});

			ui.drawTextMid("Size", F_I12, DIM, d.sizeLabel.x + 2, d.sizeLabel.y + d.sizeLabel.h / 2);
			for (int i = 0; i < lay::PICTURE_SIZES; i++)
			{
				Kit::Choice c;
				c.name = SIZE_NAMES[i];
				c.on = (int)p.aspect == i;
				c.focused = cursor == i;
				c.pressed = press == i;
				c.font = F_P16;
				k.choiceTile(d.size[i], c);
			}

			if (d.lookRows)
			{
				ui.drawTextMid("Look \xC2\xB7 off unless you turn one on", F_I12, DIM, d.lookLabel.x + 2, d.lookLabel.y + d.lookLabel.h / 2);
				for (int i = 0; i < (int)d.look.size(); i++)
				{
					Kit::Choice c;
					c.name = pictureLookLabel(i);
					c.on = l.look == i;
					c.focused = cursor == d.lookAt(i);
					c.pressed = press == d.lookAt(i);
					c.font = F_P13;
					Ui& u = ui;
					c.sample = [&u, i](Rect r) { lookSample(u, r, i); };
					k.choiceTile(d.look[i], c);
				}

				Kit::SliderRow s;
				s.label = "Strength";
				s.value = l.look == LOOK_OFF ? "None for Off" : std::to_string(l.strength) + "%";
				s.share = l.look == LOOK_OFF ? 0 : sliderShare(l.strength, strengthRange());
				s.focused = cursor == d.strengthAt();
				s.held = ui.slideHeld(SL_PICTURE, 0);
				k.sliderRow(d.strength, d.track, s);

				const bool useFocused = cursor == d.useForAt();
				if (useFocused) k.flat(d.useFor, BUTTON, R_BUTTON, ACCENT);
				ui.drawTextMid("Use for", F_P16, useFocused ? ACCENT : TEXT, d.useFor.x + 12, d.useFor.y + d.useFor.h / 2);
				Rect seg{d.useAll.x, d.useAll.y, d.useOne.x + d.useOne.w - d.useAll.x, d.useAll.h};
				k.segment(seg, {"All videos", "Only this video"}, l.onlyThis ? 1 : 0);
			}

			Kit::Row adj;
			adj.glyph = &G_EQ;
			adj.name = "Adjust\xE2\x80\xA6";
			adj.note = adjustNote(p);
			adj.font = F_P16;
			adj.trail = Kit::Trail::Chevron;
			adj.focused = cursor == d.adjustAt();
			adj.pressed = press == d.adjustAt();
			k.row(d.adjust, adj);

			std::vector<Hint> hints{{"D-PAD", "Move"}};
			if (cursor == d.strengthAt()) hints.push_back({"LEFT/RIGHT", "10% steps"});
			hints.push_back({"A", "Choose"});
			if (l.look != LOOK_OFF) hints.push_back({"Y", "Hold: compare"});
			hints.push_back({"SELECT/B", "Close"});
			k.hints(hints);
		}

		std::string signature(const Player& p) const override
		{
			Player::LookInUse l = p.lookInUse();
			std::string sig = std::to_string(cursor) + "," + std::to_string(press) + "," +
				std::to_string(comparing) + "," + std::to_string(ui.touchIsLocked()) + "|" + std::to_string((int)p.aspect) +
				"|" + std::to_string(l.look) + "," + std::to_string(l.strength) + "," + std::to_string(l.onlyThis);
			for (int i = 0; i < 4; i++) sig += "|" + std::to_string((int)std::lround(p.videoAdjust(i)));
			return sig;
		}
	};

	void Ui::openPicture(const Player& p)
	{
		pushOverlay(std::make_unique<PictureSheet>(*this, p));
	}
}
