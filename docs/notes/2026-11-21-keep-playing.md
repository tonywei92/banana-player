# Play random, Keep playing, and list section headings (2026-11-21)

The rules are in docs/design/session.md §19 (Extend, `rail`, `added`, I18). What else was learned:

- **Section headings** are general: `lay::Sections` (bpcore, tested in `testKitLayoutLists`) maps a list's rows to its
  items with headings put in; the cursor stays an item, so keys never land on a heading. `SmoothList::Spec::heading`
  makes `rowAt` answer -1 on one (touch skips it); `Kit::Row::heading` draws it (label, rule, an optional action chip
  whose box is `Kit::headingAction`). A heading has the row pitch of the others, so the smooth list's maths is
  unchanged. The List sheet's Up next is the first user; the dashboard's `ListHead` (components.js) is its twin.
- **When to add**: at the start of the list's last song, not at its end. Adding at the end would need the reducer to
  wait for the host (it has no library), and the song would stop first; adding a song ahead leaves the Session's own
  Ended rule untouched (§16) and shows the songs in Up next before they play.
- **A small library**: Play random takes 50; with 50 songs or fewer it takes them all, and Keep playing then has
  nothing new to add (logged: "every song of the library is in the list already"). The check run used 60 generated
  2 s tones.
- **Hint bar**: six hints didn't fit Music's views; **L3 Search** was dropped for **Y Random** (the rail has Search).
- **Test runs**: `BP_AO_PCM` is a file path (mpv's pcm output, appended, untimed). `BP_AO_PCM=null` wrote a 100 GB
  file named `null` in the working folder in two minutes; use `BP_AO_PCM=/dev/null`.

Checked: core-test (named case, fuzz, Sections), a `--press` run (Music › Y, R×49 → "keep playing: 10 songs added",
Up next's heading; with `set:keep-playing=no` the list stopped at its last song), one headless Chrome run (the Music
page's Play random → a rail list of 50; a page's Step to the end → 10 added; the queue's heading; Turn off → the
setting off).
