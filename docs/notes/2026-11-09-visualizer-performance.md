# Visualizer performance (2026-11-09)

## Problem

The owner saw the music visualizer lag on hi-res FLAC, and badly above 1x speed, while the audio stayed smooth.

## Method

Measured on the handheld (RK3568, Cortex-A55), not the Mac: a libavfilter benchmark cross-compiled in the arm64 Docker
image against the bundle's FFmpeg 7.1.1 `.so` files, run over SSH, single-threaded. It is in `tools/viz-bench/`
(README there; results `before.txt`, `after.txt`).

## Root causes

1. **Per-frame colour conversion dominated.** `format=yuv420p` (RGBA to YUV through swscale) cost about 6.5 to 7 ms
   per 640x480 frame, and `colorkey` about 5 ms on the art strip. The Mac hid it: Bars took 0.7 ms/frame there and
   18.4 ms on the handheld; Art+viz Bars about 0.85 ms on the Mac and about 4 ms on the handheld after the fix.
2. **mpv applies `speed` in the audio output chain after lavfi-complex**, so the graph makes 30 frames per second of
   song time: 60 per wall second at 2x, 75 at 2.5x. The old Bars at 2x needed about 110% of a core on mpv's core
   thread, so mpv dropped video frames while the audio stayed smooth.
3. **Hi-res input.** Spectrum at 192 kHz cost 1.9x (8.7 to 16.5 ms): one column per FFT hop, each a full-frame copy,
   with the old separate `fps` filter discarding most of them. It also drew mostly ultrasound and scrolled about 4x
   faster. Waves cost 1.2x. Bars and Circle barely depend on the sample rate.
4. **Circle's `nullsrc d=1` still inputs ran at 25 fps**, so the first second of each song (and after a seek) made 55
   frames (`estimated-vf-fps` 51).

## Fix (`src/settings.cpp`, `src/settings.h`)

- Every visual branch starts `aformat=sample_rates=44100|48000` (plus mono where the channels share a colour: Bars,
  Circle).
- At most 30 frames per second of song time, by the filter's own `rate`/`fps`.
- No per-frame full-frame conversion and no `colorkey`. Only `format=rgb0` at the end in Visualizer mode (plain RGBA
  showed mpv's alpha checkerboard).
- Art + visualizer: the art slice is `overlay`'s main input, held as one frame with `settb=AVTB` (otherwise timestamps
  round to 1/25 s); the drawing is overlaid with `alpha=premultiplied`.
- Spectrum is composited in yuv444 (`opacity=4`, the art converted once to full-range yuv444p).
- Circle's remap tables cover only its 400x400 square (`CIRCLE_BOX`, `CIRCLE_X`, `CIRCLE_Y`), then `pad`; its still
  inputs are `r=1:d=1`.
- Deliberately not varying the fps with `speed`: changing the graph mid-song resets mpv's audio chain and reopens the
  sound output (a second or more on the handheld).

## Numbers (handheld, ms per frame, 44.1 kHz / 192 kHz, before to after; all 30.0 fps)

| Graph | Before | After |
|---|---|---|
| Bars | 18.4 / 15.8 | 7.3 / 8.0 |
| Bars art+viz | 12.5 / 12.1 | 4.0 / 5.0 |
| Waves | 8.7 / 10.4 | 2.2 / 3.4 |
| Waves art+viz | 8.6 / 9.7 | 2.3 / 3.4 |
| Spectrum | 8.7 / 16.5 | 1.5 / 2.8 |
| Spectrum art+viz | 17.5 / 23.2 | 6.1 / 7.6 |
| Circle | 13.7 / 13.8 | 8.4 / 9.4 |

At 2x, after (share of a core): Bars 44% (was 110%), Spectrum art+viz 37% (was 105%), Circle 51% (was 82%).

## Ruled out

`video-sync=audio`, `interpolation`, `display-resample`.

## Rule

Time any graph change on the handheld with `tools/viz-bench/`; the Mac underestimates conversions about 10x. The
Timeline visualizer is being brought in line separately and must follow the same rules (no per-frame conversion, at
most 30 frames per second of song time, a low sample rate in).

## With the equalizer and effects

The owner's lag came back with the EQ on, speed above 1x and a hi-res FLAC together.

