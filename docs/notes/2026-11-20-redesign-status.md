# Redesign: where it stands, and what is next (2026-11-20)

Branch `redesign-2026-11-20` (= `redesign-impl` at the time of writing); the spec is on `redesign-preview`
(`docs/redesign/`, published as the design page). Nothing is merged to main or released: that waits for the owner.

## Built (on the branch)

- **Shell:** the rail (Search key, Home, Files, Videos, Music, Books, Extensions, Network; the Now playing key above
  Settings; a 3-unit accent bar on the active section), the header, the hint bar. No mini bar anywhere; START opens
  Settings; L3 and the rail's key open Search.
- **Look:** Clean is the only style (High contrast stays Classic); keys stay keys (every labelled key, the player's
  keys and chips, Home's shortcuts keep edge and lip). Type: all Pixelify Sans (IBM Plex Sans is bundled, unused).
  Themes: Gundam's proposed values, Dark's Bone accent. Spacing on the scale (lay::SP2..SP24).
- **Pages:** Home (Now playing / Continue / Last played card, Recent with real album art), Files, Playlists (Music's
  third view), Settings (ten groups), Extensions (Manga inside it, the stop-the-video question), Music (albums,
  artists, album and artist pages), Videos (All / Folders / Not finished), Books (Reading now, shelf), Search
  (keyboard, grouped results, recent searches), Network (list, form, Find), the Web dashboard page, the readers'
  chrome and the sideways Book bar, Info in groups, the update sheets, All recent by day.
- **Player:** chips open their own sheets (no tabs), Repeat / Shuffle change in place, More opens on the chips,
  SELECT opens More, UP / DOWN are the volume (a hold stops at 100%), Next / Previous play from a pause.
- **Quick settings:** a centred panel on the kit, the Now playing block always (idle when nothing plays), real art.
- **Media index:** Music (Music / Musics folders, tags), Videos (Videos / Movies folders, /data/roms/*), incremental
  cache, background worker, search.
- **Visualizers:** Faders (for Pocket console), Lanterns (for Rain check), Bouncy keys fixed, cards see-through.

## Next (in order)

1. **Fixes seen in the last shots:** Music's and Videos' "From … on this card" line drawn twice; Books' shelf
   heading with no books under it; Search's keyboard runs past the right margin; the state line logs the Search page
   as "settings".
2. **Check on the handheld** everything merged since the last push (it was offline): readers, Info, updates, All
   recent, Search, Network, the Web page, Music / Videos / Books, the new rail.
3. **Match album art:** wire the built pieces (artbackdrop maths, Kit::backdrop) to the song player and the top screen,
   on the art worker, with the cache, the 0.5 s fade and the switch (Settings › Music & sound, the Display sheet).
4. **Library pages, the rest:** real covers on album tiles (seen only with art-less test songs so far), Books' covers
   (decoded, not drawn), Videos' More (Copy / Move / Rename / Delete / Sort), a progress bar on "reading the cards",
   a playing mark on video rows.
5. **The web dashboard's tabs:** Home, Files, Videos, Music (Playlists inside), Books, as the spec draws.
6. **Manga's pages inside the shell** (today they keep their own frame; L / R still step Manga's tabs).
7. **Settings' missing rows:** Picture look, Top screen off for music, Dim before off, Stay awake, Dim controls while
   reading; the video looks (Sharpen, Scanlines…) and keeping the player's Adjust.
8. **Smaller gaps:** Files' Play next; group copy / move to or from a server; Recent cards titled by the song's tag;
   comics opened from Files / Recent should ask before stopping a video; the photo More's Slideshow / Copy / Move /
   Delete; the top-screen song lines in Pixelify (mpv draws them in Inter).
9. **Before a release:** tools/web-smoke.sh, the arm64 Docker check for Linux-only code, README pictures regenerated,
   the owner's go.

## Decisions still open

- The brand mark's stripes (ACCENT on BG, drawn; or ACCENT and TEXT).
- The rail's Now playing key details (position above Settings, hold opens More, a kept slot): drawn, not confirmed.
- Whether the top-screen song title and artist (mpv's lines) move to the UI's face.

## Where things are

- Spec and decisions: `docs/redesign/backlog.md` (Decided / waiting), `components.md`, the design page.
- Per-step notes: `docs/notes/2026-11-19-*.md` (kit inventory, kit pages, media index, library pages, Quick settings,
  Clean style, spacing scale, readers / info / updates, search / network / web).
- How agents are run: `docs/conducting-agents.md` (local, not committed).
