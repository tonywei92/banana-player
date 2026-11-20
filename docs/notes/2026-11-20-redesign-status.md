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

Done since: items 1–8 (Manga fully on the kit, Settings' rows incl. Top screen off for music, Play next, Recent
by tag, comics ask, photo More). Left:
1. **Manga:** a source's keys (spec: Y Filters, X More); a source's and a server's pages untried (no source on desktop).
   The browser's network folders still use the 0.88 scaling.
2. **Server group copy / move** and **mpv's top-screen song lines font** (open decisions).
3. **Before a release:** tools/web-smoke.sh, the arm64 Docker check for Linux-only code, README pictures regenerated,
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
