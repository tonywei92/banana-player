// ---------- where everything went ----------
const MAP=[
['Getting around'],
['Home’s four tiles: Files, Playlists, Network, Extensions','Home, 2 × 2','The rail, on every shell screen; and START › Menu','mov'],
['Section markers by the clock, L / R','Header of each section','The rail (named); L / R unchanged','chg'],
['Books section','(none: books open from Files)','Rail › Books: EPUB, PDF, TXT and comics','new'],
['Comics (CBZ, CBR)','Files (with the Manga reader on), the Manga library','Those, and Books › Comics; one reader, one place kept','chg'],
['Recent strip: 5 cards + All recent','Home, top','Home: Continue card + Recent row','chg'],
['All recent, tabs All / Videos / Music / Books (L / R)','Home › All recent','Same page; the filter is also on Home; steps with L2 / R2','chg'],
['A card’s menu (Y, hold): Resume, From the start, Show in folder, Add to playlist, Like, Remove','Home','X (or hold) on the card','mov'],
['X on Home: Clear Recent, Show hidden folders, Settings','Home','X › Clear recent · Files’ X › Show hidden folders · rail › Settings','mov'],
['B on Home asks to quit','Home','Unchanged','same'],
['Home with nothing played yet','An empty Recent strip over the four tiles','An empty state: Nothing played yet, Browse Files (A), Web dashboard (Y); the top screen a welcome with three ways to start','chg'],
['The lid question at the first start: Pause / Keep playing','A centred box','A confirm sheet over the fresh Home, Pause first and on B; the top screen explains both; changed later in Settings › Power & screens','chg'],
['Each section opens where you left it','Sections','Unchanged','same'],
['Finding something by its name','(none: walk to it)','Search: L3 on any list, or the header’s magnifier; results by kind from an index of both cards; a search box in the Web dashboard’s header','new'],
['Music by album and artist','(none: folders and playlists only)','Rail › Music: Albums (covers) and Artists, from the songs’ tags; Play and Shuffle on each; a Music tab on the Web dashboard','new'],
['The mini bar; SELECT = Now playing; header’s three bars; Playing / Paused / In browser chips','Every page but the player','Unchanged','same'],
['Top screen with nothing playing','Logo and one line','A preview of what the cursor is on','new'],
['Files, Network and playlists'],
['A opens / plays; B up a folder; LEFT / RIGHT page','Lists','Unchanged','same'],
['“..” row','First row of a folder','Removed: ‹ and B','chg'],
['X Options: Info, Refresh, Hide / Unhide folder, Show hidden folders','Lists, X','X · More, “This folder” column','chg'],
['Y Menu: Info, Copy…, Move…, Rename…, Delete, Add to playlist…','Lists, Y','X · More, “This file” column','mov'],
['Select files (X › Select files, or hold)','Files, Network','Y, or hold; also in X · More','chg'],
['Play a folder / Shuffle a folder','(web dashboard only)','X · More, “This folder”','new'],
['Copy / Move: walk to the folder, SELECT puts it there, X cancels; progress, never replaces','Files, Network','Unchanged','same'],
['Sizes, lengths, where you stopped, hearts, natural order, Read only lock','Rows','Unchanged; more of it on the top screen','same'],
['A book, document or comic on a server: copied to the card first, with progress, then opens','Progress over the whole screen','A sheet over the folder: bar, how much, time left, Cancel; then it opens by itself','chg'],
['That copy failing','Stops, nothing half-copied left','A sheet naming it: Lost the connection (Try again) or Not enough room (Clear old copies), nothing left behind','chg'],
['A copy already kept (2 GB of them at most)','Opens at once','Same; its row has an “On card” chip, and X · More can remove the copy','chg'],
['A video or song on a server: plays from it, reads ahead 64 MB, “Buffering”, “Lost the connection” / “Can’t reach it” with Retry','Top screen','Same; the player’s state line says Buffering, the bar shows the read-ahead; rows say “Plays from the server”','chg'],
['Network: Find on the network, Add connection, Refresh','Network, X (and A / Y when empty)','Network’s empty state keys; X · More','same'],
['A connection’s Edit, Test, Delete','Network, Y or hold','X · More or hold','mov'],
['Web dashboard… shortcut','Network, X','Home › Send from phone · Quick settings · Settings','mov'],
['Playlists: Liked first, New playlist last','Playlists','Unchanged','same'],
['A playlist’s Play, Shuffle play, Rename…, Delete','Y or hold','X · More or hold','mov'],
['Inside a playlist: Play all, Add files…, Add folder…, Remove duplicates, Remove missing, Select','X','X · More (Select also on Y)','same'],
['An item’s Play from here, Move, Remove, Show in folder, Unlike','Y','X · More','mov'],
['Select mode: A ticks, SELECT all, X Remove, Y Move, drag the grip','Playlist','A ticks, SELECT all; Remove and Move in X · More and as keys on the select bar; grip unchanged','chg'],
['The player'],
['A play / pause · L / R file · L2 / R2 chapter · LEFT / RIGHT seek · L3 Like','Keys','Unchanged','same'],
['UP / DOWN: faster / slower','Player keys','Gone from the D-pad: UP and DOWN do nothing on the player. Speed is set in View › Speed (SELECT, or a tap on the Speed chip)','chg'],
['Stop','START › Playing › Stop','A key on the player: Like · Playlist · List · Stop. Also X · More › Stop, for the keys','chg'],
['View (Tracks, Display) by touch','A key, or the two track lines','The chips, each on its tab; no View key. SELECT by the keys, named in the hint bar','chg'],
['Back 5 s / forward 10 s by touch','(none: drag the bar)','Two keys beside Pause','new'],
['X: In order → Loop all → Loop one → Shuffle','Key in the second row','Shuffle switch + Repeat Off / All / One at the top of Y · List; state on the Repeat / Shuffle chip','chg'],
['Y: Up next','Page','Y · List, first tab','same'],
['SELECT (video): Tracks = Chapters, Subtitles, Audio, Picture','Page with tabs','SELECT · View = Subtitles, Audio, Picture, Speed; Chapters is Y · List’s second tab','chg'],
['SELECT (song): Display popup: Show, Style','Popup','SELECT · View, Display tab: the modes and the styles on one page','chg'],
['Sound: Equalizer and Effects','A page, from the line under the keys or Settings','SELECT · View, Equalizer and Effects tabs; the Sound chip; Settings › Music & sound','mov'],
['EQ keys: Y / X preset, SELECT on / off, A save, START options','Sound page','L2 / R2 preset, Y on / off, A save, X options','chg'],
['Picture size: Fit, Fill, Stretch','SELECT › Tracks › Picture','SELECT · View › Picture, a row of three; the Picture chip','same'],
['Picture looks: Sharpen, Soften, Scanlines, LCD grid, Retro; Strength; global, with Only this video as an option','(none)','SELECT · View › Picture; shown on the Picture chip, in a top-screen note when a video starts with one on, on the mini bar; default in Settings › Playback','new'],
['Brightness, Contrast, Saturation, Gamma for a video; Reset; All videos / Only this video','(none)','SELECT · View › Adjust; shown on the Picture chip (“Adjusted”, “+adj”)','new'],
['Show modes: Album art, Art + visualizer, Visualizer, None','Display popup; Settings › Music','View › Display, four icon tiles in a row: Album art only, Art + visualizer, Visualizer only, None; the Display chip says which in a short form (Art, Art + viz, Visualizer, None) and nothing else; Settings › Music & sound › Music display','chg'],
['Album art’s size on the top screen','Scaled to the full 480 height; in Art + visualizer the visualizer is laid over its bottom','Smaller: 240 units beside the title, artist and album, the pair centred; 224 with the visualizer, above a band of bars along the bottom; the bars are under the art, never over it. A change to today’s app','chg'],
['Title, artist and album on the top screen','A setting, Song info on top: off, or written over the picture','Always there, no setting. With art: the art left (240 / 224 units), the three lines to its right, the pair centred; over a visualizer alone, top left; None is the title alone','chg'],
['A song with no album art','Album art falls back to the title alone; Art + visualizer falls back to the visualizer alone','The Art placeholder (a record in its sleeve, in the theme’s colours) in the art’s spot in both art modes; the layout and the chosen mode stay','chg'],
['SELECT again in the popup: the next Show mode','Display popup','Unchanged: SELECT in View › Display steps Show; B closes','same'],
['Visualizer style (20)','Display popup, the Style row under Show; Settings › Music','View › Display, the grid of twenty icon tiles under the four modes (one page, as the popup was); Settings › Music & sound › Visualizer. Kept and changeable when the mode has no visualizer: “not shown now”','chg'],
['Song info on top (the setting)','Settings › Music','Removed: the title, artist and album are always shown','chg'],
['A song’s top screen: its ground','Black','The theme’s BG in Album art only, Art + visualizer and None, with the song’s text in the theme’s TEXT; in Art + visualizer the style is drawn straight over it (Peaks and the six simple styles, Mirror bars, Wave, Dots, Levels, Area and Radial, in the theme’s ACCENT and TEXT), with no black strip; a pixel-art style alone keeps its own picture','chg'],
['Visualizer frame rate (30 / 45 / 60)','Settings › Music','Settings › Music & sound','same'],
['Speed 0.5× to 2.5×','UP / DOWN, a key that shows it','View › Speed (SELECT, or the Speed chip); the chip shows it','mov'],
['Audio and subtitle lines (tap for Tracks)','Two lines under the keys','Subtitles and Audio chips','chg'],
['Track names in Tracks: one line each','SELECT › Tracks','Two-line rows: the language, then name · flags · format · where; cut at the end, a file’s name in the middle; the full name in the top-screen note','chg'],
['A track with no name, or no language','(as the file says)','The language’s name from its code; “Unknown language · Track 5”','new'],
['Track names in other scripts, and right to left','Drawn per run in a font that has them','The same, with a floor: native script only on the 17-unit line, the interface’s name everywhere smaller; layout never flips','chg'],
['Chapters of a long film; names that don’t fit; no names','Chapters tab','Y · List › Chapters: 46-unit rows, “17 of 42”, LEFT / RIGHT a page, names cut with “…”, “Chapter N” when unnamed; no thumbnails','chg'],
['Chapter line (tap for Chapters); marks on the bar','Above the bar','Unchanged, with L2 / R2 written on it','same'],
['What plays next','(open Up next)','The Next line','new'],
['A title too long for its place','Cut with “…” (a file name in its middle)','It moves along in its box, twice, then rests; a start it shares with its album or folder is left out. Settings › Power & screens › Scroll long titles','new'],
['Like and Add to playlist keys','First row','Second row: Like, Playlist','same'],
['Lock, Info, Screen off, Stop','Bottom row','X · More, first four tiles (Screen off also in Quick settings)','mov'],
['START › Playing: Now playing, Like, Add to playlist…, Play on, Show in folder, Info, Stop','START menu tab','Removed. Every item is in X · More or on the player’s face (Like, Playlist, Stop); Now playing is SELECT and the mini bar','mov'],
['The key for ⋯ (More) on the player','START (the Playing tab)','X, as on every screen. SELECT stays View','chg'],
['Hold A to play here; Waiting for a browser; Play on','Player','Unchanged; Play on is a row of X · More','same'],
['B / ‹: back, it keeps playing; Stop stops everywhere','Player','Unchanged','same'],
['Progress bar: tap to jump, drag to scrub','Player','Unchanged (taller: 14 units, 44-unit touch band)','same'],
['Photos, books, PDFs, comics'],
['Photo: LEFT / RIGHT / L / R, UP / DOWN zoom, A 200%, L3 rotate, SELECT Fit / Fill','Keys','Unchanged','same'],
['Photo keys: Prev, Next, Zoom ±, Rotate, Fit, Info, Menu','2 × 4 grid','Prev / Next large below; Zoom ±, Rotate, Fit beside; Info in X · More; Menu is START','chg'],
['Pan area, box or photo drag, double-tap, glide, keep zoom','Photo','Unchanged','same'],
['Text: UP / DOWN lines; LEFT / RIGHT, L / R, A a screen','Keys','Unchanged','same'],
['Text: jump a chapter','(Contents only)','L2 / R2','new'],
['Text: Y Contents','Key + button','Y · Contents (first tab)','same'],
['Text: X Bookmarks (add, go, delete)','Key + button','Y · Contents, Bookmarks tab; a one-tap bookmark key in the header; “Add here” in the sheet','mov'],
['Text: START Text options','Full page','Looks → SELECT · Text. Find, Encoding, Book mode → X · More','mov'],
['Text: SELECT Book mode','Key + button','SELECT · Text › View › Book mode (SELECT, A); also X · More','chg'],
['Text: L3 Find, R / L next / previous match','Key','Unchanged, plus a Find key','same'],
['Font, Text size, Line spacing, Margins, Colours, Hold, Page turn','Text options','SELECT · Text; defaults also in Settings › Reading','mov'],
['Brightness & filter (screens, Extra dim, Warm / Warmer / Dim)','Sun key; options row','“Light” key in every viewer; unchanged sheet','same'],
['PDF: LEFT / RIGHT, L / R, UP / DOWN zoom, A, Y Pages','Keys','Unchanged','same'],
['PDF: X Single page / Vertical scroll; SELECT Fullscreen','Keys + “Vertical scroll” button','SELECT · View: Single page, Vertical scroll, Fullscreen','chg'],
['PDF: START Options (View, Fullscreen, Brightness & filter, Page fit, Zoom, Pages)','Full page','SELECT · View; Page fit also a key; Pages on Y','mov'],
['PDF: jump far','Pages grid','Pages grid, and a page slider','new'],
['Comic: RIGHT / LEFT, L / R, L2 / R2, UP / DOWN, A, L3 turn, Y Pages','Keys','Unchanged','same'],
['Comic: X reading mode; SELECT Fullscreen; START Reading menu','Keys','All rows of SELECT · View (Fullscreen first)','chg'],
['Comic: slider, Prev / Next chapter, Fullscreen, Brightness keys','Reading page','Chapter, Pages, View, Light, Chapter','chg'],
['Manga'],
['Manga reader','Home › Extensions › tile','Rail › Manga, while the reader is on','mov'],
['Entering Manga while a video plays','The video stops at once; a 5 s notice','A sheet asks first (Stay / Stop video & open Manga); a red mark on Manga in the rail while a video plays','chg'],
['A video Manga stopped','Place kept; its Recent card','Home’s Continue card, marked “Stopped for Manga”','chg'],
['Playing a video outside Manga stops the manga engine','Silent','Unchanged, with one line on the top screen saying so','chg'],
['The engine loading on the way back into Manga (about 25 s)','A chip counting seconds up','A row under the tabs with a bar against the usual time; card series usable, source series dimmed until ready','chg'],
['Music and photos in Manga','Go on','Unchanged','same'],
['The engine is up','A chip “Manga engine ready” for two seconds','The loading row turns to “Online sources ready” and folds away; dimmed covers light up; you stay where you are; a series you were waiting on opens','chg'],
['The engine failed to start','A chip “Manga engine didn’t start” for four seconds','A row that stays, with the reason (memory / error) and Try again (Y); X › Go back to the stopped video; card series still work','chg'],
['Sources: Installed, Available, Repositories','X on Browse; LEFT / RIGHT step the lists','Same page; L2 / R2 step the lists','chg'],
['Repositories: last checked, A for Refresh / Remove (remove its sources too?), Add repository…, Y checks all','Sources › Repositories','Same; rows are two lines with address and state; a failed one says so and has Retry; the top screen shows the whole address','chg'],
['First-time notice: repositories are added by you','When the reader is first turned on','There, and once before the first repository is added','chg'],
['Typing a repository’s address','On-screen keyboard','Same keyboard with https://, .json, .pb keys; SELECT hands over to the phone','chg'],
['Manage extensions (On / Off, first-time sheet)','Extensions drawer, START','Settings › Extensions','mov'],
['Tabs Library, History, Browse, Downloads','L / R','L2 / R2, or tap','chg'],
['Manga settings','START on Manga pages; gear in Manage extensions','X · More › Manga settings; Settings › Manga','mov'],
['Library X: Remove missing · History X: Clear · Downloads X: Pause / Delete','X acts at once','X · More lists them (one press more)','chg'],
['Browse: X Sources, Y Add server; a server’s Edit / Delete','Keys','X · More: Sources, Add server, Edit, Delete; “Manage sources” and “Add” rows stay','chg'],
['A source: Popular / Latest / Search tabs, X Filters, Y Search','L / R, X, Y','L2 / R2 tabs; Filters and Search in X · More and as keys in the header','chg'],
['A series: Resume, Download (R2), Refresh (L2), Info, heart (Y), X read / unread','Buttons + keys','Buttons stay; Y selects chapters (to download or mark); the rest in X · More','chg'],
['Settings and system'],
['Settings','START › Settings tab, 8 tiles','Rail › Settings (or START › Settings), 12 tiles','mov'],
['Group to group: Y / X','Inside a group','L2 / R2','chg'],
['Subtitles; Music; Photos; Remember; Files; Controls & screen; Updates; Web dashboard','Groups','Same rows. Remember → Playback, Music → Music & sound, Files → Files & history, Controls & screen → Power & screens, with Max volume and Button hotkeys moved to Playback','same'],
['A setting’s explanation; the subtitle preview','Bottom screen','Top screen','mov'],
['Text and PDF viewer defaults','(only inside a book)','Settings › Reading','new'],
['The app’s colours','One look: black, cream and yellow','Settings › Power & screens › Theme, seventeen: Yellow (the default, today’s look), Dark, Midnight blue, Dark purple, Dark red, Pink, Coffee, Mint, Retro green, Sunset, Candy, 16-bit, 16-bit Lavender, 32-bit, Sakura, Light, High contrast. Both screens and the Web dashboard; cards over a picture stay dark','new'],
['A book’s page colours: Dark, Light, Sepia, Black','Text options','The same four, and Match theme first (the default): the page follows the app’s theme until you choose one','chg'],
['The bottom screen while reading','Stays on','Stays on; may dim to its lowest (Settings › Reading › Dim the controls while reading)','new'],
['About','START › About tab','Settings › About; START › About','mov'],
['Web dashboard page: QR, address, PIN, New PIN; the globe','Settings, Quick settings, the globe','Unchanged','same'],
['Quick settings (F): Volume, Brightness, Same for both, Web dashboard, Incognito, Quit','Sheet','Unchanged contents, as tiles; + Screen off now, battery percent','chg'],
['Quit','Quick settings, B on Home, F + START','Those, and START › Menu','same'],
['START menu','Tabs: Playing, Settings, About','Menu: places, Now playing, Quick settings, Quit','chg'],
['The speed key turns yellow off 1×','Player','The rule for every state: value in yellow with a dot on its control; a pill on the mini bar','chg'],
['Incognito’s hat-and-glasses mark beside the clock','Header','A yellow badge in the header, every screen; Recent’s heading says it is not being added to','chg'],

['Extra dim / filter: Brightness key lit with “Warm · −30%”; a dot on the text viewer’s sun','Readers','The Light key in every viewer, lit, with the same words; on the page-number line in Fullscreen and Book mode','chg'],
['EQ / effect on; Shuffle; Repeat','Sound line; the X key','Sound, Shuffle and Repeat chips in yellow; pills on the mini bar','chg'],
['Volume card: 100% mark, yellow past it, red past 120%','Top screen, while it shows','Same marks; cream up to 100% so yellow means boosted; one line says what is happening','chg'],
['The first-time warning past 120%','A warning, then it carries on','The press stops at 120% and asks once (Stay at 120% / Go louder); Volume + again goes on','chg'],
['Volume slider','Quick settings','Quick settings only (none on the player): cream to a “100” mark, yellow past it','chg'],
['Playing in a browser: “Playing in Chrome on Mac”, bar on the browser’s time, A plays / pauses it, L / R its list','Player, status line, top card','Same; the state line reads “In Chrome on Mac” in yellow and a “Chrome” badge sits in the header on every screen','chg'],
['Hold A (0.6 s) to play here, the key filling','Play key; top card “Hold A to play here”','Same; “Hold: play it here” is written on the key, and the top screen fills with it','chg'],
['Equalizer, effects, visualizer while a browser plays','Not offered','Sound, Sound FX and Display chips, and their tabs in View, dimmed with “Handheld only”; on a video, Picture and Adjust too','chg'],
['Browser volume on the volume keys','Card says Browser volume','Unchanged','same'],
['Mini bar “Playing in Chrome on Mac” (tap mark: pause; hold: take over); “In browser” row chip; “Go to the player to play it here”','Lists','Unchanged; the mini bar’s text is its yellow state pill','same'],
['“Your browser went · paused here”; “Waiting for Chrome on Mac” (B plays here)','Top card','Unchanged; the “went” card stays until the next key','chg'],
['Play on: Handheld and each open page','START › Playing › Play on','X · More › Play on…; each row says what that place has or needs','mov'],
['Alerts (about forty messages)','A centred box with OK','The sheet: title, one line, one key; red only when something failed','chg'],
['Playback failure: Can’t play, skipped; Buffering; Lost the connection, Can’t reach it (A retries)','Top screen','Same, and on the player’s state line','chg'],
['Empty folder; gone playlist items; a card taken out','Messages and [Gone]','Empty state with Go up; a red Gone chip with which kind; a dimmed row and a notice','chg'],
['Waits: Reading folder…, Finding…, Testing…','Busy dialogs','One busy sheet with Cancel on B','chg'],
['Delete: press the row twice','Menu','A sheet naming the file, Cancel focused (lighter removals keep press-twice)','chg'],
['Quit confirm; update popup; touch lock','Dialogs; a small lock','Sheets; a full-panel lock to hold','chg'],
['No Wi-Fi','A header icon; messages on use','A red “Off” badge in the header; each place that needs the network says so','chg'],
['Screen off (music) / (video) timers; Screen off by hand','Settings › Controls & screen; the player’s key','Settings › Power & screens; X · More and Quick settings; a five-second dimmed warning before an automatic one','chg'],
['With the bottom screen off: playback keys work; a touch or other key wakes it and does nothing else','Player','Unchanged','same'],
['Lid: pauses, then the handheld sleeps; stays paused on waking','BananaOS + player','Unchanged by default; the top screen says “Paused: the handheld slept”. Keep playing as today; with the lid shut: volume hold = next / previous, Power = play / pause, the shoulder keys a setting, the rest ignored','chg'],
['Supporting the project','(nowhere)','Settings › About, at its foot: one line, a small QR code and ko-fi.com/tonysong. The Web dashboard: one link at the foot of Home. Nowhere else','new'],
['Incognito: nothing about the lid or the dashboard','Quick settings','The lid or sleep in incognito: Nothing / Close all (default) / Quit. The Web dashboard is off while incognito is on','new'],
['The handheld sleeping during a copy, download or update','It stops with the handheld','Kept awake until done (needs BananaOS), with an “Awake” badge and the reason in Quick settings','new'],
['A song’s top screen','Always on while it plays','Optional Top screen off (music): Never / 5 / 15 min','new'],
['The dashboard’s QR page turns the bottom screen off after 90 s','Web dashboard page','Unchanged','same'],
['A browser signing in: a note on the top screen; the globe dim / yellow','Top screen, header','The note with who, address and time, 6 s; shown over a video only for a sign-in with the PIN; the globe a yellow badge with the count','chg'],
['A browser signing out; New PIN signing everyone out','(the globe dims)','A 3-second note, never over a video','new'],
['Wrong PINs: a device waits 60 s after five; the PIN changes after thirty and the handheld says so','Dashboard; a message on the handheld for the change','Both are notes in red on the top screen, shown over a video, and lines on the dashboard page','chg'],
['Book mode for text: held sideways, a page a screen, UP / DOWN (as held), L / R, A and swipes turn; a tap in the middle opens Text options; B or SELECT back to scroll','Text viewer','Same turning. A tap in the middle (or START) brings an upright Book bar: progress, Contents, Text, Light, Find, Back to scrolling; sheets open upright at the page’s width','chg'],
['Book for comics: a page a screen, the spread follows the reading mode, UP / DOWN turn the spread, L / R one page, a tap shows page numbers','Manga reader','Same; page numbers and notes upright at each page’s foot; the Book bar as for text. Reading mode moves from X to View','chg'],
['Menus and notices in Book','Keep the screen’s usual orientation (sideways to the reader)','Upright, as the pages are: the same sheet at 480 wide','chg'],
['Fullscreen for comics: one tall view over both screens, the hinge gap skipped; a tap shows the controls bar with the mini bar','Manga reader, PDF vertical scroll','Same; the bar is the reader’s own row; “Fullscreen” is a row of View (SELECT)','chg'],
['Update prompt; level cards; Loading; Lid; Screen off timers; Lock (hold to unlock); F + L2 screenshot','System','Unchanged','same'],
['On-screen keyboard; dialogs (press twice to confirm)','Overlays','Unchanged','same'],
];
const TAGN={new:'NEW',chg:'CHANGED',mov:'MOVED',same:'SAME'};
document.getElementById('maptable').innerHTML='<tr><th>Feature</th><th>Today</th><th>In the proposal</th><th></th></tr>'+MAP.map(r=>r.length==1?`<tr class="grp"><td colspan="4">${r[0]}</td></tr>`:`<tr><td>${r[0]}</td><td>${r[1]}</td><td>${r[2]}</td><td><span class="tag ${r[3]}">${TAGN[r[3]]}</span></td></tr>`).join('');

