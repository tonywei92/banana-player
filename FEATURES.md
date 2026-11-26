# Banana Player: all the features

Everything Banana Player does, button by button. For installing it, see the [README](README.md).

- [What it plays](#what-it-plays)
- [Finding something to play](#finding-something-to-play)
- [Playlists](#playlists)
- [Controls](#controls): [while playing](#while-playing), [photos](#looking-at-a-photo), [the file list](#in-the-file-list), [Settings from START](#settings-from-start)
- [Photos](#photos)
- [Picture size](#picture-size)
- [Music](#music)
- [Sound: equalizer and effects](#sound-equalizer-and-effects)
- [Chapters](#chapters)
- [Settings](#settings)
- [Quick settings](#quick-settings)
- [Search](#search)
- [Web dashboard](#web-dashboard)
- [Extensions](#extensions)
- [Updates](#updates)
- [More](#more)

## What it plays

| | |
|---|---|
| **Video** | MP4, MKV, WebM, AVI, MOV, TS, M4V, MPG, WMV, FLV and more: H.264, HEVC, VP9, AV1, MPEG-2/4… |
| **Music** | MP3, FLAC, OGG, WAV, M4A, M4B (audiobooks), AAC, Opus, WMA, AIFF, APE and more |
| **Photos** | JPG, PNG, WebP, GIF (animated too), BMP, TIFF, AVIF |
| **Documents** | TXT (any size), EPUB, PDF, opened from Files ([Documents](#documents)) |
| **Comics** | CBZ, CBR and PDF in the Manga reader ([Extensions](#extensions)); EPUB comics in Files |
| **Subtitles** | Inside the video (SRT, ASS/SSA, PGS, VobSub, VTT), or files next to it (`movie.srt`, `movie.en.ass`) |
| **Languages** | Subtitles, titles and file names in Latin scripts (Vietnamese too), Cyrillic, Greek, Chinese, Japanese, Korean, Thai, Arabic, Hebrew and Hindi, on both panels (the top panel's Preview pane and cards too). Text with kana takes the Japanese forms of its kanji; other Han text the Simplified Chinese ones |

Videos are decoded by the handheld's CPU. The various 1080p videos tested play well; very high bitrates, and especially
HEVC (x265), may drop frames. **Info** shows how it's keeping up.

## Finding something to play

Open Banana Player and the bottom screen shows **Home**:

- **Continue**: the last thing you played, as a card with its picture and title. Left in the middle, it says where
  (`6:12 / 9:56 · Videos`), with a bar and **Resume**; played through (a song, a finished film), it is **Last played**
  with **Play**. What plays now says **Now playing**, with a **NOW** mark on its
  picture, the time and its state pills after it, and **Pause** where **Resume** was (**Play** when paused); **A** or
  a tap on the card goes back to the player, and its key pauses and plays it without leaving Home (as **A** does on
  the player). Held, the key takes it over from a browser that plays it. A
  video the Manga reader stopped says **Stopped for Manga** until it plays again or something else is played. With
  everything played through there is no Continue card and **Recent** moves to the top. The pictures on the Continue and
  The card follows Recent's rules: when its file is gone or its card is out it shows dimmed with the reason and opening it
  opens its More (Remove from Recent first); X, a hold or a Y does the same, and once removed it is gone for good (the
  card falls back to the next Recent item, or the empty state).
  Recent cards are square (1:1), as Music's album art, and each Recent item is laid as an album tile is (the square
  picture with its title and second line under it, the albums grid's sizes). A video's picture is its list thumbnail
  (the middle of it), a book's its cover, once read; until then, and for a photo or a network file, the kind's mark.
  A song's picture is its real
  album art: the picture in the file, else `cover.*`, `folder.*` or `front.*` beside it (its title is the title tag
  when it has one, not the file's name). A song without any keeps the drawn record. The card is Home's one place for
  what plays. Home's Recent and shortcuts stay where they are either way.
- **Recent**, under it: up to four cards, newest first, with the kind's mark, the title, what it was
  (`Played · 3:34`, `7% read`, `6:12 / 9:56`) and a bar; a liked file has a yellow heart, a server's a network mark,
  and a song's card shows its album art when it has one.
  The kind chips beside **Recent** are **All**, **Videos**, **Music** and **Books** with how many: tap one (the
  filters' keys come with the rail) or step them with **L/R**. The chevron after the
  cards, and **(All recent)**, open every card, with the same chips. All recent's rows are grouped under **Today**,
  **Yesterday** and **Earlier** (an entry kept from before the player recorded times sits under Earlier). Recent keeps
  the last 100 of each kind, so
  reading doesn't push your videos out, nor music your books. While Home or All recent shows, its order stays put (the
  next song starting doesn't move the cards); what you played comes to the front the next time you open it.
- **A song plays in its list again**: a song of Recent remembers the list it played from (a playlist, by its name; an
  album, an artist, a Music folder, **Play random** with what **Autoplay** added; a folder of Files). Opening it
  plays it in that list, in the same order (Autoplay applies only if it was a Music list): the songs Autoplay
  had added come back with it, under their heading in Up next, and Autoplay (as it is set now) goes on from its
  last song. A song of the library with no list kept (played before this) plays in its Music folder's list.
  album, an artist, a Music folder, **Play random** with what **Keep playing** added; a folder of Files). Opening it
  plays it in that list, in the same order (Keep playing applies only if it was a Music list): the songs Keep playing
  had added come back with it, under their heading in Up next, and Keep playing (as it is set now) goes on from its
  last song (from the web dashboard's Recent too, in the browser or on the handheld). A song of the library with no list kept (played before this) plays in its Music folder's list.
  **Stop** and **B** go back to the page the player was opened from (Home, All recent), never to a section you
  didn't come from. A list gone since (the playlist deleted or renamed, the album or folder gone) plays the song
  alone and says why. Videos and books open as before.
- **Missing files**: a file deleted or moved away, or on a card that's out, stays in Recent dimmed, its note saying why
  (**Not on this handheld any more**, **Card 2 is out**); it's never taken out unasked. Opening it opens its sheet with
  the reason and **Remove from Recent**; the card back in, its files are listed as before. **Clear missing** (Home's
  More column, when any are gone) removes the deleted ones, not those of a card that's only out.
- Three shortcuts: **Shuffle Liked** (**Browse Files** until something is liked), **Send from phone** (the Web
  dashboard's page, to scan its code) and **All recent**.
- **A** resumes or plays (on **Now playing**: **A** goes to the player), **X** (or a hold on a card, the Now playing
  card included) opens the card's **More** sheet: **Resume** / **Go to it**, **Play from the
  start**, **Show in folder**, **Add to playlist…**, **Like** / **Unlike** (L3), **Info** and **Remove from Recent**;
  the right column has Home's own **All recent**, **Browse Files**, **Show hidden folders**, **Clear recent…** (press
  twice), **Settings** and **Quick settings**. With no card under the cursor, **X** opens that column alone.
  **Y** opens the same card sheet. Tap a card to play it, tap its key to resume, tap a chip to filter it.
- With nothing played yet Home is an **Empty state**: **Nothing played yet**, what goes here, and **Browse Files** (A)
  and **Web dashboard** (Y); no Continue, Recent or shortcut row (each would be empty). With the play history off it
  says so instead.

Move with the D-pad: **UP/DOWN** between Continue, the cards and the shortcuts, **LEFT/RIGHT** along them. Home is a
section's home: **B** asks **Quit Banana Player?** (Cancel is focused; **B** or a tap outside cancels; also **Quit** in Quick settings, **F**, or **F + START**).
**START** opens **Settings** (there is no START menu any more). Its places are the rail's cells, **F** is Quick
settings, **L3** is Search, and About is Settings' last group.

Every section now shares one frame: a **rail** down the left with a **Search** key pinned at its top, **Home**,
**Files**, **Videos**, **Music**, **Books**, **Extensions**, and at the foot the **Now playing key** over
**Settings** (no START cap: START is Settings from every screen).
**Network** is a tab of **Files**: the places and the connections' list each have a strip, **Files** | **Network**, stepped with **L / R** or a tap (the folders under them have none).

**The on-screen keyboard** (a connection's fields, a name, a repository address, Find) is the same keyboard Search uses, over the whole panel. A **number row** (1 to 0) is always the top row of keys, whatever else shows, over the three rows of letters. Its bottom row is words, each with its button's cap: **Shift** (**Y**), **123** (**L3**), **#+=**, **Space** (**X**), **Clear** (**L2**), **Del** (**B**) and **Done** (**START**). Shift is a toggle: press it and it stays lit, the letters shown and typed as capitals, until you press it again; **123** and **#+=** (the common marks; the braces and rarer marks) stay lit the same way, and pressing the lit one brings the letters back (**L3** or **SELECT** steps letters, 123, #+= round). **Clear** empties the field. **A** types, **B** deletes (on an empty field it closes, keeping what was there), **L / R** move the caret; a tap does the same on the keys. A password shows stars; a port takes digits only. Search's keyboard is this one less **#+=**.
**L2/R2** step the sections and wrap (they pass Search and the Now playing key by), or tap one; the D-pad stays in the page. The cells are
52 units tall, 45 with seven or more so all show at once; when they still don't fit, a finger drag scrolls them
(Search, the Now playing key and Settings stay put). Arriving at a section starts and stops nothing, and no cell
carries a warning: **Extensions** is one section for every extension that is on, and the Manga reader is a row of its
page. The header carries the back key, the section's title and where you are, the status marks and **X** (More); its
status cluster is one touch target for Quick settings. The
Search key is in the rail. **L3** and that key open **Search** (a tool, not a section: no rail cell lights on it and
**B** goes back where you were), and its key is lit while the page shows.
The hint bar along the foot says what the buttons do there.

Inside a section the list opens where you left it (its top if that folder or playlist is gone); **B** steps back one
level (up a folder, out of an album or an artist) until the section's home. A **section's home** (Home, Files' places
and Network, Videos' and Music's views, Books, the Extensions page, Settings' tiles) has no back key in its header,
and **B** there asks **Quit Banana Player?** (it never jumps to Home). The rail works as a tab bar: a tap on another section goes back
to where you were in it; a tap on the section you are in goes to its home at the top (Music with an album open: the
view you were on, from its first row); on its home already, the list glides to its top. **L2/R2** only step the
sections. **Playlists** is no cell of the rail: it is Music's third view
(see [The Music section](#the-music-section)).

Folders come first, then files in natural order (episode 2 before episode 10). Each row has an icon (folder, video,
song, photo), and each file shows its size. Songs and videos on the card show their length too (`3:34 · 3.5 MB`; a
video you stopped part-way, `at 12:03 / 1:45:00 · 1.2 GB`), filled in a moment after the folder opens, the rows on
screen first. A file whose format keeps no length in its header (MPEG-TS `.ts`/`.m2ts`/`.mts`, `.mpg`/`.mpeg`/`.vob`,
raw `.aac`) or that can't be read (empty, cut short, broken) shows only its size; so do network files. Lengths are kept
in the player's state folder, so a folder you've opened before shows them at once on the next launch. Anything you
stopped part-way through shows where you left off. The line
under the header shows where you are (`share / Movies`), and the bottom line shows what each button does there: the
button's name in yellow, then what it does. See [Playlists](#playlists).

Everything in a folder plays as one playlist: **L/R** go to the previous or next file (and play it, even when it was
paused), and the next one starts when
the current one ends. **Previous** on a **song**: 5 seconds or more in, it goes back to its start; before that, to the
file before. **Previous** on a **video** always goes to the video before it, however far in it is; on the first video
of the list it does nothing (the top screen says **This is the first one**) unless Repeat All is on, which goes round to
the last one. On the first song in its first 5 seconds it does nothing too (the same note), so the Prev button is never
greyed out. The web dashboard's Previous (both targets, and
a headset's or the lock screen's) follows the same rule. Photos simply step back. Folders can mix videos and music. Photos are a list of their own: from a photo you step through
the folder's photos, and from a video or a song through its videos and songs. When you stop something, you're back in
its folder on that file.

## The Music section

Songs by what they are, not where they are. The **Music** cell opens four views of the cards' index, stepped with
**L/R** or tapped: **Albums**, **Artists**, **Playlists** (the same playlists as ever, drawn under Music; Liked
first) and **Folders** (the same songs by where their files lie). The line under the title says where the list comes from and how much (`From Music on both cards · 148 albums ·
1,912 songs`; `on this card` when one card has none). Albums and Artists fill by themselves from the folders named
**Music** or **Musics** (any capitals) at the top of each card, plus BananaOS's own `/data/roms/music`; every such
folder is read, however deep inside it everything counts. While a card is first read a notice row names the card
and counts its folders under a bar that follows them, and the line under the title counts the songs read so far. A
song anywhere else is not listed here: it stays in Files,
plays the same, and Search finds it. There is no list of all songs.

- **Albums**: a grid of covers, five across. The art is the album's own (the picture in the file, else `cover.*` /
  `folder.*` beside it), the drawn record while it decodes or for good when there is none; under it the album's name
  and its artist. An album that plays carries a yellow play mark on its cover, the one paused a pause mark. **A**
  (or a tap) opens the album.
- **Artists**: a row each (`3 albums · 34 songs`), the unknown artist last. **A** opens the artist.
- **Playlists**: the list of playlists is Music's third view: the same chips over it (a tap on **Albums** or
  **Artists** goes there), then a 46-tall row each (Liked first, **New playlist** last, a **Playing** mark on the
  one that plays). A playlist's own page, its select and move modes and the Add to... picker are the browser's, as
  they were, with the Music cell kept lit (**L** steps back to Artists, **R** goes on to Folders).
- **Folders**: the Music folders as folders, for music kept in order by hand. With a Music folder on each card the
  view opens on the two of them (each named with its card); with one it opens inside it. A folder's row says how many
  songs are under it, however deep; under the folders come the songs lying in the folder itself, by file name, with
  their artist, length, heart and a **Now** pill on the one that plays. **A** (or a tap) opens a folder or plays a
  song with the folder's songs as the list; **B** goes up a folder (the cursor back on the one you left) and, at the
  top, Home; **LEFT/RIGHT** move a screenful; the line under the title says where you are (`Music › The Plantains ·
  4 songs`). **X** · More on a folder adds all its songs to a playlist or shows it in Files; on a song it is the
  song's own sheet. Only the index's songs are listed: a file the index doesn't read is in Files.

**Play random**: the shuffle key at the right of the view chips (or **Y** on Albums, Artists and Folders) plays
50 songs of the whole library (music videos left out) in a random order, none twice, as a new list named **Random**.
On the dashboard's Music page, **Play random** (beside the chips) does the same, playing where picks go.

**Autoplay** (Settings › Music, on by default; also **X** · More on the song player, Up next's heading, and the
dashboard): music started from the Music section (an album, an artist, a folder, Play random; on the dashboard its
Music page) plays on past its end. When the list's last song starts, 10 more songs from the library that aren't in
the list yet are added after it, in a random order, so the music goes on without a gap (and again at their last).
Up next shows them under a heading, **Autoplay · songs picked for you**, with a **Turn off** key (a tap; or a
row's **More**); turned off, the heading says **Songs picked for you** and offers **Turn on**, and the list ends at
its last song as any list does. Turning it off never takes the added songs out (they stay until you remove them);
turned on while the last song plays, the 10 are added at once. Playlists, Files and Videos never get songs added, nor
a list with Repeat All or One. Picking a row of the list in Up next keeps the added songs marked.

**Remove from list**: in Up next, **X** (or a row held) opens the row's **More**: **Play from here**, **Remove from
list**, and Autoplay on / off. Removing takes the song out of the list that plays (a playlist's own file is
unchanged: the row says so); removing the one that plays goes on to the next (nothing after it: it stops).
list**, and Keep playing on / off. Removing takes the song out of the list that plays (a playlist's own file is
unchanged: the row says so); removing the one that plays goes on to the next (nothing after it: it stops). The web
dashboard's Up next has the same: each row's **⋯** (or a right-click) opens Play from here, Remove from list and Keep
playing on / off, through the session (`/api/intent` `Remove`), whoever plays.

An album's page: the cover, the artist and year, the counts (`12 songs, 1 a video · 41 min`), and the keys **Play**
(**A**), **Shuffle** (**Y**) and **+** (Add to playlist, touch only). Under them the songs in disc, track and name
order — a music video sits among them with a **VIDEO** pill and plays as one item of the album's list (Next goes on
to the next song); the one playing shows its mark in the number's place, a liked one its heart, each its length.
**A** on Play plays the album from its first song; **A** on a song plays from that song; **Y** anywhere plays it
shuffled; **DOWN** steps from the keys into the songs. **X** is **More**: **Add to playlist…** (the album's songs into
one of yours; New playlist… starts from the album's name), **Show in Files**, and on a song **Like / Unlike** too.
**B** is back to the grid.

An artist's page: the same keys (everything of theirs, by album and track), then their albums newest first as a strip
of covers (**◀/▶** steps, **A** opens one), then every song of theirs with its album and length (**A** plays from
there). **X** is the same **More** (theirs, or the song's). **B** is back to the list.

**Scrolling** (Music's Albums grid, Artists, an album's songs and an artist's songs; Videos; the Books shelf; Files and
a connection's folders; the Network tab's connections and Find on the network; All recent; the list of playlists, a
playlist's items in every mode, and the **Add to…** picker) moves as a phone's lists do: the list
follows the finger by the pixel, glides on after a flick and slows to a stop, gives a little past either end and
springs back. A touch on a gliding list stops it there (and opens nothing); a finger that drifts less than 8 units is a
tap. The **D-pad**'s cursor is kept in view by a short glide (held, it follows). A row half out of the list is cut at
its edge. A finger anywhere on a list's rows (or the room under the last) drags it; a tap opens the row it went down
on, as before. Books' two tabs each scroll under their tab strip. A connection's form, Home and the
lists not named here still scroll by whole rows.

**Pull to refresh**: on Music's lists, Videos, Books, Files and a connection's folders, Manga's Library and On this
handheld, and the Sources lists, a finger that pulls the top of the list down shows **Pull to refresh**, then, pulled
far enough, **Release to refresh**; let go there and the list is read again (Music, Videos and Books: the cards'
library walked again; Files: the folder read again, as More's Refresh; Manga: its folders read again, Sources: the
repositories asked again, as Y). A **Refreshing…** row shows over the top for a moment. A shorter pull springs back
and does nothing.

With no Music folder on either card (or none with a song in it) the page says so — **No music here yet**, the folder's
name in the accent, **Browse Files** (A) and **Web dashboard** (Y) — and **L/R** still reaches Playlists. While a
card is read the first time a notice row under the chips says the albums fill in as it goes, and the page shows them
when the pass lands. A music video's tags are read as a song's are; without any it goes by its file's name, in its
folder's album, exactly as an untagged song does. Tag text in an old code page (Shift-JIS, GBK or Big5 bytes in a RIFF/RMP3 or ID3v1 tag) is shown as the right characters, not boxes: valid UTF-8 stays, otherwise the first code page that reads cleanly as CJK or kana, else Latin-1.

## The Videos section

The films on the cards in one list, without walking folders: filled by itself from the folders named **Videos** or
**Movies** (any capitals) at the top of each card, plus BananaOS's own `/data/roms/videos`. Three views, stepped with
**L/R** or tapped, with their counts:

- **All**: every video by name. A row shows its length (`9:56`), where it was left (`at 6:12 / 9:56` with a bar), a
  tick when played through, a **Playing** pill (or **Paused**) on the row that plays, and its folder, size and date
  (`Movies · 2.1 GB · 12 Nov`), with a thumbnail: a frame from a tenth of the way in (a third if that one is
  black), made once in the background and kept (the kind's mark stands in until it lands, and for a file with no
  picture). The top screen shows the video under the cursor: a larger frame of it across the top, fading into the
  page, then where it was left (`Video · left at 6:12 of 9:56`, `watched`, `not played yet`), its name and its facts
  two to a line — **Length** (with its chapters), **Size**, **Picture** (`H.264 · 1920 × 816`), **Changed**,
  **Audio** (`English 5.1, Italian`), **Subtitles** (`English, Spanish + 6`) and its folder. The frames and facts are
  kept in the state folder's `cache/videos` (96 MB at most, the least recently looked at going first; a file changed
  on the card is read again), so a second visit shows them at once. Nothing is made while a video plays or a file
  opens; what is kept still shows. Playing a video writes what the player found in it over the kept facts (a
  subtitle file put beside it, the exact length), so the top screen shows the change next time.
- **Folders**: the folders directly inside the Videos folders, each with how much is under it (`42 videos`), then the
  loose files under them. **A** opens a folder's own list (everything under it, however deep).
- **Not finished**: what you started and left, newest first, with its place.

**A** plays from the place you left it, with the view's videos as the list (Next goes to the next row; a folder's
list is the folder's). **X** is **More**: **Play from the start**, **Add to playlist…**, **Like / Unlike**, **Show in
Files** (Files at its folder with the cursor on it, to rename, move or delete it there; **B** in that folder comes
back here; Music's and Books' More have it too), **Info**, then the file's own rows — **Copy…**, **Move…**, **Rename…** and **Delete** (it asks first; what
plays can't be moved, renamed or deleted until it's stopped) — and **Sort**, stepped in place: by **Name** (the
usual), **Newest first** or **Biggest first** (Not finished always keeps its own newest-first). A delete, a rename
or a move done, the index walks the cards again, so the list is true again a moment later. **◀/▶** page the list,
**B** leaves a folder (the views are the section's home: **B** there asks to quit). **X** also ends in two rows on the right, the same on Videos, Music and
Books: **Where videos come from** opens the **Sources** sheet (each folder read now as card / folder with its count,
**A** opens it in Files, one that holds nothing is dim; the Web dashboard's address; the naming rule) and **Web
dashboard** opens its page. The line under the title says where the list reads from (`From Videos and Movies on both
cards · 86 videos`, three or more counted: `From 3 folders on both cards`; Books reads **Books**, **Ebook** and
**Ebooks**). The first time the app starts, an empty **Videos**, **Music** and **Books** are made on the main card
for each section with no folder of its names (once, never on a second card), so a card in a computer shows where
things go; the Web dashboard's Videos, Music and Books tabs each have an **Upload** button that sends files into that section's folder: you pick the card first (**SD1**, **SD2**: only those that are there; the one that already has the folder is the default, else the main card) and the folder is made if it is missing. Dropping files on one of these tabs does the same (the card question, that section's folder; only that section's kinds, others get a message). With no Videos folder on either
card the page says so, with **Browse Files** (A) and **Web dashboard** (Y); while a card is first read a notice row
says which card, how many folders are done, and a bar that follows them; the crumb counts what has been read so
far. A video anywhere else on a card is not listed here: it is in Files, found by Search, kept in
Recent; and a video inside a Music folder belongs to Music.

- **Web dashboard, Videos / Music / Books pages.** Music has a **Folders** view (the music folders on each card, then the folders inside, then the songs; a song plays with its folder as the list, from the tapped one; the volume sliders (bar, Now playing) move five a notch with the mouse wheel or trackpad). Videos' **Folders** opens folders in place, every video row shows the handheld's cached thumbnail (`/api/vthumb`, from `<state>/cache/videos`, cacheable; one not made yet is asked of the art worker by the request, 404 + `Retry-After`, and the page asks again a few times, nothing made while a video plays on the handheld) and **Not finished** shows a bar and the time left. Books lists the handheld's own shelf: the cards' Books folders plus every EPUB, PDF or comic opened from anywhere (it was empty for anyone who opens books from Files), with covers (`/api/bookcover`) and reading progress; a tap opens it on the handheld.
  Every song, video and book row there (folder views and Books included) has a **⋯** with **Show in Files**: the dashboard's Files page at its folder, the file lit and scrolled into view, ready to rename, move or delete; Back returns to the page.
- **Web dashboard, Videos / Music / Books pages.** Music has a **Folders** view (the music folders on each card, then the folders inside, then the songs; a song plays with its folder as the list). Videos' **Folders** opens folders in place, every video row shows the handheld's cached thumbnail (`/api/vthumb`, from `<state>/cache/videos`, cacheable) and **Not finished** shows a bar and the time left. Books lists the handheld's own shelf: the cards' Books folders plus every EPUB, PDF or comic opened from anywhere (it was empty for anyone who opens books from Files), with covers (`/api/bookcover`) and reading progress; a tap reads it in the browser (Reading in the browser).

## The Books section

A shelf of the book files on the cards — EPUB, PDF and comics (CBZ, CBR); not TXT, which Files opens in the text
viewer — in two tabs, each label with its count: **Reading now** is every book with a place kept, newest first, each
cover with how far in; **Shelf** is every book, by name. Both are five across. **L** / **R** (or a tap on a tab) switch
them; Books opens on Reading now while a book is in progress, else on the Shelf (back from a reader, on the tab you left). **A** (or a tap) opens the book in its own reader — a text in the text viewer, a PDF, a
comic or a pictures EPUB in the page reader — at the place you left it; **B** from the reader comes back to the shelf.

The shelf (and Reading now) holds only what lies in a section folder: a book opened from Files anywhere else stays in Home's Recent
and in Files, never here (the same rule keeps songs outside the Music folders out of Music and videos outside the Videos folders out of
Videos; the web dashboard's pages follow it). The shelf is filled from
the folders named **Books**, **Ebook** or **Ebooks** (any capitals; not Documents, whose PDFs are opened from Files) at the top of each card. Every cover is decoded — an
EPUB's own, a PDF's first page, a comic's first page — on the cover worker (the drawn sleeve, keyed by the name,
stands in until the answer, and for good for a TXT, which has no picture of its own). **X** is
**More**: **Read from the start** (its place forgotten, then opened), **Info** and **Show in Files**. **◀/▶** move along the
covers; **B** asks to quit (it is the section's home). With no books anywhere the page says so, with **Browse Files** (A) and **Web dashboard** (Y).
On the web dashboard an upload to Music, Videos or Books shows in its list as soon as the handheld has indexed it (the page lists again when the index pass lands, no reload). Files still lists and opens every book, and Recent keeps its Books filter; comics still need the Manga reader turned
on, and say so when it isn't.

### Catalogs (OPDS)

Books' third tab, **Catalogs** (after Reading now and Shelf; **L** / **R** step all three, no count on it): books
downloaded from an OPDS 1.2 catalog (Calibre's content server, Calibre-Web, COPS, Kavita, Komga, Project Gutenberg)
straight into a Books folder, so they are on the Shelf at once. With none saved the tab says what a catalog is, with
**Add a catalog** (A) and **Try Gutenberg** (Y: Project Gutenberg saved, no sign-in). With some, each is a row (its
host, who is signed in, how many of its books are on the card, "Downloading 1" while one comes) and **Add a
catalog…** last; **A** opens one, **Y** adds one, **X** is More (Open, Edit catalog…, Test connection, Remove
catalog… — Remove asks, and says the books stay; the right column Add a catalog… and Where books come from).

- **The form** (Network's): **Address** first (a bare host gets `http://`; a `user:password@` part moves into the
  rows; a Kavita address keeps its key sealed and shows it as dots), **Name** (filled from the catalog's own title by
  Test or Save; typed, it wins), **Sign in** None / An account (◀/▶), **User**, **Password** (sealed on Save, like
  Network's; reads "Saved" when editing), and **Copy from Manga's servers…** when Manga has an OPDS or Kavita server
  this list hasn't (it fills the address, the user and the password). **Y** Test ("It works · 1,204 books" or why
  not), **START** Save (it tests first; a failed test asks Save anyway / Edit), **B** back.
- **A catalog**: its folders as rows (the catalog's own words, a count when it gives one), **Search this catalog**
  on top when it names a search (OpenSearch or a `{searchTerms}` feed; **Y** from any of its pages; Done searches,
  the words kept for the next); a folder of books as the Shelf's covers, five across, with a format badge when not
  EPUB, **On card** on what this catalog already gave the card, a bar on the one downloading, **Can't open** on one
  in no format the readers open. More › **View** switches covers / a two-line list; More › Refresh, or pull down,
  reads it again. The next page comes by itself near the end ("Loading the next page…"); the crumb counts "20 of
  1,204" when the catalog says. **B** goes up one level; from the top, back to the tab. A catalog that doesn't answer
  says why on its page ("Can't reach …", "Not a catalog"), **A** tries again, **Y** opens the Shelf, **X** edits it.
- **A book's page**: its cover, author, year · language · tags, four lines of the summary; the formats as chips (only
  when there are several: EPUB, then PDF, CBZ, CBR, TXT; ◀/▶ choose, each with its size); **A** "Download the EPUB",
  and under it where it saves and the room left. Downloading, the key becomes the bar ("0.2 of 0.6 MB · 37%"):
  **Y** cancels (nothing half-saved is left), **B** leaves it running, one at a time. When whole: "Saved to Books"
  and the key turns to **Read** (A opens it in its reader, as from the Shelf); More: Download again, Show in Files,
  Info. A book in no readable format says which it has and that Calibre converts it.
- **Where it saves**: `<card>/Books/<catalog's name>/<Title> - <Author>.<ext>`, on the card with more room when both
  have a Books folder (a Books folder is made on the first card when there is none). Written to a hidden part file,
  renamed when whole; a download cut off leaves nothing (its part file is removed at the next start). Names are made
  safe for exFAT.
- **Sign-in**: Basic and Digest (Calibre's default); the login is sent only once the catalog asks, and only to the
  catalog's own host: a book's link on another host is fetched without it. Passwords never reach the log.
- Not yet: the books' own covers from the catalog (the drawn cover stands in), the summary on the top screen, adding
  a catalog from the web dashboard, OPDS 2.0.

## Playing while you browse

A file picked in Files plays with the rest of its folder after it (**Up next**): its videos and songs only, never its
photos, documents or other files. One the handheld can't play (a damaged file, a format it has no decoder for) stops
with a note when you pick it; reached by **Next**, **Previous** or the end of the one before, it's skipped with a note
(**… can't be played · skipped**) and the next one plays.

Leaving the player doesn't stop what plays. **B** (or **Back**, or **<**) on the playing screen takes you to the list,
and the song or video goes on: a video keeps playing full size on the top screen, a song keeps its visualizer or art.
Paused stays paused. You land back **where you were before you opened the player**: the same page, folder, cursor
and scroll (Home, a folder, All recent, Settings…); from a playlist's page, the list of playlists, the cursor on
that playlist. Started with a file, or after **Show in folder**,
you land in the folder (or playlist) it plays from, the cursor on it. The same goes for **Stop**, and for the end of a
video's list while the playing screen was up; a song's list ending keeps the song up instead.

- **What plays, on every screen:** the rail's **Now playing key**, just above Settings (the art at 30 and three
  playing bars, a pause mark while paused; its slot is kept while nothing plays). The art is the song's real
  album cover, decoded in the background — the Art placeholder until it arrives, and for good when the file has
  none; a video shows its film mark. Tap it for the playing screen; hold
  it for its menu: **Now playing**, **Play** / **Pause**, **Like**, **Add to playlist…**, **Show in folder**, **Stop**.
  **SELECT** is its key on any section page. Quick settings' **Now playing block** (F, on every screen, the player
  included) is the one place to play, pause and skip without leaving the page; its **Play** held takes playback over
  from a browser. It is the panel's lead: taller than its rows, on the accent's tint while something plays, with the
  art, **Now playing** or **Paused** over the title, artist and time over a progress line, and Play / Pause in the accent.
  There is no bar at the foot of any page any more (the owner, 2026-11-19): the body has that room.
- **The header** shows three small yellow bars left of the clock while something plays behind the list (dim and still
  when it's paused), and the status cluster (those bars, Wi-Fi, the battery and the clock) is one touch target:
  **F**'s panel, Quick settings, anywhere. The globe inside it keeps its own tap.
- **The playing file's row** carries a chip: **Playing**, **Paused** or **In browser** (the web dashboard plays it). Its card on Home says **Now** with its time and bar running. **A** on that row or card goes back to it,
  never from its start.
- **Back to the playing screen:** **SELECT** on any section page, a tap on the rail's Now playing key, **A** on its
  row or card, or **B** on Home. From a book or a comic, a video takes the top screen
  back while its player shows; **B** returns to the page you were reading.
- **L3** (press the stick) likes or unlikes what plays **on the player**; in the shell's lists it is Search.
- **Stop** ends it: **Stop** at the bottom right of the playing screen, **X · More** › **Stop**, or a hold on the
  rail's Now playing key for its menu. The place is kept (its card on Home resumes it). A song that reaches the end of the folder or playlist
  doesn't close: it stays on the playing screen (or Home's card keeps it) at **0:00**, paused, and **A** (or the power key
  with the lid shut, or **Play** in the browser) plays it again from its start. A video's end still stops: on the playing
  screen you're back in the list on it; while browsing the top screen says **Finished**.
- **Opening a photo** while a song or video plays doesn't stop it: the photo takes the top screen and what plays goes
  on behind it, sound and all (a song's next song follows as usual; a video's picture just isn't shown, or decoded,
  meanwhile). Close the photo (**B**, **Close**) and you're back where you opened it, the video showing again where it
  has got to. A page playing it in a browser isn't disturbed either.
- **Screen off (music)** and **Screen off (video)** (Settings) work while browsing too, as long as something plays (not paused).
- Started with a file (`banana-player FILE`): **B** opens the list on its folder, as if you'd started with none.

## Playlists

A playlist is a list of your videos and songs, in the order you set. It can mix files from your cards and from network
folders. Playlists have a section of their own, between Files and Network.

- **Make one:** in **Playlists**, **A** on **New playlist** (the last row), or **X** → **New playlist…**, then name it on the
  keyboard. A name is 40 characters at most, can't have a **/** in it, and can't match one you already have (capitals
  don't count).
- **Open one:** **A** on it shows its items, in order. The section lists Liked first, then your playlists by name, each with its number of items. A playlist's page has **Play**, **Shuffle** and (on a playlist of your own) **Add** over the rows: **Play** starts at the top, **Shuffle** turns Shuffle on and starts at a random item, **Add** offers **Add files…** and **Add folder…**. **UP** from the first row reaches those keys.
- **Its rows:** each shows its place, what it is, its name, a heart if it is liked, and its length. The one that plays
  has the play mark in the place's stead, a **Now** pill and where it has got to. A file that is gone says **Gone**.
- **Its menu:** **X** (or **Y**, or a hold on a playlist) gives **Play**, **Shuffle play**, **Rename…** and **Delete
  playlist** (press it twice). Deleting a playlist leaves its files where they are.

### Playing

- **A** on an item plays the playlist from there. **Play all** (the playlist's **X** menu) starts at the top, and
  **Play from here** (an item's **Y** menu) starts at that item.
- **Shuffle play** turns **Shuffle** on and starts at a random item; the rest follow in a random order (the list keeps
  its own order where it's shown). Shuffle stays on for what you play next, till you turn it off (the **Shuffle** chip
  on a song, its switch in the **List** sheet, or the web's Shuffle button).
- **L / R** go to the previous or next item, as in a folder. On **Loop all**, the playlist starts again after its last
  item.

### Adding items

- In **Files**, **Y** starts select mode with the focused file ticked; **A** (or a tap) ticks and unticks, **SELECT**
  is all / none, and the bar at the foot holds **Playlist**, **Copy**, **Move** and **Delete**. **X** is More (the same
  field as the bar, and Info). On the touch screen, a hold on a file starts select mode with that file ticked. **B**
  leaves select mode. In **Network**, **X** → **Select files** still ticks files for a playlist; **B** goes up a folder
  there and the ticks stay.
- The **Playlist** key adds the ticked videos and songs to a playlist (or **New playlist…**). **Copy** / **Move** hold
  all the ticked files and folders while you walk to the destination; **Delete** asks first, in a sheet.
  Copy and Move also work between a card and a connection's folder, for a whole selection: tick the files, **Copy** or
  **Move**, open the folder (on the card, or under **Network**) and put them there. They go one after another with one
  progress sheet (**Cancel** stops after the file being written); each file is whole or not at all, so a stop or a
  lost connection leaves the ones before it done and the message says how many.
- Inside a playlist, **X** → **Add files…** opens the file list, and the files you tick are added to that playlist.
  **Add folder…** adds a whole folder instead. Both are the Files page: its rows and bar (**All**, **Cancel**, **Add N**; or **Cancel**, **Add this folder**), with
  a cross for the header's key; **Y** adds, **X** cancels (twice when files are ticked), **B** goes up a folder.
- On any folder, **X** · More → **Add folder to playlist…** adds that folder's files (in Network, the folder's menu
  still has it).
- A folder adds only the files directly in it, not those in its subfolders. Photos are skipped, and the note says how
  many. Files are added in the folder's order.
- The same file can be in a playlist twice. **X** → **Remove duplicates** (in the playlist's **X** menu) keeps the first
  of each. **Remove missing** takes out the items whose file is on no card now, after a second **A** that says how many
  (put a card that's out back first; network items are never taken out).
- A playlist holds 2,000 items at most. An add that goes past that adds what fits, and says so.
- Editing the playlist that's playing (from the handheld or the dashboard) changes what plays next: songs added
  after the last one play after it (**Next** reaches them), removed ones are skipped, moved ones play in their new place.
  With Shuffle, added songs play after the rest of the round. Nothing restarts.

### Select, remove and move

In a playlist, **Y** (or a hold on an item, or **X** → **Select**) turns on select mode:

- **A** ticks an item (a tap does too). **SELECT** ticks them all, or clears them all.
- The bar at the foot has **Remove**, **Move** and **Add to…**. **Remove** takes the ticked items out of the playlist and
  asks for a second press; the files stay where they are. **Add to…** opens the playlist picker for the ticked items.
  **X** → **More** has the same actions. **B** leaves select mode.
- Every row shows a grip at its right while selecting: a finger drags a row by it, ticked or not.

To move items: tick them, then press **Y** (**Move**). The ticked items gather into one lifted block. **UP / DOWN** moves
it a row at a time, **LEFT / RIGHT** a page at a time, **L2 / R2** to the top or the foot. **A** puts it down (the ticks
stay), and **B** puts it back. On the touch screen, drag a row's grip instead, as in select mode. **Move** in an item's
**X** menu starts a move too, and after it you're back in the normal list.

Single items: **X** on an item (or a hold) has **Play from here**, **Play next**, **Move**, **Remove from playlist** and
**Show in folder** (which opens the item's folder, with the file under the cursor). **X** with the keys row focused is
the playlist's own menu.

### Missing items

If a file is gone (its card is out, or it was deleted), its row reads **[Gone]** before its name. A network item whose
connection was deleted shows the same. While a playlist plays, Next and Previous skip the local files that are gone,
with a note. **Y** on a gone item offers **Remove from playlist**.

Network items keep their subtitles and covers, as they do in a network folder. The first one may take a moment longer to
start, while its folder is read for them.

Each playlist is saved on the card as an `.m3u` text file (the usual playlist format), in
`home/.config/banana-player/playlists/`. A change is saved about a second after you make it, and when you leave
Playlists.

## Like and Add to playlist

Like a song or a video to keep it in **Liked**, a playlist of its own. Photos and folders can't be liked.

- **Like:** the **Like** key (the heart, under the transport row) while something plays, **X** → **Like**, or
  **L3** on the RG DS Plus on the player (in a list, L3 is Search). The heart turns yellow and the top screen says **Added to Liked** with the title
  (**Removed from Liked** when you press it again). A like is the file's, wherever it plays from.
- **Add to playlist:** the **Playlist** key (beside Like), or **X** → **Add to playlist…**. **New playlist…** comes
  first, then the playlists you changed last. A playlist that already has it says **✓ Added**: choosing it asks
  **Add again?**, and a second **A** adds another copy. **New playlist…** names one on the keyboard and adds to it. The
  button shows a check and the top screen says where it went.
- **Liked** is always first in **Playlists**, with a yellow heart. It's newest first; it can be reordered like any
  playlist (select mode, **Y**/the grip), but it can't be renamed or deleted, and only Like adds to it. Inside it, an
  item's **X** menu has **Play from here**, **Unlike**, **Add to playlist…** and **Show in folder**; select mode's
  **Remove** is **Unlike** (press it twice). A tap on a row's heart unlikes it.
- **Home:** a liked file's Continue and Recent cards have a small yellow heart, and a card's **More** sheet (X, a hold,
  or Y) has **Like** / **Unlike** and **Add to playlist…**. In **Files** and **Network**, liked files have a small
  heart before their note.
- Liked holds 2,000; liking one more lets the oldest go. Likes made in the web dashboard show up on the handheld at
  once, and the other way round.

## Controls

### While playing

| Button | What it does |
|---|---|
| **A** | Play / pause |
| **LEFT / RIGHT** | Back 5 seconds / forward 10 seconds (hold to keep going); the **5 s** and **10 s** keys either side of Play do the same by touch |
| **L / R** | Previous / next file in the folder (a song 5 s or more in restarts first; a video always goes to the one before; on the first video: nothing, unless Repeat All) |
| **L2 / R2** | Previous / next chapter (**F + L2** is still the screenshot) |
| **UP / DOWN** | Volume, five a step, as the handheld's volume keys: the level card on the top screen, and past 100% the player's own gain up to **Max volume** (with its warning; **DOWN** takes that off first). Hold to repeat: after 0.4 s, eight steps a second; a hold going UP stops at 100%, and going past it takes a fresh press. Inside a sheet or a list, **UP / DOWN** move the cursor |
| **X** | **More**: its first row is the player's five chips, under the cursor; then **Lock touch**, **Screen off**, **Sleep timer**, **Info** and **Stop** (in red), and **Like**, **Add to playlist…**, **Show in folder** and, while a dashboard page that can play is open, **Play on…**. On a chip, **A** opens that chip's sheet and More closes; on **Repeat** or **Shuffle** it changes it and More stays open. **LEFT / RIGHT** move along the chips, **DOWN** to the tiles and rows; **A** chooses, **X** or **B** closes |
| **Y** | **List** (also a tap on the **Next** line): one sheet with **Up next** (the folder or playlist that plays, in its own order, the one playing marked **Now**) and **Chapters** (every chapter with its start; a file without chapters has only Up next), with **Shuffle**'s switch and **Repeat** (Off / All / One) in its header. **L / R** step the tabs, **UP / DOWN** move, **LEFT / RIGHT** page, **A** plays from the row, **X** opens the row's More (Remove from list), **L2 / R2** jump a chapter with the sheet open, **Y** or **B** closes |
| **SELECT** | **More**, the same sheet **X** opens (the hint bar says **X/SELECT More**). Its first row is the five chips: **A** on one opens its own sheet, and More closes. A video: **Subtitles** (Off, the file's tracks, and **Subtitle style…**), **Audio**, **Picture** (Fit, Fill, Stretch, then **Adjust…**) and **Speed** (0.5x to 2.5x). A song: **Display** (the four Show modes and a **Visualizer style** row), **Equalizer**, **Effects** and **Speed**. There are no tabs and **L / R** do nothing in a sheet: **UP / DOWN** move, **LEFT / RIGHT** page a long list, **A** chooses, **SELECT** or **B** closes (on a song's **Display**, **SELECT** steps the Show mode). **Picture**'s last row opens **Adjust**; **‹** or **B** goes back to **Picture** |
| **START** | **Settings**: the ten group tiles; **B** or **START** closes it back to where you were (the player, here) |
| **L3** (press the stick, RG DS Plus) | Like / Unlike what plays |
| **B** (or **Back**) | Back to the list: it keeps playing (see [Playing while you browse](#playing-while-you-browse)) |
| **F + L2** | Screenshot of both screens (saved in the card's `screenshots` folder) |
| **F + START** | Quit Banana Player at once (anywhere; your place is kept) |
| **Menu/Function** (F) | Quick settings: volume, brightness, Same brightness for both screens, Web dashboard, incognito mode, Quit |

**By touch:**
- **Progress bar:** scrub it as on a trackpad: slide a finger along it (or just above or below it), from anywhere,
  and the place moves on from where it was by how far the finger travels, until you lift, even if the finger drifts
  off the bar; it seeks only when you lift (the picture and sound stay where they were while you slide). A tap jumps to the tapped place as you lift; a touch alone changes nothing. While
  you slide, the knob grows and the time shows over it (and the chapter's name under the bar).
- **Every slider works this way** (this bar, the quick settings' Volume and Brightness, the readers' bars and page
  sliders, the Brightness sheet's bars, the equalizer's bands, which go up and down): slide anywhere in its row to
  move it on from where it is, tap to jump; nothing else under the finger reacts meanwhile, and the small shift of a
  fingertip as it lifts changes nothing. While you slide it shows a bigger knob, and its value where the finger
  doesn't hide it.
- **Buttons:** they act when you lift your finger, so slide off one to cancel.
- **The transport row:** **Prev**, **5 s** back, **Play / Pause** (the wide yellow key), **10 s** forward, **Next**.
- **The second row**, the same on a video and a song: **Like**, **Playlist** (Add to playlist…), **Sleep** (the
  [sleep timer](#sleep-timer); the list is **Y**, or a tap on the **Next** line) and **Stop** (at the right edge, in plain ink: it stops everywhere, the place kept; **X** › **More** has
  the same Stop in red). **SELECT** opens **More**, as **X** does, and its first row is the five chips; the hint bar
  says **X/SELECT More**.
- **The chips** under them say how it is set, and a tap changes or opens each (they are touch only: the D-pad reaches
  them through More's first row). Five on both. A video: **Subtitles**, **Audio**, **Picture**, **Speed** and
  **Repeat**. A song: **Equalizer** (the equalizer's preset), **Sound FX** (the effect), **Display** (its value is the
  Show mode in its short form, **Art**, **Art + viz**, **Visualizer** or **None**, never the style), **Speed** and
  **Shuffle**. Each opens its own sheet at once: no View sheet and no tabs. **Repeat** and **Shuffle** open nothing:
  **Repeat** steps **Off → All → One → Off** in place (and wears the repeat-one mark on One), **Shuffle** turns on and
  off. No note is put up: the chip's word and its colour say it.
  A chip whose value isn't the normal one is yellow with a dot (a speed that isn't 1x, Repeat all or one, Shuffle, Fill
  or Stretch, an equalizer or effect left on), so it can't go unnoticed.
- **Adjust** (a video's **Picture** sheet, last row): **Brightness**, **Contrast**, **Saturation** and **Gamma** for a
  film that is too dark, flat or loud in colour; **LEFT / RIGHT** change the focused one, **A** puts it back to 0,
  **X** resets all four, **B** or **‹** goes back to **Picture**. The film on the top screen changes as they move.
  They are kept in the settings file, for every video from one run to the next, and never touch a photo.
- **The Next line** along the bottom names what plays after this: its length once the lists have probed it, else its
  place in the list ("2 of 7"; "shuffled" with Shuffle on), **A new shuffled round** at a round's end, **Nothing after
  this** at the end of a list played in order. A tap opens the List sheet on Up next, as **Y** does.
- **A song's Repeat mark** ends the Next line's row, so Repeat can't go unnoticed (a video has its **Repeat** chip):
  the loop mark, dim while Repeat is **Off**, in the accent with a dot on **All**, and the repeat-one mark on **One**. A
  tap steps **Off → All → One → Off**, the same setting as the List sheet's, and the top screen names the new state.
  While Repeat is All or One the top screen wears the same mark in its top right corner, in every Show mode. One item
  under a Repeat reads **This song again** on the Next line.
- **Chapters:** tap the chapter line above the progress bar (`2 / 8 · its name`) for the List sheet on Chapters.
- **Lists:** tap to choose, drag to scroll. A drag moves only the highlight, to the row under your finger when you lift
  it (it never plays); a tap still plays.
- **Back to the list:** tap **<** (top left): it keeps playing. **Stop** is the second row's fourth key, at the right
  (and in **More**, **⋯** or **X**, in red): it stops it, where **<** and **B** only leave the player.
- **Lock touch**, **Screen off** and **Info** are in **More** too. Locked, the controls go dark under one large lock:
  hold it for a second to unlock (the buttons still work, and **X** → **Unlock touch** unlocks as well).
- **<** is at the top left of every page and does what **B** does: in the file list it goes up a folder, on a page it
  goes back (in Settings, a group goes back to the tiles and the tiles close it). A section's home has none, and **B** there asks to quit.

**List** (**Y**) on a playlist's song opens that playlist's own page (its order, its edits), the cursor on the song,
what plays going on behind; the first time a playlist's song opens, the List key glows a soft yellow twice to say so.
**B** from the player goes back to where you opened it from (a folder, Home, Settings…): a playlist's page (not the
list of playlists), an album, an artist, a folder or Videos' list, the cursor on what played (the photo last viewed,
the song Next moved on to) where that list has it.
what plays going on behind.
**B** from the player goes back to where you opened it from (a folder, Home, Settings…), and from a playlist's page to
the list of playlists, the cursor on that playlist; back in the folder of what played (or the photo last viewed), the
cursor is on it.

Otherwise **List** (**Y**, or a tap on the **Next** line) opens its **Up next** tab: what plays from (a folder, a playlist, Recent's
folder, a selection from the web) in its own order, which Shuffle never reorders. The one playing is marked **Now**
(**Paused**, or **Browser** when a web page plays it), with each file's length where it's known; the sheet opens scrolled
to it. **A** (or a tap) plays that row as picking it from its list would (Shuffle and Loop stay as they are; with Shuffle
on, a new order from there), on the handheld or in the browser that plays the list; **A** on the one playing just goes
back to it. **B** goes back. Its **Chapters** tab has every chapter of the file, the one playing marked, with **L2 / R2**
jumping from either tab; a row's **A** plays from it. The header's **Shuffle** switch and **Repeat** Off / All / One are
the two settings the two keys used to share.

Under the progress bar you'll see the time played, the time left (at the current speed) and the total length.

The top line of the player shows what's playing from: the folder (`Videos / Movies`), or a playlist (**Playlist ·** its
name, then the server's name when the item is on one), and `3 / 12`, the file's place in its folder or playlist (with
Shuffle on too: the list keeps its order). Under it the title, and on its right how it plays: **Playing**, **Paused**,
or, while a dashboard page plays it, **In Chrome on Mac** (the Play key then says **Hold: play it here** and fills as
you hold it). A song's fifth chip is **Shuffle**; a video's fifth is **Repeat**.

### Sleep timer

Fall asleep to a song or a film: it pauses at the time you choose, quietly, and the handheld sleeps.

- **Open it:** the **Sleep** key on the player (where List was; the list is **Y**, or a tap on the **Next** line), or
  **X** › **Sleep timer**, or, while one runs, a tap on the clock badge in the header of any page.
- **The sheet:** **Pause after** **15**, **30**, **45**, **60** or **90 min**, **End of this song** (**video**, or
  **chapter** where what plays has chapters, as an audiobook) and **Off**. The D-pad moves (it opens on 30 min, or on the
  one in use), **A** or a tap sets it and closes (it counts from now, replacing one that runs), **X** adds the snooze to
  a running one, **B** or a tap outside closes. The top screen notes "Sleep timer · 30 min · pauses at 22:17".
- **While it runs:** the Sleep key reads what is left ("24 min", in yellow with a dot), every page's header has a clock
  badge with it, and a song's top screen has it quietly at the top right. It counts by the clock, playing or paused.
- **The last minute:** the volume fades to silence (the player's own volume: the handheld's level and its bar never
  move), and a card says "Sleeping in 0:48" on the top screen, and on the bottom screen too if it is lit (it never
  lights a dark screen). **A** gives **10 more min** (the snooze setting), **B** turns the timer off; the volume comes
  back within a second. **UP / DOWN** are still the volume; no other key does anything there. End of this song has no
  fade: the card shows for its last minute ("Sleeping after this song · 0:48") and the song ends as it was made.
- **At its end:** it pauses (never stops: the list and the place are kept, back where the fade began, so nothing heard
  at a whisper is missed), both screens turn off (the top panel's backlight too), and 3 s later the handheld sleeps (the setting **When the sleep timer
  ends**: Sleep, or Pause to stay awake). A copy, a move or a download in progress keeps it awake, paused and dark; a
  browser signed in to the dashboard doesn't. A key or a touch in those 3 s wakes the screens and calls the sleep off.
  On waking: "Paused by the sleep timer" and "A plays" on the top screen until the first key.
- **Stop** (anywhere) turns it off ("Sleep timer off"); so does any sleep (the lid, the power key, the idle sleep). If
  the list ends first, its last minute starts then, with nothing to fade. With the lid shut and **Keep playing**, the
  fade is the warning and a press of **Power** in that minute is 10 more minutes. It is never saved: a timer doesn't
  outlive a quit or a sleep.
- **On the web page:** Now playing has a **Sleep timer** row (a desktop: a clock key at the right of the player bar)
  that shows what is left and opens the same choices, with **10 more min** and **Timer off** while one runs. Its last
  minute puts a card at the foot of every page; whichever side answers, both cards go. When it sleeps the handheld, the
  page says "The handheld is asleep (sleep timer)".

### Looking at a photo

| Button | What it does |
|---|---|
| **LEFT / RIGHT** or **L / R** | Previous / next photo (hold to keep going) |
| **UP / DOWN** | Zoom in / out: 100%, 150%, 200%, 300%, 400%, 600%, 800% |
| **A** | Zoom to 200%, or back to the whole photo |
| **Left stick** (any way) | Pans a zoomed photo while held, faster the further it is pushed (a full push is about a screen a second) |
| **L3** (press the stick) | Rotate a quarter turn |
| **SELECT** or **X** | **More**: four chips changed in place (**Shown**: Fit / Fill, **Keep zoom**, **Pan area**, **A drag moves**), then **Info**, **Show in folder** and **Photo settings**. The **Fit** key on the photo's face steps Fit / Fill in place |
| **START** | Settings |
| **B** (or **Back**) | Close the photo (**Close** at the bottom right too) |

See [Photos](#photos) for the touch screen.

### In the file list

**Files' places.** Files' first page lists where your files can be, each a card: **Internal storage** (the system's
own storage; **SD card 1** when BananaOS itself runs from a card), **SD card 2** when a card is in the second slot,
and the **Videos** shortcut with the storage it is on. A storage's card shows its free room (`21 GB free`), a bar of
what is used, and `43 GB used of 64 GB` under it. Nearly full (under a tenth free, or under 1 GB) the bar and the
free line turn red and the line says `nearly full`. Sizes are the ones on a card's box (1000s).

**A storage on the top screen.** With the cursor on a place, the top screen shows that storage (for the **Videos**
shortcut, the storage it is on): its name, where it is and its filesystem (`/run/sd2 · exFAT`, and `Read-only` when
it is), the free room in large figures with `43 GB used of 64 GB` beside it, and a bar of what is used cut into
**what fills it**: **Videos**, **Music** and **Books** (how many, and how much room they take), then **Other** for
the rest (photos and everything else). The kinds come from what your library already lists, so nothing is searched
for it: before the library has been read the bar is in one piece, and Books joins once the Books section was opened.
Nearly full, the label, the figure and the bar turn red. A card taken out before the list caught up reads `Card out`.

| Button | What it does |
|---|---|
| **A** (or tap it) | Open the folder, play the file (or open a document), or open the playlist. Files lists every file of the cards; one nothing here opens says so |
| **B** or **Back** | Up one folder; at a section's home (Files' places, Network, Playlists) it asks to quit. An empty folder's page offers **Go up** on **A** and **Send files** on **Y** |
| **UP / DOWN** | Move |
| **LEFT / RIGHT** | Page up / down |
| **L2 / R2** | Previous / next section: Home, Files, Videos, Music, Books, Extensions, Settings (**L / R** step Files and Network) |
| **X** (or **⋯** in the header) | **More**: the focused file on the left (**Play from the start** / **Open**, **Add to playlist…**, **Like** / **Unlike**, **Go to album** / **Go to artist** for a song, **Info**, **Copy…**, **Move…**, **Rename…**, **Delete**), and the folder it is in on the right (**Select files**, **Play all N**, **Shuffle play**, **Add folder to playlist…**, **Refresh**, **Hide folder**, **Show hidden folders**). In Network: **Add connection**, **Find on the network**, **Refresh**, **Web dashboard…**. In Playlists: **New playlist…**. In All recent: Home's options |
| **Hold** (touch screen) | On a file in Files or Network: starts selecting, with that file ticked |
| **Y** | In Files: select mode (**A** tick, **SELECT** all / none, the bar for Playlist / Copy / Move / Delete, **X** More). On a network connection: **Edit connection**, **Test connection**, **Delete connection** (press twice) (or hold it on the touch screen). On a playlist or its item: see [Playlists](#playlists) |
| **SELECT** | Back to what plays (the playing screen): the rail's Now playing key's key, on any section page; in select mode, All / none |
| **L3** (press the stick) | **Search**: the cards' search (its page keeps the last query; the rail's key does the same). In a document or the Manga reader L3 is Find or Likes, as those pages say |
| **START** | **Settings**: its ten group tiles; **B** or **START** returns to the folder you were in; **F** is Quick settings |
| **The rail's Now playing key** (touch screen) | Just above Settings, on every section page: the song's album art (a video's film mark) and the playing bars. Tap it for the playing screen · hold: its menu. Its slot is kept while nothing plays |

### Copy, move, rename, delete

**X** on a file or folder in Files, **Copy…** or **Move…** holds it while you go to the folder it goes in (on the same
card or the other one); a bar along the foot says what's held, and **SELECT** (or **Copy here** / **Move here**) puts it
there, **X** (or **Cancel**) lets it go. Select mode's bar does the same with every ticked file and folder (one at a
time to a server). Something of the same name already there is never replaced: the new one is called "name (2)". A move
on the same card is instant (a group move copies first, then removes the originals). A copy, or a move to the other
card, shows its progress in a job sheet (a bar, how much of how much, the file on its way) until it ends; **B** or
**Cancel** stops it, and nothing half-copied is left. It checks first that there's room, and that a FAT card isn't asked
to hold a file of 4 GB or more. **Rename…** types the new name on the keyboard. **Delete** opens a Confirm sheet that
names the file (the safe **Cancel** has the focus; **B** is Cancel) and deletes only on the red **Delete**. What's
playing can't be moved, renamed or deleted.

The redesigned Files page is a section: its header names the folder and its path (**Files › Videos › Movies**), the
list counts its items and the room on the card at the foot. The old **..** row is gone (**B** and the header's back
key go up). A folder this account can't write shows **Read only** in the header, and **Move**, **Rename**, **Delete**
and **Hide folder** are dimmed with that reason. An empty folder is a page of its own: what it has, with **Go up**
(**A**) and **Send files** (**Y**, the Web dashboard page).

### Settings from START

From any screen, **START** opens **Settings** directly, on its ten group tiles (Playback, Subtitles, Music & sound,
Photos, Reading, Files & history, Power & screens, Web dashboard, Updates, About), under the kit's header. **A** or a
tap opens one; in a group, **B** goes back to the tiles. **B** or **START** at the tiles closes Settings back to where
you were: the player, the page of a book, the folder. The hint bar there names **F Quick settings**, so Quit stays in
reach; the Settings page itself teaches **B / START Back**, and the first-start pane says it too.

Settings reached from the **rail** or **L2/R2** is a section instead: its tiles are the section's home (no back key,
**B** does nothing there). That is the whole difference: the
same page, but it remembers which way it was opened.

There is no START menu: its places are the rail's cells, **F** is Quick settings, **L3** is Search, About is Settings'
last group, Now playing is **SELECT** and the rail's Now playing key, and Quit is in Quick settings or **F + START**.
Nothing is lost: like, Add to playlist…, Show in folder, Play on, Info and Stop are on the player's **X · More** sheet
(Stop is also a key on the player). See [Like and Add to playlist](#like-and-add-to-playlist) and
[Play on](#play-on-from-the-handheld).

To quit, use **Quit Banana Player** in quick settings (**F**), or **F + START** from anywhere.

## Search

Search also matches a song's file name (its extension off, `_ - .` read as spaces), so "gaia" finds Ultramangaia_ED2_Beat_on_Dream_on.mp3 though no tag holds it; those hits come after the tag matches.

Find an artist, an album, a song, a video, a book, or a folder inside your Music or Videos folders by any part of
its name, from the **Search** key at the top of the rail or **L3** on a list. The page is titled **Search library**:
it searches the media library only — the artists, albums and songs of the Music folders (a song by its title, artist
or album), the videos of the Videos folders, the folders inside either (at any depth, by the folder's own name), and
the books on the Books shelf — not Files' other folders (a file outside the library is found in Files). The keyboard
opens at once (the on-screen keyboard, less **#+=**; **B** deletes, and closes on an empty field); **START** (or Done)
shows the results, and the page keeps the query when you come back to it (**Y** edits it). With the field empty it
rests on **Recent searches** (the last eight, kept in the state folder; **Clear recent searches** empties it, and
incognito keeps none).

Results sit under four tabs, **All**, **Music**, **Videos** and **Books**, each with its count (`Music 14`); **L / R**
step them round, or tap one. They come in sections, each under its heading with its count, in this order: **Artists**,
**Albums**, **Music folders**, **Songs**, then **Video folders**, **Videos**, then **Books** (one with nothing is left
out). Under **All**, when more than one section found something, each shows its first three and a **See all 10
songs** row (**A** on it: that kind's tab, on that section); the **Music** and **Videos** tabs list their sections
whole, **Books** its books. A new search starts on All.

- **What matches:** an artist by their name; an album by its name, and after those the albums of an artist the text
  names ("lanterns" lists The Lanterns, then their albums); a folder by its own name (the Music and Videos folders
  themselves are not results); a song by its title first, then the ones found only by their artist's or album's
  words. Names that start with the text come first. Case and accents never matter.
- **The rows:** an artist has a round record and `2 albums · 21 songs`; an album its cover (or the record in its
  sleeve) and `artist · 12 songs`; a folder the folder icon and how much is under it, however deep, with `in Rock ·`
  before it when it is not directly in the Music or Videos folder.
- **A opens a result as its section would:** a video or a song plays with its folder as the list, a book opens at
  your place; an **album** or an **artist** opens its page of Music's, a **folder inside Music** opens Music › Folders
  inside it, a **folder inside Videos** opens Videos' list of it. **B** on that page (the hint bar says **Results**)
  comes back to the results, the cursor on the row you opened; from deeper inside a Music folder B climbs back to it
  first. Picking a section on the rail ends that: the section is then its own.

**UP/DOWN** move, **LEFT/RIGHT** page, a finger scrolls the list (it glides), **B** goes back where you were. While
the cards are first being read a notice row says so and the list fills in. Nothing found is an empty page saying what
was searched (Files' other folders and network folders are not), with **Try another** (**A**) and **Browse Files**
(**Y**).

**On the Web dashboard:** Search library is there too, with the same sections, order and tabs. On a computer the
field is at the top of the sidebar (**/** focuses it on a page with no find field of its own); on a phone a magnifier
in every page's header, and on a tablet a Search key in the sidebar, open the Search page with its own field. Results
come as you type, and the query is in the address (`#/search?q=`), so the browser's Back returns to them. A song or a
video plays where picks go (this browser or the handheld), a book opens in the reader, an album or an artist opens its
Music page, a folder opens Music's or Videos' Folders view inside it; the page's back arrow returns to the results. A
section lists at most 200 (a line says so: type more of the name).

## Network folders

A connection's folders are the Files page: the same rows, keys, select mode and **More** as a card's folder (header: *Files › Network › server › path*), with a loader while the first listing comes in.

Play videos, music and photos straight from a computer or a NAS on your Wi-Fi, over **SMB** (Windows file sharing,
most NAS boxes, Samba) or **FTP** (and FTPS). What plays isn't copied to the card.

- The Network section lists the saved connections on the kit's rows (name, then kind and address, never the password),
  with **Find on the network** and **Add connection…** as rows at the foot as well as in **X · More**. With none yet
  it offers **Find nearby** (tap it, or **A**); **Y** adds one by its address instead.
- **Find on the network** (its row, or **X** in Network) lists the computers sharing files nearby (Windows, Mac, Linux, NAS):
  pick one, type your user name and password, and Save. No address to type: a computer found by its name is found
  again by that name, even when its address changes.
- **Add connection** (X) for one it can't find, or for FTP: the type, the server's address or name, and optionally the
  port, the share or folder to start in, and how to sign in (an account, or as a guest / anonymous for FTP). **Name**
  is optional: left empty, it's named after the server ("Media on MY-PC"). **Test connection** tries it before you save.
- Passwords are saved encrypted with a key that only this handheld has.
- **Y** on a connection: edit it, test it, or delete it.

Network files play like the ones on your card: the folder plays as a playlist, subtitles next to a video (`movie.srt`,
`movie.en.srt`...) and a folder's `cover.jpg` are found, Recent lists them, and they resume where you left
off. The player reads ahead (up to 64 MB), so a short Wi-Fi drop doesn't stop it; while it waits for more, the top
screen says **Buffering**. If the connection is lost for good (20 seconds), or the server can't be reached, it stops
on that file and says so (**Lost the connection**, or **Can't reach it**), never skipping on through the rest of the
folder or playlist; **Retry** (**A**) plays it again from where it stopped.

A network folder lists every file, each kind with its icon, as Files does:

- A book, a document or a comic (TXT, EPUB, PDF, CBZ, CBR) is copied to the card first, with its progress, then opens
  as one on the card does; a network that drops while you read takes nothing away. A copy opened before opens at once
  (the copies kept come to 2 GB at most, the oldest going first to make room for the next; one file bigger than
  that is kept alone).
- **Y** (or **X**) on a file or folder: **Info** (its kind, size, where it is, and whether you can change it; its lines
  are grouped under **File**, then **Video**, **Audio** and **Subtitles** with each track's language and format, when it
  is the file loaded), **Copy**,
  **Move**, **Rename**, **Delete**, as on the cards. Copy and Move go between the card and the network either way, and
  between network folders: open the folder it goes in (on the card or on a server) and press **SELECT**. A copy shows
  its progress until it ends, and nothing half-copied is left behind, on the card or on the server; a move deletes its
  original only once the copy is whole.
- A folder the account can't write to shows a lock and **Read only** at the top: there Move, Rename and Delete are
  dimmed (they say why), and nothing can be copied into it. (To know, the player puts a hidden empty file,
  `.bp-write-test`, in the folder you open and takes it away again at once.)

## Photos

Pick a photo in the file list. The bottom screen shows the photo small, in the **pan area** (on the left; Settings ›
Photos can put it on the right), with a box around the part you see on top:

- **Drag** the box over the small photo to look around: the top screen follows it. Or, with Settings › Photos ›
  **Drag moves: The photo**, drag the photo itself, as on a phone. Let go while moving and it glides on.
- **Double-tap** to zoom to 200% on that spot. Double-tap again to see all of it.
- **Tap** somewhere on the small photo to look there.

The buttons beside it do the same as the handheld's: Prev, Next, Zoom −, Zoom +, Rotate, Fit/Fill, Info and Menu.
The **Fit** key steps Fit / Fill in place, and **SELECT** (or **X**, or **More**) opens the photo's **More**: its four
chips change how photos are shown in place (**Shown**, **Keep zoom**, **Pan area**, **A drag moves**), and **Info**,
**Show in folder** and **Photo settings** follow them.

The next photo opens at the same zoom, in the same corner, which suits scanned comics page by page (turn this off with
**Keep zoom between photos**, or the **Keep zoom** chip on a photo's More). A song or video playing when you open a photo plays on behind it (see
[Playing while you browse](#playing-while-you-browse)). Camera photos (JPEG) taken sideways are shown upright, and animated GIFs play. Photos don't go into
Recent. **Info** shows the photo's size, format, and when and with which camera it was taken.

## Documents

Files lists text files, books and PDFs with the videos, songs and photos, and **A** opens them in a reader of their
own. Each opens where you left it, and goes into Recent (its card says the page, or how far through). Comics (CBZ, CBR)
open the same way as a PDF, with the Manga reader turned on (Extensions); without it, Files says to turn it on.

### Text and books (TXT, EPUB)

A `.txt` file, or an EPUB that is a book of text, is shown on the top screen as a page of text that you scroll. The
bottom screen holds the controls: how far through you are (a bar: tap or drag it to go there), a big area to drag the
text with your finger (a drag stops where you lift your finger, only a quick flick glides on; a tap on its upper half goes a screen back, on its lower
half a screen on), and **Contents**, **Bookmarks**, **Options** (Text options) and **Book mode**, each with its key
under it (Options has none: it is the touch key). The header carries the book's name at 18 units, its line under it, and
a bookmark key: one tap keeps the place (as Bookmarks › Add a bookmark here).

| Button | What it does |
|---|---|
| **UP** / **DOWN** | A few lines up or down |
| **Left stick** (any way) | Scrolls smoothly while it is held: a small push crawls, a full push is about a screen a second (up / down; lists, menus and the text viewer scroll, left / right do nothing there) |
| **LEFT** / **RIGHT**, **L** / **R**, **A** | A screen back or on |
| **SELECT** | **Book mode** (below) |
| **X** | **Bookmarks**: add one here, go to one, delete one |
| **Y** | **Contents** (an EPUB's chapters): **A** goes to one |
| **START** | **Text options** (the Options button): how the text looks; **START** or **B** closes them. Held sideways too, upright over the pages (a tap in the middle shows the **Book bar**). (In every reader **SELECT** is the full view and **START** is how it is shown) |
| **L3** (press the stick) | **Find** (below) |
| **B** | Back to Files |

**Book mode:** hold the handheld sideways like an open book, and each screen shows a page of text, upright, the text going
on from the left page to the right one. **UP** and **DOWN** turn the pages the way they point as you hold it (as do **L**
/ **R** and **A**, and one push of the left stick up or down), or swipe along the bottom screen; a tap near either end of the bottom screen turns that way, a tap in
its middle shows the **Book bar**, upright as the pages are: the title, how far, then **Contents**,
**Text**, **Light** and **Find**, and **Back to scrolling**. Each page shows how far through you are at its foot. **B** (or **SELECT**) goes back
to the scroll; **B** there closes the document. Turn it on with **SELECT**, the **Book mode** button, or Text options › **View**; **Hold** says which side the top screen is on (left or
right), and the D-pad follows it. Going back a page may split the text a little differently than going on did, as the
pages are made from where you are, never from the start of the book. A turn folds the page at the spine, the hinge between the screens, as a page turns in a book: going on, the
right-hand page folds away and the new left-hand one opens over the old (back, the other way round). Text options ›
**Page turn** (Fold or Off) turns the fold off, and the pages change at once. Each turn says **Next page** or
**Previous page** with how far through you are, upright as the pages are, and fades out after a moment.

**Find** (**L3**, or Text options › **Find…**): type a word or phrase, and the text goes to the next place it shows
(from where you are, round past the end), with every match marked. **R** goes to the next, **L** the one before,
**B** leaves Find. Letters A–Z match in any case. In a TXT of any size the search runs over the file as it is on the
card, so it's quick; a book is searched chapter by chapter. While it runs, a sheet shows how far it got (**B** stops
it); a word says when there's nothing more to find.

**Bookmarks** (**X**, the button, or Text options › **Bookmarks**): **Add a bookmark here** keeps the place with its first
words; the list, in reading order, goes to one with **A** and deletes one with **X** (twice).

**Text options:** **Find…**, **Bookmarks**, **View** (Scroll or Book mode), **Hold** and **Page turn** (in Book mode), **Font** (Serif, Sans, Mono, Pixel, Pixel CJK), **Text size** (five sizes), **Line spacing**,
**Margins**, **Colours** (Match theme, Dark, Light, Sepia, Black: Match theme, the default, is the Light page with the Light, 16-bit, 16-bit Lavender, 32-bit, Sakura or Gundam theme, Black with High contrast, else Dark; one picked by hand holds whatever the theme), **Brightness & filter** (the sheet below) and **Encoding** (a TXT only: Auto, or the code page an old file was written in, when
its letters come out wrong). They're kept for the next file. To go anywhere in the text, tap or drag the bar at the
top of the bottom screen.

A TXT of any size opens at once and jumps anywhere at once: only the text on the screen is ever read, so a file of
hundreds of megabytes is as quick as a short one. Its encoding is found by itself (UTF-8, UTF-16, or an old code page
such as Windows-1250 or -1251). A book keeps its headings, paragraphs, bold and italic, quotes and pictures, in the
viewer's own font and colours. Letters a font doesn't have (Chinese, Japanese, Korean, Thai, Arabic, Hebrew, Hindi)
come from the player's other fonts.

### PDF (and comics as EPUB)

A PDF, or an EPUB that is a comic (a picture a page), opens a page at a time, sharp at any zoom: each page is drawn
for the size it's shown at. The header carries the file's name at 18 units with the page, the zoom and the fit under
it, and a **More** key (as **X**). A comic (CBZ, CBR) from Files reads the same way.

- **Single page** (the first time): the bottom screen shows the page small, in the **pan area**, with a box around
  the part you see on top, as with a photo: drag the box to look around, tap the small page to look there, double-tap
  to zoom to 200% or back. Beside it the hand's keys: **Page** back and on (**L**/**R**), **Zoom** out and in
  (**DOWN**/**UP**), and **Page fit** (Page or Width: a tap changes it). Under them a **page slider** (the page shown
  at its left, the last at its right: drag it to go far).
- **Vertical scroll**: the pages one under the other, scrolled with a finger on the bottom screen's big area, or a
  screenful at a time with **LEFT/RIGHT** (and **A**); the page slider is under the header.
- Both end on one row: **Pages** (**Y**), **View** (**START**), **Fullscreen** (**SELECT**) and **Light** (Brightness &
  filter; what's on in its place while Extra dim or a filter is).
- **Fullscreen** in one step: **SELECT** (as in the Manga reader) or the **Fullscreen** key puts the pages over both screens as
  one tall strip, from either layout; **SELECT** or **B** goes back to the layout you came from. On the way in a line says
  so for a moment. In fullscreen a tap in the middle of the bottom screen shows the **navigation** over the page: the
  file's name and *Page 4 of 30*, the page slider, **Page** back and on (**L** / **R**), **Pages** (**Y**), **View**
  (**START**) and **One screen** (**SELECT**), all of them touchable. It goes after four seconds untouched or on
  another tap in the middle; a tap near the top or the bottom goes a screenful back or on (with it showing too), and
  a drag scrolls.
- **View** (**START**) is a sheet: **Layout** (Single page, Vertical scroll, Fullscreen: the strip across both screens),
  **Hinge gap** (in Fullscreen: the part of the page skipped under the hinge, the Manga reader's setting; the pages
  above move as you change it), **Page fit**, **Zoom**, **Turn** (a quarter at a time, for a scan on its side; Single page only) and **Light & filter**.
  **UP/DOWN** choose, **LEFT/RIGHT** change it at once (the page above follows), **START** or **B** close (**SELECT** there goes Fullscreen); a tap on a
  choice or a side of a value changes it. The layout and the fit are kept for the next PDF. Going Fullscreen keeps the
  sheet open over the page; there **SELECT** or **B** goes back to one screen and **START** opens View again.
- **More** (**X**): a browser's page while it differs from this one, **Info**, **Show in folder**, **Light & filter**
  and **Reading settings**.

| Button | What it does |
|---|---|
| **LEFT** / **RIGHT** | Back / on: through a zoomed page first, then to the page before or after |
| **L** / **R** | The page before / after, straight to it |
| **UP** / **DOWN** | Zoom in / out (both views, as in the manga reader and the photo viewer) |
| **Left stick** (any way) | Vertical scroll (and Fullscreen): scrolls smoothly while held, and pans a zoomed strip left and right. Single page: pans a page that is zoomed, wide or taller than the screen, sideways too |
| **L3** (press the stick) | Turn the page a quarter turn (Single page; as in the Manga reader) |
| **A** | Single page: 200% or back to the whole page. Vertical scroll: on |
| **X** | **More** |
| **SELECT** | **Fullscreen** on / off |
| **Y** | **Pages**: every page small; **A** goes to one |
| **START** | **View**: the layout (Single page, Vertical scroll, Fullscreen), the fit, the zoom, the turn. (Settings: **X** › Reading settings) |
| **B** | Back to Files |

A PDF locked with a password says so and doesn't open.

### Brightness while reading

Every reader (TXT and EPUB, PDF, the Manga reader) has a **Brightness** button (the sun beside the text viewer's
buttons) and a **Brightness & filter** row in its options. The sheet it opens, over the bottom screen:

- **Screens** (or **Top screen** and **Bottom screen**): the screens' own light, 10–100%. Under it, **Same brightness
  for both screens** (the quick settings' box): ticked, one slider sets both; unticked, each screen has its own.
- **Extra dim**: darker than the screens' lowest, for reading in the dark: the pages themselves are darkened, 10–80%,
  on both screens. Kept for every reader.
- **Filter** (Off, Warm, Warmer, Dim): a warm, yellowish light is easier on the eyes at night. Each reader keeps its own.

**UP/DOWN** choose, **LEFT/RIGHT** change, a finger on a bar's row takes the bar (it follows the finger until it lifts) or taps a filter; **B** (or a tap outside) closes it.
While Extra dim or a filter is on, the Brightness button says so: lit in yellow with what's on (**Warm · −30%**; the
text viewer's sun gets a dot).

## Picture size

On a video, the **Picture** chip (or **X** / **SELECT** › **Picture**): **A** chooses, the one in use has its radio filled.
**Adjust…** is its last row, for Brightness, Contrast, Saturation and Gamma.

| | |
|---|---|
| **Fit** | The whole picture, with black bars where its shape doesn't match the screen |
| **Fill** | Fills the screen; the edges of the picture that don't fit are cropped |
| **Stretch** | Fills the screen by stretching the picture |

## Music

Songs show their title, artist and album on the top screen. While a song plays:

- **Display** (**SELECT** or **X** opens More, then its **Display** chip; the player's **Display** chip opens it too). One page: under "Top screen shows", four tiles in a row, one always in use: **Album art only**,
  **Art + visualizer** (default: the art above the visualizer along the foot), **Visualizer only**, or **None** (just the title:
  the lightest on the battery). Under them the line "Visualizer · <style>" (with Album art only or None: "<style> · not
  shown now: the top screen shows <mode>"; the style can still be set, and is kept), then the fifteen styles in a 5 × 3
  grid of tiles (the same fifteen, in the same order, on Settings' Visualizer style row), and at the sheet's foot the
  **Ambient mode** switch (the same row as Settings › Music & sound's). The tile in use is filled in
  the theme's accent; the cursor is an accent edge. The styles: **Peaks** (64 bars in the theme's accent,
  bass on the left, each with a cap in the theme's text colour that stays at the bar's highest point and slowly falls
  back, over a faint grid), **Ribbon** (default: the spectrum as one smooth glowing line, bass to treble, a soft fade beneath
  it and a slower line behind showing where the song has been sitting), **Mirror bars** (the spectrum's bars growing
  up and down from a faint centre line), **Faders** (a mixing desk from above: sixteen thin rails across the screen,
  a knob riding each one at its band's level, bass at the left, with a small tick holding each band's recent peak
  before it falls back; silence leaves every knob on the floor in a row), **Horizon** (the spectrum as a
  mirrored skyline, the bass in the middle, on a horizon line with a low sun behind and a striped reflection below),
  **Grid** (a floor in perspective: the front ridge is the spectrum now, bass in the middle, and each ridge behind it
  a moment ago, rolling to the horizon and fading), **Starfield** (stars streaming out of the middle: quiet passages
  drift, loud ones rush, each star stretching into a streak), **Strings** (eight strings across the screen, the thick
  bass one at the foot, each vibrating as wide as its band is loud, the blur of its swing behind it), **Helix** (two
  strands of beads twisting round each other, bass at the left: a bead swells with its band and the twist turns faster
  as the song gets louder), **Sparks** (small lights lifting off the foot where the music is, a louder band sending
  more and higher, and a brighter handful on each beat), **Bouncy keys** (a keyboard whose keys rise with their
  bands, and a ball hopping from key to key, a hop a beat, toward a loud key, which dips and lights as it lands),
  **Rocket** (a little rocket flying right, its flame growing with the bass, its smoke trail the spectrum, the stars
  streaking past faster as the song gets louder; with Art + visualizer the rocket stays in the band and its stars
  streak over the whole screen behind the art), **Lanterns** (a string of paper lanterns across the screen, each
  lantern's paper going from unlit to the accent with its band, the beat nudging the string so they swing and settle;
  seven alone, nine smaller ones in the band, and silence hangs them straight, still and unlit), **Sleepy moon** (a moon that dozes through quiet passages breathing out Zs and wakes when the song gets
  loud, rays out, stars twinkling with the treble over a cloud bank that is the spectrum; with the art a smaller moon
  at the top right) or **Dance line** (twelve tiny dancers, one a band, jumping as high as their band is loud, the
  line swaying in time). The bar styles' bars stand out from their neighbours, so the
  spectrum has peaks and valleys; silence stays still. With Art + visualizer, every style is drawn along the bottom of
  the screen, under the art, on the theme's ground, except **Sparks**, **Starfield** and **Sleepy moon**, which fill the whole screen
  behind the art and the song's lines (they pass behind them, never over them; so does Rocket's starfield), the art and lines then centred on
  the screen as on Album art only. All fifteen are drawn by the player, not
  mpv, at the Visualizer frame rate setting, in the theme's colours (a theme change recolours them at once):
  switching among them, or to None and back, never interrupts the sound. The song's lines are drawn over the
  visualizer with no box behind them (the shapes pass under the text, which keeps an outline in the theme's
  background colour so it stays readable); only a volume or other card keeps the shapes off its place. A song from a network folder shows its style still (flat: the
  player reads only local files for the visualizer). Nothing on the top screen is drawn by a filter graph any more:
  mpv only plays the song's sound, so changing the mode or the style never reopens it. Earlier styles no longer offered (Night city, Aurora, Dots, Steps, Barcode, Rings, Banana
  dancer, Pixel sea, Jelly wobble, Washing machine, Tube man, and the older Circle, Sound garden, Bubble tea, Long
  cat, Area, Radial, Wave, Levels, Campfire, Bunny hop, Fishbowl, Vibing cat, Popcorn machine, Jellyfish, Music box,
  coral Peaks, Hi-fi LED, Synthwave, Banana stack, Heartbeat, Oscilloscope, Turntable, Disco ball, Bars, Waves,
  Spectrum and Lite) read as Peaks. The **D-pad** moves over both groups (DOWN from a mode to the style under
  it, UP back), and moving changes nothing: **A** (or a tap) sets the mode or the style, and the top screen follows at
  once. **SELECT** steps the mode (Album art only → Art + visualizer → Visualizer only → None), **B** closes. The art is a square with rounded corners and a thin edge: 240 units on Album art only, 224 above the
  visualizer on Art + visualizer. A song without art shows a placeholder in the art's place (a colour wheel with a note on it), in
  the same layout: the mode you chose is the mode you see. Album art can be inside the song file or a `cover.jpg` in
  the same folder. A song with no cover shows a flat **record in its sleeve** in the theme's colours (on the top screen, by the artist line on the player page and on the dashboard's covers), placed one of four ways by the album's name so a grid of them is not one drawing repeated.
- **Ambient mode** (on by default; the row in Settings › Music & sound, and the foot of the **Display** sheet
  above). A song's art becomes the atmosphere behind the player page and the top screen: the art, blurred and muted,
  under a dark veil in the theme's own charcoal tinted by one hue from the art — deepened where the text sits, so
  every key, chip, bar and word keeps the theme's colours and its contrast. A new song's art cross-fades in over
  half a second, and fades out over half a second when the song stops (on both screens); until it is read the previous one stays. A grey, black, white or neon art, one whose hue sits
  opposite the theme's accent, or a song with no art at all, falls back to a plain ground (a light theme's its own
  background). The two screens share one picture, as if stacked into one tall portrait screen: the top screen shows
  the art's upper half, the bottom screen its lower half. While a song plays, the browser's pages (Home, Files, Music,
  Videos, Books, Extensions, Settings, Search…) sit on the same backdrop too, behind the rail, the header and the
  list, under a lighter veil than the player's (still 4.5:1 for text on the bare ground), with the hint bar a
  tinted glass the art shows through; rows and cards keep their own surfaces. A book's, comic's or Manga's pages, the photo viewer and a video
  never get one, nor does the High contrast theme. The maths runs on
  the cover worker; each artwork is kept in `<state dir>/artbg/` by its hash (16 MB at most, the oldest dropped
  first), so a song seen before needs no decode.
- **Y: List**, as on a video: **Up next**, and **Chapters** when the song or audiobook has them (**L2** / **R2** jump
  from either tab); a tap on the chapter line above the progress bar opens its **Chapters** tab. **Shuffle**'s switch
  and **Repeat** Off / All / One are in the sheet's header, and the song's fifth chip is a **Shuffle** switch of its own.
- **X: More**, as on a video (its first row is the song's five chips, then Lock touch, Screen off, Info, Stop, Like, Add to playlist…, Show in folder, Play on…).
  Shuffle and Repeat no longer share a key: each is set on its own (the **Shuffle** chip turns it on and off in place).
- **Shuffle** never reorders what you see (the folder, the playlist, Up next keep their order): **Next** and a song's
  end go to a random one not played yet in this round, **Previous** goes back the way it came. The song playing when
  you turn it on (or off) plays on. With **Loop all**, each round is followed by a new random one (never the same song
  twice in a row); with Loop off the list stops once every song has played; with **Loop one** the song repeats.
- Shuffle and Repeat are settings: they stay as they are for every folder, playlist or file you play next, here or
  from the web, and after a restart. The web sets them separately. They apply to videos as to songs, from the same
  List sheet (a video's **Repeat** chip steps it Off / All / One in place; a song's **Shuffle** chip turns Shuffle on or off).

**Long titles move** (the Title marquee): a title too long for its place is drawn from its start, its end fading
out (the text itself goes see-through, so the album-art backdrop or a card shows under it), and after 1.5 s moves left until its end shows, rests, fades back to its start and, 3 s later, does
it once more; then it rests at its start. It does this for the player's title (a song or a video), the song's title on the top screen (every Show mode; artist and album stay cut with "…") and the focused row
of a list (one pass, once the cursor has rested on it). Quick settings' Now playing block is the same marquee, in a
panel. One title moves at a time on a screen; a sheet or menu over it
holds it at its start, and it starts again on a new title, when the page opens or the screen wakes. A title that
doesn't fit also leaves out a start it shares with its artist, album, folder or playing list ("Yellow Album - 03 -
Sunny Peel" shows "03 - Sunny Peel") when that start ends at " - ", ": ", " | " or "_" and three letters or more are
left; lists, Search and Info keep the whole name. **Settings › Power & screens › Scroll long titles** (On by default):
Off cuts long titles with "…" as before.

Settings › Music has the same two choices as **Music display** and **Visualizer style**. The song's title, artist and
album (with its year) are always on the top screen (there is no setting), each line cut with "…" if it's too long.
With Album art only, Art + visualizer and None the top screen takes the theme: its ground is the theme's background (light
in a light theme) and the lines are in its text colours; every style alone (Visualizer only) is drawn on it too. With the art (Album art only, Art + visualizer) the art moves left (240 units; 224 above the band) and the three
lines sit to its right, centred on the art's middle; the art and the lines are centred together on their real width, so
a short title doesn't leave them hugging the left, and nothing is ever written over the art. Over Visualizer only the
lines are at the top left, outlined in the theme's background colour, with the visualizer passing under them; a long title's moving line fades at its
edges by the text itself fading, not by a band of background over the visualizer. An audiobook's or a song's
chapter is a line in the accent under the title. **None** shows the title in the middle (then "artist · album"). **Visualizer frame
rate** (30, 45 or 60 fps; 45 by default) is how often the styles draw (each frame on a steady beat of its own,
not when the sound's analysis happens to be ready): everything moves at the same speed at any rate, only less
smoothly at 30, which leaves the handheld more time for the sound.

## Sound: equalizer and effects

The equalizer and the sound effects change the sound of music: songs and audiobooks. Videos are never changed. Open
them from the **Equalizer** or **Sound FX** chip (or **More**, then that chip: each opens its own sheet, no tabs).
Settings › Music › **Sound FX** still opens the Sound page, whose two tabs are the same. In the sheets, **LEFT / RIGHT**
pick a band and **UP / DOWN** changes it,
**L2 / R2** step the presets, **Y** turns the equalizer on or off, **A** saves, and **X** opens Options. On the Sound
page itself the keys are the ones below. The Equalizer sheet is the full height a sheet may have (470 units), so
its ten bands are about 300 tall.

**Equalizer** shapes the sound with ten bands, from 31 Hz to 16 kHz, each boosted or cut by up to 12 dB.

- **LEFT / RIGHT** picks a band and **UP / DOWN** changes it. On the touch screen, drag a band's slider, and double-tap
  a band to set it back to 0 dB. Every change is heard at once.
- **Y / X** step through the presets, **SELECT** turns the equalizer on or off, **A** saves, and **START** opens
  Options.
- Built-in presets: **Flat**, **Bass Boost**, **Bass Cut**, **Treble Boost**, **Loudness**, **Vocal**, **Spoken Word**,
  **Rock**, **Pop**, **Jazz**, **Classical** and **Electronic**. The loud ones are turned down a little on their own,
  so they don't distort.
- Change any preset and it becomes **Custom**. It keeps playing, and it's remembered after a restart. **A** saves it:
  over your own preset if that's where it came from, or as a new preset of yours after a built-in, named on the
  on-screen keyboard (24 characters at most, and not a built-in's name). **Reset** (tap it, or Options › Reset)
  brings back the preset's own settings.
- Options holds **All presets**, **Save as new preset**, **Rename** and **Delete** for your own presets (Delete asks
  you to confirm), and Reset.

**Effects** puts a room around the music. Pick one with **UP / DOWN** and **A**, and you hear it at once.

- **None** turns the effect off. **Room**, **Concert hall**, **Church** and **Cave** are reverbs: the sound of a room,
  a hall, a church or a cave. **Wide stereo** spreads the sound wider than the speakers. **Headphones** (crossfeed)
  mixes a little of each side into the other, so it's easier on the ears.
- The effect is separate from the equalizer's switch: an effect plays with the equalizer off.
- The reverbs use a little more of the handheld's processor than the rest.

The top screen shows a small card when the preset, the effect, or the equalizer's on or off changes.

## Chapters

Videos (MKV, MP4, M4V) and audiobooks (M4B, which play as music) have chapters when the file
marks them:

- **L2** goes back: to the start of the current chapter if you're more than 5 seconds into it, otherwise to the
  chapter before. **R2** goes to the next chapter. On the last one it stops there and doesn't run into the next file
  (**R** does that).
- The top screen shows a card on each jump: **Chapter 3 of 12** and the chapter's title.
- The line above the progress bar on the Main page shows the chapter you're in (its title, or "Chapter 3 of 12" if it
  has none). Tap it for the list.
- That line, or the **Chapters** tab of **List** (**Y**), opens the chapters: every chapter with its start
  time, and the one playing marked. **A** (or a tap) plays from the chapter you pick, and **L2** / **R2** still work
  there.
- Marks on the progress bar show where each chapter starts.
- Audiobooks and songs with chapters also show the chapter under the title on the top screen.
- Nothing pops up over a film by itself: the card appears only when you jump with **L2** or **R2**.

All three are remembered.

## Settings

**START** opens Settings on ten tiles (Playback, Subtitles, Music & sound, Photos, Reading, Files & history, Power & screens, Web dashboard, Updates and About), two to a row: move with the D-pad (**UP/DOWN** a row, **LEFT/RIGHT** a tile),
open one with **A** or a tap, and **B** goes back to them; **B** or **START** at the tiles closes Settings, back to
where you were. Settings and its groups have the kit's header: the back key at
the left, then the page's title with its crumb (**Settings · 3 of 10**) under it on the same line, and a group's
neighbours named either side of **L** / **R** at the right (those keys step the groups). Seven rows fit under that
line. In a group, **UP/DOWN** chooses a row, **LEFT/RIGHT** changes it in place, and the
line above the bottom says what the setting under the cursor does. **Subtitle style...** at the end of the **Subtitles** sheet
opens the Subtitles group; **B** there goes back to that sheet, on Subtitles.

| Group | Setting | Default | |
|---|---|---|---|
| **Playback** | Remember position | On | Reopening a video continues where you stopped (songs always start from the beginning) |
| | Remember audio track | On | Keeps each file's audio track; a new file starts in the language you chose last |
| | Remember subtitles | On | The same, for subtitles |
| | Picture look | Off | A look on every video: Sharpen, Soften, Scanlines, LCD grid or Retro (a software filter, read when a film opens; songs and photos are left as they are) |
| | Max volume | 120% | How far the volume keys (and the player's **UP / DOWN**) go on the handheld: 100%, 120% or 150%. Past 100% (the handheld's own top) the player turns the sound up itself, 5% a press, for films mixed quiet; the volume card marks 100% and shows the rest in yellow, red past 120%, and the first time past 120% a warning says it can damage your hearing and the speakers. Down takes that off first. It starts again at 100% each time the player opens. The web dashboard's handheld volume goes as far; a browser playing the sound itself keeps its own 0–100%. Two volumes, each its own: while a browser plays the sound, the handheld's volume keys turn that browser's volume (the card says **Browser volume**) and leave the handheld's as it was; the dashboard's volume shows both then (**Browser volume**, **Handheld volume**), and moving one never moves the other |
| | Button hotkeys | On | Off: buttons can't pause, skip or close while something plays (touch still can) |
| **Subtitles** | Font, size, colour, border, border colour, text encoding | Inter, Medium, White, Normal, Black, Auto | How plain subtitles (SRT, VTT…) look, with a live preview. Styled subtitles (ASS/SSA) keep their own look. **Text encoding** is how a subtitle file's letters are read, on the handheld and in the browser: Auto reads UTF-8, and guesses an older code page (a file named for its language, like `movie.sl.srt`, tries that language's first); if letters like š, č or ж still come out wrong, pick the file's: Central European (Windows-1250, ISO-8859-2, DOS 852), Cyrillic (Windows-1251, KOI8-R), Greek, Turkish, Baltic, Hebrew, Arabic or Western. A subtitle showing changes at once. |
| **Music & sound** | Music display, Ambient mode, Visualizer style, Visualizer frame rate | Album art only, On, Peaks, 45 fps | What the top screen shows for music, and which visualizer (both also on the **Display** sheet while a song plays). **Ambient mode**: a song's art, blurred and tinted by the theme's own ground, behind the player and the top screen (a switch, the same one at the foot of the **Display** sheet). **Sound** opens the [Sound page](#sound-equalizer-and-effects): the equalizer and the effects |
| **Photos** | Keep zoom between photos | On | The next photo opens at the same zoom and place. Off: each opens whole |
| | Pan area | Left | Which side of the bottom screen the photo's pan area is on |
| | Drag moves | The box | A drag in the pan area moves the box over the small photo, or the photo itself (like a phone) |
| **Reading** | Font, Text size, Line spacing, Margins, Colours | Serif, Large, Normal, Normal, Match theme | The defaults for every TXT and EPUB; each book's own Text options (START) still overrides them for that book. Five text sizes; Medium is the smallest that reads comfortably on the handheld |
| | Text opens as | Scroll | Scroll down the page, or Book: held sideways with a page on each screen |
| | PDF opens as | Single page | Single page, or Strip: a PDF's pages one after another |
| | Dim the controls while reading | 1 minute | A book, comic or document open: this screen dims after that long untouched; a touch lifts it and still turns the page. Never turns it off |
| **Files & history** | Remember last folder | Off | The file list opens where you last were |
| | Show hidden folders | Off | Shows folders you've hidden (they contain a `.hidden` file), so you can unhide them |
| | Keep play history | On | Keeps Recent. Turning it off also deletes the history |
| | Network files in Recent | On | Files played from a network folder go in Recent too. Off: they're left out, and the ones already there are removed |
| | Clear play history | | Press **A** (or tap) twice. Positions you stopped at are kept |
| **Power & screens** | Theme | Dark | The app's colours on both screens and on the Web dashboard, eighteen: Yellow (the look before themes), Dark, Midnight blue, Dark purple, Dark red, Pink, Coffee, Mint, Retro green, Sunset, Candy, 16-bit, 16-bit Lavender, 32-bit, Sakura (cherry-blossom pink, light), Gundam (a white mobile suit: a grey-blue ground, one blue for keys and the accent, a navy rail and hint bar, the yellow where you are and on the chosen key's edge, a red Play/Pause and main key; light), Light (warm paper, for sunlight) and High contrast. Every theme but High contrast is drawn Clean: the page's pieces have no edges, a quiet fill says what is pressed, and the keys stay keys (a key keeps its Classic edge, lip and solid fill). High contrast is always Classic, with its edge and lip on everything. **LEFT/RIGHT** applies it at once, both screens and any open sheet redrawn; dashboard pages follow with their next state update (and open in it next time). Videos, photos, album art, visualizers, subtitles, the QR code and a book's page keep their own colours; the cards on the top screen (volume, chapter, notes, Playing in your browser) stay dark in every theme. With nothing playing the top screen (the icon, the name and its line) is on the theme's background too. While the row is focused, the top screen previews it in the theme's own colours: every theme as a swatch in three rows of six (the one in use outlined), a Play key, a Back key, a focused row, pills and a bar as they look now, and a small card that stays dark; it follows LEFT/RIGHT. It shows whatever plays: a video or a song goes on underneath (its sound too), and its picture is back the moment the cursor leaves the row or Settings closes; a volume or brightness card still shows over the preview. Not in Quick settings |
| | Brightness (or Top and Bottom brightness) | as BananaOS has them | The screens' backlight, 10% to 100%: the same levels as BananaOS's own Settings and the brightness keys, kept after a restart |
| | Same for both screens | On | One brightness for both screens; off, each its own |
| | Screen off (music) | 30 seconds | While a song plays, the bottom screen turns off by itself after that long without a touch or a button: Never, 15 or 30 seconds, 1, 2 or 5 minutes. Not while paused, and never while a photo or a comic is shown. A touch or any button brings it back |
| | Screen off (video) | 30 s | The same while a video plays; Never keeps its seek bar, tracks and subtitles lit and ready to touch |
| | Top screen off (music) | Never | While a song plays, the top screen goes black after that long without a press or touch; any press or touch, or the next song, brings it back. Drawn black: the backlight is untouched |
| | Dim before turning off | On | The last 5 seconds before Screen off: this screen dims smoothly to a third of its brightness, as a phone about to turn off (no countdown; the backlight is lowered for the moment only, never the level BananaOS keeps; on a computer a dark veil stands in); a press or touch then only brings it back to full and does nothing else. The level comes back on a wake, at Screen off and at the quit |
| | Stay awake for copies and downloads | On | A copy, a move, a chapter download or an update holds off the handheld's own sleep until it is done |
| | When the lid closes | Pause | What closing the lid does, with something playing or stopped. See [Lid and sleep](#controls) |
| | Power key, lid shut | Play / pause | With the lid shut and **Keep playing**: Play / pause (a press plays or pauses, a 2 s hold sleeps) or Sleep (the key stays BananaOS's: a press sleeps, as with the lid open). At 45% with its reason while the lid is set to Pause |
| | Volume keys, lid shut | Volume and skip | With the lid shut and **Keep playing**: a press is one volume step; Volume and skip holds 0.6 s for next (+) / previous (-), Volume only holds as with the lid open (the player never takes the keys). At 45% with its reason while the lid is set to Pause |
| | Shoulder keys, lid shut | Ignored | With the lid shut and **Keep playing**, what **L**, **R**, **L2** and **R2** do: Ignored (nothing), Hold (act when held 0.6 s), or As usual (act at once). At 45% with its reason while the lid is set to Pause |
| | Lid or sleep in incognito | Close all | While incognito is on: what the lid closing or the handheld sleeping does. Nothing, Close all (stop, close books and photos, Home, leave incognito) or Quit. See [Incognito](#quick-settings) |
| | When the sleep timer ends | Sleep | What the [sleep timer](#sleep-timer) does at its end. Sleep: it pauses, both screens turn off and the handheld sleeps. Pause: it pauses and the screens turn off; the handheld stays awake |
| | Sleep timer snooze | 10 min | How much longer it plays when you press **A** in the sleep timer's last minute (or **10 more min** on the web page): 5, 10, 15 or 30 minutes |
| | Battery percentage | Off | Shows the battery's percent beside its icon (the icon, with a bolt while charging, is always there) |
| | Clock | 24-hour | The time in the top corner of the bottom screen, left of the battery: 24-hour (`14:32`), 12-hour (`2:32 PM`) or Off. Not over a photo |
| **Updates** | Check automatically | On | Looks for a new version a few seconds after the start, in the background (Wi-Fi needed) |
| | Check for updates | | Looks now, and shows what it found |
| | Update to … | | Downloads the new version (shown when there's one), then **Restart now** |
| **Web dashboard** | Turn on or off, New PIN | Off | Control the player and manage files and playlists from a phone or computer on your Wi-Fi. See [Web dashboard](#web-dashboard). While incognito is on the tile says **Off in incognito** and the page offers **Leave incognito** instead |
| **About** | Version, licence, warranty, the manga-sources note, the libraries, and the support line | | The last group (a tile; there is no About tab): at its foot, under a rule, **Banana Player is free. If you’d like to support it:** with a static QR (nothing is fetched or opened by the handheld) and **ko-fi.com/tonysong** written out. **Y** or a tap on the code shows it large on the top screen, as big as fits with a white margin, the address under it; **B**, **Y** again or a tap closes it, and leaving About does too |

## Web dashboard

Turn on the web dashboard and open the handheld's address in a phone's or computer's browser. From there you can
control what's playing, look through the card, upload and organise your files, and make playlists. It's off until you
turn it on.

### Turning it on

- **Settings › Web dashboard**, or **Quick settings** (F) › **Web dashboard** (a switch tile), or the Network section's
  **X** › **Web dashboard…**.
- The handheld's page shows a **QR code**, the **address** (for example `192.168.1.23:8080`) and a four-character
  **PIN**. Scan the QR code with your phone's camera: it opens the dashboard and signs you in. Or type the address into
  a browser and enter the PIN.
- The phone or computer must be on the same Wi-Fi as the handheld.
- On the page the **Web dashboard** switch is the one control: **A** (or a tap on its row) turns it on or off. With
  it off the page says what it is for and nothing else. **No Wi-Fi** keeps the switch on and hides the QR, address and
  PIN until there is an address; a port already taken says **The dashboard couldn't start**, with **Try again**.
- One tab per browser: opening the dashboard in a second tab doesn't connect it; it says **Banana Player is open in
  another tab** and asks **Switch here**. Switching moves it there (the other tab stops playing and offers the same).
  Uploads under way in the tab that steps aside still finish.
- It stays on across restarts until you turn it off. The PIN stays the same too, until you press **New PIN** on the
  handheld (press it twice). That signs out every browser.
- While the QR code is on the screen, the bottom screen turns off after 90 seconds without a press (a touch brings it
  back).

On the handheld, a **globe** beside the battery shows the dashboard is on: dim while no browser is connected, yellow when
one is. Tap it to open the page. When a browser signs in, a note pops up on the top screen.

Nothing runs while the dashboard is off. While it's on and idle it uses next to no battery, and a browser tab that's
hidden disconnects.

**Incognito:** while it's on the dashboard's server does not listen. Its switch keeps the value you set, and the server
comes back by itself when you leave incognito, with the same PIN and the browsers still signed in. Quick settings' tile
and Settings' tile say **Off in incognito**, and the page shows the switch at 45%, the reason, and **Leave incognito**
(Y). A browser that was connected shows **The dashboard is off** (it is never told why) and tries again quietly; a page
that was playing stops, its place kept on the handheld. With a browser signed in, turning incognito on asks first.

### Security

- Only devices on your home network (private addresses) can connect. Nothing outside it can.
- Wrong PINs: after five wrong tries from one device it waits 60 seconds before trying again, longer each time. After
  30 wrong tries in total the PIN changes and the handheld says so.
- A browser stays signed in for seven days.
- The connection is **not encrypted** (plain HTTP, no certificate). Use it on your home Wi-Fi only.
- The browser can only reach your media folders on the card. The player's own settings and the BananaOS system folders
  are never shown.

### In the browser

Five sections, as the handheld's rail has them: **Home**, **Files**, **Videos**, **Music** and **Books** (the reader's
**Manga** joins while the Manga reader is on). The dashboard opens on **Home**. At the foot of Home,
one dim line says the version: **Banana Player 0.7.27 · Source · Support Banana Player**, the last two plain links
opening in a new tab (the repository, and ko-fi.com/tonysong; nothing is loaded from there and there is no QR on the
page). On a phone they're the tab bar at the bottom, with a small
player floating over it; on a tablet an icon rail on the left and a player bar at the bottom; on a computer a sidebar
(the sections, a shortcut list of playlists, and a card with the handheld's battery, every storage it has (the
internal one and each card in it: free room, a bar of what is used, used of total, red when nearly full) and **Sign out**) and
the player bar. On a phone, **Sign out** is behind the handheld button at the top right of the sections. The player
itself is not a tab: the small player and the player bar open it, and it keeps playing as you move around.
A video playing in this browser goes on, picture and all, when you leave it: it plays in the small player's
(or the player bar's) cover tile, without its subtitles; a tap on it opens the player again, where it carries on
full size.

**Videos** is the handheld's Videos page: the same three views (**All**, **Folders** — a folder row opens its files
in the Files tab — and **Not finished**, newest first), the row's place and size on it, and the view's own list when
you play one (in the browser or on the handheld, as from Files). **Music** is the handheld's three views: the
**Albums** grid (a tap opens the album, with **Play** and **Shuffle**; a song's row plays from that song), the
**Artists** list (a tap opens their songs, by album), and **Playlists** — the playlists live here now, not as a tab
of their own (the sidebar's shortcut list still goes straight to one). With no Music folder on either card the page
says so and offers **Upload to Music**. **Books** is the shelf: every EPUB, PDF, comic and text file from the
Books and Documents folders; a tap **reads the book in the browser** (below), its **⋯** offers Continue here / Open
on handheld / Download and says who read it last.

**Reading in the browser** (Books, `#/books/read?p=…`; docs/redesign/web-books.md). The reader is the whole page;
a tap in the middle shows its bars (‹ back, the title, ⋯; a slider, where you are, and "Shared with the handheld" /
"Saving…" / "Not sent yet"). The left and right thirds, a swipe, ← → / Space / PageUp / PageDown turn the page (a
right-to-left comic turns the other way); Home / End, `[` `]` (a text's chapter), `C` Contents, `D` Bookmarks, `T`
Text, `+` `−` size, `F` full screen, Esc leaves. An **EPUB or TXT** is laid out from the handheld's own text blocks
(the publisher's styling is not kept, as on the handheld) in pages, two columns from 900 px, with its own Light /
Sepia / Dark paper, Serif / Sans and size, kept in that browser; Contents and Bookmarks (shared with the handheld's).
A **comic** (CBZ, CBR, a picture EPUB) shows the archive's own pictures, the next two fetched ahead. A **PDF** is
drawn in the browser by pdf.js (bundled: no internet needed). **One place per book**, the handheld's: it opens
where it was last read on either device ("Picked up from the handheld: … · Back to …" when that was elsewhere), a
turn is saved 1.5 s later (at once when the page hides), and the other device's turns are a note with **Go there**,
never a jump. Read on while the handheld couldn't be reached and on the handheld too: **Two places in this book**
asks which to keep (the furthest first; the other becomes the bookmark "Other place", or a note back to its page).
**Open on handheld** (⋯) opens the book there at the browser's place and the page follows it (**Read here
instead**). On the handheld, a browser's place in the book it has open is the first row of Contents (**Y**: "Where
Safari on iPhone is · 11%"; a PDF's or comic's options), with no note over the page. An open reader doesn't keep the
handheld awake: only its requests count as use.
The **Text** sheet also has **Spacing** (Tight / Normal / Loose), **Margins** (Narrow / Normal / Wide) and **Layout**:
**Pages** or **Scroll** (one long column; the next part follows as the end nears, **Previous part** over the first;
the place is the block at the top). A comic or PDF has **View** (the top bar, or ⋯): **Pages** One / Two / Strip
(`1` `2` `S`; Two, the default, only in a landscape window 1000 px wide or more: the cover alone unless **Cover ·
Paired**, a wide page alone, the place kept is the first of the pair; Strip: one long scroll), **Fit** Page / Width /
Height / 100% (`W` `H` `0`), **Reads** left to right or right to left (this browser's, per book). Zoom to 4×: pinch,
Ctrl+wheel, `+` `−`, a double tap (to the width and back); a zoomed page drags or scrolls, and a swipe turns only at
its edge. **Pages** (`G`, the grid key) shows every page small, the one read marked; a tap goes there. On a computer
(1100 px and wider) the bars stay, with ‹ Books, the title (a comic's pages and direction), the keys (a text:
Bookmark this page `D`, Text `T`, Contents `C`; pages: View, Pages) and **Open on handheld**; in full screen they hide
until the pointer moves. A text's **Contents** and **Bookmarks** are pinned at the side from 1200 px (`C` shows or
hides it). **Home** lists books too (a **Books** filter): its Continue card for a book asks **Read here** (at its
place) or **Open on handheld**, and a book's card or row reads it here (a network book opens on the handheld). Not
yet: upload ghosts and the card choice beside Upload.

**Playing** controls the music or video. Where it plays is a two-part switch at the top (**Handheld** · **This
browser**), and in the player bar a button that opens the same choice, saying what happens (for example "pauses the
handheld, carries on at 1:27"):

- **On handheld** (the default): controls what the handheld plays. Play and pause, previous and next, seeking, back 5
  seconds and forward 10, loop, shuffle (see below), volume, the equalizer (presets and bands), the sound effects, chapters, and what's
  up next.
- **In this browser**: plays the file on the phone or computer, and pauses the handheld. Here there's no equalizer,
  effects or handheld volume (a volume slider for the browser instead), and the chapters are the browser's own.
  Also on iPhone and iPad.
- While the browser plays, the handheld shows **Playing in Chrome on Mac** (the browser and device, as the browser
  describes itself; or **Paused in …**) and its bar
  runs on with the browser's time. **A** (or a tap on **Play**/**Pause**) on the handheld plays and pauses the browser;
  the handheld stays quiet. **Hold A** (or hold the Play button) for 0.6 s to play it on the handheld instead, from where
  the browser was: the button fills as you hold, the top screen says **Playing on the handheld**, and the browser stops.
  The top screen's card says **Hold A to play here** on the player, and **Go to the player to play it here** on any other
  page (where A does that page's own thing).
  **L / R** on the handheld move the browser's list on, however fast they're pressed: every press counts, and both end
  on the same file (**L** 5 s or more into the browser's song restarts it first; a video always goes to the one before).
- **Where it plays is one choice for the whole system**, the switch's: anything opened plays there, whichever device
  it was picked on. With **This browser** chosen, a song or video opened on the handheld (from the list, Recent, a
  playlist, Play folder, Shuffle play, Quick settings' block) or in another browser tab plays **in that browser**, in the
  handheld's list, and the handheld holds it paused, as if the browser had picked it. Picked fast, the last pick wins.
  Only the switch, **Continue here**, **Play here** / **Play on handheld**, the handheld's **Play on** and holding **A**
  change the choice: Stop (on either side), the end of a list, pausing, a photo, a dropped connection and a file the browser can't play leave it
  as it is. Switching in another tab moves it to that tab.
  A file the browser can't play (AVI, AC3/DTS, WMA, APE…): picked in the dashboard (a folder, Recent, a playlist, a
  row of **Up next**), it asks first, **Play on handheld** or **Cancel** (the default); reached by **Next**, **Previous**
  or the end of the one before, it's skipped in the browser (**… isn't supported in this browser · skipped**) and the
  list goes on there (backwards after Previous; a list none of which plays in the browser stops once each was tried).
  Network files play on the handheld once, with the browser still chosen for the next one. A photo opened on the handheld while the browser plays leaves the browser playing;
  closing the photo, the handheld holds the browser's file again. Once the chosen tab has been closed (or lost its connection) for 10 seconds, the
  handheld is chosen again: what the browser played waits there paused at its place (**Your browser went · paused
  here**), and **Play** carries on. **Hold A** is still the way to bring the sound back to the handheld. A tab that's
  closed (or reloaded, or sent to another site) tells the handheld as it goes, and then the handheld waits 3 seconds,
  not 10 (a reload is back well within them). A tab that loses its connection without a word is found at the next
  keep-alive, then the 10 seconds.
  Leaving the handheld's player (**B**) changes nothing for the browser: its mini bar reads **Playing in Chrome on Mac**
  (tap its mark to play or pause the browser, hold the mark to take it over). **Stop** on the handheld stops the browser
  too (the browser stays chosen: the next pick plays there). **Stop** in the browser unloads the handheld's copy too.
- <a id="play-on-from-the-handheld"></a>**Play on, from the handheld** (**X · More** › **Play on…**, shown while at
  least one signed-in dashboard page that can play is open, not over a photo): a list of where it can play, the
  **Handheld** first, then each open page by its browser and device (**Chrome on Mac**, **Safari on iPhone**,
  **Firefox on Windows**…; a second one of the same kind gets **2**; one that doesn't say reads **Browser**). The one
  playing now is ticked (**Playing now**, **Paused there**). **Up/Down** and **A** (or a tap on a row) pick, **B**
  goes back. Picking moves it exactly as the dashboard's switch does: the same place, the same audio and subtitles,
  and the picked one is where picks go from then on. The picker closes on the playing screen.
- **A browser that won't start the sound by itself** (Safari, and Chrome on a page nobody has tapped yet, refuse sound
  a tap didn't ask for): the page shows one big **Tap to play** over the picture ("The handheld sent Sintel here · Your
  browser needs one tap to start the sound"; over any page, if the player isn't showing). Meanwhile the handheld's top
  screen says **Waiting for Chrome on Mac** · **Tap Play here on that page to start the sound** · **B: play on the
  handheld instead**, and its status line **Waiting for Chrome on Mac**. **B** on the handheld (or **Handheld** in
  Play on) plays it there instead, from the same place; **A** pauses it. One tap on the page plays it and the waiting
  ends.
- A dashboard in another tab or device, while a browser plays, is a remote for it: **Play**/**Pause**, seeking,
  chapters, **Previous**/**Next** and **Stop** act on what that browser plays (the sound stays there).
- While the handheld plays something behind its list (its player left, not stopped), the dashboard's player says
  **On handheld · in the background**, and its controls still drive it.
- Switching the target while something plays carries on from the same place, with the same audio track and subtitles
  (a second language, English subtitles: chosen on one side, they're what the other plays; one the browser can't play
  plays its default there and comes back on the handheld). A paused file stays paused where it goes.
- A file the browser can't play (AVI, AC3 or DTS sound, WMA, APE and so on) says **Plays on the handheld only**, and
  asks before playing it there.
- A file the handheld plays from a network folder shows here too, marked **Playing on your handheld via the network**
  (and "From *folder* on *connection*"): its controls work from the page (play, pause, seek, Next and Previous, Up next,
  audio and subtitles, speed, volume), but it can't move to this browser (**Network files play on the handheld only**).
- **Tracks** in the browser (a video's audio and subtitles), as Plex does it: the video stays the file itself (Direct
  Play) whenever it can.
  - **Audio:** every track of the file is listed. Safari switches in the file itself. Other browsers play the file's
    default track as it is (**Direct Play**); another one comes as a **Direct Stream**: the handheld sends the video and
    that track copied into a stream, never converted, so the picture blinks for about a second as it switches, a seek
    takes a moment, and **Details** says **Direct Stream**. The default track again is the file again. A track the
    browser can't play (DTS, TrueHD, PCM…, or AC-3 where the browser has no AC-3) says **Plays on the handheld only**
    and offers **Play this one on the handheld**.
  - **Subtitles:** the file's text tracks (SRT, WebVTT, MP4 text) and the files beside it (`.srt`, `.vtt`); the first
    time an embedded one is chosen it's read out of the file (**Preparing subtitles… 40%**), then kept. Styled ones
    (ASS/SSA, inside or beside the video) are drawn in the browser with their own look, positions, effects and the
    fonts the file carries. Picture subtitles (Blu-ray PGS, DVD VobSub, DVB; inside the video, or a `.idx`/`.sup`
    beside it) show too: the handheld turns about a minute of them at a time into pictures, ahead of where you are,
    placed where the disc puts them on the picture (fullscreen too). After a jump they appear within a moment. **Size**
    and **Position** set how plain subtitles look in this browser; styled and picture ones keep theirs.
  - **On the video** (inline and fullscreen, phone and computer): **CC** turns subtitles off, and on
    again with the ones you had (in the next file: the same ones, else the same language, never signs-only first);
    **C** on a keyboard does the same. The **gear** opens **Audio track ›**, **Subtitles ›** and **Speed ›** (0.5× to
    2×, kept for the next file): the one playing is ticked, and the ones that can't play here are dimmed with the
    reason (a tap on one offers the handheld). Arrows move, **Esc** or **←** go back, **Esc** closes.
  - When a Direct Stream doesn't play, it says why (**this browser can't play this video's format in a new stream**,
    or **the stream from your handheld broke off**), goes back to the default track, and the handheld's log keeps
    what the browser said.
  - The audio track and subtitles go with the video when the target switches, both ways, and stay through a reload
    of the page; one the browser can't play starts on its default track, said so.
- **Up next**, **Chapters**, **Tracks** (audio and subtitles), **Sound** (the volume, the equalizer's presets and its ten
  bands, the effects) and **Details**: on a phone three keys under the controls open them as a sheet; on a tablet
  they're a card under the player; on a computer a column beside it, while the controls are in the player bar.
  **Up next** shows each item's length: a playlist's own, else (a folder, this browser's queue) asked of the handheld
  for the rows as they scroll into view.
- **When a list ends** (a folder, a playlist, Recent with its folder, a selection; played to its last item with Loop
  off, in this browser or on the handheld): a song doesn't close. It stays, back at **0:00**, paused, and **Play** (or
  **A** on the handheld, or the power key with the lid shut) plays it again from its start. A video's end keeps the
  player on it at its end, with **The end of <list>** and **Clear** above it. **Play** plays the list again from its
  first item, **Previous** the last one again (**Next** has nothing to go to), and **Up next** keeps the whole list, the
  last one marked **Finished**: a tap on any row plays the list from there. It plays where you play (this browser, or
  the handheld). The player bar and the phone's small player show it too (**Finished · <list>**; a tap opens the
  player). Nothing starts by itself; **Clear**, or **Stop** on the handheld, leaves **Nothing playing**. The handheld's
  own screens go back to the list for a video. **Shuffle** and **Loop** still work there; with Shuffle on, **Play**
  starts the list again at a random song.
- **Shuffle and Loop** are the handheld's two settings, whichever side plays, and every page and the handheld show the
  same (even with nothing playing: the buttons stay in the player bar or the phone's empty player). **Shuffle** turns
  it on or off without a break in what plays; **Loop** goes off → all → one. They combine freely: Shuffle with Loop
  all plays on for ever, a new random round each time; Shuffle with Loop off stops after every song once. Up next keeps
  the list's own order; a row tapped plays from there, random again after it.

**Files** lists the card's folders:

- **A tap** on a file plays it on the handheld or in this browser (where you last played). A song keeps you on the
  list, with the small player showing it; a video opens the player, to watch it.
- **Upload files** and **Upload folder** in the folder's toolbar (no Upload folder on iPhone or iPad), or drag files
  and folders onto the page (a tablet or a computer). While files go up, a round key at the bottom right shows how
  many (**5/12**) and a ring fills by the bytes sent; a failed file shows red on the ring and a red badge. A tap on it lists each file:
  cancel one, **Retry** a failed one, **Pause** (what's on its way starts again from the beginning), **Cancel all**.
  When all are up, the key ticks and a note says where they went, with **Show**. Uploads carry on while you move
  between pages, and the key stays on every page (not over the player) while they run or failed.
- **Files' first page** lists every place the handheld has, as its own Files does: each storage with its free room, a
  bar of what is used and `used of total` (red when nearly full), and the Videos shortcut with where it is. A card
  put in or taken out shows within a couple of seconds, without a reload.
- **New folder**, **Rename**, **Move**, **Copy**, **Download**, and **Delete** (permanent: it asks first and can't be undone).
- **Move…** and **Copy…** (a row's **⋯**, or the selection's band) open a folder picker. With a second card in, a
  choice at its top switches between the handheld's places (**Internal storage**, **SD card 2**), so files and folders go
  from one card to the other. Nothing is ever replaced: an item whose name is taken there is left where it was, and
  **Copy here** is off in the item's own folder. A move on one card is instant. A copy, or a move to the other card,
  runs on the handheld and carries on with the page closed: a round key at the bottom right fills by the bytes copied
  (**53%**), and a tap shows what it's copying, how much of how much, and **Cancel**, which removes what was half
  copied. A move removes the originals only once their copies are in place. It checks the room first (and that no file
  is over 4 GB for a card that can't hold one), and a toast says how it went, with **Show**.
- **Play** a file or folder on the handheld or in this browser, or **Add to playlist**.
- **Select** several files and folders to do any of these at once (or a long press on a row; on a computer, the **Select**
  button, or the box that fades in over a row's icon after a moment's hover). A band of actions floats at the bottom. Rename works on one item at a
  time.
- **Up a folder** (the first row, **..**, and the arrow beside the folder path) goes to the parent folder;
  on a computer **Backspace** or **Alt+Up** does too. With two cards in, a card's top goes up to the list of cards
  (the path starts with **Cards**); with one, its top has none.
- Songs and videos on the card show their length before their size (`3:34 · 3.5 MB · Nov 10, 2026`), asked of the
  handheld for the rows as they scroll into view (none for the formats and files the handheld's list shows none for).
- On a computer, **Filter this folder** narrows the list as you type (**/** goes there).
- Any kind of file can be uploaded and named (a book, a PDF, a note, a file with no extension); only a name starting
  with a dot is refused (those are the player's own). The page lists every file, so you can see and delete what you
  put there, but Play, Like and Add to playlist are offered only for songs and videos (photos open in the viewer).
  What isn't music, video or a photo never reaches Up next or a playlist: a folder plays and adds only its media, and
  Add to playlist on a file that isn't one says so.

**Playlists** are the same playlists as the handheld's Playlists section:

- Create, rename, delete (the files stay where they are), and open a playlist. The page shows them as cards. Past 12
  playlists: the ones played or added to lately in a **Recently used** row, then **Find a playlist** (**/** on a computer;
  Enter opens the first match), a sort (**A–Z**, **Recent**, **Size**) and **List** or **Grid**. The list (the default
  there) has a letter heading for each letter. The browser remembers the sort and the view. The computer's sidebar
  lists Liked and up to five recently used playlists, then **All N playlists**.
- Drag to change the order, remove items (with **Undo**), add files from **Files**.
- **Play** or **Shuffle play**, on the handheld or in this browser.

**Like and Add to playlist** in the browser:

- **The heart** likes what's playing, or unlikes it. On a phone it's beside the title, with **⋯**, and on the small
  player; on a tablet or a computer it's in the player bar at the bottom, after the title, with an **Add to playlist**
  button. Playing in this browser, the heart is that file's.
- **Add to playlist**: the player's **⋯** on a phone, the bar's button on a tablet or a computer. A playlist that already has it
  says **✓ Added**; choosing it again asks **Add again**, and a second click adds a second copy. **New playlist…** makes
  one from the list itself. With more than 7 playlists there's **Find**: what doesn't match is dimmed, and Enter adds to the
  first match, or makes a playlist of that name if none matches. The playlists you added to last come first, marked
  **Recent**. Liked is never listed.
- A song or video's **⋯** in Files has **Like** / **Unlike** (with **Undo**) and **Add to playlist…**; photos and
  folders have no Like. Liked files have a small yellow heart after their name. **Add** in a selection adds them all.
- **Liked** is pinned at the top of Playlists. It's newest first, and the dashboard doesn't reorder it (the handheld
  can): each row has a heart that unlikes
  it (with **Undo**, which puts it back at the top), and **⋯ › Select…** unlikes several at once. A network file in it
  plays on the handheld only, and a file gone from the card is marked **Gone**.
- A like on the handheld shows up at once: the heart fills, and on the Liked page the new row comes in at the top, lit.

**Home** is where the dashboard opens: what you played or read last, on the handheld or in this browser, ready to
pick up. It is the handheld's Home Recent strip, in the browser.

- **Continue**, at the top: the newest item, large, with one yellow button for what comes next: **Resume at 4:12** for
  a video left part-way, **Play** for a song or something played to its end. Beside it **From the start**, the heart (**Like**) and **⋯**. A line under the buttons
  says where it will play.
- **Play on** (**Handheld** · **This browser**), at the top of the page, is the same switch as the player's: a tap on
  anything in Home plays there.
- While something plays (picked here, on the handheld or anywhere else), that card is **Now playing** (or **Paused**)
  and where, with its time running, and its buttons are **Open player** and **Pause** / **Play**. A video playing in
  this browser plays right in the card (a tap opens the player); the small player and the player bar keep only its
  picture then, and have the video again on every other page.
- A tap on an item in Home plays it in that card and stays on Home (a video too).
- **Recent**, below: everything older, newest first (up to 100 of each kind), and while something plays, all of it (what plays
  marked **Playing**). The order stays as it was when Home was opened: playing from it, or the next song starting,
  doesn't move the rows (a removed one goes); what was played comes to the front the next time Home is opened. The
  handheld's Home strip and All recent do the same. **All**, **Videos** and **Music** choose
  what shows, and **Grid** or **List** how (a grid on a tablet or a computer, a list on a phone, until you choose; the
  browser remembers). The first 8 show (6 on a phone), then **Show all**.
- The cover's shape tells the kind: a video fills the frame, a song is a square sleeve. Each item says where it was left: **0:27 / 0:52** with a
  yellow bar, or **Played**. What plays now is marked **Playing**.
- **Books, PDFs, comics and text files** aren't listed here: they're read on the handheld's screens only, and its own
  Home keeps them with their page.
- **Network files** are marked **Network** and play on the handheld only, whatever **Play on** says (with **This
  browser** chosen, a note says so). They have no Like, Add to playlist or Show in Files here.
- A file that is gone is dimmed, marked **Gone** (or **Card out**) and says why (**Not on this handheld any more**,
  **Card 2 is out**); a tap opens its sheet with the reason and **Remove from Recent**. **⋯** at the top **› Clear
  missing** removes the deleted ones, on the handheld too.
- A song plays in the list it played from (a playlist, an album, an artist, a folder, Play random), on the handheld or
  in this browser, as on the handheld; a list gone since plays the song alone, said in a toast.
- **⋯** on an item (right-click on a computer): **Resume on handheld**, **Resume in this browser**, **Play from the
  start**, **Like** / **Unlike**, **Add to playlist…**, **Show in Files** and **Remove from Recent** (with **Undo**;
  the place it was left at is kept, as on the handheld).
- **⋯** at the top **› Clear Recent…** (asks first) empties it, on the handheld too; the places your videos were left
  at are kept.
- **Quick actions**, at the bottom: **Shuffle Liked**, **Upload files** (to the folder you were last in, or the card)
  and **New playlist**.
- It follows the handheld live: a file played, removed or cleared on either side shows on the other at once.
- **Nothing played yet:** a welcome instead, with **Browse Files** and **Upload files**, four ways to start (Files,
  uploading, a playlist, and network folders, which are added on the handheld: **Network › Add connection**), and how
  the handheld and the browser work together.
- **Play history off** (the handheld's **Settings › Files › Keep play history**): Home says so and where to turn it
  back on, with the same ways to start.
- The old address **#/recent** opens Home.

**Manga** appears in the nav only while the reader is on, with three pages:

- **Library**: your series as cards. Opening one lists its chapters — read or unread, the page you were on, and its
  download state (**Queued**, its pages done, **Paused**, **On card**) — tick the ones you want and press **Download
  selected**, or take every unread one with **Download all unread**; a series from your card says its chapters are
  already on the handheld. The bin takes it out of your library (the files stay where they are).
- **Sources**: a **Repositories** card — paste an address and **Add**, each one with **Refresh** and **Remove** (its
  installed sources stay), and the handheld's note under the card — then **Installed** (**Update** when there's one,
  the bin to uninstall, which asks first) and **Available** (**Install**). Adult sources are left out unless **Adult
  sources** is **Shown**.
- **Downloads**: the queue — each chapter with its pages done, pause or resume one, **Y** removes a failed or queued
  one (again to confirm), **Clear failed** removes every failed chapter and its partial pages, **Pause all** /
  **Resume all**, and a **Low on card space** note when the card is nearly full. Downloads are started here too, from
  a series' chapter list in **Library**; with none queued the page reads **Queue chapters from a series page**.

Changes show up live on both sides: what you do on the handheld appears in the browser, and the other way round.

Not yet: network folders in the browser, a grid of photo thumbnails, and saving equalizer presets from the browser.

## Extensions

**Extensions** is the rail's section for everything that adds to Banana Player: its page lists the extensions that are
on, and under a rule **Manage extensions**, which lists each extension with what it does and an **On/Off** switch
(**A**). **X** (or the gear) opens the extension's own settings page: for the reader, **Manga settings**, a page of the
Manga app's (not one of Banana Player's Settings). Turning one off hides it and stops it; nothing is deleted. Settings
has an **Extensions** group tile that opens the same Manage extensions page, and **B** returns to the door it came
through.

- **Manga reader**: comics and manga from the files on your cards, from your servers (Komga, Kavita, OPDS), and from
  sources you install from repositories. The first time it's turned on, a sheet says where to find it and that
  repositories are added by you: Banana Player doesn't provide or check their content.

### The Extensions page

- A row for each extension that is on: the **Manga reader**'s mark, its name, a line of what is in it (the series
  read so far and downloads, once the reader has been opened) and a chevron; then a rule and **Manage extensions**.
  **A** (or a tap) opens the row; **UP/DOWN** move; **B** asks to quit (the section's home); **L2/R2** step the sections; **L3** is Search, as
  everywhere in the shell. Arriving on the page, by a tap on the rail or **L2/R2**, starts and stops nothing: a video
  keeps playing, with the rail's Now playing key above Settings.
- While a **video** plays (or is paused) the Manga row wears a red **Stops the video** pill, and **A** on it asks
  first: **Stop the video to open Manga?** with **Stay** (focused, and **B**) and **Stop video & open Manga**. The
  video's place is kept, as Stop keeps it, and Home's Continue card reads **Stopped for Manga**. **Y** (or a tap on the
  line) ticks **Don't ask again**, which stops the video at once from then on; it is the Manga settings' **Manga and a
  video** setting. A song or a photo is never asked about: it goes on under the reader (F, then the Now playing block,
  is the way back to it).
- With none on, the page is its Empty state: **No extensions are on**, what an extension is, and one key, **Manage
  extensions** (**A**).
- Coming back to the page lands on Manga where you left it (its tab and place are kept) when the reader is on; **B**
  on Manga's pages comes back to this list. Manage extensions is reached from here (Settings has no Extensions group).

### Manage extensions

- **Manage extensions** lists the reader with its mark, what it does and an **On/Off** switch. **A** (or a tap on the
  row) turns it on or off; **X** opens **Manga settings**, the reader's own settings page. It is a page of the
  Extensions section: the rail stays, its header reads *Extensions › Manage extensions*, **L2/R2** step the sections.
- The first time the reader is turned on, a sheet comes over **Manage extensions**: it says the reader reads from your
  files and servers, and from sources in repositories you add, and that repositories are added by you: Banana Player
  doesn't provide or check their content. **Open Manga** opens the reader, **Done** closes the sheet.
- Home keeps no Extensions tile or drawer: the page is the drawer, promoted to a section.

### The Manga reader

The reader is an app of its own, inside the Extensions section: the rail stays at the left (its **Extensions** cell
lit), the header names the page (*Manga*, with the tab and its count under it: *Library · 9 series*), and the hint bar
is the shell's. It opens on **Library** and has four tabs, stepped with **L/R** (or a tap): **Library**, **History**,
**Browse** and **Downloads** (*Downloads 2* while two are queued). **L2/R2** step the sections, as on every page of the
rail, and coming back to Extensions lands on Manga where you left it (unless a video plays); **L3** is Search. A
source's **Popular / Latest / Search** and the Sources page's **Available / Repositories** step with **L/R** too. A
sheet or menu over a Manga page comes over the whole panel, rail included, and takes the keys until it closes.
**B** goes back — out of Browse's list of series first, then to the **Extensions** page (it was Home before). Reading a
chapter keeps the whole panel (no rail).
**START** opens Banana Player's **Settings**, as on every page of the rail (the reading page keeps START for View).
**Manga settings**, the reader's own settings page, is the last row of **X** on Library and on History (and **X** on
Manage extensions); **B** there comes back where you were. Music keeps playing while you read: the pages take the top screen, and F's Now playing
block is on every page, the reading page included (its **SELECT** opens what plays: a video shows on the top screen again, and
**B** comes back to the pages). **Sources** is a page of its own, opened
with **X** on Browse (below), where sources are installed from repositories you add; **Downloads** is the queue and what's
on the card (below).

**Library** is a grid of covers, five across, each with its title under it and its unread count at its top right;
**History** is one row a series, newest first: its cover, its title, then its source, the chapter and the page
(*Ch. 12 · p. 4 of 30*), when it was read at the right and a thin bar showing how far through the chapter you are.
The row itself continues (**A** or a tap); **Y** opens the series' page instead and **X** is More. A series that can't open now
stays, dimmed, and says why (*source removed*, *not on a card*).

A series' page has one row of keys (**Resume**, and for a source's series **Get**, **Info** and the heart)
over its chapters, each saying its own state (a dot on the one Resume opens, read ones dimmed, the page, its
download); the series' source and counts are the header's line. Pulling the chapters down past the top checks a
source for new chapters (**L** does the same), and reads the Manga folders again for a series on the handheld. A source's catalogue steps **Popular / Latest / Search** with **L/R**; Sources'
lists, a server's catalogue and form, and Manga settings are drawn the same way, in the rail's frame.

**Browse** and **Downloads** are lists of rows: the row you're on carries its key (a server's **Edit**, a chapter's
**Pause** / **Resume** or **Remove**), which a tap presses too; Downloads' storage line and Browse's place in a network
folder are the header's line. In Downloads **Y** pauses or resumes the chapter (or all, on **Pause all**), and **X** is
**More**: Pause / Resume, Remove from the queue, Delete from the card (choose it twice), Pause all, Clear failed. Browse ends its fixed rows with **Sources…** and **Add a server…** (the
same form **Y** opens).

**On the touch screen** every list and grid in the app scrolls with a finger: drag it up or down (Library's covers,
History, Browse and its *On this handheld*, Downloads' queue, its series on the card and their chapters, Sources, a
server's catalogue, a network folder, a series' chapters). A tap without a drag does what **A** does on that row or
cover, as the finger lifts.

**Where series come from.** Each of your cards may have a **Manga** folder (or `manga`, or `MANGA`); the reader reads it
each time you open the app:

- a folder of pictures is a chapter;
- a folder of chapters (folders of pictures, or `.cbz` files) is a series;
- a `.cbz` file (or a `.zip` renamed to one), a `.cbr` (or `.rar`) or a `.pdf` is a chapter, and one lying in the Manga
  folder itself is a series of a single chapter (EPUBs and text files aren't read here: Files opens them);
- one level of volume folders is read as well: a **Vol 1** folder holds the chapters of that volume, and they carry its
  number;
- `ComicInfo.xml` inside an archive gives the series' title and the chapter's number and volume;
- the chapters are listed in reading order (by volume and number where the names carry them, otherwise by name);
- a series' cover is its first chapter's first page: the real cover is decoded in the background, and a plain coloured
  one stands in until it arrives.

**Library.** The series as covers, five to a row, starting right under the tabs: each cover carries the number of
chapters you haven't read. After reading, the cursor stays on the series you read (which moves to the front). A series
that is a single file (a PDF or a CBZ on its own) opens straight in the reader where you left it, and **B** comes back here. **X**:
**Remove missing** takes out the series of the card whose files are on no card now (deleted, or on a card that's out),
and books read here before Files took them over (EPUB, TXT), with their read marks and History, after a second **A**.
In **History**, **A** on a series that's gone says so. With nothing in it yet:
*Your library is empty* — put CBZ files (or CBR, PDF) or folders of pictures in a Manga folder on your card, or add series from
Browse.

**History.** The series you've read, newest first, one row each: the cover, the series, the chapter you were last at
with its page (*Ch. 12 · p. 14/38*), a bar of how far into that chapter you got, and when. **A** continues at that
chapter's page (a source's series opens its page on that chapter). **X** clears the list (press **X** again to
confirm); the rows are rebuilt from what is left. With the reading history off in **Manga
settings** the list stays empty, but your places and your read marks are kept either way.

**Browse.** *On this handheld* holds the series found on your cards; **A** opens the list of them, each with how many
chapters it has, and **A** on one opens the series. In that list **X** shows where to copy manga (the card's Manga
folder, named in full) and how to lay it out, and **Y** on a series offers **Open**, **Info** (its chapters, size, where
it is, its path) and **Delete** (its files taken off the card; **A** twice). Under it come your saved network connections (from the Files
browser's **Network**), each with its kind and host, then your saved servers (**X** on one for **Edit** / **Delete**),
then the sources you've installed, each with its language. **X** opens **Sources** (**X** on a server row is still
**Edit** / **Delete**); **Y** adds a server (below). With no sources yet, a row says **Add sources in Sources (X on
Browse)**: **A** or a tap opens it.

**Network folders.** **A** on a saved connection lists its folders through the worker (nothing waits on the main
panel); **A** on a folder that is laid out like a Manga folder on the card — chapter folders of pictures, `.cbz`/`.zip`
archives, or pictures directly — opens it as a series page with its chapters. **A** on a chapter copies it into the page
cache's own folder first, then opens it in the reader; **B** steps back to the folder. Series and progress are kept by
their `bpnet://` path, never by the connection's password. A folder of series folders is browsed, not opened as a
series, and an archive in a folder opens as a one-chapter series.

**Servers.** **Y** on Browse opens the Add server form: **Kind** (Komga, Kavita or OPDS, LEFT/RIGHT), **Name**,
**Address**, **User** and **Password** — **API key** instead of Password for Kavita — then **Test** and **Save**. A
password is sealed on Save and never shown again; a typed address loses any `user:password@` part before it is kept.
**A** on a saved server opens
its libraries (Komga) or its OPDS catalogue, a cover grid whose covers arrive as you scroll to them; **A** on a series
opens its chapters and **A** on one reads it, pages coming from the server with its sign-in header. **X** on a server
row opens **Edit** and **Delete** (Delete asks twice). Kavita is read through its OPDS address, which carries the API
key in the path as Kavita writes it; the key is sealed like a password — split out of the address into the **API key**
field (the kept address has `{key}` where the key was), put back only in the request being sent, and replaced by
`{key}` in every answer before it's read, so no list, link or page address holds it.

**A source.** **A** on one opens it: **Popular**, **Latest** (when the source has one) and **Search** as tabs, stepped
with **L/R** or a tap (the Search tab, or **X** › Search this source, asks for the words on the keyboard; **Y** is the source's **Filters** and **X** its **More**), and the results as a grid that
fills in as you move down it, each tile showing the series' real cover once it has come in (fetched with the source's
own headers while the tile is on screen, kept in a cache on the card; a plain coloured one stands in until it arrives).
**Y** opens the **Filters** sheet, where the source has filters: its own sorts, selects, checks, include / exclude
lists (**A** cycles off → include → exclude), text fields and groups as rows, each showing its current value, with
**Reset** back to the defaults and **Apply** to run the search again from page 1 — the values stay with that source
while the app runs. **A** opens a series, **B** goes back to Browse's rows, and an error under the grid
offers **A: Try again**. A source's series page is laid out like a card series': **Resume** (**A**), **Get**
(**R**), **Info** and the heart (**Y**) in one row of buttons (**L** checks for new chapters), then the chapters with their read marks,
**X** to mark one read or unread, **A** to read it. **Info** shows the series' description, author and status in a
dialog (**A**, **B** or a touch closes it); the page itself keeps only the source, the counts and the author line. **Resume** keeps the series' own place, and the heart keeps it in
your library — a source's series starts outside it.

**A source series on the card.** The details and the chapter list are kept when they're fetched, under
`manga/series/` in the state folder, named by the series' hashed id (never its address). Opening the series shows them
at once and asks the source nothing; **L Check** is the only thing that asks again, and it replaces the kept list
when the answer comes. A check that fails keeps the list on screen and says so in a short note. Once the list is 6 hours
old or more, the details line under the title ends **· Checked 7h ago · L** (a fresh one adds nothing, and nothing is
ever asked by itself). An extension's series doesn't start the manga engine when it opens either: that
waits for **Resume** or a chapter; while a check runs it ends **Checking for new chapters…**.

**Waiting for the manga engine.** The engine takes about 30 seconds to start. **Resume** or a chapter pressed before
it answers puts the series page in a waiting state: a spinner, **Starting the manga engine…**, a bar over
**12 s · usually about 30 s** (past that: **taking longer than usual**), and **Resume opens by itself when it's
ready**; the top screen says the same, large. Then **Checking this series…**, **Getting the chapters…**, and the
chapter opens without another press. **B** (or the header's back) cancels and the page is as it was; no other key acts
meanwhile. If the engine doesn't start, or isn't installed, a note says so. A **downloaded** chapter never waits: when
the chapter Resume opens (or the one picked) is on the card, it opens at once, engine or not.

**Sources.** **X** on Browse opens its own page (its header says **Sources**; **B** goes back to Browse). Its lists step
with **L/R** (**L2/R2** are the sections): **Installed**, **Available** and **Repositories**. **Y**
checks the repositories again, and the top screen says how it went: **Installed …**, **Updated …**, **Removed …**, or why
it couldn't. With nothing yet they say where to start: *No sources yet* (add a repository, then install from
**Available**) and *Nothing to install* (add a repository first).

**Tachiyomi / Mihon repositories.** An address ending in `index.pb` or `index.min.json` is a Java repository (a
Tachiyomi/Mihon-format one you trust; Banana Player names and includes none), not a Lua one. It needs the optional
**Extension support** download (Manga settings; inside, a Java runtime and Suwayomi, never named on screen): without
it the repository is kept as waiting (*Waits for the engine*) and sent to it once it runs. With it, adding one starts it (*Loading manga engine… about 25 s*)
and registers the store with Suwayomi; it is listed with the Lua repositories under the store's own name, with no
badge (the screen never says Java, jar or Suwayomi). Removing it removes the store. Its
extensions show under **Available** beside the Lua ones (name, language, version; adult ones hidden
while **Adult sources** is Hidden), **A** installs one, and **Installed** lists them with **Update** when Suwayomi
reports a newer one and **Uninstall** (asked twice). A Java source appears in **Browse** as one row per source and
language — same Popular / Latest / Search grid, covers, filters sheet, series page and reading page as a Lua source.
Its series and reading progress are kept under `java:<source id>` with the Suwayomi manga id, so the Library and
History open them again through the host. While the extensions start, a notice at the top of every page counts the seconds (then *Extensions ready*). No video plays in the manga reader or while the extensions run (the handheld's memory can't hold both): the Extensions page's Manga row asks first while a video plays (**Stops the video**, then **Stay** / **Stop video & open Manga**), and any other way in stops a video as Stop would, with a notice; music and photos go on. The runtime is started as Manga is entered and kept running while any Manga page is open (a chapter read from the card included, so its 25 s start isn't paid again on the way back to a source); it stops five minutes after Manga is left, at once when a video plays outside Manga, and when Banana Player quits; the
hosted pages are read from `127.0.0.1` through the same page cache as any other chapter.

- **Repositories**: each one you've added, with when it was last checked (or *Not checked yet*). **A** on one offers
  **Refresh** and **Remove**, and removing asks **Remove its sources too?** (**Yes** takes its sources with it, **No**
  leaves them installed, **Cancel** goes back). The last row, **Add repository…**, types the address on the keyboard
  (it starts with `https://`); the repository's index is fetched and checked, and its sources show under
  **Available**.
- **Search** on Available (**L3**, or the magnifier key beside **Filter** at the right of the tabs): type a few
  letters on the keyboard, which says how many sources match as you go (*12 of 340 sources*); **Done** narrows the
  list. Every word has to be in a source's name, its language (`ja` or *Japanese*) or its repository's name; capitals
  don't matter. A line over the list shows the words and how many were found, with **Clear** (or **B**; the next
  **B** leaves the page); a touch on the words types them again. **Search and Filter both apply**: the languages
  kept and **Installed only** narrow what is searched, and each key is lit while it narrows the list. Nothing found
  reads *No sources found*. The search lasts for the visit.
- **Available**: what your repositories offer that you haven't installed; **A** installs one (**Installing…**, then a
  note), checked against the repository's index as it goes. Sources a repository marks as adult are left out while
  **Adult sources** is **Hidden** (Manga settings), here and in Browse.
- **Installed**: each source with its language and version, an **Update** chip on the row where the repository has a
  newer one; **A** opens its details (its repository, version and language) with **Uninstall**, and **Update** when
  there's one, while **X** opens the same as **Options** (**Update** greyed with *Up to date* when there's none).
  **Uninstall** is asked twice. Every list carries the note *Repositories are added by you. Banana Player doesn't
  provide or check their content.*

To try all that without finding a repository of your own: Banana Player's own files hold a test repository,
`tools/manga-test-repo` — the **Banana Player Manga Test Repository**, one source of public-domain comic strips from
Wikimedia Commons (Little Nemo in Slumberland, Dream of the Rarebit Fiend, Krazy Kat, The Yellow Kid, Buster Brown).
Add the address of its `index.json` as a repository, install **Public domain comics (Wikimedia Commons)** from
**Available**, and it's in **Browse**.

**A series.** Its title, how many chapters it has and how many you haven't read. **Resume** opens the chapter you were
in, or the first unread one, or the first. The heart (**Y**) takes the series out of your library and puts it back;
every series found on your card is in the library to start with. The chapters are listed under: the one **Resume** would
open has a dot beside it, a chapter you've read is dimmed and ticked, and one you started shows the page you were on. In
Vertical scroll a chapter also comes back to the place inside its page (a PDF's tall page included).
**X** marks the chapter under the cursor read or unread. **B** goes back to where you came from.

**Downloading a chapter.** A source's series page has **Download** (**R2**, or the button): **This chapter**, **Next 5
unread**, **All unread** or **Choose chapters…** (the whole list, ticked with **A** and downloaded with **Y**). The sheet
says how much the pick will take where the page counts are known, and the room the card has left. Each picked chapter's
page list is asked of the source first; it then waits in the queue, one chapter at a time and two pages at once. A
chapter already here or queued is not picked twice, and a chapter on the card opens straight from its `.cbz`, with no
network. The chapter list marks each one: **Queued**, its pages done (**12/40**), **Paused**, **Failed**, or **On
card**.

**Downloads.** The header's line says what the downloads take and the card's free space. The queue comes first: each
chapter's series and chapter, pages done of total over a progress line, and on the row you're on its **Pause** /
**Resume** key (**Y**, or a tap; a failed one **Retry**). A **Pause all / Resume all** row follows while anything is
queued. Then what is on the card, by series, with its chapters and size; **A** lists a series' chapters (**A** on one
reads it, **X** deletes it after a second press). **X** elsewhere is **More**: Pause / Resume, **Remove from the
queue** (a failed or waiting chapter), **Delete from the card** (a series), **Pause all**, **Clear failed** (every
failed chapter and the pages it left); Remove and Delete are chosen twice. Downloads need
the network and resume where they stopped after a pause, a lost connection or a quit. A downloaded chapter and its
online chapter share one place, read mark and History row, whichever way it is opened (Browse, Downloads or *On this
handheld*).

**Reading.** A chapter opens on the top screen; the bottom screen holds the controls: the series' name with the chapter,
*Page 4 of 12* and the reading mode under it, the page slider, the big pad and a row of six keys (the chapter before,
**Pages**, **View**, **Fullscreen**, **Light**, the chapter after). The pad is where the finger works: *Swipe here to turn
the page* between two turn marks (or *Drag to scroll* in **Vertical scroll**), a line along its foot naming double-tap, and
*Loading…* at its top while a page is decoded. In fullscreen and Book the page tag after a turn, and the navigation's page
line, name the chapter first, as quietly: *Ch. 1 · 12 / 30* (the chapter's number, else its title cut short; a comic
opened from Files has none).

| Button | What it does |
|---|---|
| **RIGHT** / **LEFT** | Forward / back: a screenful down the page, then the next page; back goes a screenful up it, then to the page before (in **Vertical scroll**, a screenful down or up). With **Swap Left/Right** on, **LEFT** is forward |
| **UP** / **DOWN** | Zoom in / out, in small steps: 100, 110, 125, 140, 160, 180, 200, 225, 250, 300, 350, 400, 500, 600, 700, 800 %. The new zoom shows on the page a moment, then fades. **A** zooms to 200 % or back to the fit |
| **Left stick** (any way) | **Vertical scroll**: scrolls smoothly while held, faster the further it is pushed; zoomed in, left and right pan across it (in fullscreen too). Paged modes: pans a page that is zoomed, wide or taller than the screen, up, down, left and right; a page that fits does not move. **Book**: one push turns a spread, the way its D-pad points; zoomed in, it pans both screens' picture |
| **L / R** | Previous / next page, straight to it |
| **X** | **More** (below): Mark read, Get this chapter, Bookmark this page; Info, Chapters, Manga settings. The reading mode it used to step is a row of **View** |
| **Y** | The Pages grid (below) |
| **L2 / R2** | Previous / next chapter |
| **START** | **View** (below): how the chapter is held and read |
| **SELECT** | Fullscreen on both screens (below); in **Book**, Fullscreen instead of Book |
| **L3** (press the stick) | Turn the page a quarter turn: 90° clockwise each press, in the paged modes (kept for the chapter; **Vertical scroll** ignores it) |
| **B** | Back to the chapters (from Fullscreen and Book too: the view stays as it was); your place is kept |

- The **page fit** is a setting (below): **Page** (the whole page on the screen), **Width** (as wide as the screen, read
  down a screenful at a time) or **Fill** (the page covers the screen: it's cropped, and the overshoot pans in reading
  order). It applies at once to the chapter you're reading.
- At the end of a chapter the screen says **End of chapter · R2 or RIGHT for** the next chapter by its name (**LEFT**
  with **Swap Left/Right** on), and the forward key (or **R2**) opens it; the header's line says *the end*. The last
  chapter says **You're caught up · that was the last chapter**. The top screen says it too, on a card at the foot of
  the page for four seconds each time the end is reached (the chapter and *8 of 8*, **End of chapter**, the keys and the
  next chapter's name), so the last page stays readable under it. In Book the page numbers say *turn once more for* the
  next chapter by name.
- A source's chapter that is still being asked for shows *Getting the chapter…* on both screens (the top one large,
  with the spinner, the series and the chapter); **B** goes back. One that didn't open says why, and **A** tries again.
- **Scrolling is the finger's and the left stick's**: the D-pad's **UP/DOWN** zoom, they never scroll. The stick
  scrolls while held, up or down, with a dead zone at rest and a speed that follows the push. **By touch**: drag in the big
  area on the bottom screen to turn the page — it follows your finger and snaps to a page when you let go (a quick
  flick turns the page even from a short drag), and in **Vertical scroll** it scrolls — the slider jumps to a page, and the
  row's keys go to the chapter before or after (the narrow keys at its ends), open **Pages** or **View**, turn fullscreen
  on (as **SELECT** does), or open **Light** (Brightness, above, under Books and documents). The header's **⋯** is **More**. In
  fullscreen the whole bottom screen is the page, and the finger drags it anywhere; a tap in the middle of the page
  shows the **navigation** over it (the series and *Page 4 of 30*, the page slider, **Chapter** and **Page** back and
  on, **Pages**, **View** and **One screen**; it goes after four seconds untouched or on another tap in the middle);
  a tap near an edge turns instead, with the navigation showing or not (the top and bottom
  of a strip a screenful back or on, the left and right sides of a page as **LEFT** and **RIGHT**). A double-tap on the
  page zooms to 200 % at that spot, and a double-tap again goes back to the fit.
- Your place is kept on the card, in the player's own folder (`home/.config/banana-player`): the page of every chapter
  you've opened, and when.

**A chapter from a source.** It opens like a chapter on your card, from the source's own page addresses: the pages are
fetched as the reader needs them — three at a time, in reading order — decoded as they arrive (the page number says
*Loading…*), and kept in the page cache, so a page you've read opens at once the next time. Read marks and History
treat it as they do a chapter on your card. A browser check stops either of them and says so plainly: **This source
asks for a browser check (Cloudflare). Banana Player can't pass it yet.** (**DDoS-Guard** where that's the one) —
under a source's grid with **A: Try again** while browsing, or in red where the page number sits while reading.

**View (START).** **START** while reading (or the **View** key) opens a sheet over the page (in fullscreen and Book
too), in the screen's usual orientation. **Layout** is its first row, three chips: **One screen**, **Fullscreen**,
**Book**; choosing another changes the screen under it and the sheet stays open, its rows following the new layout
(**START** or **B** closes it). Under it: **Reading mode** (Right to
left, Left to right or Vertical scroll, in every layout: Layout never changes it. Book needs pages, so there a Vertical
scroll reads a page a screen, the row says **Vertical scroll (paged)** and the sheet's line *Scrolls again outside
Book*; back in One screen or Fullscreen it is Vertical scroll again, as it was set), **Page fit** (dimmed in Vertical scroll: it fits the
width), **Zoom**, **Hinge gap** (a fullscreen Vertical scroll's: dimmed with its reason otherwise), **Swap Left / Right**
and **Light & filter**; in Book, **Hold** (**Top screen left** or **Top screen right**) in place of the fit, the zoom and
the gap. **UP/DOWN** choose a row, **LEFT/RIGHT** change it where you stand (the pages follow at once), **A** steps it on,
**B** or **START** closes the sheet, and **SELECT** closes it and goes Fullscreen; a tap on a chip or on a stepper's side
changes it, a tap above the sheet closes it. Choosing One screen or Fullscreen there also sets **Open fullscreen**, so
the next chapter opens the same way.

**More (X).** **X** while reading (or the header's **⋯**) opens the More sheet. At the left, this chapter: **Mark read**
(or **Mark unread**), **Get this chapter** (a source's chapter is queued for the card; dimmed with *on the card*,
*queued* or *a server's* when there is nothing to get) and **Bookmark this page** (**Remove this bookmark** on a page
that has one). At the right: **Info** (the series, the chapter, the page, its bookmarks, the source or the file),
**Chapters** (back to the series, as **B**) and **Manga settings**.

**Page bookmarks.** A bookmark marks one page of a chapter: the top screen says *Page 4 bookmarked* for a moment, the
Pages grid shows the mark on that page's corner, and the grid's second tab lists the bookmarked pages alone. They are
kept with the chapter's place (the Manga store's `progress.tsv`), up to 200 a chapter, and a downloaded copy shares its
source chapter's. A PDF or comic opened from Files has the same row in its **More** (**X**), kept with the documents'
bookmarks.

**Book (held sideways).** **View** › Layout › **Book**: hold the handheld sideways like a book, the hinge down
the middle, and each screen shows one whole page, turned upright as you hold it. **Hold** says which side the top
screen is on (**Top screen left**, the buttons under your right hand, or **Top screen right**). The spread follows the
reading mode: in Right to left the earlier page is on the right. The D-pad's **UP** and **DOWN** point left and right
as held and turn the spread (two pages; Swap Left/Right turns that round); **L / R** move one page, to realign a
spread; **START** opens **View** and **X** **More**, as in the other layouts, upright sheets over the pages (Reading mode in
View switches the direction; a Vertical scroll series turns its pages the way the **Reading mode** setting reads, right
to left when that is Vertical scroll too);
a swipe along the bottom screen turns the spread and a tap in the middle shows the **Book bar**: the spread's page
numbers on its slider, **Pages**, **View**, **Light**, **Next chapter**, and **One screen** to leave Book, upright as the
pages are. The chapter and page numbers ("Ch. 1 · 3–4 / 6") show for a few seconds
as a screen's pages load or turn, then fade, at the foot of the spread and upright as held; at the chapter's end they
stay and say what comes next (turn once more for the next chapter). **Chapter and page in Book** (Manga settings, and
a View row in Book): **Hide**, **On turn** (the default) or **Always**.
**Zoom in Book:** the D-pad's **LEFT** and **RIGHT** (up and down as held: the one up zooms in) step through the fit,
150, 200 and 300 %; a double-tap on the bottom screen zooms to 200 % there, or back to the fit. Zoomed, both screens
show one picture of the pair (or the wide page) and move together: a finger dragged on the bottom screen or the left
stick pans it. The turn keys first take the view to the pages' end that way, then turn; a new screen opens at the fit.
No page fit in Book. The menu and notices
keep the screen's usual orientation. Book is kept for the series, as the reading mode is.
**Wide pages** (a double page drawn as one picture: at least 1.2 times as wide as tall, never the first page) span
both screens: cut down the middle, its left half on the left screen and its right half on the right, at one size and
continuous across the hinge (no strip skipped, unlike fullscreen; a pair of normal pages keeps the **Hinge gap**). Right to
left changes only the page order, never the halves. A wide page is one turn on its own; the page before it, if it
would have no partner, sits alone with the other screen blank, and pairing starts again after it. The place kept is
still a page number, and reopening lands on the screen holding it. **Wide pages span both screens** (Manga settings,
on by default) turns this off: a wide page then sits whole on one screen like any other.
**Page turn animation:** in Book a turn slides the screen pair out and the next one in along both screens (about 200
ms, easing to a stop), the way the book reads (Right to left reverses it); a wide page is one screen. Turns pressed in
quick succession (a held key, quick taps) change at once, so the pages never lag behind the buttons. **Page turn
animation** (Manga settings, on by default) turns it off. (A text's Book has its own fold.)

**Fullscreen (both screens).** **SELECT** while reading turns it on and off, and so does the **Fullscreen** key
in the reading page's row. The two panels become one tall view and
the gap under the hinge is skipped, so the art lines up across the fold; the bottom panel draws its share of the pages
from the pages the reader has already decoded. The gap's size is a setting of its own, **Hinge gap** in **Manga
settings**: **36** of the panel's 480 lines by default. **SELECT** leaves fullscreen (a line says so each time you turn it on: “SELECT: one screen”); **B** goes back to the chapters
and fullscreen stays, so the next chapter opens in it too.
The page number shows for a moment after the page changes. A tap puts the controls bar over the lower part of the
view; a tap anywhere outside it puts it away again and does nothing else (no page turn, no drag).

**The Pages grid.** **Y** while reading (or the **Pages** key) opens the chapter's thumbnails in a sheet
over the reader (a PDF's **Pages** is the same sheet), six to a row with each page's number under it, on the page
you're reading, marked **Here** ("You are on page N" at the top). The pages in view get their thumbnails as the grid shows them, wherever you were
reading (a moment's empty place first; a page of an online chapter not downloaded yet stays empty). **UP/DOWN/LEFT/RIGHT** move the cursor and the top screen follows it, **L2/R2** move a screenful, **A** jumps
there and closes the grid, **L / R** (or a tap on a tab) switch between **Pages** and **Bookmarks** (the bookmarked pages
alone, each count on its tab; with none, a line says where one is made), **B** or **Y** closes it; on the touch screen, tap a thumbnail to jump, drag to scroll, or
tap above the sheet to close it.

### Manga settings

The reader's own settings are a page of their own, **Manga settings**, inside the Manga app: they are not in Banana
Player's Settings (whose **Extensions** group opens Manage extensions). **START** opens them on any Manga page, and on the four tabs a gear key right of the tab strip (with START beside it) does too, by touch (the Sources
page and a series page), the gear (**X**) in **Manage extensions** opens them too, and the reader's **More** (**X**) opens
them while you read. **B** goes back where you came from.

| Setting | Values | Default | |
|---|---|---|---|
| **Reading mode** | Right to left / Left to right / Vertical scroll | Vertical scroll | How a series opens until you pick one for it (X while reading). |
| **Page fit** | Page / Width / Fill | Page | Width: a page as wide as the screen, read down with RIGHT a screenful at a time. Fill: the page covers the screen, cropped either way, panned in reading order. |
| **Swap Left/Right** | Off / On | Off | Off: RIGHT is always forward. On: LEFT is forward (to match a right-to-left book). |
| **Hinge gap** | 0 to 200 px | 36 px | The part of the page hidden under the hinge in fullscreen, so pictures line up across the gap. Vertical scroll, and the space between a Book pair's pages (a wide page runs on across it); Right to left and Left to right put the screens edge to edge (0 px). |
| **Wide pages span both screens** | On / Off | On | In Book, a double page is cut down the middle, half on each screen. Off: it shows whole on one. |
| **Chapter and page in Book** | Hide / On turn / Always | On turn | The "Ch. 1 · 3–4 / 6" tag beside the pages in Book. On turn: a few seconds as pages load or turn, then it fades. Always: it stays. |
| **Page turn animation** | On / Off | On | In Book, a turn slides the pages across both screens. Quick turns in a row are never slowed by it. Off: the pages change at once. |
| **Open fullscreen** | On / Off | On | A chapter opens on both screens. SELECT leaves. View's Layout sets it too. |
| **Preload** | 2 / 3 / 6 pages | 3 pages | More: smoother on a slow server, more data. |
| **Page cache** | 100 MB / 300 MB / 1 GB | 1 GB | Pages kept on the card to read again without downloading; past the cap the least recently read go first. Covers: 50 MB more. |
| **Clear cache** | (a button: *Empty*, or *Clear · 214 MB*) | | Empties the pages kept on the card. Press A twice. |
| **Extension support** | Not installed / Downloading / Installed · 344 MB / Running / Failed | Not installed | Needed for repositories whose address ends in index.pb or index.min.json: a 241 MB download, about 300 MB of memory while one of their sources is open. A installs; A twice removes. |
| **Adult sources** | Hidden / Shown | Hidden | Sources a repository marks as adult. |
| **Reading history** | On / Off | On | Off clears it. |
| **Secure DNS** | Off / Cloudflare (1.1.1.1) | Off | Looks up sites through Cloudflare over HTTPS (one.one.one.one), past a network that blocks them. |
| Repositories | | | Repositories are added by you. Banana Player doesn't provide or check their content. |

The reader itself is turned on and off in **Manage extensions** on the handheld (only there): **A** on its row. While
it's on, the web dashboard has a **Manga** page with its repositories (add one by pasting its address, refresh, remove);
browsing, reading and downloads are the handheld's.

The page cache holds the pages of chapters read from a source, and the chapters copied from a network folder, under
the one **Cache size** (the oldest go first; **Clear cache** says how much it holds), and **Adult
sources** hides what a repository marks adult until you show it.

**Coming next:** pinch zoom (the screen reads one finger), and reading a manga in the browser.

## Updates

Banana Player updates itself when it's installed on the Ports shelf (from 0.2.3 on; Wi-Fi needed):

- A few seconds after it starts, it looks for a new version in the background (turn this off in Settings › Updates ›
  **Check automatically**).
- When there's one, the file list asks in a sheet: **Update now** (**A**), **Later** (**B**) or **Not until tomorrow**.
- **Update now** downloads it and checks it against its published checksum (a damaged download changes nothing). While
  it downloads the sheet shows how far it got, with **Hide** (**B**: the download goes on) and **Stop the download**
  (**Y**: the part files go and nothing was changed). Then it
  offers **Restart now** (or **Later**, and it's put in place the next time you start Banana Player). An update that
  failed says why and changes nothing: **Try again** (**A**), **Close** (**B**) and **Details** (**Y**, the whole reason).
- The version before is kept on the card as `roms/ports/BananaPlayer.old`.
- Installed from the BananaOS Store, the Store updates it instead: open the Store to see an update.

Your settings, resume positions and history are kept through every update: they live in `home/.config/banana-player`
on the card, not in the player's folder.

## Quick settings

Press the **Menu/Function** button (F), anywhere, for a panel centred on the bottom screen, over the rail: the
kit's own look — **Volume** and **Brightness** as rows with a **−** and **+** key either side of the bar, the
switches as tiles two to a row, **Quit** as the one red key, and the hint bar along the foot naming the keys
(with Button hotkeys off it says **Tap outside to close**).

- **Now playing** (its first block, always there): the art (48; a song's album cover — the Art placeholder until
  the background decode answers, and for good when the file has none — a video's film mark, or a music mark when
  nothing plays), the title, the artist and the time, then **Previous 52**, **Play / Pause 68** and **Next 52**, and a
  line along its foot showing how far it has got. It has the state marks too (1.5×, Repeat one, a look, the EQ,
  "Playing in Chrome"), in yellow after the time. With nothing loaded it is the same block, idle: a music mark,
  **Nothing playing** / **What you play shows here**, the three keys dimmed, no line; the cursor passes it and it
  isn't pressed. The **D-pad** opens on **Volume**; **UP** reaches the block. **A** on it plays or pauses,
  **LEFT / RIGHT** seek (5 s back, 10 s on), **L / R** step to the previous and next from any row, **SELECT**
  opens the player. By touch its three keys do their jobs; a tap anywhere else on it (the art, the title, the
  line) opens the player and closes the panel. A finger held on **Play** takes playback over from a browser. Its
  actions go through the session like the player's keys.
- **Volume** and **Brightness**: touch the bar (anywhere in its row between **−** and **+**) and slide, tap **−** / **+**, or use **LEFT / RIGHT**. They change the same
  levels as BananaOS's own settings and its volume and brightness keys, and they're kept after a restart. With
  **Max volume** past 100 the volume bar carries the 100 mark and its number takes the colour of the part it is
  in (yellow past 100, red past 120).
- **Same brightness for both screens** (a switch tile): on, one Brightness bar sets both screens; off, **Top** and
  **Bottom** each get their own.
- **Web dashboard** (a switch tile): on or off. While it's on, its own key (Right, then A, or a tap) opens its page: the
  QR code, the address, the PIN and **New PIN**. See [Web dashboard](#web-dashboard).
- **Incognito mode** (a switch tile): while it's on, nothing you play goes into Recent and nothing you read
  goes into the manga History; where you stopped a film, your reading places and read marks, playlists, Liked and
  downloads all work as usual. A hat-and-glasses mark sits beside the clock while it's on. It's never saved: off again
  whenever Banana Player starts. While it's on the **Web dashboard is off** (the switch keeps its value; the server is
  back with the same PIN and sign-ins when you leave), the globe leaves the header, and Play on isn't offered; with a
  browser signed in, turning it on asks **Turn on incognito?** first. Its tile's second line says what the lid or a sleep
  does: **Lid or sleep: closes all** or **quits** (Settings › Power & screens › Lid or sleep in incognito).
- **Quit Banana Player** (the one red key).

**F** again, **B** or a tap outside the panel closes it. (F with another button, like F + L2 or F + volume, does that
combination instead.)

## More

- **Info** (while playing): file, size, length, resolution, codecs, frame rate, bitrate, audio format, every track and
  the file's tags.
- **Battery:** an icon on the bottom screen, red when it's low, with a bolt while charging (and its percent, if you
  turn that on in Settings).
- **Volume and brightness:** the keys show a level card on the top screen, top right, the same card on every screen (a
  video, music, a photo, nothing playing, the manga reader, a document): icon, name and value, a bar under them. Cream at
  normal levels; with Max volume at 120 or 150 the bar runs to that top with a mark at 100, and past 100 the value and
  the part past the mark turn yellow with "Boosted" under it, red past 120 with "Very loud: can harm hearing and the
  speakers". The manga reader's and the text viewer's zoom is the same card without a bar (200 wide).
- **Chapter and Equalizer cards:** the same spot and body, a small label over the value in yellow ("Chapter 3 of 8" over
  its title, with a segment bar of the chapters; "Equalizer" over "Rock · On").
- **Notes:** the short messages ("Added to Liked", "Speed 1.5×", "This is the first one", ...) are one card at the
  bottom centre of the top screen (over a video in the middle instead, for 1.5 s, off the subtitles and the level card): a line in yellow (red for a failure), an optional icon and dim second line; 3 s.
- **Playing in a browser, Loading, Buffering:** one state card: a tile with an icon (the spinner while loading), a
  title, a line, and a hint with its key cap.
- **Loading:** a file that takes a moment to open (a big photo, a slow card) shows a spinner on the top screen.
- **Lock** (**X** → **Lock touch**): locks the touch screen while playing; the buttons still work (**X** → **Screen
  off** too). Hold the large lock for a second to unlock, or **X** → **Unlock touch**.
- **Screen off** (**X** → **Screen off**): turns the bottom screen off to save battery. While the player is showing,
  the playback buttons keep working with it off (**A**, **LEFT/RIGHT**, **L/R**, **L2/R2**, **L3**); a touch or any
  other button turns it back on (that press does nothing else). Elsewhere (a list), any button turns it back on. It also turns off by itself
  while a song or video plays and nothing is touched (Settings › Power & screens › **Screen off (music)** and
  **Screen off (video)**).
- **Lid:** closing it pauses; opening it keeps it paused until you press play. Settings › Power & screens › **When
  the lid closes: Pause / Keep playing** (Pause by default; the first start without the setting asks, Pause
  first, and says where to change it). With **Autoplay**, while the web dashboard is on (a browser may be playing from it), and while a song or a video plays on the
  handheld (playing or paused, not stopped, not a dashboard page playing it), the player switches BananaOS's own *Closing the
  lid* setting to stay awake (the `lid_sleep` line of `/data/banana/bananad.conf` set to `no`, nothing else in the file
  touched) and puts it back a few seconds after playing stops or ends (a pause keeps it, so the power key can play again), or when the player quits. If the lid is
  shut by then, the handheld goes to sleep as if the lid had just closed. If you changed *Closing the lid* in BananaOS's
  Settings meanwhile, your choice stays. After a crash the launcher puts it back; after a power loss, the next start
  does. `log.txt`: `lid: sleep off while playing…`, `lid: sleep restored`, `lid: restore skipped, changed in BananaOS
  Settings`. With BananaOS's lid sleep already off, nothing is changed and closing the lid keeps playing.
- **Lid or sleep in incognito** (Settings › Power & screens): what happens while incognito is on when the lid closes or
  the handheld sleeps (by the power key's 2 s hold or BananaOS's idle minutes): **Nothing**, **Close all** (the default:
  what plays stops everywhere, a book, PDF, comic, photo and the Manga reader close, sheets close, Home shows, and
  incognito ends; no note on waking) or **Quit** (Banana Player quits). Close all and Quit win over Keep playing; a book's
  place and read marks are kept as Stop keeps them. With Keep playing and nothing loaded the handheld sleeps as BananaOS
  normally does.
- **Power key with the lid shut:** Settings › Power & screens › **Power key, lid shut**: **Play / pause** (default) or
  **Sleep**. With Play / pause, while the lid is shut with **Keep playing** and something is loaded here (playing or
  paused, not stopped), the power key is play/pause: a short press pauses or plays, as **A** does. Holding it 2 seconds
  sleeps: the player lets the key go and asks BananaOS for the sleep closing the lid would bring. With the lid open,
  nothing loaded, or **Pause** set, the power key is BananaOS's as before. After a hold's sleep it lets go until the
  wake (and is play/pause again then), so the key that wakes the handheld reaches bananad; a key bananad holds
  exclusively stays BananaOS's too. With incognito on, the 2 s hold does **Lid or sleep in incognito** first (Close all
  or Quit), then sleeps. `log.txt`: `power: … taken: lid shut…`, `power:
  press: play/pause`, `power: hold: asking bananad to sleep`, `power: released (…)`. With **Sleep** the player never takes
  the key: a press sleeps the handheld, as with the lid open.
- **Volume keys with the lid shut:** Settings › Power & screens › **Volume keys, lid shut**: **Volume and skip** (default)
  or **Volume only**. With Volume and skip, while the lid is shut with **Keep playing** and something is loaded (playing or
  paused), the player takes the volume keys' own input device (`gpio-keys-volume`, apart from the power key and the lid
  switch) so BananaOS never moves the volume by itself. **Volume +** / **Volume −** let go before 0.6 s: one volume step
  (5%, when you let go) through BananaOS's own `/usr/libexec/banana/volume`; it stops at 100% (a boost set before stays).
  Held 0.6 s: **next** / **previous** and the volume does not move; one hold, one step. A key down 5 s is taken as stuck
  and ignored until let go. A song and a video get the same keys, and they act on what plays, never on the page left open.
  The grab ends the moment the lid opens, Keep playing goes off, nothing is loaded, the player quits, or the main loop
  stops answering (3 s). No sound effects. `log.txt`: `volume keys: … taken: lid shut…`, `volume keys: Volume + press:
  volume 45%`, `volume keys: Volume - held 601 ms: previous`, `volume keys: … stuck…`, `volume keys: released (…)`.
  With **Volume only** the player never takes the keys: a held key runs the volume, as with the lid open.
- **Shoulder keys with the lid shut:** Settings › Power & screens › **Shoulder keys, lid shut**: **Ignored** (default),
  **Hold** (L / R previous / next and L2 / R2 previous / next chapter when held 0.6 s; one hold, one step) or **As usual**
  (at once). With **Pause** the row is at 45% ("The lid pauses"). Nothing else under the lid does anything while the lid
  is shut and something is loaded: the face keys, the D-pad, START, SELECT, F, the stick and the touch screen are ignored.
  With nothing loaded, closing it sleeps the handheld as BananaOS says.
- **Headphones out:** pulling the headphones out of the jack, Bluetooth headphones disconnecting, or a USB sound card
  unplugged while something plays pauses it, so the sound doesn't go on from the speakers. Plugging back in doesn't play
  (press play). `log.txt`: `audio: headphones out, paused` (or `Bluetooth headphones disconnected`, `USB sound card
  unplugged`).
- **Top screen with nothing playing:** a Preview pane drawn by the app in the theme's colours: the brand mark and name, then
  what the bottom screen's cursor is on (a Recent card on Home, a file's kind and size in Files, a video with its frame
  and facts, an album with its art, a book with its cover, a setting's help, Search, Extensions). The art and covers
  are decoded off the main thread; the cover's place shows until they land, and for good where there is none. A fresh install shows the welcome: the app's icon, the name and three ways
  to start. Loading, Buffering and "Playing in your browser" are one State card (tile with icon or spinner, title, line, key).
- **Sleep:** when nothing plays, the handheld sleeps after BananaOS's idle minutes (its `autosleep_minutes`, 10 unless
  changed in `/data/banana/bananad.conf`; 0 is never), counted from the last button, touch or page turn in the player.
  The player asks bananad for the sleep as the menu's Sleep does. It stays awake while: a song or video plays (paused
  is not playing), a dashboard page plays, a file job runs (copy, move, delete, a book fetched from the network),
  Manga chapters download, the Manga extension engine starts or installs, the dashboard was used in the last 2
  minutes (a request or an upload), an update downloads, or the charger is in. `log.txt` says why it slept
  (`idle sleep: asking bananad…`) or what kept it awake (`idle sleep: … kept awake: …`, once per change). Waking
  leaves a paused video paused.

### Files: Play next

**X** on a song or video in Files has **Play next**: it queues that file right after the one playing (Shuffle too: it
comes next in the shuffled order). With nothing playing it simply plays the file.

### Recent: songs titled by their tag

A song's card in Recent (Home and All recent) carries the title from its tags when the cards' index has read it (a song
in a Music folder); any other file, and a song not read yet, keeps its file name.

### Comics ask before stopping a video

A comic (CBZ, CBR, a comic EPUB) opened from Files, Recent or a connection while a video plays or is paused asks first,
as Manga's row does: **Stay** or **Stop video & open comic** (the video's place waits in Continue). **Don't ask again**
(or Settings > Manga) stops the video at once. Music and photos are not affected.

### Photo viewer's More: Slideshow, Copy, Move, Delete

**X** (or SELECT) over a photo adds **Slideshow** (with more than one photo: the next photo every 5 s, **Stop slideshow**
to end it; L / R still step, it ends at the last photo and when the viewer closes), **Copy...** and **Move...** (the
viewer closes on the photo's folder with the clipboard set: open the folder it goes in, then SELECT, as in Files) and
**Delete** (asks first, then the viewer closes). Not for a photo on a connection.
