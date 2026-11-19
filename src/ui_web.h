// The web dashboard's page on the handheld (the address, the QR code, the PIN), the bottom panel's side: an area of
// Ui (Ui::Area, ui.h; docs/notes/ui-areas.md). What every page shows of the dashboard (the header's globe, the quick
// settings' row, its switch, the address, the PIN, the screen-off clock) is Ui's, and stays in ui_web.cpp.
#pragma once
#include "ui.h"

namespace bp
{
	struct Ui::Web : Ui::Area
	{
		explicit Web(Ui& u) : Area(u) {}

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Web; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		void tick(Player& p) override;
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;
		// Ui::pointer hands this page its touches before the updater's pop-up and the mini bar, as it always did
		bool pointerFirst() const override { return true; }

		// ---- for the rest of Ui ----
		void open();	// Ui::openWeb: remembers where it came from
		void close();	// back there

		// Page::Web: where it was opened from (B goes back there)
		struct WebBack { Page page = Page::Browse; bool settingsOver = false; int setGroup = -1, cursor = 0, scroll = 0; } webBack;
		void webNewPin(Player& p);			// New PIN: pressed twice (2 s apart at most)
		void webPrimary(Player& p);			// the empty page's button: Turn on, Try again
		// the QR code of the page's URL, made again when the URL changes: its modules, a row at a time
		std::string qrText;
		std::vector<unsigned char> qrMods;
		int qrSize = 0;
	};
}
