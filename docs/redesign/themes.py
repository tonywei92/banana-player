#!/usr/bin/env python3
"""The redesign's themes: every role's value per theme, and the contrast of each pair that must stay readable.
`python3 themes.py`        the contrast table (exit 1 if a pair is under its floor)
`python3 themes.py css`    the blocks pasted into src/style.css
`python3 themes.py md`     the roles table pasted into components.md
A theme gives the base roles; the edges, lips, rail and tints are worked out as src/theme.h does (edgeOf, lipOf, mix)."""
import sys

# base roles per theme, by src/theme.h's names (short keys below: ROLES). EM: EDGE_MIX, how far an edge goes
# from its fill towards TEXT, in percent. top: the theme whose values the TOP_* tokens take (itself unless named).
# PT: how strong a preview pane's decorative tint is over BG (the preview only).
# page: the reader's page colours that "Match theme" picks (text-theme's own ids; not roles).
T = {
 'yellow': dict(name='Yellow', BG='000000', SF='121116', BT='26242E', PR='3E3B4A', TR='1E1D24', RU='2A2833', TX='E8E2D3',
                DM='8F8A80', DO='ABA597', AC='FFD23F', OA='000000', DG='F0645A'),
 'dark': dict(name='Dark', BG='000000', SF='141416', BT='27272B', PR='404046', TR='1F1F23', RU='2C2C31', TX='D2D2D8',
              DM='8C8C94', DO='A9A9B1', AC='FFFFFF', OA='000000', DG='F47067'),
 'darkred': dict(name='Dark red', BG='0A0405', SF='190C0E', BT='361D21', PR='4D2A2F', TR='241315', RU='382224', TX='EFE1DD',
                 DM='A08B87', DO='BCA8A4', AC='FF6A6E', OA='000000', DG='FFB224'),
 'pink': dict(name='Pink', BG='0C060A', SF='1B0F17', BT='301C29', PR='4B2E41', TR='25151F', RU='37222F', TX='F1E2E9',
              DM='A18B97', DO='BAA5B1', AC='FF8FC7', OA='000000', DG='FFB224'),
 'blue': dict(name='Midnight blue', BG='040816', SF='0C142E', BT='1A2649', PR='2C3C6C', TR='131C3A', RU='202C52', TX='E1E7F5',
              DM='8893B0', DO='A5AFCA', AC='8CC8FF', OA='000000', DG='FF7A70'),
 'light': dict(name='Light', BG='F7F4EC', SF='FFFDF8', BT='E6E0D2', PR='D0C9B8', TR='DED8C9', RU='D5CEBE', TX='1B1A20',
               DM='625E56', DO='55514A', AC='7A5200', OA='FFFDF8', DG='A3261D', RA='ECE7DB', EM=55, PT=22, top='yellow', page='light'),
 'contrast': dict(name='High contrast', BG='000000', SF='0A0A0A', BT='1E1E1E', PR='3A3A3A', TR='2B2B2B', RU='5E5E5E', TX='FFFFFF',
                  DM='C6C6C6', DO='D8D8D8', AC='FFE14D', OA='000000', DG='FF8A80', EM=50, page='black'),
}
ORDER = ['yellow', 'dark', 'darkred', 'pink', 'blue', 'light', 'contrast']
PAGES = {'dark': ('1B1A20', 'E8E2D3', '8F8A80'), 'light': ('F4F1E8', '222222', '6B675E'), 'black': ('000000', 'BBBBBB', '8A8A8A')}

def rgb(h): return [int(h[i:i + 2], 16) for i in (0, 2, 4)]
def hx(c): return '%02X%02X%02X' % tuple(c)
def edge(c, tx, pct=30):  # theme.h's edgeOf: pct of the way from the fill to TEXT, rounded
    return hx([a + int(((b - a) * pct + (50 if b >= a else -50)) / 100) for a, b in zip(rgb(c), rgb(tx))])
