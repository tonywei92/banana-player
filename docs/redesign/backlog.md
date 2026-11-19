# Design backlog

What the redesign spec doesn't draw yet, found by the journeys audit (`journeys.py`, coverage.md › Journeys,
2026-11-19). Each is to be designed before its screen is built. Ordered by how much a journey depends on it.

## Gaps

1. **A playlist's inside, select mode and reorder (grip):** two screens. The only core journey that can't be walked:
   today a playlist's inside exists only as its "Gone items" state.
2. **Liked's own list:** the Like note and Home › Shuffle Liked are drawn; the list itself is not.
3. **Update states:** Downloading, Restart now, Didn't work: three small cards on `update` (only "is out" is drawn).
4. **More (X) menus** for a Home card, a playlist, a book, the readers, a PDF, a photo, a song, the comic reader, a
   connection, select mode, a search result, an album and an artist. They follow the `actions` pattern; draw the
   reader's and a Home card's, keep the rest as lists in `keys`.
5. **Pages grid** (PDF, comic) and the **View sheet for a PDF and a photo:** draw one of each.
6. **Comic reader's View sheet:** SELECT steps the three modes as a stand-in today.
7. **Music:** a song's List and More from Files (no song row to press there); an artist's page (only a mini today).
8. **Small screens:** Network with no connection, All recent, Manga › History, the dashboard page's Off and No Wi-Fi
   views.
9. **The phone's dashboard pages** for the web-dashboard journey (the handheld side is wired).
10. **Photos:** View and More.

## Decisions waiting on the owner

- **L3 while browsing:** Search everywhere in the shell (recommended), with Like on the player and in More; today
  `playlists` and `browser-mini` say "Like what plays".
- **Music's place:** a rail section (drawn now; the rail scrolls with Manga on, Network out of view unless open) or
  tabs inside Playlists.
- **A Songs list in Music**, or Search alone for finding a song (recommended: none for now).
- ~~The brand mark~~ **Decided (owner, 2026-11-19):** the brand mark is the app's icon, so it becomes a small
  version of the new clapperboard icon (simplified, flat, in the theme's roles, 22 units) on every pane that shows it
  (about forty). To draw.
- **Circle** is gone from the app (the final fifteen styles); nothing to decide unless it comes back.

## Visualizers (the app, 2026-11-19)

The final fifteen, in order: Peaks, Mirror bars, Pocket console, Night city, Horizon, Aurora, Dots, Steps, Barcode,
Rings, Banana dancer, Pixel sea, Jelly wobble, Washing machine, Tube man. The spec's style lists still name the
earlier set and need the same update when it is next touched.
