#pragma once
#include <functional>
#include <vector>
// The top panel's song layout (docs/redesign/components.md, Top screen › Song layout): where the art box and the
// title lines go in each Show mode, in the panel's 640x480 units. Pure maths, no window: Player places the art (mpv's
// video margins, an image overlay, or the graph's still) and writes the ASS text from it.
#include <string>

namespace bp
{
	// the title lines' sizes and gaps (units): title 28 semibold, artist 17, album · year 14
	constexpr int SONG_TITLE_FS = 28, SONG_ARTIST_FS = 17, SONG_ALBUM_FS = 14;
	// the Repeat mark (Repeat all / one; none while Off): the loop glyph in the panel's top right corner, its cells 2
	// units, 16 in from the top and the right; over a visualizer alone the lines top left stop short of it
	constexpr int SONG_REPEAT_CELL = 2, SONG_REPEAT_MARGIN = 16, SONG_REPEAT_ROOM = 34;
	// Art + visualizer: the band of bars along the foot (the styles' 200-row strip); the art's group sits above it
	constexpr int SONG_BAND_TOP = 280;

	struct SongLayout
	{
		bool art = false;	// an art box is drawn (the art, or its placeholder)
		int artX = 0, artY = 0, artS = 0;	// the box, a square
		bool text = false;	// the title lines to the art's right
		int textX = 0, textW = 0;	// their column (each line cut to textW)
		int midY = 0;	// the lines' block is centred on this (the art's middle)
	};

	// showArt: Album art only (true) or Art + visualizer (false); widest: the widest of the lines as they'd be drawn
	// uncut (units; songTextWidth). The title lines are always shown (there is no setting): the group (art + 28 + text)
	// is centred on its real width in 40-unit margins; the text column never reaches under the art
	SongLayout songLayout(bool showArt, int widest);

	// a line's width estimated at font size fs (units): libass measures nothing for us, so letters are counted, about
	// 0.45 x fs a Latin one (Inter; 0.48 bold: measured on its rendering, a little over), fs a wide (CJK) one
	int songTextWidth(const std::string& s, int fs, bool bold = false);
	// the line cut to maxW units, "…" when cut
	std::string songFitText(const std::string& s, int fs, int maxW, bool bold = false);

	// the art box's look: radius 8, a 2-unit edge (edgeOf(SURFACE): it shows on a light ground and a dark one)
	constexpr int SONG_ART_RADIUS = 8, SONG_ART_EDGE = 2;

	// lavfi-complex: a style's art + visualizer graph with its art input ("[vid1] scale=640:480:..." up to setsar)
	// swapped for a still of the theme's ground (art == 0: the art is laid over by the player), or for the art fitted
	// once into an art x art square at (x, y) on that ground. ground: lavfi's colour ("black", "0xRRGGBB"); band: the
	// colour of the rows the style draws its strip over (SONG_BAND_TOP down), its own ground (today black, the styles'
	// own; the hook for a themed band). Styles that use the art their own way (Circle's ring, Turntable's label) are
	// left as they are; the graph then still names [vid1]
	std::string vizPlaceArt(const std::string& graph, int x, int y, int art, const std::string& ground = "black",
		const std::string& band = "black");

	// Repeated start (docs/redesign/components.md, Title marquee): what plays, shown in a box it doesn't fit, leaves out
	// a start that is one of `starts` (the artist, the album, the folder, the playing list's shared start: in that order,
	// case aside) when it ends at a separator (" - ", " – ", " — ", ": ", " | ", "_"; the separator goes with it) and three
	// letters or more are left (digits and marks aside). Two starts at most; a title that fits is never changed
	std::string shownTitle(const std::string& title, const std::vector<std::string>& starts, const std::function<bool(const std::string&)>& fits);
	// the start every name shares (three names or more), up to a separator, four letters or more; "" if none
	std::string sharedStart(const std::vector<std::string>& names);
	// what shownTitle may leave out, in its order: the artist, the album, the file's folder, the list's shared start
	// (names: the playing list's file names)
	std::vector<std::string> titleStarts(const std::string& artist, const std::string& album, const std::string& path,
		const std::vector<std::string>& names);
}
