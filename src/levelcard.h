// The cards over the top panel, each drawn as one picture at the panel's own pixels (docs/redesign/components.md,
// "Top screen"): the level card (Volume, Brightness, Browser volume, the reader's Zoom), the label card (the chapter and
// Equalizer: a small label over a value), the note (one line, an optional second) and the state card (Playing in a
// browser, Loading, Buffering). Every top-panel screen shows the same pictures: mpv's (video, music, a photo, the idle
// screen) as image overlays (Player), the manga and text readers' as a card over their pages (CardOverlay in
// pagerender.h). One drawing, so the screens can't drift apart. All sizes are the spec's 640x480 units, scaled to the
// panel. Main thread only (the fonts are opened once and kept).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bp
{
	struct LevelCardImage
	{
		std::shared_ptr<std::vector<uint8_t>> rgba;	// straight alpha, w*h*4
		int w = 0, h = 0;
		bool ok() const { return rgba && w > 0 && h > 0; }
	};

	// an icon: the G_* glyph's rows ('#' drawn); none when rows is null
	struct CardGlyph
	{
		const char* const* rows = nullptr;
		int h = 0;
	};

	// the bar's whole for a label: the volume's is the Max volume setting (120, 150: a mark at 100, louder in
	// colour), everything else's 100
	int levelTop(const std::string& label, int volumeMax);
	// The level card: icon, name and value on a line; with top > 0 a bar under them (and, for Volume with top > 100,
	// the third line about the mark and the boost). top <= 0: the line alone, 200 wide (the reader's zoom: percent < 0
	// leaves the value out). Cream at normal levels, yellow past 100%, red past 120%.
	LevelCardImage drawLevelCard(const std::string& fontsDir, int panelH, const std::string& label, int percent, int top, CardGlyph icon);
	// The chapter and Equalizer cards: the small label dim over the value in yellow. `starts` (the chapters' starts
	// as shares of the file, ascending) and `active` draw the chapter bar under them
	LevelCardImage drawLabelCard(const std::string& fontsDir, int panelH, const std::string& label, const std::string& value,
		const std::vector<float>& starts = {}, int active = -1);
	// The note: one line in yellow (red for a failure) with an optional icon, and an optional second line
	LevelCardImage drawNoteCard(const std::string& fontsDir, int panelH, CardGlyph icon, const std::string& line1,
		const std::string& line2, bool danger);
	// The sleep timer's quiet mark (a clock and "24 min", dim) on a song's top screen, top right
	LevelCardImage drawQuietMark(const std::string& fontsDir, int panelH, CardGlyph icon, const std::string& text);
	// where a card goes on a panel pw x ph: the level, label and chapter cards top right (20 units from the edges), the
	// note at the bottom centre (26 up)
	void levelCardPos(int pw, int ph, const LevelCardImage& card, int& x, int& y);
	// centre: in the panel's middle (over a video: the bottom is the subtitles', the top right the level card's), else
	// bottom centre
	void noteCardPos(int pw, int ph, const LevelCardImage& card, int& x, int& y, bool centre = false);
	// mpv's overlay-add takes premultiplied BGRA
	std::vector<uint8_t> premultipliedBgra(const LevelCardImage& card);
}