// ---------- components ----------
{const TG={reuse:['same','REUSE'],variant:['chg','VARIANT'],new:['new','NEW']};let g='';
 document.getElementById('complist').innerHTML=COMPS.map((c,i)=>{const e=c.ex();const hd=c.g!=g?`<div class="eyebrow" style="margin-top:22px">${g=c.g}</div>`:'';
  return hd+`<div class="comp" id="c-${i}"><div class="panel" style="aspect-ratio:640/${e.h}"><div class="s${c.g=='Top screen'?' top':''}" style="height:${e.h}px">${e.inner}</div></div><div><h3>${c.n} <span class="tag ${TG[c.st][0]}">${TG[c.st][1]}</span></h3><dl><dt>Used</dt><dd>${c.use}</dd><dt>Anatomy</dt><dd>${c.an}</dd><dt>Inputs</dt><dd>${c.pr}</dd><dt>Keys, touch</dt><dd>${c.in}</dd><dt>Code</dt><dd>${c.maps}</dd></dl></div></div>`}).join('');
 const n=k=>COMPS.filter(c=>c.st==k).length;
 document.getElementById('compcount').textContent=`${COMPS.length} components: ${n('reuse')} reuse, ${n('variant')} variant, ${n('new')} new.`;
 document.getElementById('themetable').innerHTML='<tr><th>Theme</th><th>BG</th><th>SURFACE</th><th>ACCENT</th><th>TEXT</th><th>What it is</th></tr>'+THEMES.map(t=>`<tr><td class="k">${t[1]}</td>${[2,3,4,5].map(i=>`<td><span style="display:inline-block;width:14px;height:14px;border-radius:4px;vertical-align:-2px;margin-right:6px;border:1px solid var(--line);background:#${t[i]}"></span><code>#${t[i]}</code></td>`).join('')}<td>${t[7]}</td></tr>`).join('');
 document.getElementById('forktable').innerHTML='<tr><th>What</th><th>Was drawn as</th><th>Every screen now uses</th><th>Status</th></tr>'+FORKS.map(r=>`<tr><td>${r[0]}</td><td>${r[1]}</td><td>${r[2]}</td><td>${r[3]}</td></tr>`).join('');}
// ---------- the theme switch (page chrome): data-bp on <html>, which the stylesheet's theme blocks read ----------
function setTheme(id){if(!THEMES.some(t=>t[0]==id))id='yellow';document.documentElement.dataset.bp=id;try{localStorage.setItem('bptheme',id)}catch(e){}
 document.querySelectorAll('#themebar button').forEach(b=>b.classList.toggle('on',b.dataset.theme==id))}
function stepTheme(){const i=THEMES.findIndex(t=>t[0]==document.documentElement.dataset.bp);setTheme(THEMES[(i+1)%THEMES.length][0])}
{const bar=document.getElementById('themebar');
 bar.insertAdjacentHTML('beforeend',THEMES.map(t=>`<button data-theme="${t[0]}" title="${t[1]}"><i style="background:linear-gradient(90deg,#${t[2]} 50%,#${t[4]} 50%)"></i><b>${t[1]}</b></button>`).join(''));
 bar.addEventListener('click',e=>{const b=e.target.closest('button[data-theme]');if(b)setTheme(b.dataset.theme)});
 let t=(location.search.match(/[?&]theme=([a-z0-9]+)/)||[])[1];if(!t)try{t=localStorage.getItem('bptheme')}catch(e){}
 setTheme(t||'dark')}
// ---------- rendering ----------
const BY={};SCREENS.forEach(s=>BY[s.id]=s);
const devSide=s=>`<div class="lbl"><span>Left page: ${s.flip?'bottom screen · touch':'top screen'}</span><span>held sideways · each page 480 × 640</span><span>Right page: ${s.flip?'top screen':'bottom screen · touch'}</span></div><div class="pair"><div class="panel pt"><div class="s${topCls(s.top())}">${s.top()}</div></div><div class="vhinge"></div><div class="panel pt"><div class="s">${s.bottom()}</div></div></div><div class="lbl b"><span>Hold: top screen ${s.flip?'right':'left'}</span><span>${s.name}</span></div>`;
const dev=s=>s.side?devSide(s):`<div class="lbl"><span>Top screen</span><span>640 × 480</span></div><div class="panel"><div class="s${topCls(s.top())}">${s.top()}</div></div><div class="hinge"></div><div class="panel"><div class="s">${s.bottom()}</div></div><div class="lbl b"><span>Bottom screen · touch</span><span>${s.name}</span></div>`;
const kb=k=>k.split(' or ').map(p=>p.split('/').map(x=>`<kbd>${x}</kbd>`).join(' ')).join(' or ');
const gal=document.getElementById('gallery'),nav=document.getElementById('navscreens');
let lastG='';
SCREENS.forEach(s=>{
	if(s.group!=lastG){lastG=s.group;nav.insertAdjacentHTML('beforeend',`<h4>${s.group}</h4>`)}
	nav.insertAdjacentHTML('beforeend',`<a href="#s-${s.id}">${s.name}${s.isNew?'<small>NEW</small>':''}</a>`);
	const goes=[...new Set(s.keys.map(k=>k[2]).concat(s.also.map(a=>a[1])).filter(g=>g&&g!='<'&&BY[g]))];
	gal.insertAdjacentHTML('beforeend',`<article class="screen ${s.side?'wide':''}" id="s-${s.id}"><div class="device ${s.side?'side':''}" data-gal="${s.id}">${dev(s)}</div><div class="notes"><div class="eyebrow">${s.group}${s.isNew?' · <span class="tag new">NEW</span>':''}</div><h3>${s.name}</h3><p class="why">${s.why}</p>
	<ul>${s.notes.map(n=>`<li><span class="tag ${n[0]}">${TAGN[n[0]]}</span> ${n[1]}</li>`).join('')}</ul>${s.extra?s.extra():''}
	<div class="keys">${s.keys.map(k=>`${`<span>${kb(k[0])}</span>`}<span>${k[1]}</span>`).join('')}${s.also.map(a=>`<span><kbd>then</kbd></span><span>${a[0]}</span>`).join('')}</div>
	<p class="goes"><a href="#proto" data-jump="${s.id}">Open in the walk-through</a>${goes.length?' · leads to '+goes.map(g=>`<a href="#s-${g}">${BY[g].name}</a>`).join(', '):''}</p></div></article>`);
});

// ---------- the walk-through ----------
const P_={cur:'home',hist:[]};
const pd=document.getElementById('protoDev'),pad=document.getElementById('protoPad');
function show(id,push=true){
	if(id=='theme:step'){stepTheme();return}
	if(id=='<'){id=P_.hist.pop()||'home';push=false}
	if(!BY[id])return;
	if(push&&id!=P_.cur)P_.hist.push(P_.cur);
	if(P_.hist.length>40)P_.hist.shift();
	P_.cur=id;const s=BY[id];
	pd.innerHTML=dev(s);pd.classList.toggle('side',!!s.side);document.getElementById('protoBox').classList.toggle('wide',!!s.side);fit();
	document.getElementById('protoName').textContent=s.name;
	document.getElementById('protoGroup').textContent=s.group;
	document.getElementById('protoWhy').textContent=s.why;
	pad.innerHTML=s.keys.map((k,i)=>`<button data-k="${i}" ${k[2]?'':'disabled'}><kbd>${k[0]}</kbd><span>${k[1]}</span></button>`).join('')+s.also.map(a=>`<button data-jump="${a[1]}"><kbd>then</kbd><span>${a[0]}</span></button>`).join('');
}
pad.addEventListener('click',e=>{const b=e.target.closest('button[data-k]');if(b){const k=BY[P_.cur].keys[+b.dataset.k];if(k[2])show(k[2])}});
document.addEventListener('click',e=>{
	const j=e.target.closest('[data-jump]');if(j){show(j.dataset.jump);if(j.tagName!='A'){e.preventDefault()}return}
	const g=e.target.closest('[data-go]');if(!g||!g.dataset.go)return;
	if(g.closest('#protoDev'))show(g.dataset.go);
	else{const t=document.getElementById('s-'+g.dataset.go);if(t)t.scrollIntoView({behavior:'smooth',block:'start'})}
});
document.getElementById('protoBack').addEventListener('click',()=>show('<'));
const hot=document.getElementById('hotToggle');const setHot=()=>pd.classList.toggle('hot',hot.checked);hot.addEventListener('change',setHot);setHot();
const KEYMAP={a:'A',b:'B',x:'X',y:'Y',l:'L',r:'R',s:'SELECT',enter:'START',f:'F'};
document.addEventListener('keydown',e=>{
	if(e.metaKey||e.ctrlKey||e.altKey||/input|textarea|select/i.test(e.target.tagName))return;
	const box=document.getElementById('protoBox').getBoundingClientRect();if(box.bottom<0||box.top>innerHeight)return;
	const want=KEYMAP[e.key.toLowerCase()];if(!want)return;
	const k=BY[P_.cur].keys.find(k=>k[2]&&k[0].split(/\/| or /).includes(want));if(k){e.preventDefault();show(k[2])}
});

// ---------- scale each panel's 640x480 drawing to its box ----------
function fit(){document.querySelectorAll('.panel').forEach(p=>p.style.setProperty('--k',p.clientWidth/(p.classList.contains('pt')?480:640)))}
new ResizeObserver(fit).observe(document.body);addEventListener('resize',fit);
show('home',false);fit();
if(document.fonts&&document.fonts.ready)document.fonts.ready.then(fit);

// real physical size: the panel's width in mm (4.0 in and 4.3 in at 4:3), with a calibration factor
{const bar=document.getElementById('physbar'),cal=document.getElementById('cal');
 let c=1;try{c=+localStorage.getItem('bpcal')||1}catch(e){}
 cal.value=c;document.body.style.setProperty('--cal',c);
 cal.addEventListener('input',()=>{document.body.style.setProperty('--cal',cal.value);try{localStorage.setItem('bpcal',cal.value)}catch(e){};fit()});
 bar.addEventListener('click',e=>{const b=e.target.closest('button[data-phys]');if(!b)return;bar.querySelectorAll('button').forEach(x=>x.classList.toggle('on',x==b));
  document.body.classList.toggle('phys',!!b.dataset.phys);document.body.style.setProperty('--pmm',b.dataset.phys||0);fit()});
 const q=(location.search.match(/phys=([\d.]+)/)||[])[1];if(q){const b=[...bar.querySelectorAll('button')].find(x=>x.dataset.phys==q);if(b)b.click()}}
// the side nav follows the scroll
const links=[...document.querySelectorAll('nav.side a[href^="#"]')];
const io=new IntersectionObserver(es=>es.forEach(e=>{if(e.isIntersecting){links.forEach(a=>a.classList.toggle('on',a.getAttribute('href')=='#'+e.target.id))}}),{rootMargin:'-8% 0px -80% 0px'});
document.querySelectorAll('section[id],header[id],article[id]').forEach(el=>io.observe(el));

// ?sheet: only the devices, in a grid (for looking over every screen at once)
if(/[?&]sheet/.test(location.search)){const n=+((location.search.match(/cols=(\d+)/)||[])[1]||3);document.body.innerHTML=`<div style="display:grid;grid-template-columns:repeat(${n},1fr);gap:14px;padding:14px">${SCREENS.map(s=>`<div class="device ${s.side?'side':''}" style="max-width:none">${dev(s)}</div>`).join('')}</div>`;fit()}
// ?comps: only the components' examples (for looking them over)
if(/[?&]comps/.test(location.search)){const l=document.getElementById('complist');document.body.innerHTML=`<div style="display:grid;grid-template-columns:repeat(3,1fr);gap:10px;padding:10px;align-items:start">${[...l.querySelectorAll('.comp')].map(c=>`<div><div style="font:600 12px Inter;color:#aaa;margin:2px">${c.querySelector('h3').textContent}</div>${c.querySelector('.panel').outerHTML}</div>`).join('')}</div>`;fit()}
// ?only=id: one screen's card alone (for looking it over)
{const m=location.search.match(/[?&]only=([\w-]+)/);if(m){const c=document.getElementById('s-'+m[1]);if(c){document.body.innerHTML='<main style="max-width:1240px;padding:20px">'+c.outerHTML+'</main>';fit()}}}

// ---------- the Title marquee, moving (components.md › Title marquee) ----------
// Measured here as the app will: it runs only if the text is wider than its box. Rest 1.5 s, move (48 units/s for type of 24 and more, 24 under),
// rest 1.5 s, fade back. The app stops after two passes; the preview repeats so it can be seen.
function mqRun(){document.querySelectorAll('.mq:not([data-mq])').forEach(b=>{const t=b.firstElementChild,w=b.clientWidth;if(!w)return;b.dataset.mq=1;
	const d=t.offsetWidth-w;if(d<=0)return;b.classList.add('mqo');
	const l=b.querySelector('.mql'),r=b.querySelector('.mqr'),sp=parseFloat(getComputedStyle(t).fontSize)>=24?48:24,m=d/sp*1000,T=1500+m+1500+300,a=1500/T,e=(1500+m)/T,f=(3000+m)/T,g=(3150+m)/T,o={duration:T,iterations:Infinity};
	const X=x=>`translateX(${-x}px)`;
	t.animate([{transform:X(0),opacity:1,offset:0},{transform:X(0),offset:a},{transform:X(d),offset:e},{transform:X(d),opacity:1,offset:f},{transform:X(d),opacity:0,offset:g},{transform:X(0),opacity:0,offset:g},{transform:X(0),opacity:1,offset:1}],o);
	if(l)l.animate([{opacity:0,offset:0},{opacity:0,offset:a},{opacity:1,offset:Math.min(a+200/T,e)},{opacity:1,offset:g},{opacity:0,offset:g},{opacity:0,offset:1}],o);
	if(r)r.animate([{opacity:1,offset:0},{opacity:1,offset:Math.max(e-200/T,a)},{opacity:0,offset:e},{opacity:0,offset:g},{opacity:1,offset:1}],o)})}
{let q=0;const later=()=>{if(!q)q=setTimeout(()=>{q=0;mqRun()},60)};new MutationObserver(later).observe(document.body,{childList:true,subtree:true});addEventListener('click',later);addEventListener('resize',later);(document.fonts&&document.fonts.ready||Promise.resolve()).then(later)}
