# Small gaps (status note item 8), 2026-11-20

- **a. Play next** (Files' More): new Session intent `PlayNext` (docs/design/session.md section 18, named test, fuzz).
- **b. Recent titles** (ui_home.cpp `listRecent`): the title from `MediaIndex::snapshot()` songs (the index already falls back to the file name for an untagged file). Only Music-folder songs are indexed, so a song outside them keeps its file name. The artist is not on the card (it has no line for it).
- **c. Comics and a video** (ui_docs.cpp `Docs::open`, ui_ext.cpp `MangaGuard`): the Manga guard sheet takes a `go` continuation (stop the video, open the comic) and a title; same `mangaAskVideo` setting. Before this a comic opened over a playing video did not stop it at all; the spec (components.md, "Code") says every way in that takes a video's memory asks, so it now stops the video after the question. Not run on screen (no shots spent): worth a look on the handheld.
