# The top screen's pictures for Music, Books and Videos; the videos' cache (2026-11-21)

What was built, where, and what to know before touching it.

## Pieces

- `videocache.*` (bpcore, tested in `testVideoCache`): a video's cache name (FNV-1a 64 of path|size|mtime, 16 hex),
  its three files (`.meta`, `.s.jpg` 128x72, `.l.jpg` up to 960 wide), the facts' text form (`bp-video-meta 1` then a
  line a fact; unknown lines skipped), `videoMetaFromTracks` (mpv's track list, `Track::w/h` from `demux-w/-h`), and
  the pane's labels.
- `videoframe.*` (bpprobe): one open per video: the header's facts (no `avformat_find_stream_info` unless the picture
  has no size: MPEG-TS), one frame at 10% (90 s at most), a second try at a third when the first is near black (mean
  luma < 24), swscale to the large size (display aspect), the small one cropped from the large one. 6 s deadline, one
  decoder thread, 600 packets at most. `encodeJpeg` (libjpeg-turbo, `JCS_EXT_RGBA`).
- `videoartworker.*` (app): one thread, newest want first, cache hits answered at once (a hit `utimes` its files so
  `trimCacheFolder`, 96 MB, keeps what is looked at). Misses wait while busy (main's index-busy rule: a file opening or
  a video playing). A 0-byte `.s.jpg` is "no picture" for good. A `fromPlayer` meta is never overwritten by a probe.
- `Ui::MediaArt` (`ui_mediaart.*`): list thumbnails as textures (64 kept), top frames as shared `PaneImage` (3), and the
  pane's song/book covers as pixels from CoverWorker under `home:pane|` keys (routed out of `songCoversReady`; 4 kept).
  `serial` is in Videos' signature; the pane's key holds "|art" / "|f" and the facts, so a landing recomposes it.
- The pane: `PaneSpec::coverArt` is now a `shared_ptr` (it was copied every pass); `coverSquare` for albums; `hero` +
  `heroArt` for the spec's Files › a video layout (`lay::previewPane(..., square, hero)`, rows two to a line,
  `paintHero` fills and fades from 120 to 232 units).
- main: `evVideoArt = evMpv + 17` (18 registered); a playing local video's tracks are `record`ed once per
  path|tracks|chapters (log `video art: facts of … from the player`).

## Measured (Mac, generated 720p/816p H.264)

A miss 19-26 ms; a hit a few ms; pane recompose 6-7 ms at 1280x960. On the handheld (A55, software decode) expect a
1080p HEVC miss in the hundreds of ms: the worker sleeps as long as it worked after each, so it takes half a core.

## Not done / may differ on the handheld

- Files, Home's Continue card and Search don't use the frames yet (the cache is keyed by path: they can).
- `probesize` 2 MB: a file whose index is at its end over a slow card may hit the 6 s deadline; it is then not written
  and asked again next run (log `video art: … timed out`).
- glibc: the large RGBA (2 MB) is freed when the third newer frame lands; the worker's decode buffers are freed per file.
