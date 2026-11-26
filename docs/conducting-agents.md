# Conducting agents on Banana Player

How the main session hands work to other agents (Fable, OpenCode, Claude subagents) and gets it back safely. Written
from what worked, and what broke, while building 0.7.24–0.7.29.

## 1. Pick the agent by the kind of work

| Work | Who | Why |
|---|---|---|
| Design: specs, mockups, wording, "which is better" questions | **Fable** (resumable subagent) | Design judgement; OpenCode's models are bad at it (owner's call) |
| Code with a clear brief: features, fixes, refactors | **OpenCode** (`opencode run -m opencode-go/deepseek-v4.1-flash --auto '<brief>'`) or a Claude subagent | Cheap, parallel, runs in its own worktree |
| A one-line rename, a merge conflict in an index file, a version bump | **The main session itself** | Briefing costs more than doing |
| Diagnosing from the handheld's log | **The main session** reads the log, then briefs the fix | The log is the evidence; the agent gets the conclusion |

Keep one Fable agent alive and **resume it** (SendMessage) for every design follow-up: it keeps the spec in its head,
so each round costs a few lines, not a re-read.

## 2. One agent, one worktree, one branch

- Every code agent gets its **own git worktree** on its own branch (`git worktree add -b <branch> .claude/worktrees/<name> <base>`).
  Two agents in one folder overwrite each other's edits and builds.
- Base the branch on what it fixes: **main** for behaviour both designs share (Session, player, lid), **the feature
  branch** for its own screens.
- Copy the permission file (`opencode.json`) into the worktree and allow exactly what the brief needs: build, tests,
  `git add`/`git commit`, `ffmpeg`, the player for screenshots, Docker for the arm64 check. **Deny** push, merge,
  reset, checkout, switch. Never stage `opencode.json`.
- When a run needs a folder that is busy, either wait for it or start a new worktree from the same branch. Never two at once in one.

## 3. Write the brief like a hand-over to a peer

The brief is all the agent knows. A good one has, in this order:

1. **Where:** the worktree path and branch; what it must never touch (other folders, push, merge, `opencode.json`,
   interactive commands such as `git difftool`, vim, pagers).
2. **What to read first:** CLAUDE.md, the note or spec section that covers the area, the files by name. Point; don't paste.
3. **The problem as evidence:** the owner's words, the exact log lines from the handheld, what was already ruled out.
4. **The outcome, not the patch:** "a middle song goes to the next, the last stays at 0:00 paused", with the rules it
   must respect (`apply()` pure, no waiting on mpv on the main thread, threads never touch Player/Ui, reserve SDL event ids).
5. **How to check it:** build, `core-test`, `manga-check`, a named test for new state, a scripted run with **generated
   media only**, screenshots to a named folder that is not committed.
6. **Logging:** for any bug the handheld showed, add log lines that would pin it next time.
7. **Docs:** FEATURES.md for user-visible changes, a note in `docs/notes/` with the cause.
8. **The report:** commits, files, shot names, what is left, what it could not check. Few lines.

Every commit ends with the agent's own `Co-Authored-By` line.

## 4. Check lightly, but check

The main session does not re-review line by line. It does, every time:

