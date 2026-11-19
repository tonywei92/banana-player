#!/usr/bin/env python3
"""The redesign's themes: every role's value per theme, and the contrast of each pair that must stay readable.
`python3 themes.py`        the contrast table (exit 1 if a pair is under its floor)
`python3 themes.py css`    the blocks pasted into src/style.css
`python3 themes.py md`     the roles table pasted into components.md
`python3 themes.py js`     the THEMES list pasted into src/kit.js
A theme gives the base roles; the edges, lips, rail and tints are worked out as src/theme.h does (edgeOf, lipOf, mix)."""
import sys

# base roles per theme, by src/theme.h's names (short keys below: ROLES). EM: EDGE_MIX, how far an edge goes
# from its fill towards TEXT, in percent. top: the theme whose values the TOP_* tokens take (itself unless named).
# PT: how strong a preview pane's decorative tint is over BG (the preview only).
# page: the reader's page colours that "Match theme" picks (text-theme's own ids; not roles).
T = {
 'yellow': dict(name='Yellow', what='Today’s look', BG='000000', SF='121116', BT='26242E', PR='3E3B4A', TR='1E1D24', RU='2A2833', TX='E8E2D3',
                DM='8F8A80', DO='ABA597', AC='FFD23F', OA='000000', DG='F0645A'),
 'dark': dict(name='Dark', what='The default. Near-black greys, a warm off-white accent (no hue of its own); nothing pure white', BG='0B0B0D', SF='17171A', BT='2A2A2F', PR='424249', TR='222226', RU='2E2E34', TX='C2C4CA',
              DM='8A8C94', DO='A6A8B0', AC='E8E4DA', OA='0B0B0D', DG='F47067', EM=40),
 'darkred': dict(name='Dark red', what='A red cast, a red accent; DANGER is amber', BG='0A0405', SF='190C0E', BT='361D21', PR='4D2A2F', TR='241315', RU='382224', TX='EFE1DD',
                 DM='A08B87', DO='BCA8A4', AC='FF6A6E', OA='000000', DG='FFB224'),
 'pink': dict(name='Pink', what='A plum cast, a pink accent; DANGER is amber', BG='0C060A', SF='1B0F17', BT='301C29', PR='4B2E41', TR='25151F', RU='37222F', TX='F1E2E9',
              DM='A18B97', DO='BAA5B1', AC='FF8FC7', OA='000000', DG='FFB224'),
 'blue': dict(name='Midnight blue', what='Deep navy, a sky-blue accent', BG='040816', SF='0C142E', BT='1A2649', PR='2C3C6C', TR='131C3A', RU='202C52', TX='E1E7F5',
              DM='8893B0', DO='A5AFCA', AC='8CC8FF', OA='000000', DG='FF7A70'),
 'purple': dict(name='Dark purple', what='Deep violet, a soft lilac accent', BG='0A0612', SF='150E22', BT='2C1F45', PR='3D2C5E', TR='1D1430',
                RU='2E2147', TX='E6DFF2', DM='938AA8', DO='AEA5C3', AC='B79CFF', OA='0A0612', DG='FF7A70'),
 'coffee': dict(name='Coffee', what='Roast browns, cream text, a latte accent', BG='0D0907', SF='1A1410', BT='30261F', PR='4A3B30', TR='241C17',
                RU='392D25', TX='EADFCF', DM='9C8E7E', DO='B7A999', AC='E0B070', OA='0D0907', DG='FF7468'),
 'mint': dict(name='Mint', what='Dark teal, a cool mint accent', BG='050D0C', SF='0D1917', BT='1A2E2B', PR='2A4642', TR='132321',
              RU='213734', TX='DDEBE7', DM='859A95', DO='A1B5B0', AC='6FE0C2', OA='050D0C', DG='FF7A70'),
 'green': dict(name='Retro green', what='An old handheld’s greens, a lime accent', BG='0A0F08', SF='131B10', BT='22301D', PR='35492D', TR='1A2516',
               RU='2A3A24', TX='D9E4C8', DM='8A9A7C', DO='A6B598', AC='A6D44A', OA='0A0F08', DG='FF7A6B'),
 'sunset': dict(name='Sunset', what='Warm dusk, an orange accent; DANGER is hot pink', BG='0E0806', SF='1C110D', BT='33211A', PR='4E3328', TR='261812',
                RU='3A261E', TX='F1E1D2', DM='A38D7E', DO='BDA797', AC='FF9A4D', OA='0E0806', DG='FF6B9D'),
 'candy': dict(name='Candy', what='Grape night, pink-white text, an aqua accent', BG='100A14', SF='1C1323', BT='31223D', PR='4C365D', TR='26192F',
               RU='3A2947', TX='F3E4EE', DM='A08C9C', DO='BBA7B7', AC='7FE0E8', OA='100A14', DG='FF7A70'),
 'bit16': dict(name='16-bit', what='A classic console’s light grey and lavender, purple keys; light-bodied', BG='E4E3EA', SF='F4F3F8', BT='CFCDDC', PR='B8B5CC',
               TR='D6D4E0', RU='C6C4D3', TX='26242E', DM='5E5B6B', DO='4F4C5C', AC='55429C', OA='F4F3F8', DG='A81E2A', RA='DAD9E2', EM=55, PT=22,
               top='purple', page='light'),
 'bit16l': dict(name='16-bit Lavender', what='From a five-colour palette: light grey body, lavender keys, dark grey text, a deepened purple accent; light-bodied',
                BG='C2C2C2', SF='D4D4D6', BT='B5AFE1', PR='9E97D0', TR='B4B3BB', RU='96939F', TX='3B3B3B', DM='4C4A52', DO='434149', AC='4E3375',
                OA='E6E4F0', DG='8A1420', RA='B8B8BA', EM=80, PT=22, top='purple', page='light'),
 'bit32': dict(name='32-bit', what='A classic console’s cool greys, a deep blue accent; light-bodied', BG='DEDEE1', SF='EFEFF2', BT='C8C8CE', PR='B0B0B8',
               TR='D0D0D6', RU='C0C0C7', TX='1E1E22', DM='595960', DO='4B4B52', AC='1F4E9E', OA='EFEFF2', DG='961C24', RA='D3D3D8', EM=55, PT=22,
               top='dark', page='light'),
 'sakura': dict(name='Sakura', what='From a five-colour palette: peach and blush paper, pink keys, plum text, a deepened mauve accent; light-bodied; DANGER is burnt orange',
                BG='FFE6E0', SF='FFF3EF', BT='E8B3CF', PR='C1839F', TR='FACFD8', RU='E8B3CF', TX='561F33', DM='8A5069', DO='6F3C55', AC='852A59',
                OA='FFE6E0', DG='7A3800', RA='FACFD8', EM=55, PT=22, top='pink', page='light'),
 # Strong keys (optional roles; unset they are BUTTON, TEXT, ACCENT, BG, DIM_ON, ACCENT, ON_ACCENT and DIM_ON, as every other theme):
 # KEY / ON_KEY a named key's fill and ink, KF KEY_FOCUS, HB / OH the hint bar and its words, PRI / OP a primary key, OR ON_RAIL.
 'gundam': dict(name='Gundam', what='The owner’s five colours, placed: a grey-blue ground so the white armour stands off it, one blue (the owner’s #2C52B3) for keys and accent, a navy frame (rail and hint bar), yellow for where you are, red for the one primary key',
                BG='D9DFEC', SF='FFFFFF', BT='ECEFF6', PR='CBD3E4', TR='C6CEDF', RU='B4BDD0', TX='141B33', DM='53546A', DO='43445A', AC='2C52B3', OA='FFFFFF', DG='B81F28', RA='172E6B', EM=55, PT=22,
                top='blue', page='light', KEY='2C52B3', OK_='FFFFFF', KF='FFF867', HB='172E6B', OH='FFFFFF', PRI='D3222B', OP='FFFFFF', OR='C9D3EE', frame=1),
 # (kept for the drawing that compares them: not a theme of the app, not in ORDER)
 'gundam0': dict(name='Gundam, as first built', what='As first built (0.7.x): white armour, navy keys, a red primary key, the yellow as a key’s focus edge; light-bodied', BG='E8ECF4', SF='FFFFFF', BT='F3F5FA', PR='D5DDEE',
                TR='CDD4E3', RU='C3CBDB', TX='141B33', DM='5A5B6D', DO='464755', AC='1A56C9', OA='FFFFFF', DG='B81F28', RA='DFE4EE', EM=55, PT=22, top='blue', page='light',
                KEY='1C3C8E', OK_='FFFFFF', KF='FFF867', HB='172E6B', OH='FFFFFF', PRI='D3222B', OP='FFFFFF', noclean=1),
 'light': dict(name='Light', what='Warm paper, dark ink, dark amber: for sunlight', BG='F7F4EC', SF='FFFDF8', BT='E6E0D2', PR='D0C9B8', TR='DED8C9', RU='D5CEBE', TX='1B1A20',
               DM='625E56', DO='55514A', AC='7A5200', OA='FFFDF8', DG='A3261D', RA='ECE7DB', EM=55, PT=22, top='yellow', page='light'),
 'contrast': dict(name='High contrast', what='Black, white, bright yellow; text at 7 : 1 or more', BG='000000', SF='0A0A0A', BT='1E1E1E', PR='3A3A3A', TR='2B2B2B', RU='5E5E5E', TX='FFFFFF',
                  DM='C6C6C6', DO='D8D8D8', AC='FFE14D', OA='000000', DG='FF8A80', EM=50, page='black'),
}
# the stepper's order: Yellow, the calm ones, the playful ones, then the two for hard light and weak sight
# Dark's accent, for the drawing that compares them (not themes of the app): as it was, and the two candidates not taken
for _k, _ac, _n in (('dark0', 'A9C4E6', 'Dark, steel blue (before)'), ('darkb', 'ECEEF1', 'Dark, Chalk'), ('darkc', 'D3DAE4', 'Dark, Mist')):
    T[_k] = dict(T['dark'], AC=_ac, name=_n)