def lip(c): return hx([(v * 7 + 5) // 10 for v in rgb(c)])
def mix(under, c, pct): return hx([u + round((v - u) * pct / 100) for u, v in zip(rgb(under), rgb(c))])
def lum(h):
    f = lambda v: v / 12.92 if v <= .03928 else ((v + .055) / 1.055) ** 2.4
    r, g, b = [f(v / 255) for v in rgb(h)]
    return .2126 * r + .7152 * g + .0722 * b
def cr(a, b):
    x, y = sorted([lum(a), lum(b)], reverse=True)
    return (x + .05) / (y + .05)

def full(k):
    t = dict(T[k]); em = t.setdefault('EM', 30)
    t['KN'] = t['TX']; t['SC'] = t['BT']
    t['ED'] = edge(t['BT'], t['TX'], em); t['ES'] = edge(t['SF'], t['TX'], em); t['LP'] = lip(t['BT'])
    t['AE'] = edge(t['AC'], t['TX'], em); t['AL'] = lip(t['AC'])
    t['DE'] = edge(t['DG'], t['TX'], em); t['DL'] = lip(t['DG'])
    t.setdefault('RA', mix(t['BG'], t['SF'], 60))
    t['ATS'] = mix(t['SF'], t['AC'], 17); t['DTS'] = mix(t['SF'], t['DG'], 20)   # a badge's and a red pill's ground
    t['PG'] = PAGES[t.get('page', 'dark')]
    return t

def top(k):  # the TOP_* tokens: what is drawn over a picture or in its place. Dark in every theme
    s = full(T[k].get('top', k))
    return dict(TC=mix(s['BG'], s['SF'], 60), TE=s['RU'], TT=s['AC'], TX=s['TX'], TS=s['DM'], TK=s['BT'], TQ=s['SF'], TD=s['DG'],
                OA=s['OA'], EM=s['EM'])

# (what, ink, ground, floor)
def pairs(k):
    t, o = full(k), top(k); F = 7 if k == 'contrast' else 4.5
    return [('TEXT on BG', t['TX'], t['BG'], F), ('TEXT on SURFACE', t['TX'], t['SF'], F), ('TEXT on BUTTON', t['TX'], t['BT'], F),
         ('TEXT on TRACK (a neutral pill)', t['TX'], t['TR'], F),
         ('DIM on BG', t['DM'], t['BG'], F), ('DIM on SURFACE', t['DM'], t['SF'], 4.5),
         ('DIM_ON on BUTTON', t['DO'], t['BT'], 4.5), ('DIM_ON on TRACK (a chip, off)', t['DO'], t['TR'], 4.5), ('DIM_ON on RAIL', t['DO'], t['RA'], 4.5),
         ('ACCENT on BG', t['AC'], t['BG'], F), ('ACCENT on SURFACE', t['AC'], t['SF'], F),
         ('ACCENT on BUTTON (the focused row)', t['AC'], t['BT'], 4.5), ('ACCENT on its 17% tint (a badge)', t['AC'], t['ATS'], 4.5),
         ('ON_ACCENT on ACCENT', t['OA'], t['AC'], F), ('ON_ACCENT on DANGER (Quit)', t['OA'], t['DG'], 4.5),
         ('DANGER on BG', t['DG'], t['BG'], 4.5), ('DANGER on SURFACE', t['DG'], t['SF'], 4.5),
         ('DANGER on BUTTON', t['DG'], t['BT'], 4.5), ('DANGER on its 20% tint (a red pill)', t['DG'], t['DTS'], 4.5),
         ('Focus edge (ACCENT) against BG', t['AC'], t['BG'], 3), ('Focus edge against SURFACE', t['AC'], t['SF'], 3),
         ('edgeOf(BUTTON) against BG', t['ED'], t['BG'], 3), ('edgeOf(SURFACE) against BG (as today: a flat piece is its fill and edge)', t['ES'], t['BG'], 2.4),
         ('Bar fill (ACCENT) against TRACK', t['AC'], t['TR'], 3), ('KNOB against TRACK', t['KN'], t['TR'], 3),
         ('TOP_TEXT on TOP_CARD', o['TX'], o['TC'], F), ('TOP_SUB on TOP_CARD', o['TS'], o['TC'], 4.5),
         ('TOP_TITLE on TOP_CARD', o['TT'], o['TC'], F), ('TOP_DANGER on TOP_CARD', o['TD'], o['TC'], 4.5),
         ('Reader page (Match theme): text on page', t['PG'][1], t['PG'][0], F)]

# (theme.h token, key, the preview's CSS variable)
ROLES = [('BG', 'BG', '--bg'), ('SURFACE', 'SF', '--sf'), ('BUTTON', 'BT', '--bt'), ('PRESSED', 'PR', '--pr'), ('TRACK', 'TR', '--tr'),
         ('RULE', 'RU', '--ru'), ('RAIL', 'RA', '--ra'), ('SCREEN', 'SC', '--sc'), ('TEXT', 'TX', '--tx'), ('DIM', 'DM', '--dm'),
         ('DIM_ON', 'DO', '--do'), ('KNOB', 'KN', '--kn'), ('ACCENT', 'AC', '--ac'), ('ON_ACCENT', 'OA', '--oa'), ('DANGER', 'DG', '--dg')]
DERIVED = [('edgeOf(BUTTON)', 'ED', '--ed'), ('edgeOf(SURFACE)', 'ES', '--eds'), ('lipOf(BUTTON)', 'LP', '--lp'),
           ('edgeOf(ACCENT)', 'AE', '--ae'), ('lipOf(ACCENT)', 'AL', '--al'), ('edgeOf(DANGER)', 'DE', '--de'), ('lipOf(DANGER)', 'DL', '--dl')]
TOPS = [('TOP_CARD', 'TC', '--tc'), ('TOP_EDGE', 'TE', '--te'), ('TOP_TITLE', 'TT', '--ac'), ('TOP_TEXT', 'TX', '--tx'), ('TOP_SUB', 'TS', '--dm, --do'),
        ('TOP_TRACK', 'TK', '--bt'), ('TOP_SQUARE', 'TQ', '--sf'), ('TOP_DANGER', 'TD', '--dg')]

def cssvars(t):
    v = ';'.join(f'{c}:#{t[k].lower()}' for _, k, c in ROLES + DERIVED)
    return v + f";--pt:{t.get('PT', 100)}%;--pgb:#{t['PG'][0].lower()};--pgt:#{t['PG'][1].lower()};--pgd:#{t['PG'][2].lower()}"
def cssTop(o):  # inside the top scope the main variables carry the TOP_* tokens
    d = {'--tc': o['TC'], '--te': o['TE'], '--ac': o['TT'], '--tx': o['TX'], '--dm': o['TS'], '--do': o['TS'], '--bt': o['TK'], '--sf': o['TQ'],
         '--dg': o['TD'], '--oa': o['OA'], '--kn': o['TX'], '--bg': '000000', '--ed': edge(o['TK'], o['TX'], o['EM']), '--eds': edge(o['TQ'], o['TX'], o['EM']),
         '--ae': edge(o['TT'], o['TX'], o['EM']), '--ru': o['TE'], '--tr': o['TK']}
    return ';'.join(f'{c}:#{v.lower()}' for c, v in d.items())

if __name__ == '__main__':
    mode = sys.argv[1] if len(sys.argv) > 1 else ''
    if mode == 'css':
        scope = ('.s.top:not(:has(.pane))', '.s .tcard', '.s .ov')
        for k in ORDER:
            pre = '' if k == 'yellow' else f':root[data-bp={k}] '
            print(f"{pre}.s{{{cssvars(full(k))}}}")
            print(f"{','.join(pre + x for x in scope)}{{{cssTop(top(k))}}}")
    elif mode == 'md':
        hd = '| ' + ' | '.join(T[k]['name'] for k in ORDER) + ' |\n|---|---|' + '---|' * len(ORDER)
        print('| Token | Preview ' + hd)
        for n, key, c in ROLES:
            print(f'| {n} | `{c}` | ' + ' | '.join('#' + full(k)[key] for k in ORDER) + ' |')
        print(f'| EDGE_MIX | (in the edges) | ' + ' | '.join(f"{full(k)['EM']}%" for k in ORDER) + ' |')
        for n, key, c in TOPS:
            print(f'| {n} | `{c}` in the top scope | ' + ' | '.join('#' + top(k)[key] for k in ORDER) + ' |')
        print('\n| Worked out | Preview ' + hd)
        for n, key, c in DERIVED:
            print(f'| {n} | `{c}` | ' + ' | '.join('#' + full(k)[key] for k in ORDER) + ' |')
        print()
        P = [pairs(k) for k in ORDER]
        print('| Pair (floor) ' + hd[:hd.index('\n')] + '\n|---|' + '---|' * len(ORDER))
        for i, p in enumerate(P[0]):
            fl = f"{p[3]:g}" if P[-1][i][3] == p[3] else f"{p[3]:g}; High contrast {P[-1][i][3]:g}"
            print(f'| {p[0]} ({fl}) | ' + ' | '.join(f'{cr(q[i][1], q[i][2]):.1f}' for q in P) + ' |')
    else:
        bad = 0
        for k in ORDER:
            print(f"== {T[k]['name']}")
            for what, a, b, fl in pairs(k):
                r = cr(a, b); ok = r >= fl; bad += not ok
                print(f"  {'ok  ' if ok else 'FAIL'} {r:5.2f} (>= {fl:g})  {what}  #{a} on #{b}")
        sys.exit(1 if bad else 0)
