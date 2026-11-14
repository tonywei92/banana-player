import { html, useState, useEffect, useLayoutEffect, useRef, useCallback } from '../vendor/htm-preact-standalone.module.js';
import { NO_HERE, NO_HERE_WHY, Q_DESK, Q_WIDE, lsGet, lsSet, set, store, useStore } from './store.js';
import { baseName, cmpName, dirName, fmtTime, kindOfName, plural, titleFor, touchOnly, userTap } from './util.js';
import { errMsg, errToast, get, post } from './api.js';
import { playHere, playPlaylist, view } from './browser-player.js';
import { AppMenu, Art, Empty, MoreDots, PlArt, Skeleton, openSheet, useLater, useMedia } from './components.js';
import { filesHash, go } from './router.js';
import { LIKED_ID, addToPlaylist, nameDialog, newPlaylistDialog, plEdit, plUsed, showInFiles, validPlName } from './plshared.js';
import { ask, toast } from './toasts.js';
import { Icon } from './icons.js';
import { reduceMotion } from './playing.js';
import { GLYPH } from './files.js';

/* ================= playlists ================= */
let uidSeq = 0;
export function Playlists() { return store.route.id ? html`<${PlaylistView} id=${store.route.id} key=${store.route.id} />` : html`<${PlaylistList} />`; }

// "1 h 38 min", "23 min"
const fmtLong = t => { t = Math.round(t / 60); return t >= 60 ? Math.floor(t / 60) + ' h ' + (t % 60) + ' min' : Math.max(1, t) + ' min'; };
const letterOf = n => { const c = (n || '').trim().charAt(0).toUpperCase(); return /[A-Z]/.test(c) ? c : /\p{L}/u.test(c) ? c : '#'; };
export async function playFromPl(p, target, shuffle) {
	if (!userTap()) return;
	try { const r = await get('/api/playlist', { id: p.id }); await playPlaylist(p.id, r.name, r.items, 0, shuffle, target, true); } catch (e) { errToast(e); }
}

/* the Playlists page: Liked pinned, the playlists as cards; past 12 a Recent strip, then Find, a sort and a view (a list
   with letter headers by default), both remembered by this browser */
