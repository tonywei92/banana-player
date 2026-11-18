# Coverage audit: today's UI vs preview.html

Method: Page enum, Overlay structs, Ui::draw* and drawDialog titles, alert() strings, main.cpp top-panel cards, FEATURES.md headings, each grepped against the preview with data: URIs stripped. The preview's own "Screens not drawn" card (limits section) lists: Network with connections, select mode, copy/move bar, on-screen keyboard, Sources, a series page, Web dashboard page, Info, dialogs. Those are "Mentioned, not drawn" below. The six items already being added are skipped.

## Missing (not in the preview at all), most often met first

1. Playback failed on the top panel: "Can't play this file", "Lost the connection", "Can't reach it", with Retry (A); and "Buffering" while a network file waits. src/ui_main.cpp:395, player.cpp:1165; FEATURES.md:357-360. (grep: Buffering, Retry, Lost the connection = 0)
2. Empty folder: "Nothing to play here" / "No videos, music or photos in this folder". ui_browse.cpp:372,491,1209. The empty-state pattern covers Network, Playlists, Manga, Recent only.
3. Error / info alerts (AlertBox, ui_overlay.cpp:72): about 35 titles, e.g. "Couldn't delete it", "Can't put it there", "This folder is read only", "The playlist is full", "Liked fills itself", "It isn't there any more", "This connection is gone", "Can't open this file/folder", "Comics need the Manga reader", "Up to 150%", "That's very loud". The preview says only "dialogs" in the limits card, so these are really not drawn, but no alert pattern is shown at all: counted here because no screen or note shows one.
4. Gone items: a playlist row marked [Gone] (card out, file deleted, deleted connection), Next/Previous skipping with a note, Y › Remove from playlist. FEATURES.md:185-190.
5. Card taken out / second card inserted while in use ("The card was taken out", "The second card is out"; main.cpp:1595 refreshes places). No screen or state.
6. Reader end states in Manga/comics: "End of chapter: turn once more for the next", "You're caught up" (ui_manga.cpp:2527); "A page didn't load / Try again" (ui_manga.cpp:2412); waiting-for-chapter screen (ui_manga_browse.cpp:1225); series fetch error (drawMangaErr).
7. Manga engine first download: "Get the manga engine?" / "A lighter manga engine" offer and "Downloading the manga engine" with bar, Unpacking the runtime (about 241 MB; ui_manga.cpp:1541). Preview covers only the 25 s reload, not this install.
8. "Reading folder..." busy dialog with Cancel when adding a folder to a playlist (ui_playlists.cpp:1150).
9. Transient top-panel cards other than volume: zoom % card (main.cpp:1797), chapter card, EQ card, Like card, short osd toasts ("Speed 1.5x", "Copied"). FEATURES.md:1251-1260 ("level card").
10. Offline / no Wi-Fi in the places that need it: Network section, Find on the network, Updates ("Wi-Fi needed"), Manga Browse, dashboard "No Wi-Fi" view (WEB_NOWIFI_VIEW, ui_web.cpp:31). Only a header icon is mentioned.
11. Manga download sheets: Choose chapters / Download (MDLS_MENU, MDLS_CHOOSE, ui.h:2367) and the per-row download marks (Queued, Running, Paused, Failed, OnCard). Only "Y selects chapters" is in a map row.
12. Manga Sources sub-screens: Installed / Available / Repositories tabs, source modals (Details, Options, Repo, Remove), Java repository modal, Filter / Languages menu, 18+ chip (drawAdultChip). ui.h:1769-1798. (Repositories itself is already being added; the rest of Sources is not.)
13. Manga Library / History menus: "Clear history", Library dialog, local-series sheet (Open / Info / Delete, A twice), Adding manga dialog, Series info dialog. ui_manga_browse.cpp:2293,2336.
14. Manga server form (Komga, Kavita, OPDS: Server dialog, Edit / Delete) and network-folder browsing inside Manga (MangaNetView, ui.h:2108). Komga / Kavita / OPDS grep = 0.
15. Delete confirmation titled "Delete 'name'?" (second press): preview only says "Delete still asks for a second press" in a note (see also Mentioned).

## Mentioned, not drawn

