# AGENTS.md

Read `CLAUDE.md` first and follow it exactly: it is this repository's map and its hard rules (code style: tabs,
comments say why; `bpcore` holds window-free library code with its tests in `tests/core_test.cpp`). The manga reader's
design is `docs/design/manga.md`. Never commit, push or touch git history unless asked.

The shell allows only: `cmake ...`, `./build/core-test`, `ls`, `cat`, `head`, `tail`, `wc`, `grep`, `sed -n`,
`unzip -l`, `od`, `xxd`, `file`, `git status/diff/log`. Everything else (and a chain containing anything else) is
refused. A refused command means: use one from that list (one command per call). It does not mean the shell is off:
always build and run the tests before you report.