ORDER = ['yellow', 'dark', 'blue', 'purple', 'darkred', 'pink', 'coffee', 'mint', 'green', 'sunset', 'candy', 'bit16', 'bit16l', 'bit32', 'sakura', 'gundam', 'light', 'contrast']
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

def grey(L):  # the grey whose luminance is L
    v = L * 12.92 if L <= .0031308 else 1.055 * L ** (1 / 2.4) - .055
    return hx([round(max(0, min(1, v)) * 255)] * 3)
def artLimit(t):  # Match album art: how far the backdrop may go from BG before TEXT, DIM_ON or ACCENT drop under 4.5 on it (4.6 here, for rounding)
    inks = [lum(t['TX']), lum(t['DO']), lum(t['AC'])]
    if lum(t['BG']) > .5: return grey(max(4.6 * (l + .05) - .05 for l in inks))   # a light theme: the darkest the backdrop may be
    return grey(min((l + .05) / 4.6 - .05 for l in inks))                           # a dark theme: the brightest

def full(k):
    t = dict(T[k]); em = t.setdefault('EM', 30)
    t['KN'] = t['TX']; t['SC'] = t['BT']
    t['ED'] = edge(t['BT'], t['TX'], em); t['ES'] = edge(t['SF'], t['TX'], em); t['LP'] = lip(t['BT'])
    t['AE'] = edge(t['AC'], t['TX'], em); t['AL'] = lip(t['AC'])
    t['DE'] = edge(t['DG'], t['TX'], em); t['DL'] = lip(t['DG'])
    t.setdefault('RA', mix(t['BG'], t['SF'], 60))
    t['ATS'] = mix(t['SF'], t['AC'], 17); t['DTS'] = mix(t['SF'], t['DG'], 20)   # a badge's and a red pill's ground
    t['PG'] = PAGES[t.get('page', 'dark')]
    t['strong'] = 'KEY' in t
    for k, d in (('KEY', 'BT'), ('OK_', 'TX'), ('KF', 'AC'), ('HB', 'BG'), ('OH', 'DO'), ('PRI', 'AC'), ('OP', 'OA'), ('OR', 'DO')):
        t.setdefault(k, t[d])
    t['KC'] = t['OK_'] if t['strong'] else t['AC']   # a cap on a named key
    t['HC'] = (t['KF'] if t.get('frame') else t['OH']) if t['strong'] else t['AC']   # a cap in the hint bar
    t['RN'] = t['KF'] if t.get('frame') else t['AC']; t['RI'] = t['TX'] if t.get('frame') else t['OA']   # the rail's active cell and its ink
    t['KE'] = edge(t['KEY'], t['TX'], em) if t['strong'] else t['ED']; t['KL'] = lip(t['KEY']) if t['strong'] else t['LP']
    t['PE'] = edge(t['PRI'], t['TX'], em); t['PL'] = lip(t['PRI'])
    return t