1. Read the **tail of the report**.
2. `git log` and `git diff --stat` against the base: right files, right size.
3. **Build, `core-test`, `manga-check`** itself (the agent's "passed" is a claim).
4. Look at **one or two screenshots** of what changed.
5. For Linux-only code, the **arm64 bundle build** (`packaging/build-bundle.sh --quick`) compiles it.

Then merge, build the bundle, push to the handheld. If a check fails, say so in one line and send the fix back to the
same kind of agent; don't quietly patch large things yourself.

### What light checks miss, and how to cover it

These slipped through this session; each now has a line in the brief:

| Slipped through | Why | The brief now says |
|---|---|---|
| Every song stopped at its end | The test list had one song | "Test with three files: middle and last" |
| Incognito "close all" fired when the manga engine started | Two new SDL event ids weren't reserved | "Reserve any new SDL event type in the main registration count" |
| Lite drew nothing on the handheld | Mac windows show a frame that KMSDRM needs swapped | "Name what differs on the handheld; add logging that pins it" |
| An agent froze in `vimdiff` | `git diff` opened an interactive tool | "Never run anything interactive; use `git --no-pager`" |

Some bugs exist only on the handheld. For those the real check is the **owner on the device plus the log**, so the
brief always asks for logging, and the next step after a push is to read `log.txt` over ssh.

## 5. Keep the owner in the loop, briefly

- Say who is doing what, in one line per agent. When asked "what is running", check `ps` and each log's last write
  time before answering; never guess.
- A crash or restart of the main session can orphan runs or kill them: check processes and uncommitted work, then
  restart from the work left ("you continue an interrupted run; your uncommitted edits are in this worktree").
- Run long agents in the background with a notification; for detached ones, start a small waiter so the end is noticed.
- Relay results as **what changed for the user**, plus anything the owner must decide. Not the agent's whole report.
- Design questions come back as a **recommendation plus at most one alternative**, each with a one-line trade-off.

## 6. Merging and shipping

- Merge feature branches into the shared branch one at a time; build and test after each merge. Resolve index-file
  conflicts yourself (keep both lines); hand big code conflicts to an agent.
- Push to the handheld after every passing merge (`tools/push-handheld.sh`), so the owner always tests the newest build.
- Push to GitHub and publish a release **only when the owner says so**; before that: no YouTube or yt-dlp code in the
  diff, `tools/web-smoke.sh` passes, release notes follow the house rules (no other app names, no incognito callout).

## 7. Spend fewer tokens (learned 2026-11-18)

- **Don't tell an agent to read CLAUDE.md.** Subagents get it loaded already; "read it first" makes them read it a
  second time. Name the exact note *sections* it needs, and paste the two or three rules that bite for this task.
- **Resume, don't respawn.** Keep one long-lived agent per area (e.g. top screen and song, the kit, themes, the spec)
  and send follow-ups with SendMessage: it already knows the code, so a follow-up costs a few lines. Spawn fresh only
  for a new area or when the old one's context is spent.
- **The standing rules live here, not in each brief.** A brief says "Follow docs/conducting-agents.md §8" and then
  only: the worktree and branch, the outcome, the spec sections, the checks, the screenshot cap.
- **Warm build.** Before starting an agent in a new worktree, copy the parent worktree's `build/` into it (or use
  ccache), so it doesn't compile the whole project first.
- **Clear code briefs go to OpenCode first** (§1); Claude subagents for cross-cutting work or when OpenCode is out.
- **Screenshot caps:** 2–4 per agent, stated in the brief. Check the rest by reading values and code. If a
  screenshot attempt fails once, stop and say so; don't loop on it.
- **(2026-11-20) The lean loop.** OpenCode works the status note's Next list on its own from
  `docs/notes/2026-11-20-handoff.md` (GLM-5.3 for big items, DeepSeek for small ones). The conductor does not watch
  it: no log tailing, no mid-run checks. When it says it is done, `tools/land.sh BRANCH` merges, builds, tests,
  bundles and pushes in one call (a few lines of output); then read the agent's `.scratch/report.md` (≤15 lines:
  what changed, what is unsure, shot names) and look at **one** downscaled composite shot per batch.
- **New agent = `tools/agent-wt.sh NAME`** (warm build, deps cache, opencode.json, the current spec), then `cd` there
  in its own call before launching OpenCode.
- **Design: a fresh Fable agent per topic** with a short brief (the screen id, the decision, the files) instead of
  resuming a designer whose context is huge; republish the design page only when the owner asks or at day's end.
- **Read diffs by `--stat` first**, then only the hunks that matter; never cat whole big files (`ui.h`, `screens.js`).
- **Replies to the owner:** a few lines; what landed, what to try on the handheld, what needs their call.

## 8. What every code agent follows (briefs point here)

- **Readable code is the cheapest token saver.** One thing per function, named for what it does; an area's code in
  its own file; the same pattern every time (lay:: Rect → Kit draw → touch on the same Rect) so the next reader
  greps one example and knows the rest. No clever one-liners, no dead code left behind.
- **Test media ends.** An ffmpeg `color=`/`testsrc` input runs forever unless bounded: always `-t N` or
  `-frames:v 1` (a cover), and never leave ffmpeg or a player running when the item is done.
- **Obvious diffs.** One item per commit; no reformatting or renames mixed into a feature; the commit message says
  what changed and why, so a review reads `git show --stat` and a hunk, not the whole file.

- Finish each item with `.scratch/report.md` (≤15 lines: what changed, what is unsure, the shot names); commit
  with `Co-Authored-By: <model> <noreply@opencode.ai>` (OpenCode) or the Anthropic line (Claude agents).

- Work only in the given worktree and branch. No push, merge, reset, checkout, switch. Nothing interactive (vim,
  pagers, difftool): `git --no-pager`. Never stage `opencode.json`.
