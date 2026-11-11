# Memory, going back and forth between the player and the manga reader (2026-11-11)

## The question

Does anything build up when a song plays (Art + visualizer), Manga is opened, a chapter is read (the visualizer
resting under it), the reader is closed, the song is changed in the player, and all of it again? And when a video
outside the reader stops the manga engine and entering Manga starts it again?

## Method

`tools/leak-loop/loop.py` drives `--press` through N cycles and samples the process at the end of each, the reader
closed. Its kinds: `full` (Manga, the series, a chapter, steps, the pages grid, fullscreen off and on, the next
chapter, zoom, close, the player, the next song, back), `reader` (open and close only), `song` (the player and a new
song only), `strip` (chapters of 800x9000 pages: bands, the reading mode cycled each time, a turn), `engine` (a video
outside Manga, then Manga again) and `engidle` (the Manga library left open). The engine's part is played by
`tools/leak-loop/fake-java` (an HTTP server that answers 200), so its start, stop and idle stop run without the
download. Growth that scales with N is a leak; a step that stops is a cache filling.

Two machines, because they answer different questions:

- **The Mac** (macOS 27, Apple GPU): `vmmap -summary` gives the bytes and blocks in use in the malloc zones, which no
  allocator's habits hide, the GPU's regions, threads and descriptors; `MallocStackLogging` + `malloc_history
  -allBySize` names what is live at the end.
- **Linux arm64 in Docker** (Debian trixie, glibc 2.41, Xvfb, llvmpipe, `BP_AO_PCM=/dev/null`): the handheld's
  libc. `RssAnon` from `/proc/<pid>/status`. llvmpipe keeps textures in ordinary memory, where Panfrost keeps them
  in GEM objects, so the numbers are not the handheld's; what glibc does with the decoded pages is.

## What was found

### 1. Nothing leaks in the reader, the Ui, mpv or MangaNet (the Mac)

At the end of each cycle, the reader closed:

| run | malloc in use | blocks | GPU regions | threads | descriptors |
|---|---|---|---|---|---|
| `full` x50 | 62.1 MB at 5, 63.8 MB at 50 | 115.6 k, 117.2 k | 290, 372 (27.8, 34.2 MB) | 75–78 | 130 |
| `reader` x60 | | 113.6 k–116.0 k, a sawtooth | | 75–77 | 130 |
| `song` x60 | | 110.8 k at 2, 110.6 k at 60 | | 74–76 | 130 |
| `strip` x25 | 44.9–46.4 MB | 119.3 k–121.9 k, 120.8 k at 25 | 284, 316 (59.5, 61.7 MB) | 41–77 | 128–129 |
| `engine` x25 | | 110.6 k–114.6 k, a sawtooth | | 21–23 | 126 |

The sawtooth is the Ui's text cache (256 textures, then emptied: the clock and the mini bar's time make new strings
every second). `malloc_history` after 14 cycles lists no live block from `MangaReader`, `PageRenderer`, the Ui's
`mfTex`/`mpTex` or the decoder: the largest live things are AppKit's own maps. The GPU's regions still creep in
`full` (about 40 KB a cycle after cycle 20): between a run of 6 cycles and one of 30, what grew by stack is the
Metal driver's pooled command-buffer storage (`IOGPUMetalResourcePoolCreatePooledResource`, under mpv's, SDL's and
the reader's draws alike) and SDL's text textures, no page texture.

Read and found right, so nobody reads them again for this: `MangaReader::close()` (pages, `kept`, `orig`, `thumbs`,
`bands`, the queue; `flushDead` with the top window's context current, which main makes so before every close and
open); a decode that comes back for a closed chapter (dropped by `gen`); `PageRenderer::freePage` (the textures put
aside, deleted in the top's context); the Ui's `mfTex` (2 pages at most) and `mpTex` (60), both freed at close and on
a chapter change; `coverTex` (80); `ReaderFetch::end` and its `dropped` map (256); `MangaFetch::cancel`
(`cancelWanted` is taken by the worker each pass); `drainUnder`'s sink (made once); `pagedecode.cpp` (every FFmpeg,
libjpeg and libpng object owned by a guard or freed on each way out).

### 2. glibc kept the reader's memory after it closed (fixed)

Linux, `RssAnon` at the end of each cycle (the reader closed):

| run | before | after |
|---|---|---|
| `full`, cycle 2 | 274 MB | 188 MB |
| `full`, cycle 27 | 293 MB | 190 MB |
| `strip`, cycle 1 | 390 MB | 185 MB |
| `strip`, cycle 25 | 433 MB | 190 MB |
| `reader` x30 | 252 MB at 2, 272 MB at 30 | 185 MB at 2, 185 MB at 30 (`MALLOC_MMAP_THRESHOLD_=262144`) |

(Before: the build without either fix; after: with both. Threads 84 and descriptors 37 at every sample of both. An
earlier `full` x45 of the old build: 282 MB at cycle 2, 293 MB at 45. These `strip` runs went to the long series
and the ordinary one in turn, the Manga library listing the series read last first; the kind has a library of its
own now.)

