#pragma once
// The frame-time watchdog of main's loop (docs/notes/2026-11-18-manga-scroll-hitch.md): the work that can hold a pass
// on the main thread (a decoded page taken in, a texture upload, a progress save, a route check, a fetch answer) says
// here what it took, by tag. Main starts a pass after its event wait and ends it after the draws; a pass past 30 ms is
// logged at most once a second with its parts, so a hitch on the handheld names itself in log.txt.
// Main thread only: the counters are plain statics, as the loop is the only writer.
#include <SDL.h>
#include <cstdio>
#include <cstdlib>

namespace bp
{
	struct FrameWatch
	{
		static constexpr int MAX_PARTS = 12;
		struct Part
		{
			const char* tag;
			Uint32 ms;
		};
		inline static Part parts[MAX_PARTS] = {};
		inline static int count = 0;
		inline static Uint32 started = 0, lastLog = 0;

		static void begin(Uint32 now)
		{
			started = now;
			count = 0;
		}
		static void add(const char* tag, Uint32 ms)
		{
			if (!tag || !ms) return;
			for (int i = 0; i < count; i++)
				if (parts[i].tag == tag)
				{
					parts[i].ms += ms;
					return;
				}
			if (count < MAX_PARTS) parts[count++] = {tag, ms};
		}
		static void end(Uint32 now)
		{
			Uint32 total = now - started;
			// (at most a line a second: a stutter that lasts shows one frame, not every frame of it; BP_FRAME_TIMING=1:
			// every pass that did something, for a test run's numbers)
			static const bool every = getenv("BP_FRAME_TIMING") != nullptr;
			if (every ? count == 0 : total < 30 || now - lastLog < 1000) return;
			lastLog = now;
			char line[256];
			int n = snprintf(line, sizeof line, "timing: frame %u ms", (unsigned)total);
			for (int i = 0; i < count && n > 0 && n < (int)sizeof line; i++)
				n += snprintf(line + n, sizeof line - n, "%s%s %u", i ? ", " : " (", parts[i].tag, (unsigned)parts[i].ms);
			if (count && n > 0 && n < (int)sizeof line) snprintf(line + n, sizeof line - n, ")");
			fprintf(stderr, "%s\n", line);
		}
	};

	// one scoped stretch of a pass, by tag (RAII: the destructor adds its time)
	struct FrameWatchScope
	{
		const char* tag;
		Uint32 t0 = SDL_GetTicks();
		explicit FrameWatchScope(const char* t) : tag(t) {}
		~FrameWatchScope() { FrameWatch::add(tag, SDL_GetTicks() - t0); }
	};
}
