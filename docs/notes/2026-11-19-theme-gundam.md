# Gundam, the eighteenth theme (2026-11-19)

The owner's five colours (white #FFFFFF, yellow #FFF867, red #FB2F38, blue #2C52B3, grey #5A5B6D) and, the same day,
a mock-up of the player in them, whose colours this follows (its cut corners, corner tabs and stripes were not asked
for: colours only). Light-bodied, after Sakura in the stepper.

The values were taken again the same day (owner): a grey-blue ground, one blue for keys and accent, a navy frame, the
yellow where you are. `theme-gundam` keeps the first values beside them; the app uses these.

It is the first theme with **strong keys** (theme.h: `strongKeys`, `KEY`, `ON_KEY`, `KEY_FOCUS`, `HINT_BAR`, `ON_HINT`,
`PRIMARY`, `ON_PRIMARY`, `ON_RAIL`; the optional tail of a `BASES` line). Unset, they are BUTTON, TEXT, ACCENT, BG,
DIM_ON, ACCENT, ON_ACCENT and DIM_ON, so every other theme draws as before (a test checks it). Set: `Kit::key` and
`Kit::iconKey` are filled KEY with ON_KEY lettering and caps, a danger key is filled DANGER, a focused key has a
KEY_FOCUS edge; a primary key (Kit::key's Primary tone and the player's Play/Pause) is PRIMARY; the hint bar is HINT_BAR
with ON_HINT words and KEY_FOCUS caps. The rail's icons and labels are ON_RAIL, and its active cell is KEY_FOCUS with
TEXT ink. The transport keys, tiles, rows, tabs and pills keep the ordinary roles (white keys with navy ink in the
mock-up). Clean keeps a key a key (rule 7), so a named key stays KEY with ON_KEY lettering; the primary key, the hint bar
and the rail keep their strong roles.

| Role | Value | |
|---|---|---|
| BG, SURFACE, BUTTON, PRESSED | #D9DFEC, #FFFFFF, #ECEFF6, #CBD3E4 | a grey-blue ground, white tiles, one quiet key fill |
| TRACK, RULE, RAIL | #C6CEDF, #B4BDD0, #172E6B | the navy rail |
| TEXT, DIM, DIM_ON | #141B33, #53546A, #43445A | navy-black; the grey |
| ACCENT, ON_ACCENT | #2C52B3, #FFFFFF | the bar, caps, Playing, focus (5.3 on BG) |
| DANGER | #B81F28 | (4.8 on BG) |
| KEY, ON_KEY, KEY_FOCUS | #2C52B3, #FFFFFF, #FFF867 | the blue key (7.1 with white), the yellow as the chosen key's edge and the rail's active fill |
| HINT_BAR, ON_HINT | #172E6B, #FFFFFF | (12.8 : 1) |
| PRIMARY, ON_PRIMARY | #D3222B, #FFFFFF | the red, deepened until white holds 4.5 (5.2; #FB2F38 as given is 3.8) |
| ON_RAIL | #C9D3EE | the rail's icons and labels on the navy (8.6 : 1) |

EDGE_MIX 55, the Light page, Midnight blue's top cards. Checked with themes.py's pairs on a copy (all pass). The
dashboard gets the same values from `themeCss`, with `--on-rail` beside the other strong roles.