Root cause: glibc's mmap threshold is dynamic. It starts at 128 KB and rises to the size of the largest mapped block
freed so far, up to 32 MB, and the trim threshold with it. A decoded page is 2 to 32 MB of RGBA (2.3 MB at the
panel's 640 px, 24 MB zoomed to 2048 px, up to 32 MB for a long page in a paged mode) beside its decoder frame
(4 MB for a 1400x2000 JPEG), so after the first page is freed every later page, its frame and its copies come out
of the heaps: the main one and the arenas of the two decode threads, each 64 MB at a time. A heap gives memory back
only from its top, and only past the (now raised) trim threshold, so what the reader freed stayed with the process:
85 to 105 MB after ordinary chapters, 205 to 245 MB after long ones, rising with more chapters (`strip`: 390 MB
after one cycle, 433 MB after fifteen). `tools/leak-loop/arena.cpp` shows the same with nothing but the allocation
pattern (two threads, 11 MB blocks freed by the main thread; glibc 2.36): 7 MB left after the first close, 96 MB
after the 40th, 132 MB from the 160th on (119 MB in another run); 3.5 MB throughout with the threshold at 256 KB.

Fix: `mallopt(M_MMAP_THRESHOLD, 256 << 10)` first thing in `main` (glibc only). Setting it turns the dynamic rise
off, so a block of 256 KB or more is always a map of its own and goes back to the system when freed. The values
tried with `reader` x30 (`MALLOC_MMAP_THRESHOLD_`): 4 MB 205 → 214 MB, 1 MB 195 → 213 MB, 256 KB 185 → 185 MB.

Its cost, checked: 18 s of a 1920x1080 H.264 video under `strace -f -e trace=mmap,munmap`: 1005 mmap calls before,
1142 after (FFmpeg's frames come from its buffer pools, not from malloc each time), about 8 a second more.

**Not measured on the handheld.** To compare there without a build: the old build with
`MALLOC_MMAP_THRESHOLD_=262144` in the launcher's environment, against without; read a long chapter, close it, then
`grep -E 'Rss|Pss_Anon' /proc/$(pidof banana-player)/smaps_rollup`.

### 3. The engine started again every five minutes while a Manga page was open (fixed)

`Ui::mangaVideoGuard` asked for the engine on every pass while a Manga page showed and the engine was `Installed`.
`JavaExtHost`'s idle stop (five minutes with no request) leaves it `Installed`, so the stop was undone 2 ms later:
under a chapter read from the card the engine was stopped and started every five minutes (25 s of a busy core on the
handheld each time), and its 400 MB never came back while Manga was open.

```
23:44:42.030 idle 307 s: stopping it      23:44:42.283 installed      23:44:42.285 starting
```

Fix: the wish is made once per entry into the Manga pages (`engineInManga`, `engineEntryWant`), and waits while a
stop is still ending (`JavaExtHost::stopping()`) or the engine is still downloading. After an idle stop the next use
starts it, as every use always did (`graphql()` → `want()`). With `engidle` after the fix: the idle stop at 300 s,
and nothing started in the four minutes that followed. `engine` x6 after the fix: started on every entry (7
starts), stopped by every video.

The engine's own life is clean (`engine` x25, before the fix too): one child at every sample, no zombie, no
SIGKILL needed, 21 to 23 threads and 126 descriptors throughout (the watcher thread ends with its process, the GraphQL
thread is one for the run).

## What is left

- **The handheld.** GPU memory under Panfrost is not visible from the Mac or from llvmpipe: the page textures
  (`PageRenderer`, up to the 32 MB budget again as textures), the bottom panel's fullscreen pages, and what Mesa keeps
  of freed buffers. To look: `/sys/kernel/debug/dri/*/gem_names` or `gems` if debugfs is mounted, else `MemAvailable`
  and `Shmem` in `/proc/meminfo` before a chapter, with it open, and after closing it, ten times over.
- **The GPU regions' creep on the Mac** in `full` (290 → 372 regions, 27.8 → 34.2 MB in 45 cycles; 284 → 316 in
  `strip`) is the Mac's GL-on-Metal layer and SDL's Metal renderer, neither of which the handheld runs: left alone.
- On the Mac the footprint after long chapters swings between 175 and 355 MB with 45 MB in use (`strip`): its
  allocator keeps freed large blocks too (`Malloc Large (empty)`, 130 MB dirty) and lets them go under pressure.
- **A chapter from a source or a server** (`ReaderFetch`, the page cache) was read, not run: it needs the engine or
  a comic server.
- **The real engine** (Suwayomi on Java): only its start, stop and idle stop were run, with the stand-in.
- The reader's peak is the larger matter on 1 GB: a long page in a paged mode is decoded whole, up to 32 MB, and is
  in memory twice more for a moment (the decoder's frame, the texture). Not changed here.
