# AGENTS.md

Read `CLAUDE.md` first and follow it exactly: it is this repository's map and its hard rules (code style: tabs,
comments say why; `bpcore` holds window-free library code with its tests in `tests/core_test.cpp`). The manga reader's
design is `docs/design/manga.md`; the redesign's spec is `docs/redesign/`, its handoff
`docs/notes/2026-11-20-handoff.md`. Never commit, push or touch git history unless asked.

The shell allows only what `opencode.json`'s permission block names: `cmake ...`, `./build/core-test`, the scripted
player runs that look at a change (`BP_SHOTS=.scratch/shots ... ./build/banana-player ... --press "info,shot:NAME"`,
which quit by themselves), `ffmpeg ...` for the generated scratch media, `mkdir`, `ls`, `cat`, `head`, `tail`, `wc`,
`grep`, `sed -n`, `unzip -l`, `od`, `xxd`, `file`, `git status/diff/log/add/commit`,
`ssh root@192.168.1.50 ...`, `tools/push-handheld.sh root@192.168.1.50`, `packaging/build-bundle.sh`, and
`kill`/`killall ffmpeg`/`killall banana-player` for a run that strayed (an ffmpeg input without an end). Everything
else (and a chain containing anything else) is refused. A refused command means: use one from that list (one command
per call). It does not mean the shell is off: always build and run the tests before you report, and take the shot the
handoff's step 3 asks for. `.scratch/` is scratch (media, shots, state): git-ignored, never staged.
