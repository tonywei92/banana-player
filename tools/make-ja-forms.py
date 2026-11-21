#!/usr/bin/env python3
# assets/fonts/fusion-pixel-12px-proportional-ja-forms.ttf: Fusion Pixel's Japanese build cut down to the characters it
# draws differently from the Simplified Chinese build the player already bundles (about 5000 of 37689; 1.1 MB, not
# 7.2). Both from the same release (TakWolf/fusion-pixel-font, 12px proportional, ttf zip; OFL 1.1, no reserved
# font name, so a subset may keep it). src/fontruns.h uses it for text with kana.
#
#   pip install fonttools
#   tools/make-ja-forms.py <release>/fusion-pixel-12px-proportional-zh_hans.ttf \
#       <release>/fusion-pixel-12px-proportional-ja.ttf assets/fonts/fusion-pixel-12px-proportional-ja-forms.ttf
import sys
from fontTools import subset
from fontTools.ttLib import TTFont


def drawn(font, name):
	# a glyph as it is drawn: its outline and its advance
	glyf = font["glyf"]
	return glyf[name].compile(glyf), font["hmtx"][name]


def main(hans_path, ja_path, out_path):
	hans, ja = TTFont(hans_path), TTFont(ja_path)
	hmap, jmap = hans.getBestCmap(), ja.getBestCmap()
	differ = [cp for cp, name in jmap.items() if cp not in hmap or drawn(ja, name) != drawn(hans, hmap[cp])]
	opts = subset.Options()
	opts.layout_features = ["*"]
	opts.name_IDs = ["*"]
	opts.notdef_outline = True
	sub = subset.Subsetter(opts)
	sub.populate(unicodes=differ)
	sub.subset(ja)
	ja.save(out_path)
	print(f"{len(differ)} characters drawn the Japanese way -> {out_path}")


if __name__ == "__main__":
	if len(sys.argv) != 4:
		sys.exit(__doc__ or "usage: make-ja-forms.py ZH_HANS.ttf JA.ttf OUT.ttf")
	main(*sys.argv[1:])
