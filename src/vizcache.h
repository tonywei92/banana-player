#pragma once
// The visualizers' lut and lut2 expressions, made quick to set up (docs/notes/2026-11-13-viz-lut-cache.md). lut2
// works out its expression for all 256 x 256 (x, y) pairs when its graph is built, and lut for all 65536 values (even
// on 8-bit pictures): a style's ~10-25 of them, some long (palettes as if-chains, sprites, Long cat's clouds), were
// most of the 0.3-3 s a style took to come up on the handheld, paid again at every change of style or song.
// Here each one's table is worked out once, by the same evaluator (libavutil's av_expr), and turned into a lookup: an
// expression that finds the same value by halves (a few comparisons). It is kept only if it gives every one of the
// table's values exactly and is quicker; it's cached as a small text file in the maps folder, named by a hash of the
// expression (a changed style: new names, its files made again). The filter stays the same filter on the same
// pictures, so a frame costs what it did, and is the same frame.
// A graph marks the filters that may be cached by writing VIZ_LUT2 in place of "lut2=" or VIZ_LUT in place of
// "lut=": only where the inputs are 8 bits (the lookup covers 0-255; a lut on 16-bit values, as the lagfun's exp and
// log, stays "lut="). Every marker becomes the plain filter again, with the lookup or its own expression, so a graph
// is always valid after vizCacheLuts or vizPlainLuts.
#include <string>
#include <vector>

namespace bp
{
	// written in place of "lut2=" and "lut=" (always followed by c0='...')
	constexpr const char* VIZ_LUT2 = "bplut2=";
	constexpr const char* VIZ_LUT = "bplut=";

	// graph with each marked filter's expression replaced by its lookup from mapsDir/lut/, made there first when it
	// isn't yet. missing: when given, one not made yet isn't made here (that filter keeps its expression this time)
	// but its key is listed, for vizWriteLuts on another thread. An empty mapsDir: all plain
	std::string vizCacheLuts(const std::string& graph, const std::string& mapsDir, std::vector<std::string>* missing = nullptr);
	// the markers back to plain filters, their own expressions
	std::string vizPlainLuts(const std::string& graph);
	// the lookups for these keys, written into mapsDir/lut/ (those there already kept); false: any failed to write
	bool vizWriteLuts(const std::vector<std::string>& keys, const std::string& mapsDir);
	// the lookups a graph's markers would read (their keys: "2:" or "1:" and the expression), in order; the short
	// expressions, quick already, not among them
	std::vector<std::string> vizLutKeys(const std::string& graph);
	// a key's file, under mapsDir: "lut/<hash of the cache's version and the key>.txt" (the lookup, or "=": none)
	std::string vizLutFile(const std::string& key);
	// a key's values as the filter makes them on 8-bit pictures: lut2's 256 x 256 (row y, column x), lut's 256.
	// false: not a table (it doesn't parse without w and h, or a value is NaN or out of range)
	bool vizLutTable(const std::string& key, std::vector<unsigned char>& out);
	// removes the files in mapsDir/lut/ none of these graphs reads (a style changed: its old ones)
	void vizPruneLuts(const std::string& mapsDir, const std::vector<std::string>& graphs);
	// a style drawn at 60 frames a second (its sources at rate=60) drawn at fps instead, at the same speed in seconds:
	// its sources' rate=60 and r=60 to fps, each lagfun's decay d to d^(60/fps) (as far in a second), each tmix's
	// frames scaled, and n in crop's and overlay's expressions read as the frame it would be at 60 (floor(n*60/fps)),
	// so a step every k frames comes as often. A graph without rate=60 (Peaks, Circle: 30 a second) as it is
	std::string vizAtFps(const std::string& graph, int fps);
	// the copy of the sound a style measures (never what's heard: [ao]) evened out first, so every song moves the
	// picture alike: a gate keeps silence and hiss still; a slow compressor with make-up gain lifts a quiet song and
	// holds a loud one (attack 0.6 s, release 2.5 s: the level is ridden, the hits are kept); a treble shelf (+5 dB
	// from ~2.5 kHz) wakes the right of the spectrum, which sat low. Each works sample by sample with no look-ahead,
	// so the picture stays on the beat (dynaudnorm was the obvious choice, but it waits seconds ahead). Measured on
	// a song and the same 20 dB down: the bars' mean 133/90 before, 144/132 after, the movement a frame unchanged.
	// The graph's "[aid1] asplit [ao] [a]; " head rewritten; any other graph as it is
	std::string vizShapeInput(const std::string& graph);
	// (tests and tools: an expression shorter than this is left as it is, quick already)
	extern size_t vizLutMinLength;
}
