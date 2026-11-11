# leak-loop: the player and the manga reader, back and forth, measured

For `docs/notes/2026-11-11-reader-memory.md`. Nothing here is built into the player.

- `loop.py BIN N OUT [KIND]`: drives `--press` through N cycles and samples the process at the end of each (the
  kinds and what is sampled: the comment at its top). It makes its own media with ffmpeg (test patterns, near-silent
  songs). Growth that scales with N is a leak; a step that stops is a cache filling.
- `fake-java`: stands in for the manga engine (`BP_JAVA`), so its start, stop and idle stop can be run without the
  234 MB download: it answers 200 on the port the player gives it.
- `arena.cpp`: a model of the reader's allocations (two decode threads, 11 MB pages freed by the main thread) for
  glibc alone: `g++ -O1 -pthread arena.cpp -o arena && ./arena 300`, and again with
  `MALLOC_MMAP_THRESHOLD_=4194304`. It prints the RSS left after each "chapter" is closed.

On Linux without a screen or a sound device (the arm64 container of DEVELOPMENT.md's Linux check, plus `xvfb
libgl1-mesa-dri python3 procps ffmpeg zip`):

```sh
Xvfb :99 -screen 0 1400x1100x24 &
DISPLAY=:99 BP_AO_PCM=/dev/null python3 tools/leak-loop/loop.py /tmp/b/banana-player 45 /tmp/out/full full
```
