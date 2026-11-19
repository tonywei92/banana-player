#!/usr/bin/env python3
"""The preview's screens as a graph: what leads where, what can't be reached, what has no way back.

    python3 docs/redesign/journeys.py [docs/redesign] [--all] [--journeys] [--json]

It runs src/kit.js + src/screens.js in node (the screens are functions that return HTML), so the links are the
ones the walk-through really has: each key's target in `keys`, each state in `also`, and every `data-go` in the
drawn panels (under an open sheet nothing can be touched, so only the sheet's own count).
Exit code 1 when something is broken (a link to no screen, a screen nobody reaches, a screen with no way out,
a journey of JOURNEYS that can't be clicked through).
"""
import json, pathlib, re, subprocess, sys, collections

NODE = r'''
const fs=require('fs'),vm=require('vm'),d=process.argv[1];
const src=fs.readFileSync(d+'/src/kit.js','utf8')+'\n'+fs.readFileSync(d+'/src/screens.js','utf8')+'\n;globalThis.__S=SCREENS;';
const ctx={console};vm.createContext(ctx);vm.runInContext(src,ctx);
const S=ctx.__S.map(s=>({id:s.id,group:s.group,name:s.name,keys:s.keys,also:s.also||[],top:s.top(),bottom:s.bottom()}));
console.log(JSON.stringify({screens:S,miss:vm.runInContext('typeof TAP_MISS=="undefined"?[]:TAP_MISS',ctx)}));
'''
SPECIAL = {'<', 'theme:step'}          # app.js: back in the walk-through's history; step the theme
STARTS = ['first-lid', 'home-first', 'home']   # the very first start, a fresh Home, Home in use
KEYS = {'A', 'B', 'X', 'Y', 'L', 'R', 'L2', 'R2', 'L3', 'SELECT', 'START', 'F'}
# the player's bar is drawn, but the first press only keeps the screen on
HINT_OK = {'power-dim', 'phone-home', 'phone-files', 'phone-playing'}   # a phone page: the bar is the handheld's, drawn beside it (its keys: web-signin, browser-play)
NO_B = {'power-wake': 'the screen is off: any key but the playback ones wakes it'}

# The journeys, each a walk of screen ids: every step must be one click (a key, a tap, an `also`) from the one before.
# ';' starts another walk of the same journey. What is known not to be drawn is in coverage.md's Journeys table.
JOURNEYS = [
	('First start', 'first-lid home-first files video video-view video files-playing video files'),
	('Music: an album, Display, Equalizer', 'home music-albums music-album music music-view music; music music-more music-eq eq-presets music-eq music playlists music'),
	('Music: from Files, List, More, an artist', 'files files-music music music-list music music-more music-artist music-album; music-more music-album; music-albums music-artists music-artist music; music-albums music-empty files; music-albums music-reading music-album'),
	('Add to a playlist from the player', 'video picker keyboard'),
	('Videos', 'home files videos video; videos videos-empty files; videos videos-reading video; videos music-albums'),
	('Playlists', 'home music-albums playlists playlist music; playlist playlist-select playlist-move playlist-select playlist playlists; playlist-select picker; playlists gone music; home music'),
	('Liked', 'video toasts video; home music; playlists liked music; liked playlist-select; liked liked-empty files'),
	('Books', 'home books reader reader-contents reader reader-view book-text book-text-bar reader reader-more reader books pdf pdf-pages pdf pdf-view pdf pdf-more pdf books; books books-empty files'),
	('Manga: shelf, series, chapter, reader', 'home ext-section manga manga-history manga-read comic-more manga-read; manga manga-series manga-read comic-view comic-full comic-view book-comic book-comic-end manga-read manga-end manga-series manga-dl manga-downloads manga'),
	('Manga: sources and servers', 'manga manga-browse manga-source manga-series; manga-browse manga-sources manga-repos manga-repo-add manga-repos manga-repo-empty manga-repo-first manga-repo-add; manga-browse manga-server manga-browse manga-engine-get'),
	('Manga while a video plays', 'files-playing ext-section-video manga-guard manga-loading manga-ready manga-read; manga-loading manga-failed manga-loading home-stopped video'),
	('Extensions', 'home ext-section extensions manga-settings; ext-section manga ext-section home; ext-section ext-section-empty extensions; files-playing ext-section-video extensions'),
	('Search', 'home search search-results video; search-results music; search-results music-album; search-results reader; search-results search'),
	('Network', 'home settings net-list net-find net-form keyboard; net-form net-list net-folder net-stream net-folder net-copy pdf; net-copy net-copy-fail net-copy; net-folder readonly alerts; net-list offline; net-list net-empty net-find; net-empty net-form'),
	('Web dashboard', 'home-first web-page web-signin phone-home phone-files phone-playing music; web-page web-page-off web-page web-page-nowifi web-page; web-signin browser-play play-on music; browser-play browser-mini browser-play browser-lost music; quick web-page; home web-page'),
	('Settings and Theme', 'home settings set-theme set-screen lid-keys settings set-playback settings-group set-music set-photos set-reading set-files set-screen web-page extensions manga-settings set-updates set-about settings home'),
	('Quick settings', 'home quick; video quick power-wake video; quick quit home; video vol-boost vol-warn vol-boost quick'),
	('Incognito', 'quick incog-web-ask incog-web quick; set-screen incog-lid'),
	('Lid and sleep', 'first-lid home-first; set-theme set-screen lid-keys; video power-dim power-wake video'),
	('Files: select, copy, move, delete', 'files select clip job files; select picker; select delete files; files actions keyboard; actions info; actions busy files'),
	('Errors and empty states', 'files play-fail info; files empty-folder web-page; files card-out files; playlists gone; video video-more lock video'),
	('Updates', 'settings set-updates update update-downloading update-ready home; update-downloading update-failed update-downloading files'),
	('Photos', 'files photo photo-more info; photo-more photo files'),
	('Home: a card’s More, All recent', 'home home-more video; home-more recent-all home-more; home recent-all video'),
	('Quit', 'home quit home; quick quit'),
]

