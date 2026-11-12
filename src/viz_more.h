#pragma once
// The six styles added on 2026-11-12, a file each (src/viz_*.cpp): its lavfi-complex graph, and the stills it reads,
// written once into the visualizers' maps folder as Circle's tables are (Player::graphFor). The rules every graph
// keeps (docs/architecture.md, Music graphs, and the comments at peaksGraph() and pocketGraph() in settings.cpp):
// [aid1] in, [ao] (the sound, untouched) and [vo] out; each audio branch starts with aformat=sample_rates=44100|48000;
// 30 frames a second at most; YUV (or gray) all the way, nothing converted per frame; a still input is one frame
// (r=1:d=1, or a picture by movie=), never a loop; over the art ([vid1]) only the strip under it is drawn each frame.
#include <string>

namespace bp
{
	// overCover: Art + visualizer (the art comes in as [vid1]); mapsDir: where the stills are (lavfiPath-safe)
	std::string ledGraph(bool overCover, const std::string& mapsDir);
	std::string synthwaveGraph(bool overCover, const std::string& mapsDir);
	std::string bananaGraph(bool overCover, const std::string& mapsDir);
	std::string cityGraph(bool overCover, const std::string& mapsDir);
	std::string scopeGraph(bool overCover, const std::string& mapsDir);
	std::string heartGraph(bool overCover, const std::string& mapsDir);
	// their stills into dir (true: written, already there, or none needed)
	bool writeLedMaps(const std::string& dir);
	bool writeSynthwaveMaps(const std::string& dir);
	bool writeBananaMaps(const std::string& dir);
	bool writeCityMaps(const std::string& dir);
	bool writeScopeMaps(const std::string& dir);
	bool writeHeartMaps(const std::string& dir);
	// The bars' contrast, between the measured levels (one row, 0..252) and their easing: showfreqs' log scale puts
	// most bands within a few steps of each other, so each is pushed away from the mean of itself and its two
	// neighbours (x3: a band above them a peak, one below a valley) and the levels spread a little around 170
	// (x1.3). On a busy frame neighbours differ by ~27 steps of 252 rather than ~14, the mids still standing.
	// tag: a prefix for the labels, unique in the graph. Starts with a comma, ends on the row.
	inline std::string vizContrast(const std::string& tag)
	{
		return ",split [" + tag + "c1] [" + tag + "c2]; [" + tag + "c2] avgblur=sizeX=1:sizeY=1 [" + tag + "c3]; [" + tag +
			   "c1] [" + tag + "c3] lut2=c0='clip(170+1.3*(x-170)+3*(x-y),0,252)'";
	}
}