- macOS build: `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk;
  cmake -S . -B build -DCMAKE_OSX_SYSROOT=$SDKROOT && nice -n 19 cmake --build build -j2`, then `nice -n 19 ./build/core-test`
  (never a bare `-j`: three agents building at full speed froze the owner's Mac, 2026-11-26).
- Tests: the build and `core-test` only, plus at most one quick screenshot when something visual is unclear. The owner
  tests on the handheld (2026-11-18); no scripted runs or timing runs unless asked. Generated media only.
- Screenshots via `--press … shot:NAME` with `BP_SHOTS` into the session scratchpad, never the repo.
- Reserve any new SDL event type; log lines that would pin a handheld-only failure; name what may differ on the
  handheld (KMSDRM, glibc, the real panel size).
- FEATURES.md for user-visible changes; a note in `docs/notes/` for what was learned.
- Commit with your own `Co-Authored-By` line. Report in a few lines: commits, files, shot paths, what was skipped or
  changed and why, what could not be checked.

## 9. Clean up

Remove finished worktrees and delete merged branches once the work is on main. Keep unmerged branches and parked work
(e.g. `vizlite`) unless the owner says otherwise.

## 10. Token lessons from the 0.9.x run (2026-11-20 → 26)

**Who does the work**
- The conductor's own steps re-send its whole (long) history: a one-line change costs more done by the conductor than its size
  suggests. Conductor does only edits whose line it already knows (a constant, a label, a guard); everything that needs
  reading several files, a build-and-look loop or a browser goes to an agent.
- A fresh agent is cheap at the start (CLAUDE.md + one note); its cost grows with every step it takes. Pick the model by the
  job: Sonnet for contained fixes and web work, Opus for cross-cutting/stateful bugs, Fable for anything the owner calls
  design ("ask the designer/UX expert"). Measured: contained Sonnet fixes 60–90k, Opus features 100–250k, the worst
  (a screenshot pass told to look at every picture) ~100k before it was stopped.
- When a session grows long, a new session for the next batch makes the conductor cheap again.

**Briefs that keep agents short**
- Give the evidence, not the hunt: paste the 5–15 log lines that show the bug (read from the handheld's log.txt by the
  conductor with one grep), name the files/functions, say what "fixed" looks like and the exact sequence to test.
- "Token-lean: grep, read only what you change", a screenshot cap (0–2), "reply ≤N lines".
- Say what the Mac can't do (no Java engine, no KMSDRM, no slow-decode hook) so the agent tests the pure logic instead of
  fighting the harness.
- Append follow-ups to a running agent with SendMessage instead of a new agent when they touch the same files.
- Screenshots for a release: a fixed list, no looking at each picture, ONE contact sheet the owner reviews.

**Tools that save rounds**
- `tools/agent-wt.sh NAME` (worktree + warm build, now `nice -n 19 … -j4`: a full-speed copy/build froze the owner's Mac) and
  `tools/land.sh BRANCH` (commit leftovers, merge, build, core-test, bundle, push to the handheld, remove the worktree).
  land.sh refuses: unresolved conflicts/markers in the agent's tree, and any branch that commits opencode.json.
- The spec lives on redesign-impl: never copy redesign-preview's older docs/redesign over it (it silently took screens out).
- Merges of parallel agents conflict in FEATURES.md, ui_draw.cpp, kitlayout.h, screens.js: resolve by keeping both sides,
  rebuild preview.html (`python3 docs/redesign/build.py docs/redesign`), never commit a half-resolved file.

**The handheld**
- Its address changes (DHCP: .50 → .6 → .19 → .29): when SSH fails and ping fails too, ask the owner for the address; never
  scan. When ping works but SSH times out, it's the Wi-Fi napping: retry the port a few times before giving up
  (`for i in 1..6; nc -z -G 4 IP 22 && break; sleep 4`).
- Read its log with one targeted grep (filter out "preview pane now" and "timing:" noise); that line set is usually the
  whole diagnosis.

**Releases** (owner's "publish" = that exact build): bump CMakeLists + README, full `packaging/build-bundle.sh` (it runs
core-test on Linux: it caught an uninitialised struct the Mac hid), push `redesign-impl:main`, sha256, `gh release create`,
prepend "Superseded by…" to the previous one, then reply to and close the issues it fixes — never before the release is out.
Uninitialised data, use-after-free and missing includes show up only on the handheld/Linux: default-initialise structs.
