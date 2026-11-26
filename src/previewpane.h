#pragma once
// The Preview pane (docs/redesign/components.md, "Preview pane"): the top panel while nothing plays, app UI in the
// main tokens (so a light-bodied theme's top screen is light). The brand mark top left, then a text layout (a label in
// capitals, a title, a sentence, a two-column list, a key line) or the cover layout (a cover at the left, the same
// column beside it). It follows the bottom panel's cursor: Ui::topPane fills a PaneSpec, topscreen.cpp composes it
// once per change (its key) at the panel's own pixels and shows it 1:1, as About's support QR is shown.
// Its first use: Settings › Power & screens › Theme (the fifteen swatches and the theme's pieces as they look).
// While a video, a song or a photo plays the top screen is the picture: no pane.
#include "browser.h"
#include <cstdint>
#include <memory>
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
		bool wholeText = false;	// `text` runs as far down as the column has room (a book's summary), not five lines
		std::vector<std::pair<std::string, std::string>> rows;	// the two-column list: dim name, value
		std::vector<std::pair<std::string, std::string>> keys;	// the key line: caps, then what they do
		// the State card (lay::stateCard): the whole screen is a centred column: the tile with an icon (iconRows) or the
		// spinner (>= 0: the lit dot), the title (label is unused), `text` the line, the bar (share >= 0) with timeLine,
		// `keys[0]` the hint with its cap, `foot` at the foot. `danger`: the title and icon in DANGER
		bool state = false;
		int spinner = -1;
		bool danger = false;
		const char* const* iconRows = nullptr;
		int iconH = 0;
		float share = -1;
		std::string timeLine, foot;
		bool welcome = false;	// a fresh install's welcome: the app's icon (84) beside the label and title, in place of the brand mark
		PaneImage icon;	// the welcome's icon picture; none: the brand mark drawn large
		bool cover = false;	// the cover layout (the cover's place; the picture comes from coverArt)
		bool coverSquare = false;	// an album's art: the cover's place square
		// the cover's picture (a decoded cover, held by the caller's cache: shared, never copied per pass), fitted
		// inside the cover's box; none: the cover's place (SURFACE and the picture's mark)
		std::shared_ptr<const PaneImage> coverArt;
		// a book with no picture: the cover drawn from its title (bookcover.h) in the cover's place; genAuthor and genFmt
		// (a path or an extension) are its author line and the format's mark
		std::string genTitle, genAuthor, genFmt;
		// a video's layout (lay::previewPane's hero): its frame across the top, cropped to fill and fading into the
		// ground, the column over the fade and the rows two to a line; no frame: the band in SURFACE with the film mark
		bool hero = false;
		std::shared_ptr<const PaneImage> heroArt;
		// the storage pane (lay::storagePane): Files' first page with the cursor on a place. label, title and keys are
		// the pane's own; `text` is the one line of where it is; `room` the storage's (known), `fill` what fills it
		// (browser.h's storageFill), `note` the quiet line under the kinds
		bool storage = false;
		Capacity room;
		std::vector<StorageKind> fill;
		std::string note;
		// a block of the pane's own under the sentence: THEMES, every theme's swatch and the pieces as they look now
		enum Extra { NONE, THEMES } extra = NONE;
		int chosen = 0;	// THEMES: the theme outlined (the one in use)
	};

	// the pane at a panel's w x h pixels (sizes are the spec's 640 x 480 units scaled by h / 480)
	PaneImage composePane(const std::string& fontsDir, int w, int h, const PaneSpec& spec);
}
