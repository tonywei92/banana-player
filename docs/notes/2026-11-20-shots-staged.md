# The shots staged for the visual audit (2026-11-20)

The session that built items 1, 3 and 4 of the status note's Next list could build and test but not see: the
permission config then allowed the scripted player runs, so the shots were taken and left here for a vision-capable
agent to read. They are in `.scratch/shots/` (git-ignored, this worktree), each as `<name>-{top,bottom}.png`, made
with the generated card `.scratch/media/` (two tagged albums with embedded art — an orange "Yellow Album", a blue
"Night Songs" — two test-pattern videos, and three books: a TXT, the comic EPUB fixture, the PDF fixture) under
`BP_BROWSE`, state `.scratch/st` (the TXT has a reading place, so the strip shows).

What each pair is, and what to look for:

| Pair | What it shows | What to check |
|---|---|---|
| `videos` | Videos, All | the "From …" line in the header's crumb only (no second line over the chips), the chips at the body's top, the rows' size and date |
| `music` | Music, Albums | the same single from-line, the tiles' real orange and blue covers (decoded, not the drawn record), the counts |
| `books` | Books | the "Shelf · 3" heading with a row of books under it (the old bug left none), the Reading now strip, the comic EPUB's own cover decoded against the drawn sleeves |
| `search` | Search, typing | the keyboard inside the shell's body margins — the old run's letter rows ran past the right edge |
| `player`, `player2` | a song with art, both panels | Match album art: the blurred orange art under the theme's veil behind the player, and on the top screen behind the art and lines; both shots are past the cross-fade |
| `display` | the song's Display sheet | the new foot row "Match album art · On" under the styles grid |
| `vmore`, `vsort` | Videos' More sheet | the file rows (Copy / Move / Rename / Delete) after Info, and the Sort row's stepped value ("Name" there) at the sheet's foot |

The state lines in the runs' logs also verified the structural side: `press N: search q= cursor=0 typing` (Search's
own line, once printed as "settings"), `books reading=1 shelf=3`, and the backdrop's own log lines (`backdrop: the
art's hue for …`, `backdrop: the top panel composes the song's ground`).
