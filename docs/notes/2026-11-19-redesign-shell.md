# The redesign's shell: rail, header, hints, mini bar (2026-11-19)

Step 1 of the redesign's section screens: one frame every section page draws in, so Home, Files, Playlists, Settings
(and later Music, Books and Manga) do not each invent a header, a rail or a hint bar. The contract is
`lay::shell(mini)` in `src/kitlayout.h`; the drawing is `Ui::Shell` in `src/ui_shell.h` / `src/ui_shell.cpp`.

## What it draws, and where

- **Section rail** (`lay::shell().rail`, `lay::Rail`, `Ui::KitNew::rail`): Home, Files, Playlists, Extensions, Network,
  then Settings pinned at the foot. Music is a later step and is hidden; **Books has no page in the app yet** (books are
  EPUB/PDF/TXT files opened from Files), so it is hidden too. With both hidden, six cells fit and the rail never
  scrolls. L / R step the sections and wrap, Settings included (L from Home is Settings, R from Settings is Home, as
  `screens.js` has it); a tap opens one; the D-pad never enters the rail. (The rail's cell was Manga, only while its
  reader was on, with a red mark while a video played; since 2026-11-19 it is Extensions, always, and no cell carries
  a warning: docs/notes/2026-11-19-extensions-rail.md.)
- **Header** (`lay::shell().header`): the back key when `Ui::backIconShown` says there is somewhere to go, the title
  (24) and crumb (12.5) beside it, the status cluster drawn with `Kit::status` (clock, battery and its bolt, the
  playing bars, the incognito and Web badges), the More key (X's menu) and the 48 x 48 Search key left of the status
  cluster. Settings has no More, as the spec says.
- **Hint bar** (`Kit::hints(Rect, list)`): the new variant of the kit's bar; the old full-width call now forwards to it
  with the panel's own box, so its drawing is unchanged.
- **Mini bar** (`lay::shell(true).mini`, `lay::miniBar(Box)`, `Kit::miniBar`): the kit's bar right of the rail, with the
  play mark, title, time, progress line and SELECT cap. The old `drawMiniBar` is skipped on the shell's pages; the
  mini bar's play mark and bar keep their ids and the hold that takes playback over. `Ui::miniBox` / `miniMarkRight`
  give the pointer the same box the kit drew.
- **Brand mark** (`Kit::brandMark`): the clapperboard from `components.md` ("Brand mark") and `kit.js`'s `brandMark()`,
  22 units, drawn above Settings when the rail has room. The spec only shows it in the top panel's preview panes; the
  rail's foot is where the bottom panel has room for it (the owner asked for it on the bottom screens).

## The one place Search will be

`Ui::Shell::search(Player&)` in `src/ui_shell.cpp` is the only thing L3 and the header's Search key call. Today it
raises `ui.alert("Search", "Search is coming in a later step.")`. The Search page or sheet replaces that one call; the
keys and the key's rect are already wired (`F_SEARCH`, `ShellHead::search`).

## What a screen agent uses

```cpp
Ui::ShellHead h;
h.title = "Files";
h.crumb = "Files › Videos › Movies";
h.hints = {{"A", "Open"}, {"B", "Up"}, {"X", "More"}, {"Y", "Select"}, {"L/R", "Sections"}};
ui.shell->draw(ui.shell->section(p), h, p);   // the frame
// ... draw the body in ui.shell->layout(p).body
```

`Ui::Shell::draw(section, head, p)` is the frame's one entry point; `head.right` is the header's right slot for a
page's own badges or a group's neighbours (drawn from `right` leftwards, returning its left edge). The body is always
`ui.shell->layout(p).body`; drawing it never means touching the rail, the header or the hint bar.

**Bridging the old bodies.** Until a page is rebuilt, its area still draws the whole 640 x 480 panel (its own header,
hints and 640-wide rows). `Ui::renderPage` puts that drawing into a texture and copies the `0,54 .. 640,450` part into
the shell's body box (uniformly scaled, centred), and `Ui::pointer` maps the page's touches back through the same
rectangle. The small `drawSectionHeader` markers and Home's tiles are inside the cropped part until their screens are
rebuilt, which is where the rail replaces them. When a page starts drawing its own body in the body box it calls
`ui.shell->adopt(Page::Browse)` once (from its area's constructor; `Shell` is made first in `Ui`'s constructor for
this), and the texture and the mapping stop for that page.

## Redraws

`Ui::Shell::signature` is part of `Ui::signature`: the section shown, the rail's index, the pressed frame piece, the
Manga warning and whether the mini bar shows. The clock, battery, what plays and the settings are already there.

## Checked

- Desktop build (macOS, SDK 26, pdfium) and `./build/core-test` ("core: all passed"), including the appended
  `miniBar(shell.mini)` checks in `testKitLayout`.
- Not checked: the screenshots (`./build/banana-player --press shot:…` is refused by this run's shell permissions) and
  therefore the frame's pixels, the rail's labels at 11.5 units, the scaled body's sharpness on the handheld, and the
  touch mapping. The handheld itself, KMSDRM (render-target support: if the renderer has no target textures the old
  body is drawn whole and a line is logged) and the Linux build were not run.

## Kept though the spec would drop them

- All recent's tabs: the spec moves them to L2 / R2 (L / R are sections in the shell). L / R step the sections and
  L2 / R2 step All recent's tabs, so the feature keeps a key.
- Files' form (Add / Edit connection) and the Extensions drawer keep L / R while they are open.
- The mini bar's hold for the Options menu and its play mark's hold to take playback over are unchanged.
- L3 was Like while browsing; the spec makes it Search. Like stays on the player, in START's menu and on the Web
  dashboard.
