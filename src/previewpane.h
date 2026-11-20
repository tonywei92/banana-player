#pragma once
// The Preview pane (docs/redesign/components.md, "Preview pane"): the top panel while nothing plays, app UI in the
// main tokens (so a light-bodied theme's top screen is light). The brand mark top left, then a text layout (a label in
// capitals, a title, a sentence, a two-column list, a key line) or the cover layout (a cover at the left, the same
// column beside it). It follows the bottom panel's cursor: Ui::topPane fills a PaneSpec, topscreen.cpp composes it
// once per change (its key) at the panel's own pixels and shows it 1:1, as About's support QR is shown.
// Its first use: Settings › Power & screens › Theme (the fifteen swatches and the theme's pieces as they look).
// While a video, a song or a photo plays the top screen is the picture: no pane.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	struct PaneImage
	{
		int w = 0, h = 0;
		std::vector<uint8_t> rgba;	// w * h * 4, straight alpha (opaque), top row first
		bool ok() const { return w > 0 && h > 0 && rgba.size() == (size_t)w * (size_t)h * 4; }
	};

	struct PaneSpec
	{
		std::string key;	// what this pane is: composed again only when it changes (with the theme in it)
		std::string label, title, text;	// label: small capitals in ACCENT (DANGER with labelDanger)
		bool labelDanger = false;
		std::vector<std::pair<std::string, std::string>> rows;	// the two-column list: dim name, value
		std::vector<std::pair<std::string, std::string>> keys;	// the key line: caps, then what they do
		bool welcome = false;	// a fresh install's welcome: the app's icon (84) beside the label and title, in place of the brand mark
		PaneImage icon;	// the welcome's icon picture; none: the brand mark drawn large
		bool cover = false;	// the cover layout (the cover's place; the picture comes from coverArt)
		// the cover's picture (a decoded cover: the caller's own, or the shared art cache's pixels), fitted inside
		// the cover's box; none: the cover's place (SURFACE and the picture's mark)
		PaneImage coverArt;
		// a block of the pane's own under the sentence: THEMES, every theme's swatch and the pieces as they look now
		enum Extra { NONE, THEMES } extra = NONE;
		int chosen = 0;	// THEMES: the theme outlined (the one in use)
	};

	// the pane at a panel's w x h pixels (sizes are the spec's 640 x 480 units scaled by h / 480)
	PaneImage composePane(const std::string& fontsDir, int w, int h, const PaneSpec& spec);
}
