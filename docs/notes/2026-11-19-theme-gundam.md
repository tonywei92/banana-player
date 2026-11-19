# Gundam, the eighteenth theme (2026-11-19)

The owner's five colours (white #FFFFFF, yellow #FFF867, red #FB2F38, blue #2C52B3, grey #5A5B6D) and, the same day,
a mock-up of the player in them, whose colours this follows (its cut corners, corner tabs and stripes were not asked
for: colours only). Light-bodied, after Sakura in the stepper.

It is the first theme with **strong keys** (theme.h: `strongKeys`, `KEY`, `ON_KEY`, `KEY_FOCUS`, `HINT_BAR`, `ON_HINT`,
`PRIMARY`, `ON_PRIMARY`; the optional tail of a `BASES` line). Unset, they are BUTTON, TEXT, ACCENT, BG, DIM_ON, ACCENT
and ON_ACCENT, so every other theme draws as before (a test checks it). Set: `Kit::key` and `Kit::iconKey` are filled
KEY with ON_KEY lettering and caps, a danger key is filled DANGER, a focused key has a KEY_FOCUS edge; a primary key
(Kit::key's Primary tone and the player's Play/Pause) is PRIMARY; the hint bar is HINT_BAR with ON_HINT words and caps.
The transport keys, tiles, rows, tabs and pills keep the ordinary roles (white keys with navy ink in the mock-up).

| Role | Value | |
|---|---|---|
| BG, SURFACE, BUTTON, PRESSED | #E8ECF4, #FFFFFF, #F3F5FA, #D5DDEE | a cool pale ground, white tiles |
| TRACK, RULE, RAIL | #CDD4E3, #C3CBDB, #DFE4EE | |
| TEXT, DIM, DIM_ON | #141B33, #5A5B6D, #464755 | navy-black; the grey as given |
| ACCENT, ON_ACCENT | #1A56C9, #FFFFFF | the bar, caps, Playing, focus (5.0 on its badge tint) |
| DANGER | #B81F28 | (4.6 on its pill tint) |
| KEY, ON_KEY, KEY_FOCUS | #1C3C8E, #FFFFFF, #FFF867 | navy keys (10 : 1), the yellow as the chosen key's edge |
| HINT_BAR, ON_HINT | #172E6B, #FFFFFF | (12.8 : 1) |
| PRIMARY, ON_PRIMARY | #D3222B, #FFFFFF | the red, deepened until white holds 4.5 (5.2; #FB2F38 as given is 3.8) |

EDGE_MIX 55, the Light page, Midnight blue's top cards. Checked with themes.py's pairs on a copy (all pass).
