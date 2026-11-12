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
}
