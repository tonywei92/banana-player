// The player screen (Page::Main: a video or a song; docs/redesign, screens "video" and "music"), an area of Ui
// (docs/notes/ui-areas.md): its state and its handlers, drawn with the component kit (ui_kit.h) on the Rects of
// lay::playerLayout (kitlayout.h), which its touches test too. Three tiers: the transport row (what is pressed all the
// time), the state chips (what is set sometimes: each says its state, yellow when away from normal), and X's More
// sheet for the rest (Lock, Screen off, Info, Stop, Like, Add to playlist, Show in folder, Play on, and View). The row
// under the transport is Like · Playlist · List · Stop; SELECT and X both open More (its View row is the View sheet), and
// UP / DOWN do nothing here (the Next line is a tap).
// A photo is this page too (Player::playingPhoto): its viewer is unchanged and still Ui's (drawPhoto, photoKey,
// photoPointer in ui_main.cpp); the area hands it the keys, the touches and the drawing.
// Still Ui's, because other pages use them: Like (likeNow), the play mode's label (modeLabel), the hold that
// takes playback over from a browser (playHoldAt: the mini bar holds the same way), Stop's flag (stopPressed).
#pragma once
#include "ui_kit.h"

namespace bp
{
	struct Ui::PlayerPage : Ui::Area
	{
		explicit PlayerPage(Ui& u) : Area(u) {}
		bool has(Page pg) const override { return pg == Page::Main; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		void tick(Player& p) override;
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;
		bool animating() const override { return lockHoldAt != 0 || listPulseAt != 0; }

		// ---- what a finger can press: each id's Rect is rect(), which draw() and pointer() both ask ----
		enum Touch { T_NONE = -1, T_BACK, T_MORE, T_LINE, T_PREV, T_REW, T_PLAY, T_FF, T_NEXT, T_LIKE, T_ADD, T_LIST, T_STOP,
			T_CHIP0, T_CHIP_END = T_CHIP0 + lay::PLAYER_CHIPS_MAX, T_NEXTLINE = T_CHIP_END, T_COUNT };
		int pressed = T_NONE;	// under the finger: it acts when the finger lifts on it
		Rect rect(int id, const lay::PlayerLayout& l) const;	// (the touch area: wider than the key for the header's two)
		bool usable(int id, const Player& p) const;	// not dimmed
		bool act(int id, Player& p);	// true: leave the player
		void dropTouch() { pressed = T_NONE; }

		// ---- the chips ----
		enum ChipKind { C_SUBS, C_AUDIO, C_PICTURE, C_SPEED, C_MODE, C_SHUFFLE, C_SOUND, C_FX, C_TOP };
		struct ChipDef { ChipKind kind; Kit::Chip look; };
		// a video: Subtitles, Audio, Picture, Speed, Repeat; a song: Sound, Sound FX, Display, Speed, Shuffle
		std::vector<ChipDef> chips(const Player& p) const;
		void chipAct(ChipKind kind, Player& p);	// A on it, or a tap: what sets it

		// ---- the chapter line and the Next line ----
		bool chapterLineShown(const Player& p) const;
		struct NextUp
		{
			bool any = false, round = false;	// round: Shuffle at its round's end (a new random round comes)
			std::string path, note;
		};
		NextUp nextUp(const Player& p) const;	// what Next would play (the Session's nextPlace)
		bool listAct(Player& p);	// List (Y, its key): the playlist's page, or the List sheet
		// the List sheet (ui_player.cpp): tab 0 Up next, 1 Chapters, opened on the one last used; the Repeat chip,
		// the chapter line and the Next line open it on theirs
		int listTab = 0;
		void openList(int tab, Player& p);
		// View (More's View row): the View sheet (ui_player.cpp), opened on the tab last used. A video: Subtitles / Audio /
		// Picture / Speed; the track chips open it on theirs, the Speed chip on Speed. A song: Display (the mode and the
		// style on one page) / Equalizer / Effects / Speed; Sound opens Equalizer, Sound FX Effects, Display Display.
		int viewTab = 0, songViewTab = 0;
		void openView(int tab, Player& p);

		// ---- X: the More sheet's list, and Play on's sheet ----
		MoreSpec moreSpec(const Player& p) const;
		void openMoreSheet();
		void openPlayOn();

		// ---- Lock: touch does nothing but a 1 s hold on the lock (the buttons go on as Button hotkeys says) ----
		bool touchLocked = false;
		Uint32 lockHoldAt = 0, lockNoticeAt = 0;
		void lock(Player& p);
		void unlock();
		bool locked(const Player& p) const { return touchLocked && p.playing() && !p.playingPhoto(); }
		bool lockPointer(const Pointer& pt, int x, int y, Player& p);	// every touch while it's locked
		void lockTimer();	// the hold reached its second: unlocked (Ui::draw asks, before the signature)

		// let go, the bar stays where the finger left it until playback gets there (the exact seek takes a moment, a
		// buffering one longer; until then the position is still the old one: the bar flicked back and forth); -1 none
		double seekHeld = -1;
		Uint32 seekHeldAt = 0;

		// the player opened on a playlist's first song: its List key (the way to the playlist) glows a soft yellow twice
		bool listPulseArm = false;
		std::string listPulsedFor;	// the playlist it glowed for (cleared when something else plays)
		Uint32 listPulseAt = 0;
		static constexpr Uint32 LIST_PULSE_MS = 1400;

	private:
		std::string crumb(const Player& p, const Glyph*& mark) const;	// the header's line: what it plays from
		void drawBar(const Player& p, const lay::PlayerLayout& l);
		std::vector<Hint> hintList(const Player& p, bool chapters) const;
	};
}