def top(k):  # the TOP_* tokens: what is drawn over a picture or in its place. Dark in every theme
    s = full(T[k].get('top', k))
    return dict(TC=mix(s['BG'], s['SF'], 60), TE=s['RU'], TT=s['AC'], TX=s['TX'], TS=s['DM'], TK=s['BT'], TQ=s['SF'], TD=s['DG'],
                OA=s['OA'], EM=s['EM'])

# (what, ink, ground, floor)
def pairs(k):
    t, o = full(k), top(k); F = 7 if k == 'contrast' else 4.5
    C = 0 if t.get('noclean') else 1.15   # a theme that cannot carry the Clean style (its keys need their edges) is always Classic
    return [('TEXT on BG', t['TX'], t['BG'], F), ('TEXT on SURFACE', t['TX'], t['SF'], F), ('TEXT on BUTTON', t['TX'], t['BT'], F),
         ('TEXT on TRACK (a neutral pill)', t['TX'], t['TR'], F),
         ('DIM on BG', t['DM'], t['BG'], F), ('DIM_ON on BG (a song\'s artist line, the hint bar)', t['DO'], t['BG'], F), ('DIM on SURFACE', t['DM'], t['SF'], 4.5),
         ('DIM_ON on BUTTON', t['DO'], t['BT'], 4.5), ('DIM_ON on TRACK (a chip, off)', t['DO'], t['TR'], 4.5), 
         ('ACCENT on BG', t['AC'], t['BG'], F), ('ACCENT on SURFACE', t['AC'], t['SF'], F),
         ('ACCENT on BUTTON (the focused row)', t['AC'], t['BT'], 4.5), ('ACCENT on its 17% tint (a badge)', t['AC'], t['ATS'], 4.5),
         ('ON_ACCENT on ACCENT', t['OA'], t['AC'], F), ('ON_ACCENT on DANGER (Quit)', t['OA'], t['DG'], 4.5),
         ('DANGER on BG', t['DG'], t['BG'], 4.5), ('DANGER on SURFACE', t['DG'], t['SF'], 4.5),
         ('DANGER on BUTTON', t['DG'], t['BT'], 4.5), ('DANGER on its 20% tint (a red pill)', t['DG'], t['DTS'], 4.5),
         ('Focus edge (ACCENT) against BG', t['AC'], t['BG'], 3), ('Focus edge against SURFACE', t['AC'], t['SF'], 3),
         ('edgeOf(BUTTON) against BG', t['ED'], t['BG'], 3), ('edgeOf(SURFACE) against BG (as today: a flat piece, the album art\'s edge)', t['ES'], t['BG'], 2.4),
         ('Peaks and the six simple styles: marks (ACCENT) against BG', t['AC'], t['BG'], 3), ('Bar fill (ACCENT) against TRACK', t['AC'], t['TR'], 3), ('KNOB against TRACK', t['KN'], t['TR'], 3),
         ('Clean style: BUTTON (a key, a card, a tile with no edge) against BG', t['BT'], t['BG'], C),
         ('Clean style: BUTTON against SURFACE (a key on a sheet)', t['BT'], t['SF'], C),
         ('Clean style: PRESSED against BUTTON', t['PR'], t['BT'], 1.2),
         ] + ([] if k == 'contrast' else [('Match album art: TEXT on the backdrop at its limit', t['TX'], artLimit(t), 4.5),
         ('Match album art: DIM_ON on the backdrop at its limit', t['DO'], artLimit(t), 4.5), ('Match album art: ACCENT on the backdrop at its limit', t['AC'], artLimit(t), 4.5),
         ('Match album art: the limit against BG (room for the art to show)', artLimit(t), t['BG'], 1.15)]) + [
         ('ON_KEY on KEY (a named key)', t['OK_'], t['KEY'], 4.5), ('ON_PRIMARY on PRIMARY', t['OP'], t['PRI'], 4.5),
         ('ON_HINT on HINT_BAR', t['OH'], t['HB'], 4.5), ('ON_RAIL on RAIL', t['OR'], t['RA'], 4.5),
         ('The rail’s active cell: its ink on its fill', t['RI'], t['RN'], 4.5), ('KEY_FOCUS (a focused key’s edge) against KEY', t['KF'], t['KEY'], 3),
         ('TOP_TEXT on TOP_CARD', o['TX'], o['TC'], F), ('TOP_SUB on TOP_CARD', o['TS'], o['TC'], 4.5),
         ('TOP_TITLE on TOP_CARD', o['TT'], o['TC'], F), ('TOP_DANGER on TOP_CARD', o['TD'], o['TC'], 4.5),
         ('Reader page (Match theme): text on page', t['PG'][1], t['PG'][0], F)]