- Update popup: "Banana Player X is out" / Downloading % / "is ready, Restart now" / "The update didn't work" / Later / Not until tomorrow. Map: "Update prompt ... Unchanged". drawUpdatePopup (update.cpp, ui_main); FEATURES.md:1213-1226. Settings tile reads only "Up to date"; Settings › Updates rows (Check automatically) not shown.
- Dashboard page (Page::Web): off / on / No Wi-Fi / failed views, QR, address, PIN, New PIN, Sign out. ui.h:1371, ui_web.cpp; FEATURES.md:645-690. Map: "Unchanged"; limits card: not drawn.
- Network with connections: list of servers, kind/host rows, empty Network state keys. Map rows only.
- Find on the network (the "Finding..." dialog and result list) and Add connection form (type, host, port, share, user, password, Test connection), password stored sealed. ui_forms.cpp; FEATURES.md:339-350. Map names Find / Add / Edit / Test / Delete only.
- Network file progress for books/docs copied to the card first (not the already-added download item): same file-job panel.
- Copy / Move / Delete job panel (drawFileJob, ui_fileops.cpp:535) and the clip bar "Copy 'x' ... Cancel / Paste here" (drawClipBar). Map: "Copy / Move ... progress, never replaces".
- Select mode (SEL_EDIT / ADD / PICK, select bar). Map row only.
- On-screen keyboard (Ui drawKeyboard; rename, search, PIN, passwords). Map: "Unchanged".
- Add-to-playlist picker ("Add 'x' to..."; ui_playlists.cpp:1104). "Add to playlist" is a row in More; the picker is not shown.
- Info (Page::Info, "Info" dialog; tags, codecs, tracks). Map: Info is a tile in More.
- Quit confirm (drawQuitDialog) and Exit page. Map: "B on Home asks to quit".
- Lock: "Hold the lock 1 s to unlock" state (ui_main.cpp:391,681). Map: "Lock (hold to unlock) Unchanged".
- Lid close / sleep / wake (FEATURES.md:1261), Screen off timers, F + L2 screenshot, Loading spinner: one map row "Unchanged".
- Battery: charging bolt, red low state, percent (FEATURES.md:1250). Preview shows percent on Quick settings only in a note.
- Extensions drawer "No extensions are on" and the first-time sheet ("Open Manga / Done"); Manage extensions switch. Map: "Manage extensions (On/Off, first-time sheet) -> Settings › Extensions".
- Manga series page (Resume, Download, Refresh, Info, heart, chapters list), Sources, Filters, Search. Limits card.
- Manga Downloads tab (queue, pause / cancel all / retry; FEATURES.md:810) and Manga settings page. Map rows only.
- Read-only folder (lock + "Read only", Move / Rename / Delete dimmed with a reason; FEATURES.md:372). One note mentions it.
- Delete confirmation (see Missing 15), playlist "Remove duplicates / Remove missing". One note each.
- EQ Presets dialog (96,22 box, "Presets") and EQ options (save / rename / delete preset). Music-eq screen shows the tab; the Presets list is named only.
- Settings groups other than the tiles: Subtitles preview, Music, Photos, Files, Controls, Updates, Web: tile text only (a group is drawn once, "settings-group").

## Covered (drawn or fully noted)

- Home (Continue, Recent, filters), All recent, START Menu, Files list, More (X) on a file, rail, mini bar and Playing/Paused chips.
- Books section, empty state, text reader, Text view sheet, Contents/Bookmarks, PDF, Extra dim / Light sheet.
- Video player, View, Picture looks, Adjust, List (Y), More while playing, Away-from-normal marks (and the same while browsing).
- Song player, View › Display (Show), View › Visualizer (the style, its own tab), Equalizer tab, visualizer alone.
- Photo viewer; Manga section, reader, Manga-while-video guard, engine loading, "Stopped for Manga" on Home.
- Settings, a settings group, Quick settings (F), volume past 100% and the 120% warning, Incognito badge.
- Playing in a browser, Hold A, browser went away, mini bar while a browser plays, Play on picker.
- Key grammar, device sizes (RG DS / Plus), interactive prototype.

## Added after the audit (new features, not in today's UI)

- Incognito, the lid and sleep: Settings › Power & screens › Lid or sleep in incognito (Nothing / Close all / Quit). Drawn: `incog-lid`. Spec: lid-and-incognito.md, section 1.
- Incognito and the Web dashboard: the server is off while incognito is on; the dimmed switch, the page, the question when a browser is signed in, what the browser sees. Drawn: `incog-web`, `incog-web-ask`. Section 2.
- The keys with the lid shut: volume hold = next / previous, Power key, lid shut (Play / pause / Sleep), Volume keys, lid shut (Volume and skip / Volume only), Shoulder keys, lid shut (Ignored / Hold / As usual), keys under the lid ignored, the player owning the volume keys while shut, no sleep with the lid shut; options A and B. Drawn: `lid-keys`; the rows in `set-screen`. Section 3.
- The Power & screens group and its table now use today's wording, When the lid closes: Pause / Keep playing (0.7.27), in place of the earlier "Keep music playing" proposal.
- The player's third row: View removed (the chips open it; SELECT by the keys, in the hint bar), Stop in its place; the chips touch only, UP / DOWN nothing. Drawn: `video`, `music`, `video-chips`.
- Support link: Settings › About's last block (a line, a QR code of https://ko-fi.com/tonysong, the address written out), and one link at the foot of the Web dashboard's Home, which has no About today. Only there; nothing fetched. Drawn: `set-about`.
- START's Playing tab: removed, item by item accounted for (the table under `video-more`). X is More, SELECT is View, on the player as elsewhere.
- Chips and View: a chip opens the one tabbed View sheet on its tab (no tab-less form); five chips on a song as on a video (Sound, Sound FX, Display, Speed, Shuffle), the Display chip in short values; the Equalizer tab takes the full sheet height. Drawn: `music-eq`, `video-chips`.
- Themes: Settings › Power & screens › Theme, fifteen of them (Yellow, the default and today's look; Dark; Midnight blue; Dark purple; Dark red; Pink; Coffee; Mint; Retro green; Sunset; Candy; 16-bit; 32-bit; Light; High contrast), a set of values for theme.h's tokens and nothing else. The Web dashboard follows; cards over a picture stay dark; Reading › Colours gains Match theme. Drawn: `set-theme`, and every screen through the Theme switch at the foot of the preview. Spec: components.md, Themes; values and contrast: themes.py.
