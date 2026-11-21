# Match album art: the owner's tuned values (2026-11-21)

The owner tuned the backdrop in a tuner page that mirrors `src/artbackdrop.cpp/.h`. Every tuned constant is now a named
`constexpr` in `src/artbackdrop.h` (same names as the tuner); restore the old look by putting the PREVIOUS column back
there. `CACHE_VERSION` went 3 to 4 (cached backdrops hold the percentiles and bands). Line numbers are the old code's.

| Name | Previous | New | Was at |
|---|---|---|---|
| BLUR_SIGMA | 10 | 12 | artbackdrop.cpp:226 (literal) |
| BLUR_RADIUS | 30 | 27 | artbackdrop.h:BLUR_RADIUS |
| SATURATION | 0.55 | 1.03 | artbackdrop.cpp:273 |
| CANVAS_X0 / CANVAS_W | 40 / 80 | 33 / 96 | artbackdrop.h |
| BAND_ROW | (480+168)*H/960 = 81 | 112 | artbackdrop.h |
| HUE_L_MIN / HUE_L_MAX | 0.10 / 0.90 | 0.10 / 0.90 | artbackdrop.cpp:178 |
| HUE_S_MIN / HUE_S_MAX | 0.10 / 0.85 | 0.10 / 0.85 | artbackdrop.cpp:179 |
| HUE_SHARE_MIN | 0.05 | 0.05 | artbackdrop.cpp:180 |
| CHARCOAL_MIX | 50 | 50 | artbackdrop.cpp:296 |
| DARK_TINT_S / L / MIX | 0.15 / 0.13 / 45 | 0.15 / 0 / 54 | artbackdrop.cpp:368-371 |
| FALLBACK_TOP_MIX | 35 | 24 | artbackdrop.cpp:306 |
| LIGHT_TINT_S / L / MIX | 0.34 / 0.82 / 70 | 0.41 / 0.57 / 42 | artbackdrop.cpp:325-329 |
| LIFT_L0 / L1 / S | 0.70 / 0.92 / 0.85 | 0.73 / 0.90 / 0.96 | artbackdrop.h |
| VEIL_BASE | 58 52 74 86 92 | 58 52 74 86 92 | artbackdrop.h |
| LIST_VEIL_BASE | 44 44 50 54 58 | 45 44 50 54 60 | artbackdrop.h |
| LIFT_VEIL_BASE | 30 30 36 42 48 | 39 44 68 61 68 | artbackdrop.h |
| VEIL_CAP | 96 | 91 | artbackdrop.cpp:341,348,350,384,391,393 |
| CONTRAST_TEXT | 4.5 | 4.1 | artbackdrop.cpp:345,387-388 |
| CONTRAST_DIM | 3.0 | 3.4 | artbackdrop.cpp:345,387 |
| CONTRAST_ACCENT | 3.0 | 3.7 | artbackdrop.cpp:346 |
| PERCENTILE_HI / LO | 0.95 / 0.05 | 0.76 / 0.31 | artbackdrop.cpp:280-283 |
| CLASH_ACCENT_S_MIN | 0.30 | 0.30 | artbackdrop.cpp:317 |
| CLASH_HUE_GAP | 150 | 150 | artbackdrop.cpp:317 |

Unchanged values are listed so the whole set is named. Note: the player's dark veil used CONTRAST_TEXT for both the
text stops and the DIM_ON stops (as before, both at 4.5).
