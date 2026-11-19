# Gundam, the eighteenth theme (2026-11-19)

Built from the owner's five colours: white #FFFFFF, yellow #FFF867, red #FB2F38, blue #2C52B3, grey #5A5B6D. Light-bodied,
after Sakura in the stepper. One line in `src/theme.cpp`; the dashboard's CSS and the setting's choices come from that table.

| Role | Value | From |
|---|---|---|
| SURFACE | #FFFFFF | white, as given (the armour: tiles, rows, sheets) |
| ACCENT | #2C52B3 | blue, as given (7.1 : 1 on white) |
| ON_ACCENT | #FFF867 | yellow, as given: lettering on the blue keys and on Quit's red (6.4 and 6.1 : 1) |
| DIM | #5A5B6D | grey, as given (5.7 on BG, 6.7 on white) |
| DANGER | #B02127 | the red 30% towards black |
| TEXT, DIM_ON | #282931, #464755 | the grey 55% and 22% towards black |
| BG, RAIL, TRACK, RULE | #EDEDEF, #E5E5E8, #DEDEE2, #CDCED3 | white with 11, 16, 20, 30% of the grey |
| BUTTON, PRESSED | #DDE3F3, #BBC8E7 | white with 16, 32% of the blue |

EDGE_MIX 55, the Light page, Midnight blue's top cards (navy over a film).

What did not work as given:
- Yellow on white is 1.1 : 1: no text, edge, bar or pressed fill on the light body. On the blue it is 6.4 : 1, so it is
  ON_ACCENT, which is also where the suit wears it.
- Red as given is 3.8 : 1 on white and 3.2 on BG; yellow on it is 3.4. DANGER needs 4.5 as text on BG, SURFACE, BUTTON
  and its own 20% tint, so it is deepened; the tint pair is the one that sets how far (26% still fails at 4.47).

`docs/redesign/themes.py` is on the `redesign-preview` branch, not on main. Its checks were run on a copy with this
entry (all pass; lowest: DANGER on its tint 4.8, DANGER on BUTTON 5.3, ACCENT on its tint 5.5, DIM on BG 5.7; edges 3.5
and 3.0). The entry to add there, in ORDER after `sakura`, with the Themes tables of components.md and the preview:

```python
 'gundam': dict(name='Gundam', what='From a five-colour palette: a white mobile suit. White surfaces on pale grey, blue keys with yellow lettering, dark grey text; light-bodied; DANGER is the red deepened',
                BG='EDEDEF', SF='FFFFFF', BT='DDE3F3', PR='BBC8E7', TR='DEDEE2', RU='CDCED3', TX='282931', DM='5A5B6D', DO='464755', AC='2C52B3',
                OA='FFF867', DG='B02127', RA='E5E5E8', EM=55, PT=22, top='blue', page='light'),
```
