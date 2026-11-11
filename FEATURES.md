# Banana Player: all the features

Everything Banana Player does, button by button. For installing it, see the [README](README.md).

- [What it plays](#what-it-plays)
- [Finding something to play](#finding-something-to-play)
- [Playlists](#playlists)
- [Controls](#controls): [while playing](#while-playing), [photos](#looking-at-a-photo), [the file list](#in-the-file-list), [the menu](#the-menu-start)
- [Photos](#photos)
- [Picture size](#picture-size)
- [Music](#music)
- [Sound: equalizer and effects](#sound-equalizer-and-effects)
- [Chapters](#chapters)
- [Settings](#settings)
- [Quick settings](#quick-settings)
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
| **Subtitles** | Inside the video (SRT, ASS/SSA, PGS, VobSub, VTT), or files next to it (`movie.srt`, `movie.en.ass`) |
| **Languages** | Subtitles, titles and file names in Latin scripts (Vietnamese too), Cyrillic, Greek, Chinese, Japanese, Korean, Thai, Arabic, Hebrew and Hindi |

Videos are decoded by the handheld's CPU. The various 1080p videos tested play well; very high bitrates, and especially
HEVC (x265), may drop frames. **Info** shows how it's keeping up.

## Finding something to play

Open Banana Player and the bottom screen shows **Home**:

- **Recent**, along the top: a card for each of the last 30 things you played, newest first, with how far you got
  (`0:27 / 0:52`, or `Played · 3:12`), a bar, and a network mark for a file on a server. **A** plays it (from where you
  left off), **Y** opens its menu: **Resume from…** (or **Play**), **Play from the start**, **Show in folder** (opens
  Files or Network on that file), **Add to playlist…**, **Remove from Recent**. The last card, **All recent**, opens
  the whole list. On the touch screen tap a card to play it, swipe the strip, or hold a card for its menu. With nothing
  played yet the strip is a single line and the tiles grow.
- Four tiles, two by two: **Files** (your cards: **Videos**, the `videos` folder in `roms`, then **This card** and
  **Second card**), **Playlists**, **Network** and **Extensions** (**None on**, or **1 on · Manga reader**). Each says
  what's in it.

Move with the D-pad: **UP/DOWN** between the strip and the tiles (back to the card or the column you left),
**LEFT/RIGHT** along them. **X** on Home: **Clear Recent** (press twice), **Show hidden folders**, **Settings**. **B**
twice quits (while something plays, **B** does nothing on Home and **SELECT** goes to it), **START** opens the menu.

Inside a section the header reads its name with four markers beside it, the open one lit: **L/R** go to the section
before or after, or tap a marker. Each section opens where you left it (its top if that folder or playlist is gone),
from L/R, a marker or its tile on Home; the **Playlists** tile on Home always opens the list of playlists, the cursor on
the one you were in. **B** goes up a folder, and from the section's top back to Home, on its
tile.

Folders come first, then files in natural order (episode 2 before episode 10). Each row has an icon (folder, video,
song, photo), and each file shows its size. Songs and videos on the card show their length too (`3:34 · 3.5 MB`; a
video you stopped part-way, `at 12:03 / 1:45:00 · 1.2 GB`), filled in a moment after the folder opens, the rows on
screen first. A file whose format keeps no length in its header (MPEG-TS `.ts`/`.m2ts`/`.mts`, `.mpg`/`.mpeg`/`.vob`,
raw `.aac`) or that can't be read (empty, cut short, broken) shows only its size; so do network files. Anything you
stopped part-way through shows where you left off. The line
under the header shows where you are (`share / Movies`), and the bottom line shows what each button does there: the
button's name in yellow, then what it does. See [Playlists](#playlists).

Everything in a folder plays as one playlist: **L/R** go to the previous or next file, and the next one starts when
the current one ends. **Previous**: 5 seconds or more into a song or video, it goes back to
its start; before that, to the file before. On the first file in its first 5 seconds it does nothing (the top screen
says **This is the first one**), so the Prev button is never greyed out. The web dashboard's Previous (both targets, and
a headset's or the lock screen's) follows the same rule. Photos simply step back. Folders can mix videos and music. Photos are a list of their own: from a photo you step through
the folder's photos, and from a video or a song through its videos and songs. When you stop something, you're back in
its folder on that file.

## Playing while you browse

Leaving the player doesn't stop what plays. **B** (or **Back**, or **<**) on the playing screen takes you to the list,
and the song or video goes on: a video keeps playing full size on the top screen, a song keeps its visualizer or art.
Paused stays paused. You land back **where you were before you opened the player**: the same page, folder, cursor
and scroll (Home, a folder, All recent, START's menu…); from a playlist's page, the list of playlists, the cursor on
that playlist. Started with a file, or after **Show in folder**,
you land in the folder (or playlist) it plays from, the cursor on it. The same goes for **Stop** and for the end of
the list while the playing screen was up.

- **The mini bar**, along the bottom of Home and every list (Files, Playlists, Network, All recent): the play / pause
  mark, the title, `0:14 / 1:00` (· paused), and a line showing how far it has got. Tap the bar to go back to the
  playing screen; tap the mark on its left to play or pause and stay where you are; hold the bar for its menu:
  **Now playing**, **Play** / **Pause**, **Like**, **Add to playlist…**, **Show in folder**, **Stop**. It's hidden
  while you select files, type, fill in a connection, and on Settings and Info.
- **The header** shows three small yellow bars left of the clock while something plays behind the list (dim and still
  when it's paused).
- **The playing file's row** carries a chip: **Playing**, **Paused** or **In browser** (the web dashboard plays it). Its card on Home says **Now** with its time and bar running. **A** on that row or card goes back to it,
  never from its start.
- **Back to the playing screen:** **SELECT** on any list or on Home, a tap on the mini bar, **START** › **Playing** ›
  **Now playing**, **A** on its row or card, or **B** on Home.
- **L3** (press the stick) likes or unlikes what plays, from the lists too.
- **Stop** ends it: **Stop** at the bottom right of the playing screen, **START** › **Playing** › **Stop**, or the mini
  bar's menu. The place is kept (its card on Home resumes it). Reaching the end of the folder or playlist stops too:
  on the playing screen you're back in the list on it; while browsing the bar folds away and the top screen says
  **Finished**.
- **Opening a photo** while a song or video plays doesn't stop it: the photo takes the top screen and what plays goes
  on behind it, sound and all (a song's next song follows as usual; a video's picture just isn't shown, or decoded,
  meanwhile). Close the photo (**B**, **Close**) and you're back where you opened it, the video showing again where it
  has got to. A page playing it in a browser isn't disturbed either.
- **Screen off after** (Settings) works while browsing too, as long as something plays (not paused).
- Started with a file (`banana-player FILE`): **B** opens the list on its folder, as if you'd started with none.

## Playlists

A playlist is a list of your videos and songs, in the order you set. It can mix files from your cards and from network
folders. Playlists have a section of their own, between Files and Network.

- **Make one:** in **Playlists**, **A** on **New playlist** (the last row), or **X** → **New playlist…**, then name it on the
  keyboard. A name is 40 characters at most, can't have a **/** in it, and can't match one you already have (capitals
  don't count).
- **Open one:** **A** on it shows its items, in order. The section lists Liked first, then your playlists by name, each with its number of items.
- **Its menu:** **Y** on a playlist (or hold it) gives **Play**, **Shuffle play**, **Rename…** and **Delete playlist**
  (press it twice). Deleting a playlist leaves its files where they are.

### Playing

- **A** on an item plays the playlist from there. **Play all** (the playlist's **X** menu) starts at the top, and
  **Play from here** (an item's **Y** menu) starts at that item.
- **Shuffle play** turns **Shuffle** on and starts at a random item; the rest follow in a random order (the list keeps
  its own order where it's shown). Shuffle stays on for what you play next, till you turn it off (**X** on the music
  screen, or the web's Shuffle button).
- **L / R** go to the previous or next item, as in a folder. On **Loop all**, the playlist starts again after its last
  item.

### Adding items

- In **Files** or **Network**, **X** → **Select files**, tick the files you want (**A**, or a tap), then **Y** →
  **Add to…** and pick a playlist (or **New playlist…**). On the touch screen, a hold on a file starts this with that
  file ticked. **B** goes up a folder, as in browsing, and the ticks stay (at a section's top it leaves). **X** (or
  **Cancel**) leaves select mode from any folder; with files ticked, press it twice. The **✕** at the top left leaves
  at once.
- Inside a playlist, **X** → **Add files…** opens the file list, and the files you tick are added to that playlist.
  **Add folder…** adds a whole folder instead.
- On any folder, **Y** (or a hold) → **Add to playlist…** comes first, and adds that folder's files.
- A folder adds only the files directly in it, not those in its subfolders. Photos are skipped, and the note says how
  many. Files are added in the folder's order.
- The same file can be in a playlist twice. **X** → **Remove duplicates** (in the playlist's **X** menu) keeps the first
  of each.
- A playlist holds 2,000 items at most. An add that goes past that adds what fits, and says so.
- Editing the playlist that's playing (from the handheld or the dashboard) changes what plays next: songs added
  after the last one play after it (**Next** reaches them), removed ones are skipped, moved ones play in their new place.
  With Shuffle, added songs play after the rest of the round. Nothing restarts.

### Select, remove and move

In a playlist, **X** → **Select** (or a hold on an item) turns on select mode:

- **A** ticks an item (a tap does too). **SELECT** ticks them all, or clears them all.
- **X** → **Remove** takes the ticked items out of the playlist. Press it twice. The files stay where they are, and
  select mode ends.
- **Y** → **Move** rearranges them (below). **B** leaves select mode.

To move items: tick them, then press **Y** (**Move**). The ticked items gather into one block. **UP / DOWN** moves it a
row at a time, **LEFT / RIGHT** a page at a time. **A** drops it where it is, and **B** puts it back. On the touch
screen, drag the grip on the right of a row instead. **Move** in an item's **Y** menu starts a move too, and after it
you're back in the normal list.

Single items: an item's **Y** menu has **Play from here**, **Move**, **Remove from playlist** and **Show in folder** (which
opens the item's folder, with the file under the cursor).

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

- **Like:** the **Like** button (the heart, left of Play) while something plays, **START** → **Like** (the menu opens
  on it), or **L3** on the RG DS Plus. The heart turns yellow and the top screen says **Added to Liked** with the title
  (**Removed from Liked** when you press it again). A like is the file's, wherever it plays from.
- **Add to playlist:** the **Add** button (right of Play), or **START** → **Add to playlist…**. **New playlist…** comes
  first, then the playlists you changed last. A playlist that already has it says **✓ Added**: choosing it asks
  **Add again?**, and a second **A** adds another copy. **New playlist…** names one on the keyboard and adds to it. The
  button shows a check and the top screen says where it went.
- **Liked** is always first in **Playlists**, with a yellow heart. It's newest first and can't be reordered, renamed or
  deleted, and only Like adds to it. Inside it, an item's **Y** menu has **Play from here**, **Unlike**, **Add to
  playlist…** and **Show in folder**; select mode's **X** is **Unlike** (press it twice).
- **Home:** a liked file's Recent card has a small yellow heart, and a card's **Y** menu has **Like** / **Unlike** and
  **Add to playlist…**. In **Files** and **Network**, liked files have a small heart before their note.
- Liked holds 2,000; liking one more lets the oldest go. Likes made in the web dashboard show up on the handheld at
  once, and the other way round.

## Controls

### While playing

| Button | What it does |
|---|---|
| **A** | Play / pause |
| **LEFT / RIGHT** | Back 5 seconds / forward 10 seconds (hold to keep going; there's no on-screen button for these: drag the bar) |
| **L / R** | Previous (5 s or more in: back to the start) / next file in the folder |
| **L2 / R2** | Previous / next chapter (**F + L2** is still the screenshot) |
| **UP / DOWN** | Faster / slower (0.5x, 1x, 1.5x, 2x, 2.5x); the speed button becomes a yellow key when it isn't 1x |
| **X** | The play mode, on a video as on a song: In order → Loop all → Loop one → Shuffle (with Loop all); the key shows the loop mark with Off / All / One, or the shuffle mark with On / One / Once |
| **Y** | **Up next** (the **List** key): the folder or playlist that plays, in its own order, the one playing marked **Now** |
| **SELECT** | Video: **Tracks**, one page with four tabs in this order: **Chapters** (when the file has them), **Subtitles**, **Audio**, **Picture** (Fit, Fill, Stretch); **L / R** (or **Y / X**) switch them. It opens on the tab you used last (the first time: Chapters if the file has chapters, else Subtitles) · Music: the Display popup (what's shown, and the visualizer's style) |
| **START** | Menu, on its **Playing** tab: Like, Add to playlist…, Show in folder, Info, Stop |
| **L3** (press the stick, RG DS Plus) | Like / Unlike what plays |
| **B** (or **Back**) | Back to the list: it keeps playing (see [Playing while you browse](#playing-while-you-browse)) |
| **F + L2** | Screenshot of both screens (saved in the card's `screenshots` folder) |
| **F + START** | Quit Banana Player at once (anywhere; your place is kept) |
| **Menu/Function** (F) | Quick settings: volume, brightness, Same brightness for both screens, Web dashboard, Quit |

**By touch:**
- **Progress bar:** touch it to jump there. Hold and drag to scrub, and the picture follows your finger.
- **Buttons:** they act when you lift your finger, so slide off one to cancel.
- **Audio and subtitles:** on a video, tap the audio or subtitle line under the buttons for that tab of Tracks.
- **Sound:** on a song, tap the line under the buttons (it shows the preset and effect) to open the Sound page.
- **Chapters:** tap the chapter line above the progress bar for the Chapters tab.
- **The keys' second row**, the same on a video and a song: **List** (Up next, **Y**) under **Prev**, the play mode
  (**X**) under **Like**, then **Tracks** on a video (its tabs' marks, **SELECT**) or the music display on a song
  (**SELECT**), the speed (**UP/DOWN**) and **Menu** (**START**).
- **Lists:** tap to choose, drag to scroll.
- **Back to the list:** tap **<** (top left): it keeps playing. **Stop** (bottom right, in red) stops it, one tap.
- **Lock**, **Info** and **Screen off** are along the bottom, beside Stop.
- **<** is at the top left of every page and does what **B** does: in the file list it goes up a folder (on Home with
  nothing playing: tap twice to quit), on a page or in the menu it goes back.

**List** (**Y**) on a playlist's song opens that playlist's own page (its order, its edits), the cursor on the song,
what plays going on behind; the first time a playlist's song opens, the List key glows a soft yellow twice to say so.
**B** from the player goes back to where you opened it from (a folder, Home, Settings…), and from a playlist's page to
the list of playlists, the cursor on that playlist; back in the folder of what played (or the photo last viewed), the
cursor is on it.

Otherwise **Up next** (**Y**, or the **List** key) lists what plays from (a folder, a playlist, Recent's folder, a selection
from the web) in its own order: Shuffle never reorders it. The one playing is marked **Now** (**Paused**, or **Browser**
when a web page plays it), with each file's length where it's known; the page opens scrolled to it. **A** (or a tap)
plays that row as picking it from its list would (Shuffle and Loop stay as they are; with Shuffle on, a new order from
there), on the handheld or in the browser that plays the list; **A** on the one playing just goes back to it. **B**
goes back.

Under the progress bar you'll see the time played, the time left (at the current speed) and the total length.

The top line of the Main page shows what's playing from: the folder (`Videos / Movies`), or a playlist (its name, with a
list icon), then the card or the server it's on: **Local** or the server's name. With Shuffle on the status line says
**Playing · Shuffle**. On the right of the status line, `3 / 12` is the file's place in its folder or playlist (with
Shuffle on too: the list keeps its order).

### Looking at a photo

| Button | What it does |
|---|---|
| **LEFT / RIGHT** or **L / R** | Previous / next photo (hold to keep going) |
| **UP / DOWN** | Zoom in / out: 100%, 150%, 200%, 300%, 400%, 600%, 800% |
| **A** | Zoom to 200%, or back to the whole photo |
| **L3** (press the stick) | Rotate a quarter turn |
| **SELECT** | Fit (the whole photo) or Fill (the screen filled) |
| **START** | Menu |
| **B** (or **Back**) | Close the photo (**Close** at the bottom right too) |

See [Photos](#photos) for the touch screen.

### In the file list

| Button | What it does |
|---|---|
| **A** (or tap it) | Open the folder, play the file, or open the playlist |
| **B** or **Back** | Up one folder; at a section's top, back to Home |
| **..** (the first row) | In any folder below a section's top (a card's folder, a server's, a playlist): up one, as **B** goes. A folder opens on its first item, an empty one on this row. It's never ticked or counted |
| **UP / DOWN** | Move |
| **LEFT / RIGHT** | Page up / down |
| **L / R** | Previous / next section: Files, Playlists, Network, Extensions |
| **X** | Options: **Refresh** (read the folder again), and on a folder **Hide folder** / **Unhide folder** (or hold the folder on the touch screen). In Files: **Show hidden folders**. In Files and Network: **Select files**, to tick files and add them to a playlist. In Network: **Add connection**, **Find on the network**, **Refresh**, **Web dashboard…**. In Playlists: **New playlist…**. In All recent: Home's options |
| **Hold** (touch screen) | On a file in Files or Network: starts selecting, with that file ticked |
| **Y** | On a folder: **Add to playlist…** (first). On a network connection: **Edit connection**, **Test connection**, **Delete connection** (press twice) (or hold it on the touch screen). On a playlist or its item: see [Playlists](#playlists) |
| **SELECT** | Back to what plays (the playing screen); in select mode, Select all |
| **L3** (press the stick) | Like / Unlike what plays |
| **START** | Menu (on its **Playing** tab while something plays) |
| **Mini bar** (touch screen) | Tap: the playing screen · its play mark: play / pause · hold: its menu |

### The menu (START)

Two tabs, three while a song or video plays. Switch with **L/R** or a tap; **B**, **START** or **<** closes it. (What
you played last is on Home.)

- **Playing** (only while a song or video plays, and the tab it opens on, from the lists too): **Now playing** (from
  the lists: back to the playing screen), **Like** / **Liked**, **Add to playlist…** (with the playlists it's already
  in), **Play on** (while a [web dashboard](#web-dashboard) page that can play is open; it says where it plays now,
  see [Play on](#play-on-from-the-handheld)), **Show in folder**, **Info** and **Stop** (the place is kept). See
  [Like and Add to playlist](#like-and-add-to-playlist).
- **Settings:** see [Settings](#settings).
- **About:** version, licence and the libraries it uses.

To quit, use **Quit Banana Player** in quick settings (**F**), or **F + START** from anywhere.

## Network folders

Play videos, music and photos straight from a computer or a NAS on your Wi-Fi, over **SMB** (Windows file sharing,
most NAS boxes, Samba) or **FTP** (and FTPS). Nothing is copied to the card.

- With no connections yet, the Network section offers **Add connection** (tap it, or **A**).
- **Find on the network** (X in Network) lists the computers sharing files nearby (Windows, Mac, Linux, NAS):
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
screen says **Buffering**. If the connection is lost for good (20 seconds), it stops and says so.

## Photos

Pick a photo in the file list. The bottom screen shows the photo small, in the **pan area** (on the left; Settings ›
Photos can put it on the right), with a box around the part you see on top:

- **Drag** the box over the small photo to look around: the top screen follows it. Or, with Settings › Photos ›
  **Drag moves: The photo**, drag the photo itself, as on a phone. Let go while moving and it glides on.
- **Double-tap** to zoom to 200% on that spot. Double-tap again to see all of it.
- **Tap** somewhere on the small photo to look there.

The buttons beside it do the same as the handheld's: Prev, Next, Zoom −, Zoom +, Rotate, Fit/Fill, Info and Menu.

The next photo opens at the same zoom, in the same corner, which suits scanned comics page by page (turn this off with
**Keep zoom between photos**). A song or video playing when you open a photo plays on behind it (see
[Playing while you browse](#playing-while-you-browse)). Camera photos (JPEG) taken sideways are shown upright, and animated GIFs play. Photos don't go into
Recent. **Info** shows the photo's size, format, and when and with which camera it was taken.

## Picture size

On a video, **SELECT** › **Tracks** › **Picture** (the last tab): **A** chooses, the one in use is ticked.

| | |
|---|---|
| **Fit** | The whole picture, with black bars where its shape doesn't match the screen |
| **Fill** | Fills the screen; the edges of the picture that don't fit are cropped |
| **Stretch** | Fills the screen by stretching the picture |

## Music

Songs show their title, artist and album on the top screen. While a song plays:

- **SELECT: Display.** A popup on the bottom screen with two rows. **Show** is **Album art** (default),
  **Art + visualizer** (the visualizer over the bottom of the art), **Visualizer**, or **None** (just the title: the
  lightest on the battery). **Style** is the visualizer's look: **Peaks** (default: 64 coral bars,
  bass on the left, each with a green cap that stays at the bar's highest point and slowly falls back, over a faint
  grid), **Peaks Banana** (the same in BananaOS's colours: yellow bars, cream caps) or **Circle** (bars round a
  glowing ring, cyan to pink; with Art + visualizer, the album art sits inside the ring). Bars, Waves and Spectrum
  are retired: a saved choice of one of them becomes Peaks. The top screen
  changes as you choose. In the popup, the **D-pad** changes a setting, **SELECT** goes to the next Show mode, and
  **B** closes it. The Style row is greyed out for Album art and None, which don't show a visualizer, but it can still
  be changed. A song without art shows its title alone on Album art, and the visualizer on Art + visualizer. Album art
  can be inside the song file or a `cover.jpg` in the same folder.
- **Y: Up next**, as on a video. A song or audiobook with chapters shows them on the line above the progress bar: tap
  it for the **Chapters** page (**L2** / **R2** jump). The **Sound** page is the line under the buttons.
- **X: Shuffle and Loop**, in turn: **In order** (the key says **Off** by the loop mark), **Loop all** (**All**: the
  folder starts again after its last song), **Loop one** (**One**: the same song again) and **Shuffle** (**On** by the
  shuffle mark: Shuffle with Loop all, so it never stops), then In order again. The screen says which as it changes.
  - **Shuffle** never reorders what you see (the folder, the playlist, Up next keep their order): **Next** and a song's
    end go to a random one not played yet in this round, **Previous** goes back the way it came. The song playing when
    you turn it on (or off) plays on.
  - With **Loop all**, each round is followed by a new random one (never the same song twice in a row); with Loop off
    the list stops once every song has played; with **Loop one** the song repeats.
  - Shuffle and Loop are settings: they stay as they are for every folder, playlist or file you play next, here or from
    the web, and after a restart. The web sets them separately; a pair the key doesn't list shows the shuffle mark with
    **One** (Loop one) or **Once** (no loop), and the next press goes to In order.
  - Loop and Shuffle apply to videos as to songs (set them on a song's **X** key or in the web dashboard: a video's **X**
    is its audio and subtitles).

Settings › Music has the same two choices as **Music display** and **Visualizer**.

## Sound: equalizer and effects

The equalizer and the sound effects change the sound of music: songs and audiobooks. Videos are never changed. Open the
**Sound** page from the **Sound** button, the line under the buttons (see **By touch** under [Controls](#controls)), or
Settings › Music › **Sound**. It has two tabs: switch them with **L / R**, or tap a tab.

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
- That line, or **SELECT** on a video (the **Chapters** tab of **Tracks**), opens the chapters: every chapter with its start
  time, and the one playing marked. **A** (or a tap) plays from the chapter you pick, and **L2** / **R2** still work
  there.
- Marks on the progress bar show where each chapter starts.
- Audiobooks and songs with chapters also show the chapter under the title on the top screen.
- Nothing pops up over a film by itself: the card appears only when you jump with **L2** or **R2**.

All three are remembered.

## Settings

Settings opens on eight tiles (seven groups, and the [Web dashboard](#web-dashboard)), two to a row: move with the D-pad (**UP/DOWN** a row, **LEFT/RIGHT** a tile),
open one with **A** or a tap, and **B** goes back to them. In a group, **Y** goes to the group before it and **X** to
the one after (or tap the names either side of its title), and the line above the bottom says what the setting under the
cursor does. **Subtitle style...** at the end of the Subtitles list opens the Subtitles group; **B** there
goes back to the list.

| Group | Setting | Default | |
|---|---|---|---|
| **Subtitles** | Font, size, colour, border, border colour | Inter, Medium, White, Normal, Black | How plain subtitles (SRT, VTT…) look, with a live preview. Styled subtitles (ASS/SSA) keep their own look. |
| **Music** | Music display, Visualizer, Sound | Album art, Bars, Off | What the top screen shows for music (also on SELECT while a song plays). **Sound** opens the [Sound page](#sound-equalizer-and-effects): the equalizer and the effects |
| **Photos** | Keep zoom between photos | On | The next photo opens at the same zoom and place. Off: each opens whole |
| | Pan area | Left | Which side of the bottom screen the photo's pan area is on |
| | Drag moves | The box | A drag in the pan area moves the box over the small photo, or the photo itself (like a phone) |
| **Remember** | Remember position | On | Reopening a video continues where you stopped (songs always start from the beginning) |
| | Remember audio track | On | Keeps each file's audio track; a new file starts in the language you chose last |
| | Remember subtitles | On | The same, for subtitles |
| **Files** | Remember last folder | Off | The file list opens where you last were |
| | Show hidden folders | Off | Shows folders you've hidden (they contain a `.hidden` file), so you can unhide them |
| | Keep play history | On | Keeps Recent. Turning it off also deletes the history |
| | Network files in Recent | On | Files played from a network folder go in Recent too. Off: they're left out, and the ones already there are removed |
| | Clear play history | | Press **A** (or tap) twice. Positions you stopped at are kept |
| **Controls & screen** | Brightness (or Top and Bottom brightness) | as BananaOS has them | The screens' backlight, 10% to 100%: the same levels as BananaOS's own Settings and the brightness keys, kept after a restart |
| | Same for both screens | On | One brightness for both screens; off, each its own |
| | Button hotkeys | On | Off: buttons can't pause, skip or close while something plays (touch still can) |
| | Battery percentage | Off | Shows the battery's percent beside its icon (the icon, with a bolt while charging, is always there) |
| | Clock | 24-hour | The time in the top corner of the bottom screen, left of the battery: 24-hour (`14:32`), 12-hour (`2:32 PM`) or Off. Not over a photo |
| | Screen off after | 30 seconds | While a song or video plays, the bottom screen turns off by itself after that long without a touch or a button: Never, 15 or 30 seconds, 1, 2 or 5 minutes. Not while paused, and never over a photo. A touch or any button brings it back |
| **Updates** | Check automatically | On | Looks for a new version a few seconds after the start, in the background (Wi-Fi needed) |
| | Check for updates | | Looks now, and shows what it found |
| | Update to … | | Downloads the new version (shown when there's one), then **Restart now** |
| **Web dashboard** | Turn on or off, New PIN | Off | Control the player and manage files and playlists from a phone or computer on your Wi-Fi. See [Web dashboard](#web-dashboard) |

## Web dashboard

Turn on the web dashboard and open the handheld's address in a phone's or computer's browser. From there you can
control what's playing, look through the card, upload and organise your files, and make playlists. It's off until you
turn it on.

### Turning it on

- **Settings › Web dashboard**, or **Quick settings** (F) › **Web dashboard** (a tick box), or the Network section's
  **X** › **Web dashboard…**.
- The handheld's page shows a **QR code**, the **address** (for example `192.168.1.23:8080`) and a four-character
  **PIN**. Scan the QR code with your phone's camera: it opens the dashboard and signs you in. Or type the address into
  a browser and enter the PIN.
- The phone or computer must be on the same Wi-Fi as the handheld.
- It stays on across restarts until you turn it off. The PIN stays the same too, until you press **New PIN** on the
  handheld (press it twice). That signs out every browser.
- While the QR code is on the screen, the bottom screen turns off after 90 seconds without a press (a touch brings it
  back).

On the handheld, a **globe** beside the battery shows the dashboard is on: dim while no browser is connected, yellow when
one is. Tap it to open the page. When a browser signs in, a note pops up on the top screen.

Nothing runs while the dashboard is off. While it's on and idle it uses next to no battery, and a browser tab that's
hidden disconnects.

### Security

- Only devices on your home network (private addresses) can connect. Nothing outside it can.
- Wrong PINs: after five wrong tries from one device it waits 60 seconds before trying again, longer each time. After
  30 wrong tries in total the PIN changes and the handheld says so.
- A browser stays signed in for seven days.
- The connection is **not encrypted** (plain HTTP, no certificate). Use it on your home Wi-Fi only.
- The browser can only reach your media folders on the card. The player's own settings and the BananaOS system folders
  are never shown.

### In the browser

Three sections: **Playing**, **Files** and **Playlists**. On a phone they're the tab bar at the bottom, with a small
player floating over it; on a tablet an icon rail on the left and a player bar at the bottom; on a computer a sidebar
(the sections, a shortcut list of playlists, and a card with the handheld's battery, free space and **Sign out**) and
the player bar. On a phone, **Sign out** is behind the handheld button at the top right of Files and Playlists.
A video playing in this browser goes on, picture and all, when you leave **Playing**: it plays in the small player's
(or the player bar's) cover tile, without its subtitles; a tap on it opens **Playing** again, where it carries on
full size.

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
  **L / R** on the handheld move the browser's list on, however fast they're pressed: every press counts, and both end
  on the same file (**L** 5 s or more into the browser's song restarts it first, as Previous does everywhere).
- **Where it plays is one choice for the whole system**, the switch's: anything opened plays there, whichever device
  it was picked on. With **This browser** chosen, a song or video opened on the handheld (from the list, Recent, a
  playlist, Play folder, Shuffle play, the mini bar) or in another browser tab plays **in that browser**, in the
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
  here**), and **Play** carries on. **Hold A** is still the way to bring the sound back to the handheld.
  Leaving the handheld's player (**B**) changes nothing for the browser: its mini bar reads **Playing in Chrome on Mac**
  (tap its mark to play or pause the browser, hold the mark to take it over). **Stop** on the handheld stops the browser
  too (the browser stays chosen: the next pick plays there). **Stop** in the browser unloads the handheld's copy too.
- <a id="play-on-from-the-handheld"></a>**Play on, from the handheld** (START › Playing › **Play on**, shown while at
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
  off, in this browser or on the handheld), the player doesn't go blank: it keeps the last item, at its end, with **The
  end of <list>** and **Clear** above it. **Play** plays the list again from its first item, **Previous** the last one
  again (**Next** has nothing to go to), and **Up next** keeps the whole list, the last one marked **Finished**: a tap
  on any row plays the list from there. It plays where you play (this browser, or the handheld). The player bar and the
  phone's small player show it too (**Finished · <list>**; a tap opens the player). Nothing starts by itself; **Clear**,
  or **Stop** on the handheld, leaves **Nothing playing**. The handheld's own screens go back to the list as before.
  **Shuffle** and **Loop** still work there; with Shuffle on, **Play** starts the list again at a random song.
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
- **New folder**, **Rename**, **Move**, **Download**, and **Delete** (permanent: it asks first and can't be undone).
- **Play** a file or folder on the handheld or in this browser, or **Add to playlist**.
- **Select** several files and folders to do any of these at once (or a long press on a row; on a computer, the **Select**
  button, or the box that fades in over a row's icon after a moment's hover). A band of actions floats at the bottom. Rename works on one item at a
  time.
- **Up a folder** (the first row, **..**, and the arrow beside the folder path) goes to the parent folder;
  on a computer **Backspace** or **Alt+Up** does too. A place's top has none.
- Songs and videos on the card show their length before their size (`3:34 · 3.5 MB · Nov 10, 2026`), asked of the
  handheld for the rows as they scroll into view (none for the formats and files the handheld's list shows none for).
- On a computer, **Filter this folder** narrows the list as you type (**/** goes there).
- Uploads and new names are limited to music, video, photo and subtitle files, and images for covers. A folder can be
  deleted from the page only if everything in it is one of those.

**Playlists** are the same playlists as the handheld's Playlists section:

- Create, rename, delete (the files stay where they are), and open a playlist. The page shows them as cards. Past 12
  playlists: the ones played or added to lately in a **Recently used** row, then **Find a playlist** (**/** on a computer;
  Enter opens the first match), a sort (**A–Z**, **Recent**, **Size**) and **List** or **Grid**. The list (the default
  there) has a letter heading for each letter. The browser remembers the sort and the view. The computer's sidebar
  lists Liked, Recent and up to five recently used playlists, then **All N playlists**.
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
- **Liked** is pinned at the top of Playlists. It's newest first, with no reordering: each row has a heart that unlikes
  it (with **Undo**, which puts it back at the top), and **⋯ › Select…** unlikes several at once. A network file in it
  plays on the handheld only, and a file gone from the card is marked **Gone**.
- A like on the handheld shows up at once: the heart fills, and on the Liked page the new row comes in at the top, lit.

**Recent** is the handheld's Home Recent strip, in the browser: pinned under Liked on the Playlists page (its play
button resumes the latest), and right under Liked in a computer's sidebar.

- Newest first, as on the handheld (up to 30). Each row says where the video was left (**0:27 / 0:52**, with a yellow
  bar on its tile) or **Played**; what plays on the handheld is marked **Playing**, and a file gone from the card is
  dimmed and marked **Gone**. Network files aren't listed.
- **A tap** plays it where you last played (the handheld or this browser), from where it was left. **⋯** has **Play on
  handheld**, **Play in this browser**, **Like** / **Unlike**, **Add to playlist…**, **Show in Files** and **Remove from
  Recent** (the place it was left at is kept, as on the handheld).
- **⋯ › Clear Recent** (asks first) empties it, on the handheld too; the places your videos were left at are kept.
- It follows the handheld live: a file played, removed or cleared on either side shows on the other at once.

Changes show up live on both sides: what you do on the handheld appears in the browser, and the other way round.

Not yet: network folders in the browser, a grid of photo thumbnails, and saving equalizer presets from the browser.

## Extensions

Home's fourth tile, **Extensions**, opens the drawer: the extensions that are on, as tiles. Nothing is on at first: the
drawer says so, and **START** (or its **Manage extensions** button) opens **Manage extensions**, which lists each
extension with what it does and an **On/Off** switch (**A**). **X** (or the gear) opens the extension's own settings
page: for the reader, **Manga settings**, a page of the Manga app's (not one of Banana Player's Settings). Turning one
off hides it and stops it; nothing is deleted.

- **Manga reader**: comics and manga from the files on your cards. The first time it's turned on, a sheet says where
  to find it and that repositories are added by you: Banana Player doesn't provide or check their content.

### The drawer and Manage extensions

- With the reader on, the drawer shows **1 on** and a tile for it: **Manga reader**, *Comics and manga*. **A** opens
  it. With none on, the drawer says **No extensions are on** and explains that an extension adds to Banana Player and
  that nothing of it shows or runs until you turn it on.
- **Manage extensions** lists the reader with its mark, what it does and an **On/Off** switch. **A** turns it on or off;
  **X** (or the gear) opens **Manga settings**, the reader's own settings page.
- The first time the reader is turned on, a sheet comes over **Manage extensions**: it says the reader reads from your
  files and servers, and from sources in repositories you add, and that repositories are added by you: Banana Player
  doesn't provide or check their content. **Open Manga** opens the reader, **Done** closes the sheet.

### The Manga reader

The reader is an app of its own. It opens on **Library** and has five tabs, stepped with **L/R**: **Library**,
**History**, **Browse**, **Sources** and **Downloads**. **B** goes back — out of Browse's list of series first, then
to the drawer. **START** opens **Manga settings**, the reader's own settings page, on any of its pages (a series page
too); **B** there comes back where you were. Music keeps playing while you read: the pages take the top screen, and the
mini bar is on the app's other pages (**SELECT** there goes to what plays). **Sources** and **Downloads** say they come
next: there are no sources to install and nothing to download yet.

**Where series come from.** Each of your cards may have a **Manga** folder (or `manga`, or `MANGA`); the reader reads it
each time you open the app:

- a folder of pictures is a chapter;
- a folder of chapters (folders of pictures, or `.cbz` files) is a series;
- a `.cbz` file (or a `.zip` renamed to one) is a chapter, and one lying in the Manga folder itself is a series of a
  single chapter;
- one level of volume folders is read as well: a **Vol 1** folder holds the chapters of that volume, and they carry its
  number;
- `ComicInfo.xml` inside an archive gives the series' title and the chapter's number and volume;
- the chapters are listed in reading order (by volume and number where the names carry them, otherwise by name);
- a series' cover is its first chapter's first page: the real cover is decoded in the background, and a plain coloured
  one stands in until it arrives.

**Library.** *Continue reading* along the top: up to three cards with the cover, the series, the chapter and the page
you were on, and a bar of how far you got. **A** opens that chapter where you left it. Below, the series as covers,
five to a row, each with the number of chapters you haven't read. **A** opens the series. With nothing in it yet:
*Your library is empty* — put CBZ files or folders of pictures in a Manga folder on your card, or add series from
Browse.

**History.** Every chapter you've opened, newest first: the series, the chapter, the page and when. **A** continues
where you were. **X** clears the list (press **X** again to confirm). With the reading history off in **Manga
settings** the list stays empty, but your places and your read marks are kept either way.

**Browse.** *On this handheld* holds the series found on your cards; **A** opens the list of them, each with how many
chapters it has, and **A** on one opens the series. Below it are your saved servers and **Add server…**; network folders
are listed as coming next.

**Servers.** **Add server…** opens a form: **Kind** (Komga, Kavita or OPDS, LEFT/RIGHT), **Name**, **Address**, **User**
and **Password**, then **Test** and **Save**. A password is sealed on Save and never shown again; a typed address loses
any `user:password@` part before it is kept. **A** on a saved server opens its libraries (Komga) or its OPDS catalogue,
a cover grid; **A** on a series opens its chapters and **A** on one reads it, pages coming from the server with its
sign-in header. **X** on a server row opens **Edit** and **Delete** (Delete asks twice). Kavita is read through its OPDS
address, which carries the API key in the path as Kavita writes it.

**A series.** Its title, how many chapters it has and how many you haven't read. **Resume** opens the chapter you were
in, or the first unread one, or the first. The heart (**Y**) takes the series out of your library and puts it back;
every series found on your card is in the library to start with. The chapters are listed under: the one **Resume** would
open has a dot beside it, a chapter you've read is dimmed and ticked, and one you started shows the page you were on.
**X** marks the chapter under the cursor read or unread. **B** goes back to where you came from.

**Reading.** A chapter opens on the top screen; the bottom screen holds the controls: the chapter and the series, the
page number, the slider, the two chapter buttons and the mode. The big area says *Swipe here to turn the page* (or *Drag
here to scroll* in **Strip**), and *Loading…* shows beside the page number while a page is decoded.

| Button | What it does |
|---|---|
| **RIGHT** / **LEFT** | Forward / back: a screenful down the page, then the next page; back goes a screenful up it, then to the page before (in **Strip**, a screenful down or up). With **Swap Left/Right** on, **LEFT** is forward |
| **UP** / **DOWN** | Zoom in / out, in steps: 100, 150, 200, 300, 400, 600, 800 %. **A** zooms to 200 % or back to the fit |
| **L / R** | Previous / next page, straight to it |
| **X** | The reading mode: Right to left → Left to right → Strip (kept for that series) |
| **L2 / R2** | Previous / next chapter |
| **START** | The Reading menu (below) |
| **SELECT** | Fullscreen on both screens (below) |
| **B** | Leave fullscreen, then close the reader; your place is kept |

- The **page fit** is a setting (below): **Page** (the whole page on the screen), **Width** (as wide as the screen, read
  down a screenful at a time) or **Fill** (the page covers the screen: it's cropped, and the overshoot pans in reading
  order). It applies at once to the chapter you're reading.
- At the end of a chapter the screen says **End of chapter · R2 or RIGHT for the next** (**LEFT** with **Swap
  Left/Right** on), and the forward key (or **R2**) opens the next one. The last chapter says **You're caught up**.
- **Scrolling is the finger's alone**: the D-pad's **UP/DOWN** zoom, they never scroll. **By touch**: drag in the big
  area on the bottom screen to turn the page — it follows your finger and snaps to a page when you let go (a quick
  flick turns the page even from a short drag), and in **Strip** it scrolls — the slider jumps to a page, and the two
  buttons go to the chapter before or after. In fullscreen the whole bottom screen is the page, and the finger drags it
  anywhere.
- Your place is kept on the card, in the player's own folder (`home/.config/banana-player`): the page of every chapter
  you've opened, and when.

**The Reading menu.** **START** while reading opens it over the page (in fullscreen too): **Fullscreen**, **Reading
mode**, **Page fit**, **Zoom**, **Hinge gap** (fullscreen only), **Swap Left/Right** and **All Manga settings…**.
**UP/DOWN** choose a row, **LEFT/RIGHT** change it where you stand, **A** acts on it, and **B** or **START** closes the
menu: reading goes on exactly where it was. The last row opens **Manga settings**.

**Fullscreen (both screens).** **SELECT** while reading turns it on and off. The two panels become one tall view and
the gap under the hinge is skipped, so the art lines up across the fold; the bottom panel draws its share of the pages
from the pages the reader has already decoded. The gap's size is a setting of its own, **Hinge gap** in **Manga
settings**: **36** of the panel's 480 lines by default. **B** leaves fullscreen first, and only then closes the reader.
The page number shows for a moment after the page changes.

### Manga settings

The reader's own settings are a page of their own, **Manga settings**, inside the Manga app: they are not in Banana
Player's Settings, which has no Extensions group. **START** opens them on any Manga page (the five tabs and a series
page), the gear (**X**) in **Manage extensions** opens them too, and the Reading menu's last row opens them while you
read. **B** goes back where you came from.

| Setting | Values | Default | |
|---|---|---|---|
| **Reading mode** | Right to left / Left to right / Strip | Right to left | How a series opens until you pick one for it (X while reading). |
| **Page fit** | Page / Width / Fill | Page | Width: a page as wide as the screen, read down with RIGHT a screenful at a time. Fill: the page covers the screen, cropped either way, panned in reading order. |
| **Swap Left/Right** | Off / On | Off | Off: RIGHT is always forward. On: LEFT is forward (to match a right-to-left book). |
| **Hinge gap** | 0 to 80 px | 36 px | The strip skipped under the hinge in fullscreen, so pictures line up across the gap. |
| **Open fullscreen** | On / Off | Off | A chapter opens on both screens. SELECT or B leaves. |
| **Preload** | 2 / 3 / 6 pages | 3 pages | More: smoother on a slow server, more data. |
| **Page cache** | 100 MB / 300 MB / 1 GB | 300 MB | Pages kept on the card to read again without downloading. Covers: 50 MB more. |
| **Clear cache** | (a button: *Empty*, or *Clear · 214 MB*) | | Empties the pages kept on the card. Press A twice. |
| **Adult sources** | Hidden / Shown | Hidden | Sources a repository marks as adult. |
| **Reading history** | On / Off | On | Off clears it. |
| **In Recent on Home** | On / Off | On | |
| Repositories | | | Repositories are added by you. Banana Player doesn't provide or check their content. |

The reader itself is turned on and off in **Manage extensions** (or on the web dashboard): **A** on its row.

The page cache, **Adult sources** and **In Recent on Home** belong to the parts that aren't here yet, so the cache is
empty and the last two change nothing for now.

**Coming next:** network folders, sources from repositories, and downloads. None of that is in this version yet.

## Updates

Banana Player updates itself when it's installed on the Ports shelf (from 0.2.3 on; Wi-Fi needed):

- A few seconds after it starts, it looks for a new version in the background (turn this off in Settings › Updates ›
  **Check automatically**).
- When there's one, the file list asks: **Update now**, **Later** or **Not until tomorrow**.
- **Update now** downloads it and checks it against its published checksum (a damaged download changes nothing), then
  offers **Restart now**. Otherwise it's put in place the next time you start Banana Player.
- The version before is kept on the card as `roms/ports/BananaPlayer.old`.
- Installed from the BananaOS Store, the Store updates it instead: open the Store to see an update.

Your settings, resume positions and history are kept through every update: they live in `home/.config/banana-player`
on the card, not in the player's folder.

## Quick settings

Press the **Menu/Function** button (F), anywhere, for a panel on the bottom screen:

- **Volume** and **Brightness**: drag along the bar, tap **−** / **+**, or use **LEFT / RIGHT**. They change the same
  levels as BananaOS's own settings and its volume and brightness keys, and they're kept after a restart.
- **Same brightness for both screens** (a tick box): on, one Brightness bar sets both screens; off, **Top** and
  **Bottom** each get their own.
- **Web dashboard** (a tick box): on or off. While it's on, **Settings** (Right, then A, or a tap) opens its page: the
  QR code, the address, the PIN and **New PIN**. See [Web dashboard](#web-dashboard).
- **Quit Banana Player.**

**F** again, **B** or a tap outside the panel closes it. (F with another button, like F + L2 or F + volume, does that
combination instead.)

## More

- **Info** (while playing): file, size, length, resolution, codecs, frame rate, bitrate, audio format, every track and
  the file's tags.
- **Battery:** an icon on the bottom screen, red when it's low, with a bolt while charging (and its percent, if you
  turn that on in Settings).
- **Volume and brightness:** the keys show a level card on the top screen.
- **Loading:** a file that takes a moment to open (a big photo, a slow card) shows a spinner on the top screen.
- **Lock:** locks the touch screen while playing; Screen off still works. Hold the lock for a second to unlock.
- **Screen off:** turns the bottom screen off to save battery. While the player is showing, the playback buttons
  keep working with it off (**A**, **LEFT/RIGHT**, **L/R**, **L2/R2**, **UP/DOWN**, **L3**); a touch or any other button
  turns it back on (that press does nothing else). Elsewhere (a list), any button turns it back on. It also turns off by itself
  while a song or video plays and nothing is touched (Settings › Controls & screen › **Screen off after**).
- **Lid:** closing it pauses; opening it keeps it paused until you press play.
