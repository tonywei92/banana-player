# Small gaps (status note item 8), 2026-11-20

- **a. Play next** (Files' More): new Session intent `PlayNext` (docs/design/session.md section 18, named test, fuzz).
- **b. Recent titles** (ui_home.cpp `listRecent`): the title from `MediaIndex::snapshot()` songs (the index already falls back to the file name for an untagged file). Only Music-folder songs are indexed, so a song outside them keeps its file name. The artist is not on the card (it has no line for it).
