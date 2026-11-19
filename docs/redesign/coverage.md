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
- Song player, View › Display (the mode and the visualizer's style on one page), Equalizer tab, visualizer alone.
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
- Themes: Settings › Power & screens › Theme, seventeen of them (Yellow, the default and today's look; Dark; Midnight blue; Dark purple; Dark red; Pink; Coffee; Mint; Retro green; Sunset; Candy; 16-bit; 16-bit Lavender; 32-bit; Sakura; Light; High contrast), a set of values for theme.h's tokens and nothing else. The Web dashboard follows; cards over a picture stay dark; Reading › Colours gains Match theme. Drawn: `set-theme`, and every screen through the Theme switch at the foot of the preview. Spec: components.md, Themes; values and contrast: themes.py.
- A song's Display: one page for the mode and the visualizer's style (four icon tiles, then fifteen in a 5 × 3 grid; the app's own glyphs); View's tabs are Display, Equalizer, Effects, Speed. Song info on top is removed as a setting: the title, artist and album are always shown. The top screen's ground follows the theme in Album art only, Art + visualizer and None. Drawn: `music-view`, `music-viz`, `music-viz-pick`.
- Home on a fresh install (the Empty state on Home, a welcome on the top screen with the app's icon), the state in between (Recent with a card or two, nothing to continue), and the first-start lid question as a Confirm sheet over it. Drawn: `home-first`, `first-lid`.
- Search (new): L3 on every shell screen, the keyboard with live counts on the top screen, results grouped by kind, recent searches, nothing found, a card still being read; the dashboard's search box. Drawn: `search`, `search-results`.
- Music by albums and artists (new): a rail section with Albums (a cover grid) and Artists (rows), an album's and an artist's page with Play and Shuffle, from the songs' tags; the dashboard's Music tab. Drawn: `music-albums`, `music-album`, `music-artists`. The index both read: components.md, Search and Music: the index.

## Journeys (is every one clickable in the walk-through?)

Checked from the code, not from pictures: `python3 journeys.py --journeys` runs `src/kit.js` + `src/screens.js`, takes every
link (a key's target in `keys`, a `data-go` in the drawing, the `TAPS` and `ALSO` tables at the end of `screens.js`) and
walks each journey below one click at a time. `--all` prints every screen's links and every key that leads nowhere.

Before the first pass: 121 screens and 594 links; 79 screens reached by nothing from Home or the fresh Home (the pills above
the walk-through jumped into a few of them), 19 with no B or ‹ that led anywhere, 63 hint-bar keys missing from a `keys`
list. After: 122 screens and 782 links; none unreachable from the first start or Home, none without a way back, no
hint-bar key unlisted, no link to a screen that isn't there. 223 keys still lead nowhere: most act in place (seek, zoom,
type); the ones that should open something not drawn are the gaps under the table.

What changed to get there, none of it a redesign: targets on keys that had a screen to go to; touch targets on the rows,
tiles and keys of sheets and pages (`TAPS`); states that follow by themselves, a card pulled or the engine coming up
(`ALSO`, shown as “then” under the keys); `B` → Home on the section roots and L2 / R2 round the twelve settings groups;
shared sheets (keyboard, Info, Add to playlist) go back to where they were opened from. The rail has Music on every shell
screen, as the Music section's note already said (six cells, Network scrolled off unless open), and Network opens the
connections list, not a folder. One screen was added: `files-playing`, Files with the mini bar after B on a video.

| Journey | State | The walk; what is not drawn |
|---|---|---|
| First start | wired | Lid question → fresh Home → Files → a video → View → close → B (keeps playing) → Files with the mini bar → the player → Stop |
| Play music | wired | Music › Albums → album → Play → View › Display → Equalizer → presets. From Files: a folder of songs → a song → List (Y) → More (X) → Go to artist → an album. Music with no Music folder (Empty state) and while a card is first read |
| Playlists | wired | Playlists → a playlist’s items → play from one; Y → select mode → Move → put down → Done; Add to… → the picker; a playlist with gone items; Add to playlist from the video player → New playlist… → keyboard. A playlist’s and an item’s More are lists in `keys` |
| Liked | wired | L3 → the Like note; Home › Shuffle Liked plays; Playlists → Liked → plays; its select mode; Liked with nothing in it |
| Books | wired | Shelf → a book → Contents → back; Text → Book mode → the Book bar; More; a PDF → Pages → View → More; the empty shelf |
| Manga | wired | Shelf → History → continue; series → reader → View → Fullscreen → View → Book → end of chapter → Get → Downloads; the reader’s More; Browse → a source → a series; Sources → Repositories → add one; a server; the engine loading / ready / failed; the question while a video plays. A comic’s Pages is the PDF’s sheet. Not drawn: selecting chapters, a source’s filters |
| Search | wired | L3 on Home, Files, Music and Playlists → typing → results → a video, a song, an album, a book, a folder. A playlist result opens the playlist’s page |
| Network | wired | Connections → Find → the form → keyboard → Save → a folder → a video buffering; a book copied first, the copy failing; a read-only folder and its alert; no Wi-Fi; no connection yet (Empty state). A connection’s More (Edit, Test, Delete) is a list in `keys` |
| Web dashboard | wired, both sides | Fresh Home (Y), Home › Send from phone, Quick settings or Settings → the page (QR, address, PIN) → a browser signs in → the phone’s Home → Files → a song → Now playing → Plays on: Handheld; or it plays there → Hold A / Play on… / the browser gone. The page off and with No Wi-Fi. The phone’s Music, Playlists and Books tabs are not drawn (Music’s two pages are minis under Music: Albums) |
| Settings | wired | START → Menu → Settings → each of the twelve groups (L2 / R2 go round) → Theme, which steps the page’s theme |
| Quick settings | wired | F → Web dashboard, Incognito, Screen off now, Quit; Volume past 100% and the first time past 120% |
| Incognito | wired | Quick settings → the question when a browser is signed in → on, the dashboard off → off again; Settings › Lid or sleep in incognito; the badge on the player and the mini bar (`video-states`, `states-browse`) |
| Lid and sleep | wired | The first-start question; When the lid closes and the lid-shut keys in Settings; the bottom screen dimming → off → waking (“Paused: the handheld slept” is in that screen’s strip) |
| Files: select, copy, move, delete | wired | Y → select → Copy / Move → put it here → the job; Delete?; Rename… → keyboard; Info; Add folder to playlist → Working… |
| Errors and empty states | wired | A file that won’t play → Info; an empty folder; a card taken out; gone items; touch lock; the alert pattern |
| Updates | wired | Settings › Updates → “0.7.19 is out” → Update now → Downloading (Hide) → ready, Restart now → Home; the check failing → Try again |
| Photos | wired | Files → a photo → View (Fit / Fill) → back → More → Info |
| Home: a card’s More, All recent | wired | Home → X · More → Resume; All recent → an item |
| Quit | wired | B on Home, START › Menu, Quick settings → Quit? → Cancel |

The second pass (2026-11-19, backlog.md) drew the gaps the first one listed: 154 screens and 1,039 links, none unreachable, none without a way back,
no hint-bar key unlisted, 23 journeys walked. A phone’s page is a screen with a third drawing (`phone`), shown beside the handheld’s two panels
(what the handheld shows meanwhile); its taps are in `keys`, which is what `journeys.py` walks. What is still only listed, not drawn:

1. More (X) on a playlist and its items, Liked, a song in Files, an album, an artist, a search result, a connection, select mode and a book on the shelf: lists in `keys` (one pattern, `moreS()`; the table on “A playlist: its items” has the playlist ones).
2. Manga: selecting chapters, a source’s filters.
3. The phone’s Music, Playlists and Books tabs as phone pages (Music is drawn as desktop-width minis).
4. The one song player drawing says “Playlist · Late night” whichever list it was opened from (an album, an artist, a folder): the crumb is text, the layout is the same.
5. Shared sheets are drawn over one screen each (the picker and Info over a video, the keyboard over Files). Opened from elsewhere they show that screen behind them; B returns to where you were. Nothing to fix.