def load(root):
	out = subprocess.run(['node', '-e', NODE, str(root)], capture_output=True, text=True)
	if out.returncode:
		sys.exit('node failed:\n' + out.stderr)
	return json.loads(out.stdout)

def hint_bar(html):
	"""The hint bar the screen ends with (a sheet's own bar is drawn last, and is the one in force)."""
	bars = re.findall(r'<div class="hints">(.*?)</div>', html, re.S)
	bars = [b for b in bars if b.strip()]
	if not bars:
		return []
	return [(''.join(k2 + '/' for k2 in re.findall(r'<b class="cap[^"]*">(.*?)</b>', span)).rstrip('/'),
			re.sub(r'<[^>]+>', '', re.sub(r'<b class="cap[^"]*">.*?</b>', '', span)).strip())
			for span in re.findall(r'<span>(.*?)</span>(?=<span>|$)', bars[-1], re.S)]

def toks(k):
	return set(t for t in re.split(r'\s*(?:/| or |,|\+| )\s*', k) if t)

def graph(screens):
	ids = [s['id'] for s in screens]
	out = collections.OrderedDict((i, []) for i in ids)   # id -> [(target, how)]
	bad = []
	for s in screens:
		for k in s['keys']:
			t = k[2]
			if not t or t in SPECIAL:
				continue
			(out[s['id']] if t in out else bad).append((t, 'key ' + k[0]) if t in out else (s['id'], 'key ' + k[0], t))
		for a in s['also']:
			(out[s['id']] if a[1] in out else bad).append((a[1], 'also') if a[1] in out else (s['id'], 'also', a[1]))
		for side in ('bottom', 'top'):
			html = s[side][s[side].rfind('class="veil"') + 1:]
			for t in re.findall(r'data-go="([^"]*)"', html):
				if not t or t in SPECIAL:
					continue
				if t in out:
					out[s['id']].append((t, 'tap'))
				else:
					bad.append((s['id'], 'data-go (' + side + ')', t))
	return ids, out, bad

def reach(out, starts):
	seen, todo = set(starts), list(starts)
	while todo:
		for t, _ in out[todo.pop()]:
			if t not in seen:
				seen.add(t); todo.append(t)
	return seen

