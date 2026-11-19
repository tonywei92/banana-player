#pragma once
// The six styles added on 2026-11-12, a file each (src/viz_*.cpp): its lavfi-complex graph, and the stills it reads,
// written once into the visualizers' maps folder as Circle's tables are (Player::graphFor). The rules every graph
// keeps (docs/architecture.md, Music graphs, and the comment at peaksGraph() in settings.cpp):
// [aid1] in, [ao] (the sound, untouched) and [vo] out; each audio branch starts with aformat=sample_rates=44100|48000;
// 30 frames a second at most; YUV (or gray) all the way, nothing converted per frame; a still input is one frame
// (r=1:d=1, or a picture by movie=), never a loop; over the art ([vid1]) only the strip under it is drawn each frame.
// Written at 60 a second and drawn at the visualizer-fps setting (vizAtFps, src/vizcache.h): time is counted only by
// rate=60, lagfun's decay, tmix's frames and n in crop and overlay. A lut or lut2 whose inputs are 8 bits is written
// bplut= / bplut2= (VIZ_LUT, VIZ_LUT2): its expression is swapped for a cached lookup that gives the same values.
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
	std::string auroraGraph(bool overCover, const std::string& mapsDir);
	std::string vinylGraph(bool overCover, const std::string& mapsDir);
	std::string seaGraph(bool overCover, const std::string& mapsDir);
	std::string discoGraph(bool overCover, const std::string& mapsDir);
	std::string dancerGraph(bool overCover, const std::string& mapsDir);
	std::string wobbleGraph(bool overCover, const std::string& mapsDir);
	std::string tubemanGraph(bool overCover, const std::string& mapsDir);
	std::string washerGraph(bool overCover, const std::string& mapsDir);
	// their stills into dir (true: written, already there, or none needed)
	bool writeLedMaps(const std::string& dir);
	bool writeSynthwaveMaps(const std::string& dir);
	bool writeBananaMaps(const std::string& dir);
	bool writeCityMaps(const std::string& dir);
	bool writeScopeMaps(const std::string& dir);
	bool writeHeartMaps(const std::string& dir);
	bool writeAuroraMaps(const std::string& dir);
	bool writeVinylMaps(const std::string& dir);
	bool writeSeaMaps(const std::string& dir);
	bool writeDiscoMaps(const std::string& dir);
	bool writeDancerMaps(const std::string& dir);
	bool writeWobbleMaps(const std::string& dir);
	bool writeTubemanMaps(const std::string& dir);
	bool writeWasherMaps(const std::string& dir);
	// the simple ones (src/viz_simple.cpp): plain shapes in the theme's colours
	std::string mirrorGraph(bool overCover);
	std::string dotsGraph(bool overCover);
	std::string horizonGraph(bool overCover);
	std::string stepsGraph(bool overCover);
	std::string barcodeGraph(bool overCover);
	// Rings reads two tables from dir (writeRingsMaps)
	std::string ringsGraph(bool overCover, const std::string& dir);
	bool writeRingsMaps(const std::string& dir);
	// The bars' contrast, between the measured levels (one row, 0..252) and their easing: showfreqs' log scale puts
	// most bands within a few steps of each other, so each is pushed away from the mean of the bands around it (x6:
	// a band above them a peak, one below a valley) and the levels spread around 150 (x1.8). On a busy frame
	// neighbours differ by ~30 steps of 252 rather than ~14, and the row swings from near the floor to past half.
	// tag: a prefix for the labels, unique in the graph; reach: the neighbours each side (1 for 16 bands, more
	// where there are many); push, spread and pivot as above, for a style that wants it gentler (Pocket console:
	// its chunky dots made the full strength jumpy). Starts with a comma, ends on the row.
	inline std::string vizContrast(const std::string& tag, int reach = 1, const char* push = "6", const char* spread = "1.8",
								   const char* pivot = "150")
	{
		const std::string p = pivot;
		return ",split [" + tag + "c1] [" + tag + "c2]; [" + tag + "c2] avgblur=sizeX=" + std::to_string(reach) + ":sizeY=1 [" +
			   tag + "c3]; [" + tag + "c1] [" + tag + "c3] bplut2=c0='clip(" + p + "+" + spread + "*(x-" + p + ")+" + push + "*(x-y),0,252)'";
	}
}