function PlaylistList() {
	const s = useStore(), [list, setList] = useState(null), [err, setErr] = useState('');
	const [find, setFind] = useState(''), [sort, setSort] = useState(lsGet('bpPlSort') || 'az'), [viewPref, setViewPref] = useState(lsGet('bpPlView') || '');
	const [tbH, setTbH] = useState(64), tools = useRef();
	const wide = useMedia(Q_WIDE), desk = useMedia(Q_DESK);
	const load = () => get('/api/playlists').then(r => { setList(r.playlists || []); setErr(''); if (store.pls) set({ pls: r.playlists || [] }); }).catch(e => setErr(errMsg(e)));
	useEffect(() => { load(); }, [s.epoch, s.plTick]);
	useLayoutEffect(() => {
		const t = tools.current; if (!t || !('ResizeObserver' in window)) return;
		const ro = new ResizeObserver(() => setTbH(t.offsetHeight)); ro.observe(t); return () => ro.disconnect();
	});
	const slow = useLater(!list && !err, 300);
	const open = id => go('#/playlists/' + encodeURIComponent(id));
	const sheet = (p, anchor) => openSheet(p.name, 'playlist', [
		{ label: 'Play on handheld', icon: 'card', fn: () => playFromPl(p, 'handheld', false) },
		{ label: 'Play in this browser', icon: 'laptop', disabled: NO_HERE, sub: NO_HERE ? NO_HERE_WHY : undefined, fn: () => playFromPl(p, 'browser', false) },
		{ label: 'Shuffle play', icon: 'shuffle', fn: () => playFromPl(p, store.target, true) }, '-',
		{ label: 'Rename…', icon: 'pencil', fn: () => nameDialog({ title: 'Rename playlist', label: 'Playlist name', initial: p.name, ok: 'Rename', select: true, submit: async n => { const m = validPlName(n, list, p.id); if (m) return m; try { await post('/api/playlists/rename', { id: p.id, name: n.trim() }); load(); return ''; } catch (e) { return e.message; } } }) },
		{ label: 'Delete…', icon: 'trash', danger: true, fn: async () => { if (await ask('Delete ' + p.name + '?', 'Its files stay where they are.', { buttons: [{ label: 'Cancel', value: null }, { label: 'Delete', value: true, cls: 'danger' }] })) { try { await post('/api/playlists/delete', { id: p.id }); load(); } catch (e) { toast(e.message); } } } }],
		{ anchor, sub: plural(p.count, 'item', 'items'), seed: p.id + '/0', kind: 'audio' });
	const create = () => newPlaylistDialog((id) => open(id));
	// Liked: built in, pinned in a card of its own; played, never renamed or deleted
	const liked = list && list.find(p => p.builtin), rest = list ? list.filter(p => !p.builtin) : [];
	const likedSheet = (p, anchor) => openSheet('Liked', 'heart', [
		{ label: 'Play on handheld', icon: 'card', disabled: !p.count, fn: () => playFromPl(p, 'handheld', false) },
		{ label: 'Play in this browser', icon: 'laptop', disabled: NO_HERE || !p.count, sub: NO_HERE ? NO_HERE_WHY : undefined, fn: () => playFromPl(p, 'browser', false) },
		{ label: 'Shuffle play', icon: 'shuffle', disabled: !p.count, fn: () => playFromPl(p, store.target, true) }], { anchor });
	if (err) return html`<div class="page"><${Empty} icon="alert" title="Couldn't load playlists" sub=${err} button="Try again" onClick=${load} /></div>`;
	const many = rest.length > 12, used = plUsed();
	const mode = many ? (viewPref || 'list') : 'grid';
	const recent = many ? rest.filter(p => used[p.id]).sort((a, b) => used[b.id] - used[a.id]).slice(0, 6) : [];
	const f = many ? find.trim().toLowerCase() : '';
	const shown = rest.filter(p => !f || p.name.toLowerCase().includes(f)).sort(
		sort === 'recent' && many ? (a, b) => ((used[b.id] || 0) - (used[a.id] || 0)) || cmpName(a.name, b.name)
		: sort === 'size' && many ? (a, b) => (b.count - a.count) || cmpName(a.name, b.name) : (a, b) => cmpName(a.name, b.name));
	const pickSort = k => { setSort(k); lsSet('bpPlSort', k); };
	const pickView = k => { setViewPref(k); lsSet('bpPlView', k); };
	const card = p => html`<div class="plcell" key=${p.id}><button class="box surface plcard" onClick=${() => open(p.id)} onContextMenu=${e => { if (!touchOnly) { e.preventDefault(); sheet(p, { x: e.clientX, y: e.clientY }); } }}>
			<span class="cv"><${PlArt} p=${p} k=${3} /></span><span class="txt"><div class="n ell">${p.name}</div><div class="c">${plural(p.count, 'item', 'items')}</div></span></button>
		<button class="ib plmore" aria-label=${'More about ' + p.name} aria-haspopup="dialog" onClick=${e => sheet(p, e.currentTarget)}><${MoreDots} /></button></div>`;
	let rows = [], last = '';
	if (mode === 'list') for (const p of shown) {
		const L = letterOf(p.name);
		if (sort === 'az' && L !== last) { rows.push(html`<div class="letter" key=${'L' + L} aria-hidden="true">${L}</div>`); last = L; }
		rows.push(html`<div class="plrow" key=${p.id}><button class="main-btn" onClick=${() => open(p.id)} onContextMenu=${e => { if (!touchOnly) { e.preventDefault(); sheet(p, { x: e.clientX, y: e.clientY }); } }}><span class="tile sm"><${PlArt} p=${p} /></span><span class="grow"><div class="fn ell">${p.name}</div><div class="fm">${plural(p.count, 'item', 'items')}</div></span></button>
			${wide && p.count > 0 && html`<button class="ib rowplay" aria-label=${'Play ' + p.name} onClick=${() => playFromPl(p, store.target, false)}><${Icon} n="play" k=${2} /></button>`}
			<button class="ib more" aria-label=${'More about ' + p.name} aria-haspopup="dialog" onClick=${e => sheet(p, e.currentTarget)}><${MoreDots} /></button></div>`);
	}
	return html`<div class="page">
		<div class="phead"><div class="row"><h1>Playlists</h1><button class=${'btn' + (wide ? '' : ' icon')} aria-label=${wide ? undefined : 'New playlist'} title="New playlist" onClick=${create}><${Icon} n="plus" k=${2} />${wide && 'New playlist'}</button><${AppMenu} /></div>
			${list && html`<div class="sub"><span class="kicker">${plural(list.length, 'playlist', 'playlists')} · shared with the handheld</span></div>`}</div>
		${liked && html`<div class="plcell liked"><button class="box surface plcard liked" onClick=${() => open(liked.id)}><span class="cv"><${PlArt} p=${liked} k=${4} /></span>
				<span class="txt grow"><div class="kicker y">Pinned</div><div class="n">Liked</div><div class="c">${liked.count ? plural(liked.count, 'item', 'items') + ' · newest first' : 'Tap ♥ on anything playing'}</div></span></button>
			<span class="plcell-acts"><button class="ib" aria-label="More about Liked" aria-haspopup="dialog" onClick=${e => likedSheet(liked, e.currentTarget)}><${MoreDots} /></button>
				<button class="box yellow plplay" aria-label="Play Liked" disabled=${!liked.count} onClick=${() => playFromPl(liked, store.target, false)}><${Icon} n="play" k=${3} /></button></span></div>`}
		${!list ? (slow ? html`<${Skeleton} />` : null) : rest.length === 0 ? html`<${Empty} icon="playlist" title="No playlists yet" sub="Make one, then add songs and videos from Files." button="New playlist" onClick=${create} />` : html`
			${recent.length > 0 && html`<div class="band" style="margin-bottom:10px"><span class="kicker">Recently used</span><span class="rule"></span><span class="dim" style="font-size:12px">played or added to</span></div>
				<div class="plstrip">${recent.map(card)}</div>`}
			${many && html`<div class="pltools" ref=${tools}>
				<label class="find"><${Icon} n="search" k=${2} /><input class="field" data-slash="1" value=${find} placeholder="Find a playlist" aria-label="Find a playlist" autocomplete="off" enterkeyhint="go"
					onInput=${e => setFind(e.target.value)} onKeyDown=${e => { if (e.key === 'Enter' && shown[0]) open(shown[0].id); if (e.key === 'Escape') { setFind(''); e.target.blur(); } }} />${desk && html`<span class="key">/</span>`}</label>
				<div class="seg" role="group" aria-label="Sort">${[['az', 'A–Z'], ['recent', 'Recent'], ['size', 'Size']].map(([k, l]) => html`<button aria-pressed=${sort === k} onClick=${() => pickSort(k)}>${l}</button>`)}</div>
				<div class="seg" role="group" aria-label="View"><button aria-pressed=${mode === 'list'} aria-label="List" title="List" onClick=${() => pickView('list')}><${Icon} n="playlist" k=${2} /></button><button aria-pressed=${mode === 'grid'} aria-label="Grid" title="Grid" onClick=${() => pickView('grid')}><${Icon} n="blocks" k=${2} /></button></div></div>`}
			${shown.length === 0 ? html`<p class="note pad">No playlist called “${find.trim()}”</p>`
				: mode === 'grid' ? html`<div class="plgrid">${shown.map(card)}</div>`
				: html`<div class="box surface pllist" style=${'--tb-h:' + tbH + 'px'}>${rows}</div>`}`}</div>`;
}