# (theme.h token, key, the preview's CSS variable)
STRONG = [('KEY', 'KEY', '--key'), ('ON_KEY', 'OK_', '--onk'), ('KEY_FOCUS', 'KF', '--kf'), ('HINT_BAR', 'HB', '--hb'), ('ON_HINT', 'OH', '--oh'),
          ('PRIMARY', 'PRI', '--pri'), ('ON_PRIMARY', 'OP', '--op'), ('ON_RAIL', 'OR', '--or')]
STRONGX = [('--kc', 'KC'), ('--hc', 'HC'), ('--rn', 'RN'), ('--ri', 'RI'), ('--ke', 'KE'), ('--kl', 'KL'), ('--pe', 'PE'), ('--pl', 'PL')]
ROLES = [('BG', 'BG', '--bg'), ('SURFACE', 'SF', '--sf'), ('BUTTON', 'BT', '--bt'), ('PRESSED', 'PR', '--pr'), ('TRACK', 'TR', '--tr'),
         ('RULE', 'RU', '--ru'), ('RAIL', 'RA', '--ra'), ('SCREEN', 'SC', '--sc'), ('TEXT', 'TX', '--tx'), ('DIM', 'DM', '--dm'),
         ('DIM_ON', 'DO', '--do'), ('KNOB', 'KN', '--kn'), ('ACCENT', 'AC', '--ac'), ('ON_ACCENT', 'OA', '--oa'), ('DANGER', 'DG', '--dg')]
