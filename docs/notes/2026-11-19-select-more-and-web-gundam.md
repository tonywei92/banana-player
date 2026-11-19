# SELECT opens More; the web dashboard's Gundam keys (2026-11-19)

## Player

- `Ui::PlayerPage::key`: SELECT now calls `openMoreSheet()`, the same sheet X opens (on a video and a song; a file that
  failed to open still opens it, so Stop and leaving are there). View itself is unchanged inside its sheets: SELECT
  still closes View, and on a song's Display it still steps the Show mode.
- `moreSpec` gains **View** as its first row (the picture glyph), which calls `openView` on the tab last used for a
  video or a song. X's More and the header's `...` show the row too.
- The hint bar says **SELECT More**; the separate **X More** hint is dropped (X still opens the same sheet). Updated
  `main.cpp`'s `--help`, the Settings help for Music display / Visualizer style, the player and More comments, and
  `FEATURES.md` (the X / SELECT rows, the second row, Picture size, Music and Sound).
- `docs/architecture.md`'s Ui line now says SELECT and X open More.

## Web dashboard

`theme::themeCss()` emits `--key`, `--on-key`, `--key-focus`, `--hint-bar`, `--on-hint`, `--primary`, `--on-primary`,
their edges and lips, and a key hover. Without strong keys they are BUTTON / TEXT / ACCENT / BG / DIM_ON / ACCENT /
ON_ACCENT, so every other theme is unchanged; Gundam gets its navy keys, red primary, yellow focus and navy hint bar
(and a readable hover instead of the pale PRESSED). `style.css` uses `--key` / `--on-key` for `.box, .btn`,
`--primary` / `--on-primary` for `.box.yellow, .btn.primary` (the player's play/pause key), and `--key-focus` for a
focused box. `.box.surface` and `.btn.ghost` keep their text ink; ordinary key hover uses the key's own hover roles.

## Checked

- `cmake --build build -j`; `./build/core-test` ends `core: all passed` (new checks: Gundam's key/primary variables and
  Light's fallback values).
- Desktop `--press`: SELECT opens More with the View row; SELECT, Down, A opens View on a video and on a song; the
  player hint bar reads **SELECT More**. Shots are under `build/select-shots/` (scratch, not committed).
- Headless Chrome (muted, 1200x800, state theme gundam): the player page is light-bodied, the play/pause key red
  `#D3222B` with white ink, the ordinary keys navy `#1C3C8E` with white ink. Shot:
  `/private/tmp/claude-501/-Users-tonysong-Projects-tonywei92-banana-player/8f5cdf05-682f-4c74-aee7-d4acda54a4d0/scratchpad/webshots/gundam-player.png`
  (generated test video).

## Not checked

The handheld itself, Safari, hover/focus on a real pointer, and the other themes in a browser (only Light's values are
pinned by a test; the rest use the same defaults as before).