function PlaylistView({ id }) {
	const s = useStore();
	const [pl, setPl] = useState(null), [err, setErr] = useState(''), [selMode, setSelMode] = useState(false), [sel, setSel] = useState(new Set()), [kb, setKb] = useState(null), [live, setLive] = useState('');
	const revRef = useRef(''), listRef = useRef(), itemsRef = useRef([]), sortable = useRef(null);
	const items = pl ? pl.items : [];
	// Liked: newest first, its order its own (no grips, no Add files); Remove there is Unlike
	const builtin = id === LIKED_ID || !!(pl && pl.builtin);
	itemsRef.current = items;
	const load = useCallback(async quiet => {
		try {
			const r = await get('/api/playlist', { id });
			if (quiet && revRef.current && r.rev !== revRef.current && id !== LIKED_ID) toast(r.name + ' changed on another device');
			// a like from elsewhere: its row comes in at the top, lit
			const before = quiet && id === LIKED_ID ? new Set(itemsRef.current.map(x => x.p || x.net)) : null;
			revRef.current = r.rev; setPl({ ...r, items: (r.items || []).map(x => ({ ...x, uid: ++uidSeq, fresh: !!before && !before.has(x.p || x.net) })) }); setErr(''); setSel(new Set());
		} catch (e) { if (e.status === 404) { go('#/playlists'); } else setErr(errMsg(e)); }
	}, [id]);
	useEffect(() => { load(false); }, [id]);
	useEffect(() => { if (pl) load(true); else if (err && s.epoch) load(false); }, [s.plTick, s.epoch]);
	// the small player and the upload key make room for the selection's band
	useEffect(() => { set({ selecting: selMode }); return () => set({ selecting: false }); }, [selMode]);
	const slow = useLater(!pl && !err, 300);

	async function edit(act, params, okMsg) {
		try { const r = await plEdit(id, act, params, revRef.current); revRef.current = r.rev; return r; }
		catch (e) { if (e.body && e.body.code === 'rev') { toast((pl && pl.name || 'This playlist') + ' changed on another device'); } else toast(e.message); load(false); return null; }
	}
	async function moveTo(from, newIdx) {
		if (from === newIdx) return;
		const a = itemsRef.current.slice(), [m] = a.splice(from, 1); a.splice(newIdx, 0, m);
		setPl(p => ({ ...p, items: a }));
		await edit('move', { i: [from], to: newIdx > from ? newIdx + 1 : newIdx });
	}
	// SortableJS moves DOM nodes; put the node back and let Preact reorder from state
	useEffect(() => {
		if (!listRef.current || sortable.current || id === LIKED_ID) return;
		sortable.current = window.Sortable.create(listRef.current, { handle: '.grip', animation: 150, delay: 150, delayOnTouchOnly: true, touchStartThreshold: 4, ghostClass: 'sortable-ghost', chosenClass: 'sortable-chosen',
			onEnd: evt => { const { item, from, oldIndex, newIndex } = evt; from.removeChild(item); from.insertBefore(item, from.children[oldIndex] || null); moveTo(oldIndex, newIndex); } });
		return () => { sortable.current && sortable.current.destroy(); sortable.current = null; };
	}, [!!pl && items.length > 0]);
	useEffect(() => { if (kb) { const g = document.querySelector('[data-uid="' + kb.uid + '"] .grip'); if (g && document.activeElement !== g) g.focus(); } }, [items, kb]);

	function gripKey(e, idx, it) {
		const n = items.length;
		if (e.key === ' ' || e.key === 'Enter') {
			e.preventDefault();
			if (!kb) { setKb({ uid: it.uid, from: idx, orig: items }); setLive(`Picked up ${it.title}, position ${idx + 1} of ${n}. Use the arrow keys, then Space to drop.`); }
			else { const to = items.findIndex(x => x.uid === kb.uid); setKb(null); setLive(`Dropped at position ${to + 1} of ${n}`); if (to !== kb.from) edit('move', { i: [kb.from], to: to > kb.from ? to + 1 : to }); }
		} else if (kb && (e.key === 'ArrowUp' || e.key === 'ArrowDown')) {
			e.preventDefault();
			const to = idx + (e.key === 'ArrowUp' ? -1 : 1); if (to < 0 || to >= n) return;
			const a = items.slice(); [a[idx], a[to]] = [a[to], a[idx]]; setPl({ ...pl, items: a }); setLive(`Position ${to + 1} of ${n}`);
		} else if (kb && e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); setPl({ ...pl, items: kb.orig }); setKb(null); setLive('Cancelled'); }
	}
	async function remove(uids) {
		const removed = items.map((it, i) => ({ ...it, i })).filter(x => uids.has(x.uid));
		if (!removed.length) return;
		const r = await edit('remove', { i: removed.map(x => x.i) }); if (!r) return;
		setSel(new Set()); setSelMode(false);
		if (!reduceMotion()) { removed.forEach(x => { const row = document.querySelector('.pitem[data-uid="' + x.uid + '"]'); if (row) row.classList.add('leaving'); }); await new Promise(res => setTimeout(res, 200)); }
		setPl(p => ({ ...p, items: p.items.filter(x => !uids.has(x.uid)) }));
		if (builtin) {
			// Undo likes them again, each back in its place (in order, so the indexes hold); a network one can't be
			// liked from here
			const back = removed.filter(x => x.p);
			toast(removed.length === 1 ? 'Removed from Liked' : 'Removed ' + removed.length + ' from Liked', back.length ? { label: 'Undo', fn: async () => {
				try { for (const x of back) await post('/api/like', { p: x.p, on: 1, at: x.i }); } catch (e) { errToast(e); }
				load(false);
			} } : null, 6000);
			return;
		}
		toast('Removed ' + plural(removed.length, 'item', 'items'), { label: 'Undo', fn: async () => {
			try { let rev = revRef.current; for (const x of removed) { const r2 = await plEdit(id, 'add', { p: [x.p], at: x.i }, rev); rev = r2.rev; revRef.current = rev; } load(false); } catch (e) { toast(e.message); load(false); }
		} }, 6000);
	}
	const playItem = i => playHere(t => playPlaylist(id, pl.name, items, i, false, t, true, true), items[i].p);
	// (a network item plays on the handheld only)
	const playLiked = i => items[i].p ? playItem(i) : playPlaylist(id, pl.name, items, i, false, 'handheld', true, true);
	const addFiles = () => { set({ pick: { id, name: pl.name }, sel: new Set() }); go(filesHash(store.lastDir || (store.roots && store.roots[0].path) || '')); };
	const total = items.reduce((a, x) => a + (x.duration > 0 ? x.duration : 0), 0);
	const menu = anchor => builtin ? openSheet(pl.name, 'heart', [{ label: 'Select…', icon: 'box', sub: 'To unlike several at once', disabled: !items.length, fn: () => setSelMode(true) }], { anchor }) : openSheet(pl.name, 'playlist', [
		{ label: 'Add files…', icon: 'plus', fn: addFiles },
		{ label: 'Select…', icon: 'box', disabled: !items.length, fn: () => setSelMode(true) },
		{ label: 'Remove duplicates', icon: 'refresh', fn: async () => { const before = items.length, r = await edit('dedupe', {}); if (r) { await load(false); toast(r.count != null && before - r.count > 0 ? 'Removed ' + plural(before - r.count, 'duplicate', 'duplicates') : 'No duplicates'); } } }, '-',
		{ label: 'Rename…', icon: 'pencil', fn: () => nameDialog({ title: 'Rename playlist', label: 'Playlist name', initial: pl.name, ok: 'Rename', select: true, submit: async n => { let others = []; try { others = (await get('/api/playlists')).playlists; } catch (e) {} const m = validPlName(n, others, id); if (m) return m; try { await post('/api/playlists/rename', { id, name: n.trim() }); load(false); return ''; } catch (e) { return e.message; } } }) },
		{ label: 'Delete…', icon: 'trash', danger: true, fn: async () => { if (await ask('Delete ' + pl.name + '?', 'Its files stay where they are.', { buttons: [{ label: 'Cancel', value: null }, { label: 'Delete', value: true, cls: 'danger' }] })) { try { await post('/api/playlists/delete', { id }); go('#/playlists'); } catch (e) { toast(e.message); } } } }], { anchor, sub: plural(items.length, 'item', 'items'), seed: id + '/0', kind: 'audio' });
	const rowMenu = (it, i, anchor) => openSheet(it.title, GLYPH[kindOfName(it.p)] || 'note', [
		{ label: 'Play from here', icon: 'play', disabled: it.missing, fn: () => playPlaylist(id, pl.name, items, i, false) },
		{ label: 'Remove from playlist', icon: 'trash', danger: true, fn: () => remove(new Set([it.uid])) }], { anchor, seed: it.p || it.net, kind: kindOfName(it.p || it.net || '') });
	const likedMenu = (it, i, anchor) => openSheet(it.title, GLYPH[it.kind] || 'note', it.missing ? [{ label: 'Unlike', icon: 'heart-o', sub: 'The file is gone from the card', fn: () => remove(new Set([it.uid])) }] : [
		{ label: 'Play from here', icon: 'play', fn: () => playLiked(i) },
		{ label: 'Play in this browser', icon: 'laptop', disabled: NO_HERE || !it.p, sub: NO_HERE ? NO_HERE_WHY : it.p ? undefined : 'Network files play on the handheld only', fn: () => playPlaylist(id, pl.name, items, i, false, 'browser') }, '-',
		{ label: 'Unlike', icon: 'heart-o', fn: () => remove(new Set([it.uid])) },
		it.p && { label: 'Add to playlist…', icon: 'pl-add', fn: () => addToPlaylist([it.p], it.title) },
		it.p && { label: 'Show in Files', icon: 'folder', fn: () => showInFiles(it.p) }], { anchor, seed: it.p || it.net, kind: kindOfName(it.p || it.net || '') });
	if (err) return html`<div class="page"><${Empty} icon="alert" title="Couldn't open this playlist" sub=${err} button="Try again" onClick=${() => load(false)} /></div>`;
	if (!pl) return html`<div class="page">${slow ? html`<${Skeleton} />` : null}</div>`;
	const nowV = view(), nowPath = nowV.has && !nowV.finished ? nowV.path : '';
	const toggleSel = uid => setSel(x => { const n = new Set(x); n.has(uid) ? n.delete(uid) : n.add(uid); return n; });
	return html`<div class=${'page ' + (selMode ? 'pl-sel' : '')}>
		<div class="phead" style="padding-bottom:0"><div class="crumbs"><nav><button aria-label="Back to playlists" onClick=${() => go('#/playlists')}><${Icon} n="left" k=${1} />Playlists</button></nav></div></div>
		<div class="plhero"><span class="cv"><${PlArt} p=${{ id, builtin }} k=${builtin ? 6 : 4} /></span>
			<div class="grow" style="min-width:180px"><div class=${'kicker' + (builtin ? ' y' : '')} style="margin-bottom:8px">${builtin ? 'Built in · newest first' : 'Playlist'}</div><h1>${pl.name}</h1>
				<div class="meta2">${builtin && !items.length ? 'Nothing yet' : plural(items.length, 'item', 'items') + (total > 0 ? ' · ' + fmtLong(total) : '')}</div></div>
			<div class="acts">${items.length > 0 && html`<button class="btn primary" onClick=${() => playPlaylist(id, pl.name, items, 0, false)}><${Icon} n="play" k=${2} />Play</button>
				<button class="btn" onClick=${() => playPlaylist(id, pl.name, items, 0, true)}><${Icon} n="shuffle" k=${2} />Shuffle</button>`}
				${!builtin && html`<button class=${'btn' + (items.length ? ' icon' : '')} aria-label="Add files" title="Add files" onClick=${addFiles}><${Icon} n="plus" k=${2} />${!items.length && 'Add files'}</button>`}
				<button class="btn icon" aria-label=${'More about ' + pl.name} aria-haspopup="dialog" onClick=${e => menu(e.currentTarget)}><${MoreDots} /></button></div></div>
		${items.length === 0 && builtin ? html`<${Empty} icon="heart-o" title="Nothing liked yet" sub="Tap the heart on anything playing, here or on your handheld. It shows up here, newest first." button="Go to Playing" onClick=${() => go('#/playing')} />` : items.length === 0 ? html`<${Empty} icon="playlist" title=${pl.name + ' is empty'} sub="Add songs and videos from Files." button="Add files" onClick=${addFiles} />` : html`
		<div class="box surface listbox"><div class="flist" ref=${listRef} role="list">${items.map((it, i) => { const k = kindOfName(it.p || it.net || ''), on = sel.has(it.uid), cur = !!it.p && it.p === nowPath && !selMode; return html`<div class=${'pitem' + (on ? ' sel' : '') + (kb && kb.uid === it.uid ? ' picked' : '') + (it.missing ? ' gone' : '') + (builtin ? ' nogrip' : '') + (it.fresh ? ' fresh' : '') + (cur ? ' cur' : '')} role="listitem" key=${it.uid} data-uid=${it.uid}>
			<button class="chk" aria-label=${(on ? 'Deselect ' : 'Select ') + it.title} aria-pressed=${on} onClick=${() => toggleSel(it.uid)}><span class=${'check' + (on ? ' on' : '')}>${on && html`<${Icon} n="check" k=${2} />`}</span></button>
			${!builtin && html`<button class="grip" aria-label=${'Reorder ' + it.title} aria-roledescription="drag handle" onKeyDown=${e => gripKey(e, i, it)}><${Icon} n="grip" k=${2} /></button>`}
			<button class="main-btn tt" onClick=${() => selMode ? toggleSel(it.uid) : it.missing ? toast('That file is gone from the card') : builtin ? playLiked(i) : playItem(i)} onContextMenu=${e => { if (matchMedia('(hover: hover)').matches) { e.preventDefault(); (builtin ? likedMenu : rowMenu)(it, i, { x: e.clientX, y: e.clientY }); } }}>
				<span class="tile"><${Art} seed=${it.p || it.net} kind=${k} /></span>
				<span class="grow"><div class="fn ell">${titleFor(it.p || it.net || '', it.title)}</div>
					<div class="fm">${cur && html`<span class="chip y tiny"><${Icon} n="play" k=${1} />Playing</span>`}${it.missing && html`<span class="chip red tiny">Gone</span>`}${it.net && !it.missing && html`<span class="chip tiny"><${Icon} n="network" k=${1} />handheld only</span>`}<span class="ell">${it.net ? baseName(dirName(it.net)) : baseName(dirName(it.p))}</span></div></span>
				${it.duration > 0 && html`<span class="dur">${fmtTime(it.duration)}</span>`}</button>
			${builtin && !selMode && html`<button class="ib like on" aria-label=${'Unlike ' + it.title} title="Unlike" onClick=${() => remove(new Set([it.uid]))}><${Icon} n="heart" k=${2} /></button>`}
			${!selMode && html`<button class="ib more" aria-label=${'More about ' + it.title} aria-haspopup="dialog" onClick=${e => (builtin ? likedMenu : rowMenu)(it, i, e.currentTarget)}><${MoreDots} /></button>`}</div>`; })}</div></div>
		${selMode && html`<div class="selbar box surface" role="toolbar" aria-label="Selection"><div class="acts">
			<button class="btn sm sb" onClick=${() => { setSelMode(false); setSel(new Set()); }} aria-label="Done selecting"><${Icon} n="cross" k=${2} /><span class="lbl" aria-live="polite">${sel.size} selected</span></button>
			${sel.size < items.length ? html`<button class="btn sm sb" onClick=${() => setSel(new Set(items.map(x => x.uid)))}><${Icon} n="box-on" k=${2} /><span class="lbl">All</span></button>` : html`<button class="btn sm sb" onClick=${() => setSel(new Set())}><${Icon} n="box" k=${2} /><span class="lbl">None</span></button>`}
			<button class="btn sm danger sb" disabled=${!sel.size} onClick=${() => remove(sel)}><${Icon} n=${builtin ? 'heart-o' : 'trash'} k=${2} /><span class="lbl">${builtin ? 'Unlike' : 'Remove'}</span></button></div></div>`}`}
		<div class="sr" aria-live="assertive">${live}</div></div>`;
}