**Where it runs.** mpv's audio output chain (`filters/f_output_chain.c`) is: the user's `af`, then `userspeed`
(scaletempo2, for `speed`), then `convert` (to the sound output's rate and format). `lavfi-complex` sits before it.
The decoders have no threads of their own unless `vd-queue`/`ad-queue` are on (they aren't), so decoding,
`lavfi-complex`, the `af` (EQ, effect) and scaletempo2 all run on mpv's one core thread: whatever the sound chain
takes, the visualizer's frames lose. Only the final resampling to the hardware's rate happened elsewhere: BananaOS's
`/etc/asound.conf` makes `default` a `plug` onto `hw:1` at S16_LE, 2 channels, **48000 Hz**, so mpv opened the
output at 192 kHz and alsa-lib's plug brought it down to 48 kHz, in mpv's audio thread, after all of that.

So a 192 kHz file ran the EQ's 10 biquads, the reverb's FFT convolution (`afir`, an impulse response of 0.6-4.6 s)
and scaletempo2 at 4x the samples of the 48 kHz that is all the speakers ever play.

**Handheld numbers, before (indicative only).** One run of a libmpv bench (`mbench.c`: the player's options,
`vo=null`, an untimed null output, so it runs flat out; CPU of the whole process per wall second at that speed; 20 s
of song; EQ gains 6,5,3,0,-1,0,1,2,3,3). Its load conditions weren't logged and another agent's bench was running on
the handheld for part of it, so the figures may be inflated; no after run was made on the handheld (the owner tests
by hand).

| af | 44.1 kHz 1x / 2x | 96 kHz 1x / 2x | 192 kHz 1x / 2x |
|---|---|---|---|
| none (2x: scaletempo2) | 1% / 11% | 1% / 41% | 2% / 164% |
| EQ | 4% / 17% | 7% / 48% | 13% / 160% |
| Hall | 12% / 31% | 26% / 84% | 66% / 283% |
| EQ + Hall | 22% / 50% | 52% / 125% | 132% / 413% |
| EQ + Church / Cave | 24-25% / 56-57% | 64-70% / 144-155% | 193-198% / not run |
| EQ + Wide / Headphones | 4-5% / 17-19% | 8-9% / 50-52% | 13-15% / not run |

(Over 100%: `afir` and scaletempo2 use more than one thread, but the core thread still waits for them.)

**Fix.** A song's `af` now always starts with `@bpin:lavfi=[aformat=sample_rates=44100|48000]` (bpcore's
`afInputFilter`, put first by `Player::joinAf`): over 48 kHz is resampled to 48 kHz by swresample, before the EQ, the
effect and scaletempo2; 44.1 and 48 kHz pass untouched. It is there even with the EQ and effect off. mpv reopens
the sound output when the af's *output rate* changes (a format change it just converts): with `@bpin` always in,
switching the EQ or effect on or off never changes the rate, so no gap. That was checked on the Mac by setting af by
IPC on a 192 kHz file: `@bpeq` alone at 192 kHz, then `@bpin`, then `@bpin,@bpeq` and back. Only the first `@bpin`
reopened the output (192000 s32 to 48000 floatp); the EQ toggles never did. The visualizer still sees the original
sound (it's in `lavfi-complex`, before the af). `af-command bpeq g … equalizer@bN` / `volume … volume@pre` still
work: the labels are unchanged and only the live check moved from "af starts with @bpeq" to "af has @bpeq". The
sound itself is unchanged: ALSA resampled it to 48 kHz anyway, now swresample does it once, earlier.

Example af (EQ on, Hall): `@bpin:lavfi=[aformat=sample_rates=44100|48000],@bpeq:lavfi=[aformat=sample_fmts=fltp,
volume@pre=volume=-7.1dB:precision=float,equalizer@b0=f=31:t=o:w=1:g=6.0:precision=f64,…,equalizer@b9=…],
@bpfx:lavfi=[amovie=<state>/ir-hall.wav[ir];[in][ir]afir=minp=1024:dry=7.3:wet=7.3,alimiter=limit=0.98:level=0]`

**Mac numbers (Mac only, same bench, % of one Mac core, before / after):**

| af | 192 kHz 1x | 192 kHz 2x | 96 kHz 2x | 44.1 kHz 2x |
|---|---|---|---|---|
| none | 0.4 / 0.6 | 3.8 / 1.6 | 1.5 / 1.1 | 0.8 / 0.8 |
| EQ | 1.5 / 1.1 | 5.2 / 2.5 | 2.8 / 2.0 | 1.9 / 1.9 |
| EQ + Hall | 7.2 / 2.2 | 16.0 / 5.3 | 7.3 / 4.3 | 3.8 / 3.8 |
| EQ + Cave | 9.8 / 2.4 | 20.6 / 5.7 | 8.7 / 4.5 | 4.2 / 4.2 |
| EQ + Wide | 1.6 / 1.2 | 5.7 / 2.9 | 3.1 / 2.3 | 2.0 / 2.0 |

A 192 kHz file now costs about what a 48 kHz one does (3-4x less for EQ + a reverb at 2x); 44.1 kHz is unchanged.
On the handheld that should bring EQ + Hall at 2x on a 192 kHz file from "more than the A55s have" to roughly the
44.1 kHz row above (about half a core): still the biggest share of the sound chain. If the owner still sees lag with
a reverb at 2x on CD-rate files, the reverb (`afir`, its impulse-response length) is the next lever. A larger `afir`
`maxp` was slower on the Mac (8192 default: 0.8%; 65536: 1.3%).

In the Mac player (EQ on, Hall, 192 kHz FLAC, Up Up to 2x, Bars and Art+Spectrum): `audio-out-params` 48000 Hz,
`frame-drop-count` 0, `estimated-vf-fps` 30, the 6 s songs ended on time and the next started.

**Rule.** Anything added to a song's af goes after `@bpin` and runs at 44.1/48 kHz; keep `@bpin` first and always
present for songs (its rate is what keeps EQ/effect switches from reopening the output).

## 2026-11-12: Pocket console against Peaks

Timed with `tools/viz-bench` on the handheld (player closed), 44.1 kHz, a synthesised 60 s song, the graphs as
`visualizerGraph()` printed them that day:

| Graph | ms/frame | share of a core at 1x |
|---|---|---|
| Peaks, visualizer | 6.49 | 19.5% |
| Peaks, art + visualizer | 5.93 | 17.8% |
| Pocket console, visualizer | 6.97 | 20.9% |
| Pocket console, art + visualizer (art through the dots) | 7.28 | 21.9% |

All at 30.0 frames a second. Pocket draws at the LCD's 80x60 dots and scales up, so its per-frame work is small; most
of its cost over Peaks is the full-size grid lut2 (and over the art, the three plane lut2s). Circle (8.4 ms, 25%) stays
the dearest shipped style. Note for the next run: the handheld has no sftp-server, so copy with `scp -O`.
