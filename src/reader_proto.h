#pragma once
#include <SDL.h>
#include <cstdint>
#include <string>

namespace bp
{
	// `--manga FOLDER`: the Strip prototype on the top panel (reader_proto.cpp); returns the exit code
	int runStripProto(SDL_Window* top, SDL_GLContext gl, bool gles, const std::string& dir, Uint32 touchEvent);
}
