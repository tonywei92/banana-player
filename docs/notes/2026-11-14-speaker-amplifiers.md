# Sound on the left only, or none: the RG DS Plus's speaker amplifiers

**Seen:** on the RG DS Plus, a video's sound now and then came from the left speaker only; once, from neither.

**Not the player:** the `audio:` lines in log.txt (Player::logAudio: mpv's audio-params, audio-out-params, ao, af,
lavfi-complex, the track) said stereo in, stereo out, ALSA, no filter, no graph, while it was silent.

**The kernel's log** (`dmesg | grep aw88166`): the speakers are two aw88166 amplifiers on I2C (`2-0034`, `2-0035`),
a card of their own (`hw:1`), reached through BananaOS's default PCM: plug → `banana_eq` → `hw:1`, S16_LE 48 kHz
stereo (/etc/asound.conf, bananad's). At each stream start each chip's driver checks the I2S clock and locks its PLL;
often it can't: `check pll lock fail, reg_val:0x0001` / `mode1 iis signal check error` / `mode2 check iis failed` /
`pll check failed cannot start` / `aw88166 device start failed. retry = N`, N 0..4, about 3 s in all, then it gives
up till the next stream start. One chip giving up is one silent speaker (2-0035 alone: left only); both, silence.
103 failed starts in 45 minutes of uptime (the driver logs no success, so the share isn't known). A stream stopping
logs `pa stop check fail:0x0310` (or `0x2311`).

**What the player does about it** (it can't fix the driver):
- `audio-stream-silence=yes` (Player::start): the stream goes on through pauses and seeks (silence), so the
  amplifiers are started less often.
- `src/ampwatch.*`: a thread reads /dev/kmsg from the player's start and, on a chip's `device start failed. retry = 4`,
  tells main, which runs mpv's `ao-reload` 600 ms later (both chips' failures: one reopen), 3 times a minute at most
  (`audio: the speaker amplifier … didn't start`, `audio: the output opened again …` in log.txt). Whether a second
  start succeeds where the driver's five didn't is still to be seen on the handheld.
- Test without a real failure: `echo "aw88166 2-0035: aw88166 device start failed. retry = 4" > /dev/kmsg` (root)
  while something plays.

**Where it belongs:** BananaOS's aw88166 driver (or the I2S being late at a stream's start): games and RetroArch go
through the same chips. A report with these lines is worth filing.
