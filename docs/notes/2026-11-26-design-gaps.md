# Design spec vs shipped code (0.9.7): what is designed and not built

Audit of `docs/redesign/` (225 screens built from `src/screens.js`, `components.md`, `coverage.md`, `backlog.md`) against
the code at 0.9.7. Method: each screen's quoted strings, keys and rules were grepped in `src/`, `web/` and `FEATURES.md`
(an automatic pass over every quoted phrase, then hand checks on every screen whose phrases were missing, plus the
backlog's own "not built yet" lists). Nothing was run; a screen counts as Built when its distinctive strings, keys and
states are found. Pixel layout and spacing were not compared. The mock data in drawings ("Sintel", "17 of 42") is not a gap.

## 1. Summary

| Area (screens.js group) | Screens | Built | Partly | Not built |
|---|---|---|---|---|
| Player (video, music, browser, photo, sleep) | 44 | 30 | 2 | 12 (Lyrics) |
| Books (shelf, readers, catalogs) | 30 | 26 | 4 | 0 |
| System (quick settings, volume, power, incognito) | 12 | 9 | 3 | 0 |
| Web dashboard (phone) | 7 | 6 | 0 | 1 (`phone-lyrics`) |
| Manga, Network, Shell, Music library, Settings, Files tools, States, Playlists, Videos, Search, Viewers, Photo, Web Books (16) | 132 | 132 | 0 | 0 |
| **Total** | **225** | **203** | **9** | **13** |

`coverage.md` says 210 screens, 195 built, 14 not built, 1 kept as earlier design. Now: 13 are flagged `later`
(12 `lyrics*` and `phone-lyrics`); `power-dim` is counted under Partly here. The other 15 screens added since (13 catalogs,
web Books) are built.

## 2. Not built

Lyrics is the only wholly unbuilt design (`docs/redesign/lyrics.md`). No match for "lyric" anywhere in `src/` or `web/`
(only a `.lrc` MIME entry in `src/webmedia.cpp:44`). Confirmed.

- **Lyrics, one feature, 13 screens (L).** `lyrics` (sheet, line follows song), `lyrics-on` (top screen), `lyrics-timing`
  (0.1 s nudge, Reset timing), `lyrics-ask` (go online once), `lyrics-searching`, `lyrics-plain`, `lyrics-none`,
  `lyrics-offline`, `lyrics-off`, `lyrics-results`, `lyrics-query`, `lyrics-more` (Save beside the song, Remove), and
  `phone-lyrics` (dashboard, plus its upload). Needs: .lrc/tag/embedded lookup, an online lookup (lrclib in the design),
  a Settings row, a sheet, a top-screen layout, a keyboard flow, a Web page. Split: local .lrc + sheet + top screen (M),
  online search with Ask/results/query (M), phone page (S).

No other screen has zero support.

## 3. Partly built

| Screen / component | What is missing | Evidence | Size |
|---|---|---|---|
| `video-picture`, `video-look` | The Picture sheet has no look tiles: looks (Sharpen, Soften, Scanlines, LCD grid, Retro) are only a Settings row ("Picture look"). Also missing: per-look Strength (10 to 100%), "Use for" (Only this video), tile samples, hold Y to compare with Off. Adjust is built. The look reads once when a film opens. | `picture-look` only in `src/settings.cpp:694`; `vf` set at `src/player.cpp:686`; no match for "Strength" / "Only this video" / "compare" in the UI; `FEATURES.md:1129` | M |
| `vol-boost` (and the header-badge component) | The header badge that stays after the volume card goes (110% yellow, red past 120%) is not drawn. The same component lists badges for browser, Wi-Fi off, read only: only incognito, web (QR), sleep and read only are drawn. | `src/ui_shell.cpp:622-623` pushes incognito and QR only; `ui_kit.h:272` comment lists boost, browser, Wi-Fi off; no `badges.push_back` for them | S |
| `quick` | The "Screen off now" tile (and battery percent in the title line) is not in Quick settings; tiles are Same on both screens, Web dashboard, Incognito. Screen off is a row of the player's More instead. | `src/ui_menus.cpp:~1000-1016`; `FEATURES.md:511` | S |
| `power-dim` / Screen-off warning | Notice row, veil and "Screen off in N s": not built, by decision (the backlight eases down). Already marked. | `backlight` comment `src/ui.h:1008`; `components.md:559` | none unless the owner wants it |
| `cat-book` | More's "Download every format" is not built. | no match; `backlog.md:281` | S |
| `cat-book-have` | More's "Delete from the card…" is not built for a catalog book (it exists for manga downloads only). | `backlog.md:281`; `src/ui_manga_downloads.cpp:592` is manga | S |
| `cat-offline` | The first line "No Wi-Fi" (the header's Wi-Fi-off badge says so too); the curl reason is shown instead. | `backlog.md:281` | S |
| `cat-books` / `cat-book` | The catalog's own cover pictures (a drawn cover stands in), and the book's whole summary on the top screen. | `backlog.md:281` | M |

Components in `components.md` with no screen of their own and no code: header badges for boost, browser and Wi-Fi off
(above). Components checked and found: Switch, Sources sheet, Book bar, Quick settings panel, Title marquee, Ambient mode,
Smooth list / Pull to refresh, Clean style, Lid-shut keys, Sleep warning, Arriving stops nothing, Lock overlay, themes
(eighteen), Empty state, Read-only badge, Notice rows.

## 4. Code and spec disagree; which is meant? (owner decides)

1. **Picture looks: global or per video.** Spec: a look is global by default, "Only this video" for one film, Strength per
   look, chosen in the Picture sheet. Code: global only, in Settings, no Strength. Build the sheet, or drop the spec down to
   what shipped?
2. **Screen off now in Quick settings.** Spec adds it "so it works over a list or a book". Code keeps it in the player's More
   only. Add the tile, or edit the spec?
3. **Screen-off warning.** `components.md` still has the veil + countdown drawing (`power-dim`), kept on purpose; the owner chose
   the dim without a notice. Remove `power-dim` from the gallery, or keep as history?
4. **Header badges** for boost, browser and Wi-Fi off: spec says every screen shows them; code shows none. The level card and
   Wi-Fi icon may have been judged enough.
5. **Stale docs, not code:** `coverage.md` still says 210 screens and 14 unbuilt; `backlog.md:285` says the catalog screens are
   "all marked NOT BUILT" (they are built); `web-books.md:194` "Order to build" reads as open (the reader is built:
   `web/js/reader*.js`, `bookplace.js`, `src/web/api_books.cpp`).
6. **Spec source bug:** in `docs/redesign/src/screens.js`, the `cat-form` screen's `bottom` (around line 1967) has two template
   strings run together with no `+` (after `Home Kavita</span></div>` and after `192.168.1.28</span></div>`). As written
   the file does not parse in Node, so the gallery build would fail. Needs `+` in both places.
7. **Not drawn but built** (spec lags): the wide page in Book and its zoom; Search's kind tabs for albums, artists, folders;
   dashboard Upload > Files or Folder and drop onto a section (`coverage.md` says so; the code has folder upload,
   `src/web/api_files.cpp:1083`).