DERIVED = [('edgeOf(BUTTON)', 'ED', '--ed'), ('edgeOf(SURFACE)', 'ES', '--eds'), ('lipOf(BUTTON)', 'LP', '--lp'),
           ('edgeOf(ACCENT)', 'AE', '--ae'), ('lipOf(ACCENT)', 'AL', '--al'), ('edgeOf(DANGER)', 'DE', '--de'), ('lipOf(DANGER)', 'DL', '--dl')]
TOPS = [('TOP_CARD', 'TC', '--tc'), ('TOP_EDGE', 'TE', '--te'), ('TOP_TITLE', 'TT', '--ac'), ('TOP_TEXT', 'TX', '--tx'), ('TOP_SUB', 'TS', '--dm, --do'),
        ('TOP_TRACK', 'TK', '--bt'), ('TOP_SQUARE', 'TQ', '--sf'), ('TOP_DANGER', 'TD', '--dg')]

def cssvars(t):
    v = ';'.join(f'{c}:#{t[k].lower()}' for _, k, c in ROLES + DERIVED + STRONG) + ';' + ';'.join(f'{c}:#{t[k].lower()}' for c, k in STRONGX)
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
        for k in ('dark', 'light', 'gundam', 'gundam0', 'dark0', 'darkb', 'darkc', 'blue', 'sakura'):  # a theme pinned to one drawing (the Clean style's side-by-side screens)
            print(f":root .s.t-{k}.t-{k}{{{cssvars(full(k))}}}")
        print(':root:not([data-bp]) .s .thv>em.t-yellow,' + ','.join(f':root[data-bp={k}] .s .thv>em.t-{k}' for k in ORDER) + '{display:inline}')
        print(','.join(f':root[data-bp={k}] .s .thsw.t-{k}' for k in ORDER) + '{border-color:var(--ac);box-shadow:0 0 0 1px var(--ac)}')
    elif mode == 'js':
        print('const THEMES=[' + ','.join("['%s','%s','%s','%s','%s','%s','%s','%s']" % (k, T[k]['name'], full(k)['BG'], full(k)['SF'], full(k)['AC'],
              full(k)['TX'], full(k)['ES'], T[k]['what']) for k in ORDER) + '];')
    elif mode == 'md':
        hd = '| ' + ' | '.join(T[k]['name'] for k in ORDER) + ' |\n|---|---|' + '---|' * len(ORDER)
        print('| Token | Preview ' + hd)
        for n, key, c in ROLES:
            print(f'| {n} | `{c}` | ' + ' | '.join('#' + full(k)[key] for k in ORDER) + ' |')
        for n, key, c in STRONG:
            print(f'| {n} (optional) | `{c}` | ' + ' | '.join('#' + full(k)[key] if key in T[k] else '·' for k in ORDER) + ' |')
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
