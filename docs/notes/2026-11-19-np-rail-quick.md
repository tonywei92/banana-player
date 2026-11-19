# The Now playing key and Quick settings' block (2026-11-19)

The owner's decisions of 2026-11-19 (`docs/redesign/backlog.md`, "Decided"; `components.md` › Section rail, Quick
settings panel; `screens.js` › `quick`, `quick-playing`, `files-playing`, `states-browse`): the mini bar is gone from
every page, what plays is reached from the rail's **Now playing key** and from Quick settings' **Now playing block**,
and the header's status cluster is the touch that opens the panel.

## The mini bar is gone

- `lay::shell()` (was `lay::shell(bool mini)`) has no mini box any more: `rail`, `main`, `header`, `body`, `hints`,
  with the body running from 6 under the header to the hint bar, as Home always had it. Every caller passes nothing;
  the tests' shell checks and the Files / Playlist / Settings layouts were updated for the taller body.
- `Ui::miniBarShown`, `drawMiniBar`, `miniBox`, `miniMarkRight`, `MINI_BOX`, `MINI_PP` and `MINI_BAR` are removed,
  with the touch ids, the hold timer and the per-page `miniUp` layout branches (Docs, Manga, Sound, Web, Photos).
- `lay::MINI_*` and `lay::miniBar(Box)` stay as data only: Quick settings' block takes `lay::miniBar(card).line` for
  its 3-unit progress line.

## The rail's Now playing key

- `lay::rail` gives `Rail::np` 68 × 44 above Settings (68 × 50 at the foot), its slot always reserved, so the cells
  never change height when playback starts. Cells are 68 × 52, 45 with seven or more sections (the old 56 / 50 is
  gone). Search is at the top, then the sections' scrolling view, then the key, then Settings.
- `KitNew::rail(..., bool npPlaying, bool npPaused, const std::string& npArt)`: `npArt` empty draws nothing (the
  slot stays). Otherwise the Art placeholder at 30 and three bars in ACCENT (moving while it plays, a frame about six
  times a second); paused draws the pause mark in DIM_ON. `KitNew::NP_PRESSED` is its pressed cell.
- `Ui::Shell` draws it from `ui.barPlayer(p)` (main's player, also over a photo) and handles its touches: a tap opens
  the player (`goNowPlaying`), a hold of 600 ms opens what plays' More sheet (`openNowMenu`), and its key is SELECT
  (`Shell::key`, on any section page). L / R pass it by (`railStep` only knows the sections). `Shell::tick` times the
  hold; `Ui::tick` calls it while a shell page shows. `Shell::signature` carries playing / paused and the art's key,
  plus the bars' frame while they move.
- `testKitNewSectionRail`: the np box and its 2-unit gap to Settings, `railAt` = -1 on it, the 52 / 45 cells, the
  slot unchanged across section counts, and scrolling still leaving Search, the key and Settings in place.

## Quick settings

- `quickPanel()` is 592 wide at x = 24 (the spec's 24 from the sides), padding 16: title 48 (with the battery at its
  right), then 8, the Now playing block 64, then 8, the rows. The block is always there; the rows take what is left
  (`quickStep()`), so the panel fits above the hint bar with a volume and both brightness rows on the handheld.
- `drawQuickNow`: the card (SURFACE, focused with an ACCENT edge), the art 48 (the Art placeholder; a music mark on
  TRACK when idle), the title (F_P14, the marquee), artist · time (F_I12; DANGER when the file can't be read) and the
  state pills after it in ACCENT, then Previous 52 × 48, Play / Pause 68 × 48 (primary) and Next 52 × 48, and the
  progress line from `lay::miniBar`. Idle: "Nothing playing" / "What you play shows here", the keys at 45 %, no line,
  not pressable and skipped by the cursor.
- The cursor is a row index, or -1 for the block. It opens on Volume (0); UP from Volume reaches the block, DOWN
  leaves it, and an idle block is never a stop. A plays / pauses (`miniToggle`), LEFT / RIGHT seek 5 s back / 10 s
  on (the player's own predicting + `noteSeek` path), L / R are the player's Step (`SessionHost::stepsHere`), SELECT
  closes the panel and opens the player. Touch: the three keys act, anywhere else on the block opens the player and
  closes the panel, a hold on Play (600 ms) takes playback over from a browser (`takeOver`), and a tap on the veil, F
  or B closes.
- The QuickSheet signature carries the block's state, title, art, place and bars frame so the panel redraws with
  what plays.

## The header's status cluster

- `Ui::drawBattery` records `statusHit` (the playing bars, Wi-Fi, battery and clock, 8 either side, 54 tall) for the
  old headers, the player page and the photos; a tap opens Quick settings. The globe's own tap still wins inside it.
- `Ui::Shell::drawHeader` records the same box from `Kit::status`'s left edge to the More key; `Shell::pointer` makes
  it `F_STATUS`, and the lift calls `toggleQuick`.

## Not done, and why

- The rail's real cover (a song's decoded art, a video's frame): the shell has no cover worker; the Art placeholder
  is the stand-in, as the spec allows. The plumb is `KitNew::rail`'s `npArt` key should the worker arrive.
- The quick panel's switch tiles (two to a row) and Screen off are the spec's next variant, not part of this brief;
  the rows keep their present shape and shrink when seven need the room.
- Search's page and Music / Books' rail cells are other steps; the rail still lists the five sections that exist.

## Shots

- `.scratch/shots/quick-playing-bottom.png`: the block over Files' body (F over what plays).
- `.scratch/shots/rail-playing-bottom.png`: Files while the video plays: the rail's key above Settings, the header's
  bars, SELECT named at the foot.