def back_of(s, out):
	"""How this screen is left: a B key with a target, or the header's ‹."""
	for k in s['keys']:
		if 'B' in toks(k[0]) and k[2]:
			return k[2]
	m = re.search(r'class="btn bk"[^>]*data-go="([^"]+)"', s['bottom'])
	return m.group(1) if m else NO_B.get(s['id'])

def main():
	args = [a for a in sys.argv[1:] if not a.startswith('--')]
	flags = set(a for a in sys.argv[1:] if a.startswith('--'))
	root = pathlib.Path(args[0]) if args else pathlib.Path(__file__).resolve().parent
	data = load(root)
	screens, miss = data['screens'], data['miss']
	by = {s['id']: s for s in screens}
	ids, out, bad = graph(screens)
	inc = collections.defaultdict(set)
	for a, ts in out.items():
		for t, _ in ts:
			if t != a:
				inc[t].add(a)
	seen = reach(out, [s for s in STARTS if s in out])
	unreach = [i for i in ids if i not in seen]
	noway = [i for i in ids if not back_of(by[i], out)]
	noout = [i for i in ids if not [t for t, _ in out[i] if t != i]]
	nulls = [(s['id'], k[0], k[1]) for s in screens for k in s['keys'] if not k[2]]
	links = sum(len(set(t for t, _ in v)) for v in out.values())

	if '--json' in flags:
		print(json.dumps({'screens': ids, 'out': {k: sorted(set(t for t, _ in v)) for k, v in out.items()},
			'in': {k: sorted(inc[k]) for k in ids}, 'unreachable': unreach, 'no_back': noway, 'broken': bad}, indent=1))
		return

	print(f'{len(ids)} screens, {links} links (distinct from → to), {sum(len(v) for v in out.values())} clickable places, '
		f'{len(nulls)} keys that lead nowhere in the walk-through')
	print(f'\nLinks to a screen that does not exist: {len(bad)}')
	for b in bad: print('  %s: %s → %s' % b)
	print(f'\nTAPS that found no control to sit on: {len(miss)}')
	for m in miss: print('  ' + m)
	print(f'\nUnreachable from {" / ".join(STARTS)}: {len(unreach)}')
	for i in unreach: print(f'  {i}  (from: {", ".join(sorted(inc[i])) or "nothing"})')
	print(f'\nNo way back (no B with a target, no ‹): {len(noway)}')
	for i in noway: print(f'  {i}  (leads to: {", ".join(sorted(set(t for t, _ in out[i]))) or "nothing"})')
	print(f'\nNo way out at all: {len(noout)}')
	for i in noout: print('  ' + i)

	# the hint bar against the keys list: every key the bar names must be in `keys`
	print('\nHint bar names a key the keys list has not:')
	n = 0
	for s in screens:
		have = set()
		for k in s['keys']: have |= toks(k[0])
		for k, label in ([] if s['id'] in HINT_OK else hint_bar(s['bottom'])):
			ts = [t for t in toks(k) if t in KEYS]
			if ts and not any(t in have for t in ts):   # “L/R Tab” on a first tab lists R alone: one of a pair is enough
				n += 1; print(f'  {s["id"]}: {k} "{label}"')
	if not n: print('  none')

	if '--all' in flags:
		print('\nKeys that lead nowhere:')
		for i, k, w in nulls: print(f'  {i}: {k} = {w}')
		print('\nEvery screen: in ← / out →')
		for i in ids:
			print(f'  {i}\n    ← {", ".join(sorted(inc[i])) or "-"}\n    → {", ".join(sorted(set(t for t, _ in out[i]))) or "-"}')

	fails = 0
	if '--journeys' in flags or '--all' in flags:
		print('\nJourneys:')
		for name, path in JOURNEYS:
			state = 'wired'
			for walk in path.split(';'):
				steps = walk.split()
				missing = [x for x in steps if x not in out]
				if missing:
					state = 'no screen: ' + ', '.join(missing); break
				brk = [f'{a} → {b}' for a, b in zip(steps, steps[1:]) if b not in (t for t, _ in out[a])]
				if brk:
					state = 'breaks at ' + ', '.join(brk); break
			if state != 'wired': fails += 1
			print(f'  {"ok  " if state == "wired" else "FAIL"} {name}: {state}')

	sys.exit(1 if bad or miss or unreach or noway or noout or fails else 0)

if __name__ == '__main__':
	main()
