import { html, useState, useEffect, useRef, useMemo, useCallback } from '../vendor/htm-preact-standalone.module.js';
import { NO_HERE, NO_HERE_WHY, Q_DESK, Q_WIDE, notify, phoneNow, set, store, useStore } from './store.js';
import { errMsg, errToast, get, post } from './api.js';
import { durable, durationOf, useDurations } from './durations.js';
import { baseName, cmpName, dirName, fmtDate, fmtSize, fmtTime, isIOS, itemsText, joinPath, plural, titleFor, touchOnly } from './util.js';
import { addToPlaylist, isLiked, nameDialog, plEdit, setLike } from './plshared.js';
import { ask, openModal, toast } from './toasts.js';
import { AppMenu, Art, Empty, Modal, MoreDots, Skeleton, Storage, openSheet, useLater, useMedia } from './components.js';
import { Icon } from './icons.js';
import { cancelAllUploads, cancelUpload, clearDone, dirLabel, doneTimer, entriesFromDrop, markFresh, pauseUploads, retryUpload, startUpload } from './uploads.js';
import { filesHash, go, replaceHash } from './router.js';
import { fileUrl, playHere, playPaths, view } from './browser-player.js';
import { C } from './connection.js';
import { watchJob } from './jobs.js';

/* ================= files ================= */
// one rule for a file or folder name, wherever it's typed (FAT cards take none of these)
const nameError = n => !n ? 'Give it a name' : /[\/\\:*?"<>|]/.test(n) ? 'Names can\'t use / \\ : * ? " < > |' : n === '.' || n === '..' ? 'That name is taken by the card' : '';
store.knownFolders = new Set();
export const GLYPH = { folder: 'folder', audio: 'note', video: 'film', photo: 'picture', sub: 'cc', text: 'page', epub: 'epub', pdf: 'pdf', comic: 'comic', other: 'file' };
export const rootName = seg => (store.roots || []).find(r => r.path === seg)?.name || 'This card';

// Move… and Copy… (copy: true): a folder on any of the handheld's cards. With more than one place, a choice of them
// above the folder path. A move on one card is a rename, at once; a copy, or a move to another card, runs on the
// handheld as a job (jobs.js) and the key at the bottom shows how far it got
function MovePicker({ paths, start, onClose, done, copy }) {
	const [dir, setDir] = useState(start), [items, setItems] = useState(null), [err, setErr] = useState(''), [busy, setBusy] = useState(false), [tick, setTick] = useState(0);
	const roots = store.roots || [];
	useEffect(() => { setItems(null); setErr(''); get('/api/ls', { p: dir }).then(r => setItems(r.items.filter(i => i.dir).map(i => i.name).filter(n => !paths.includes(joinPath(dir, n))).sort(cmpName))).catch(e => setErr(errMsg(e))); }, [dir, tick]);
	const mkdir = () => nameDialog({ title: 'New folder', label: 'Folder name', ok: 'Create', submit: async n => { n = n.trim(); const bad = nameError(n); if (bad) return bad; try { await post('/api/mkdir', { p: dir, name: n }); setDir(joinPath(dir, n)); setTick(t => t + 1); return ''; } catch (e) { return e.message; } } });
	const segs = dir.split('/'), card = segs[0];
	const where = segs.length > 1 ? segs[segs.length - 1] : rootName(card);
	const n = plural(paths.length, 'item', 'items');
	const submit = async () => {
		setBusy(true);
		try {
			const r = await post(copy ? '/api/copy' : '/api/move', { p: paths, to: dir });
			onClose(); done();
			const moved = r.done.length, bad = r.failed.length;
			if (r.job) watchJob(r.job);
			if (bad) toast((copy ? 'Couldn’t copy ' : 'Couldn’t move ') + (bad === 1 ? '“' + baseName(r.failed[0].p) + '”' : plural(bad, 'item', 'items')) + ': ' + whyNot(r.failed[0].error), null, 8000);
			else if (moved && !r.job) toast('Moved ' + n + ' to ' + where);
		} catch (e) { setErr(e.body && e.body.code === 'exists' ? 'Something in ' + where + ' already has that name' : errMsg(e)); setBusy(false); }
	};
	return html`<${Modal} title=${(copy ? 'Copy ' : 'Move ') + n + ' to…'} onClose=${onClose} actions=${html`<button class="btn" onClick=${onClose}>Cancel</button><button class="btn primary" disabled=${busy || dir === dirName(paths[0])} onClick=${submit}>${copy ? 'Copy here' : 'Move here'}</button>`}>
		${roots.length > 1 && html`<div class="seg tabs2 cardpick" role="tablist" aria-label="Card">${roots.map(r => html`<button role="tab" aria-selected=${r.path === card} title=${fmtSize(r.free) + ' free'} onClick=${() => r.path !== card && setDir(r.path)}><${Icon} n="card" k=${1} /><span class="ell">${r.name}</span></button>`)}</div>`}
		<div class="crumbs" style="gap:8px"><nav aria-label="Destination" class="grow">${segs.map((sg, i) => html`${i > 0 && html`<${Icon} n="right" k=${1} cls="sep" />`}<button aria-current=${i === segs.length - 1 ? 'page' : undefined} onClick=${() => setDir(segs.slice(0, i + 1).join('/'))}>${i === 0 ? rootName(sg) : sg}</button>`)}</nav><button class="btn sm" onClick=${mkdir}><${Icon} n="plus" k=${2} />New folder</button></div>
		<div class="picklist">${items === null ? html`<div style="padding:var(--s4)" class="dim">${err ? '' : 'Loading…'}</div>` : items.length === 0 ? html`<div style="padding:var(--s4)" class="dim">No folders here</div>` :
			items.map(n => html`<button class="sheet-item" onClick=${() => setDir(joinPath(dir, n))}><${Icon} n="folder" k=${2} cls="gl-y" /><span class="grow ell">${n}</span><${Icon} n="right" k=${1} /></button>`)}</div>
		<div class="errtxt" role="alert">${err}</div></${Modal}>`;
}
// a failed item's reason, as the page says it
const whyNot = e => e === 'exists' ? 'something there already has that name' : e;
const openPicker = (paths, start, done, copy) => openModal(close => html`<${MovePicker} paths=${paths} start=${start} onClose=${close} done=${done} copy=${copy} />`);

export function downloadPaths(paths) {
	const files = paths.filter(p => !store.knownFolders.has(p));
	if (files.length < paths.length) toast('Folders can’t be downloaded: ' + (files.length ? 'downloading the files only' : 'pick files'));
	files.forEach((p, i) => setTimeout(() => { const a = document.createElement('a'); a.href = '/api/file?p=' + encodeURIComponent(p) + '&dl=1'; a.download = baseName(p); document.body.appendChild(a); a.click(); a.remove(); }, i * 500));
}
// info: {name -> listing row} for the counts in a folder's question
async function deletePaths(paths, done, info) {
	const one = paths.length === 1, row = one && info && info[baseName(paths[0])];
	const sn = store.snap, hp = sn && sn.path;
	const playing = !!hp && paths.some(p => hp === p || hp.startsWith(p + '/'));
	let title;
	if (one && row && row.dir) title = row.items || row.itemsMore ? `Delete “${row.name}” and ${row.itemsMore ? 'everything' : row.items === 1 ? 'the 1 item' : 'the ' + row.items.toLocaleString() + ' items'} in it?` : `Delete “${row.name}”?`;
	else title = one ? `Delete “${baseName(paths[0])}”?` : 'Delete ' + plural(paths.length, 'item', 'items') + '?';
	const sub = (playing ? (one ? "It's playing on your handheld. " : 'One of them is playing on your handheld. ') : '') + (one ? "It's removed from the card. This can't be undone." : "They're removed from the card. This can't be undone.");
	const r = await ask(title, sub, { buttons: [{ label: 'Cancel', value: null }, { label: playing ? 'Stop and delete' : 'Delete', value: true, cls: 'danger' }] });
	if (!r) return;
	const del = stop => post('/api/delete', { p: paths, stop: stop ? 1 : undefined });
	try { await del(playing); toast('Deleted ' + plural(paths.length, 'item', 'items')); done(); }
	catch (e) {
		if (e.status === 409 && e.body.code === 'playing' && !playing) {
			if (await ask("It's playing on your handheld", 'Stop it there and delete it?', { buttons: [{ label: 'Cancel', value: null }, { label: 'Stop and delete', value: true, cls: 'danger' }] })) {
				try { await del(true); toast('Deleted ' + plural(paths.length, 'item', 'items')); } catch (x) { toast(x.message); }
			}
		} else errToast(e.status === 409 ? Object.assign(e, { message: 'Can’t delete: ' + e.message }) : e);
		done();
	}
}
async function setHidden(paths, on, done) {
	let n = 0;
	for (const p of paths) { try { await post('/api/hide', { p, on: on ? 1 : 0 }); n++; } catch (e) { errToast(e); break; } }
	done && done();
	if (!n) return;
	const what = n === 1 ? '“' + baseName(paths[0]) + '”' : plural(n, 'folder', 'folders');
	toast(what + (n === 1 ? ' is' : ' are') + (on ? ' hidden' : ' shown again'), { label: 'Undo', fn: () => setHidden(paths.slice(0, n), !on, done) });
}
function setShowHidden(on) {
	if (store.snap) { store.snap.showHiddenFolders = on; notify(); }
	post('/api/setting', { key: 'showHiddenFolders', value: on ? 1 : 0 }).catch(errToast);
}
function renamePath(p, done) {
	const old = baseName(p), isDir = store.knownFolders.has(p);
	nameDialog({ title: 'Rename', label: 'New name', initial: old, ok: 'Rename', select: !isDir, submit: async n => {
		n = n.trim(); const bad = nameError(n); if (bad) return bad;
		if (n === old) return '';
		try { await post('/api/rename', { p, name: n }); done(); return ''; } catch (e) { return e.message; }
	} });
}

// a folder's own cover (cover.jpg, folder.png…): its songs' tiles show it
const FOLDER_ART = /^(cover|folder|front|album|albumart[^.]*)\.(jpe?g|png|webp)$/i;
function RowTile({ it, p, art }) {
	if (it.dir) return html`<span class="tile folder"><${Icon} n="folder" k=${2} /></span>`;
	const k = it.kind;
	if (k === 'audio' || k === 'video' || k === 'photo') return html`<span class="tile"><${Art} src=${k === 'audio' ? art : ''} seed=${p} kind=${k} />${k === 'video' && html`<span class="mark"><${Icon} n="film" k=${1} /></span>`}</span>`;
	return html`<span class="tile"><${Icon} n=${GLYPH[k] || 'file'} k=${2} /></span>`;
}
const newFolderIn = (path, done) => nameDialog({ title: 'New folder', label: 'Folder name', ok: 'Create', submit: async n => { n = n.trim(); const bad = nameError(n); if (bad) return bad; try { await post('/api/mkdir', { p: path, name: n }); markFresh(joinPath(path, n)); done(); return ''; } catch (e) { return e.message; } } });

export function Files() {
	const s = useStore(), route = s.route, path = route.path;
	const roots = s.roots;
	const [data, setData] = useState(null), [err, setErr] = useState(''), [selMode, setSelMode] = useState(false), [over, setOver] = useState(false), [filter, setFilter] = useState('');
	const lp = useRef({});
	const wide = useMedia(Q_WIDE), desk = useMedia(Q_DESK);
	const sel = s.sel, pick = s.pick;
	useEffect(() => { if (!path && roots && roots.length === 1) replaceHash(filesHash(roots[0].path)); }, [path, roots]);
	const load = useCallback(async quiet => {
		if (!path) return;
		if (!quiet) setData(null);
		try { const r = await get('/api/ls', { p: path }); r.items.forEach(i => { if (i.dir) store.knownFolders.add(joinPath(path, i.name)); }); setData(r); setErr(''); }
		catch (e) { if (e.status === 404 && path.includes('/')) replaceHash(filesHash(dirName(path))); else setErr(errMsg(e)); }
	}, [path]);
	useEffect(() => { load(false); store.lastDir = path; if (!store.pick) set({ sel: new Set() }); setSelMode(false); setFilter(''); }, [path]);
	// back from a lost connection: the same folder, quietly up to date (the selection stays)
	useEffect(() => { if (s.epoch && (data || err)) load(!!data); }, [s.epoch]);
	useEffect(() => { if (data && (!s.fsDir || s.fsDir === path)) load(true); }, [s.fsTick]);
	const slow = useLater(!data && !err, 300);
	// Up (Backspace or Alt+ArrowUp): the parent folder; not while typing, nor over a dialog
	useEffect(() => {
		const k = e => {
			if (e.defaultPrevented || e.ctrlKey || e.metaKey || e.shiftKey) return;
			if (!((e.key === 'Backspace' && !e.altKey) || (e.key === 'ArrowUp' && e.altKey))) return;
			const t = e.target, segs = path.split('/').filter(Boolean);
			// (a card's top goes up to the list of cards when there's more than one)
			if (segs.length < 2 && !(segs.length === 1 && (store.roots || []).length > 1)) return;
			if (t && (/^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName) || t.isContentEditable)) return;
			if (store.modals.length || store.lightbox || document.querySelector('[aria-modal="true"]')) return;
			e.preventDefault(); go(filesHash(segs.slice(0, -1).join('/')));
		};
		addEventListener('keydown', k); return () => removeEventListener('keydown', k);
	}, [path]);

	const all = useMemo(() => data ? data.items.slice().sort((a, b) => (b.dir - a.dir) || cmpName(a.name, b.name)) : [], [data]);
	const f = desk ? filter.trim().toLowerCase() : '';
	const items = f ? all.filter(i => i.name.toLowerCase().includes(f)) : all;
	const art = useMemo(() => { const c = data && data.items.find(i => !i.dir && FOLDER_ART.test(i.name)); return c ? fileUrl(joinPath(path, c.name)) : ''; }, [data]);
	const full = n => joinPath(path, n);
	const refresh = () => { set({ sel: new Set() }); setSelMode(false); load(true); };
	const setSel = x => set({ sel: new Set(x) });
	const toggle = p => { const n = new Set(store.sel); n.has(p) ? n.delete(p) : n.add(p); setSel(n); };
	const selected = [...sel], selCount = selected.length;
	const showSel = selMode || selCount > 0 || !!pick;
	const here = s.target === 'browser';
	// the phone's small player and the upload key make room for the selection's band
	useEffect(() => { set({ selecting: showSel && !pick }); return () => set({ selecting: false }); }, [showSel, !!pick]);
	const byName = useMemo(() => { const o = {}; (data ? data.items : []).forEach(i => o[i.name] = i); return o; }, [data]);
	// "Show hidden folders" changed here or on the handheld: the list follows
	const showHidden = s.snap ? s.snap.showHiddenFolders : undefined, canHide = showHidden !== undefined;
	const seenHidden = useRef(showHidden);
	useEffect(() => { if (seenHidden.current !== showHidden) { seenHidden.current = showHidden; if (data) load(true); } }, [showHidden]);
	const selFolders = [...sel].filter(p => dirName(p) === path && byName[baseName(p)] && byName[baseName(p)].dir);
	const selAllHidden = selFolders.length > 0 && selFolders.every(p => byName[baseName(p)].hidden);
	// a song's or video's length first, once it's known (asked as its row comes into view)
	const metaOf = it => { const d = it.dir ? 0 : durationOf(full(it.name)); return it.dir ? itemsText(it.items, it.itemsMore) : (d ? fmtTime(d) + ' · ' : '') + fmtSize(it.size) + (wide && it.mtime ? ' · ' + fmtDate(it.mtime) : ''); };
	const listRef = useRef();
	useDurations(listRef, [data, f, path]);

	// a row's ⋯: where it plays · Like, Add · Rename, Move, Copy, Download, Delete
	function rowSheet(it, anchor) {
		const p = full(it.name), isDir = it.dir, k = it.kind;
		const playable = k === 'audio' || k === 'video';
		const playH = { label: isDir ? 'Play folder on handheld' : 'Play on handheld', icon: 'card', fn: () => playOn('handheld', [p]) };
		const playB = { label: isDir ? 'Play folder here' : 'Play in this browser', icon: 'laptop', disabled: NO_HERE || it.playable === 'no', sub: NO_HERE ? NO_HERE_WHY : it.playable === 'no' ? 'Plays on the handheld only' : undefined, fn: () => playOn('browser', [p]) };
		const plays = (isDir || playable) ? (here ? [playB, playH] : [playH, playB]) : [];
		openSheet(it.name, isDir ? 'folder' : GLYPH[k], [...plays, '-',
			// a song or video: Like (never a photo or a folder), then Add
			playable && (liked => ({ label: liked ? 'Unlike' : 'Like', icon: liked ? 'heart' : 'heart-o', y: liked, sub: liked ? 'In Liked' : undefined, fn: () => setLike(p, !liked, 'menu') }))(isLiked(p)),
			(isDir || playable) && { label: 'Add to playlist…', icon: 'pl-add', fn: () => addToPlaylist([p], isDir ? it.name : titleFor(p)) },
			'-',
			isDir && canHide && { label: it.hidden ? 'Unhide folder' : 'Hide folder', icon: 'eye', sub: it.hidden ? 'Listed on the handheld again' : 'Left out of the handheld’s lists', fn: () => setHidden([p], !it.hidden, refresh) },
			{ label: 'Rename…', icon: 'pencil', fn: () => renamePath(p, refresh) },
			{ label: 'Move…', icon: 'folder-move', fn: () => openPicker([p], path, refresh, false) },
			{ label: 'Copy…', icon: 'copy', fn: () => openPicker([p], path, refresh, true) },
			!isDir && { label: 'Download', icon: 'download', fn: () => downloadPaths([p]) },
			{ label: 'Delete…', icon: 'trash', danger: true, fn: () => deletePaths([p], refresh, byName) }],
			{ anchor, sub: metaOf(it), seed: isDir ? null : p, kind: k, cover: k === 'audio' ? art : '' });
	}
	const playOn = (target, paths) => playPaths(paths, store.knownFolders, target);
	const playRow = (it, p) => playHere(t => playPaths([p], store.knownFolders, t, true), p, it.playable);
	const nowV = view(), nowPath = nowV.has && !nowV.finished ? nowV.path : '';
	function rowClick(it) {
		if (lp.current.fired) { lp.current.fired = false; return; }
		const p = full(it.name);
		if (showSel && !it.dir) return toggle(p);
		if (showSel && it.dir && !pick) return toggle(p);
		if (it.dir) return go(filesHash(p));
		if (it.kind === 'photo') { const ph = items.filter(i => i.kind === 'photo').map(i => full(i.name)); return set({ lightbox: { items: ph, i: ph.indexOf(p) } }); }
		if (it.kind === 'audio' || it.kind === 'video') return playRow(it, p);
		rowSheet(it);
	}
	const lpStart = (e, it) => { lp.current.mouse = e.pointerType === 'mouse'; lp.current.fired = false; lp.current.x = e.clientX; lp.current.y = e.clientY; clearTimeout(lp.current.t); if (e.pointerType === 'mouse') return; lp.current.t = setTimeout(() => { lp.current.fired = true; toggle(full(it.name)); navigator.vibrate && navigator.vibrate(10); }, 500); };
	const lpMove = e => { if (Math.hypot(e.clientX - lp.current.x, e.clientY - lp.current.y) > 10) clearTimeout(lp.current.t); };
	const lpEnd = () => clearTimeout(lp.current.t);

	const crumbs = path.split('/').filter(Boolean);
	const upload = ents => startUpload(ents, path);
	const onDrop = e => { e.preventDefault(); e.stopPropagation(); setOver(false); set({ dropping: false, overZone: false }); entriesFromDrop(e.dataTransfer).then(upload); };
	const free = data && data.free;
	const parentPath = crumbs.slice(0, -1).join('/');
	const goUp = () => go(filesHash(parentPath));
	// a card's top: up is the list of cards, when there's more than one (with one, that list is skipped)
	const manyCards = (roots || []).length > 1;
	const canUp = crumbs.length > 1 || (crumbs.length === 1 && manyCards);
	const folderName = crumbs.length > 1 ? crumbs[crumbs.length - 1] : rootName(crumbs[0]);

	if (!path) {
		if (!roots) return html`<div class="page"><${Skeleton} /></div>`;
		return html`<div class="page"><div class="phead"><div class="row"><h1>Files</h1><${AppMenu} /></div></div>
			<div class="box surface listbox"><ul class="flist">${roots.map(r => html`<li class="frow"><button class="main-btn" onClick=${() => go(filesHash(r.path))}><span class="tile folder"><${Icon} n="card" k=${2} /></span><span class="grow"><div class="fn ell">${r.name}</div><div class="fm">${fmtSize(r.free)} free</div></span></button><${Icon} n="right" k=${2} cls="chev" /></li>`)}</ul></div></div>`;
	}
	const addPick = async () => {
		try { await plEdit(pick.id, 'add', { p: selected }); toast('Added to ' + pick.name); const id = pick.id; set({ pick: null, sel: new Set() }); go('#/playlists/' + encodeURIComponent(id)); }
		catch (e) { toast(e.message); }
	};
	const cnt = { folder: 0, audio: 0, video: 0, photo: 0 };
	(data ? data.items : []).forEach(i => { const k = i.dir ? 'folder' : i.kind; if (k in cnt) cnt[k]++; });
	const chips = [['folder', 'folder', 'folders', 'folder'], ['audio', 'song', 'songs', 'note'], ['video', 'video', 'videos', 'film'], ['photo', 'photo', 'photos', 'picture']].filter(([k]) => cnt[k]);
	const head = showSel && !pick ? html`<div class="phead"><div class="row">
			<button class="ib" aria-label="Clear selection" onClick=${() => { setSel([]); setSelMode(false); }}><${Icon} n="cross" k=${2} /></button>
			<h1 aria-live="polite">${selCount} selected</h1>
			${selCount < items.length ? html`<button class="btn sm" onClick=${() => setSel(items.map(i => full(i.name)))}>All</button>` : html`<button class="btn sm" onClick=${() => setSel([])}>None</button>`}</div></div>`
		: html`<div class="phead">
			${canUp && html`<div class="crumbs"><button class="ib upbtn" aria-label=${crumbs.length > 1 ? 'Up a folder' : 'All cards'} title=${crumbs.length > 1 ? 'Up a folder (Backspace)' : 'All cards (Backspace)'} onClick=${goUp}><${Icon} n="up" k=${2} /></button><nav aria-label="Folder path">${manyCards && html`<button onClick=${() => go(filesHash(''))}>Cards</button><${Icon} n="right" k=${1} cls="sep" />`}${crumbs.slice(0, -1).map((sg, i) => html`<button onClick=${() => go(filesHash(crumbs.slice(0, i + 1).join('/')))}>${i === 0 ? rootName(sg) : sg}</button><${Icon} n="right" k=${1} cls="sep" />`)}</nav></div>`}
			<div class="row"><h1>${folderName}</h1><${AppMenu} /></div>
			${data && html`<div class="sub">${chips.map(([k, one, many, g]) => html`<span class="chip"><${Icon} n=${g} k=${1} />${plural(cnt[k], one, many)}</span>`)}<span class="grow"></span>${free != null && html`<${Storage} free=${free} total=${data.total} />`}</div>`}</div>`;
	return html`<div class=${'page files' + (showSel ? ' selmode' : '')}>
		${pick && html`<div class="pickbar box surface"><span class="t">Adding to ${pick.name} · ${selCount} selected</span><button class="btn primary sm" disabled=${!selCount} onClick=${addPick}>Add</button><button class="btn sm" onClick=${() => { const id = pick.id; set({ pick: null, sel: new Set() }); go('#/playlists/' + encodeURIComponent(id)); }}>Cancel</button></div>`}
		${head}
		${!showSel && html`<div class="toolbar">
			<button class=${'btn' + (wide ? '' : ' icon')} aria-label=${wide ? undefined : 'New folder'} title="New folder" onClick=${() => newFolderIn(path, () => load(true))}><${Icon} n="plus" k=${2} />${wide && 'New folder'}</button>
			<button class=${'btn' + (wide ? '' : ' icon')} aria-label=${wide ? undefined : 'Upload files'} title="Upload files" onClick=${() => pickUpload('files')}><${Icon} n="note" k=${2} />${wide && 'Upload files'}</button>
			${!isIOS && html`<button class=${'btn' + (wide ? '' : ' icon')} aria-label=${wide ? undefined : 'Upload a folder'} title="Upload a folder" onClick=${() => pickUpload('dir')}><${Icon} n="folder" k=${2} />${wide && 'Upload folder'}</button>`}
			<button class="btn" onClick=${() => setSelMode(true)}><${Icon} n="box-on" k=${2} />Select</button>
			${desk ? html`<label class="find"><${Icon} n="search" k=${2} /><input class="field" data-slash="1" value=${filter} placeholder="Filter this folder" aria-label="Filter this folder" autocomplete="off" onInput=${e => setFilter(e.target.value)} onKeyDown=${e => { if (e.key === 'Escape') { setFilter(''); e.target.blur(); } }} /><span class="key">/</span></label>` : html`<span class="grow"></span>`}
			${canHide && html`<button class="btn icon" aria-label="More for this folder" aria-haspopup="dialog" onClick=${e => openSheet(folderName, 'folder', [{ label: 'Show hidden folders', icon: 'eye', on: !!showHidden, sub: showHidden ? 'On: the folders hidden on the handheld are listed, dimmed' : 'Off: the folders hidden on the handheld are left out', fn: () => setShowHidden(!showHidden) }], { anchor: e.currentTarget })}><${MoreDots} /></button>`}
		</div>`}
		${!showSel && html`<div class="dropzone ${over ? 'over' : ''}" onDragEnter=${e => { e.preventDefault(); setOver(true); set({ overZone: true }); }} onDragOver=${e => { e.preventDefault(); setOver(true); store.overZone = true; }} onDragLeave=${e => { if (!e.currentTarget.contains(e.relatedTarget)) { setOver(false); set({ overZone: false }); } }} onDrop=${onDrop}>
			<${Icon} n="up" k=${2} /><span>Drop files or folders here · they go to <b>${folderName}</b></span></div>`}
		${err ? html`<${Empty} icon="alert" title="Couldn't open this folder" sub=${err} button="Try again" onClick=${() => load(false)} />` :
			!data ? (slow ? html`<${Skeleton} />` : null) :
			all.length === 0 ? html`<${Empty} icon="folder" title="This folder is empty" sub=${touchOnly ? 'Upload music, videos or photos from this device.' : 'Drop files or folders here, or use Upload files.'} button=${pick ? undefined : 'Upload'} onClick=${() => pickUpload('files')} />` :
			items.length === 0 ? html`<p class="note pad">Nothing here has “${filter.trim()}” in its name.</p>` : html`
			<div class="box surface listbox"><ul class="flist" ref=${listRef}>${!showSel && canUp && html`<li class="frow uprow"><button class="main-btn" onClick=${goUp}><span class="tile xs"><${Icon} n="up" k=${2} /></span><span class="grow"><div class="fn ell" aria-label="Up a folder">..</div></span></button></li>`}${items.map(it => { const p = full(it.name), on = sel.has(p), now = p === nowPath, playable = !it.dir && (it.kind === 'audio' || it.kind === 'video'); return html`<li class=${'frow' + (on ? ' sel' : '') + (s.fresh[p] ? ' fresh' : '') + (it.hidden ? ' hiddenf' : '') + (now && !showSel ? ' cur nowp' : '')} key=${it.name} data-dur=${playable && durable(p) && !durationOf(p) ? p : undefined}>
				<button class="chk" aria-label=${(on ? 'Deselect ' : 'Select ') + it.name} aria-pressed=${on} onClick=${() => toggle(p)}><span class=${'check' + (on ? ' on' : '')}>${on && html`<${Icon} n="check" k=${2} />`}</span></button>
				<button class="main-btn" onClick=${() => rowClick(it)} onPointerDown=${e => lpStart(e, it)} onPointerMove=${lpMove} onPointerUp=${lpEnd} onPointerCancel=${lpEnd} onContextMenu=${e => { e.preventDefault(); if (lp.current.mouse !== false && !lp.current.fired) rowSheet(it, { x: e.clientX, y: e.clientY }); }}>
					<${RowTile} it=${it} p=${p} art=${art} />
					<span class="grow"><div class="fn ell">${it.name}${it.hidden && html`<span class="hidmark" title="Hidden on the handheld"><${Icon} n="eye" k=${2} label="Hidden" /></span>`}${playable && isLiked(p) && html`<span class="likemark" title="Liked"><${Icon} n="heart-s" k=${2} label="Liked" /></span>`}</div>
						<div class="fm">${now && html`<span class="chip y tiny"><${Icon} n="play" k=${1} />Playing</span>`}<span class="ell">${it.hidden ? 'Hidden · ' : ''}${metaOf(it)}</span></div></span>
				</button>
				${playable && !showSel && html`<button class="ib rowplay" aria-label=${'Play ' + it.name} title=${here ? 'Play in this browser' : 'Play on handheld'} onClick=${() => playRow(it, p)}><${Icon} n="play" k=${2} /></button>`}
				${!showSel && html`<button class="ib more" aria-label=${'More about ' + it.name} aria-haspopup="dialog" onClick=${e => rowSheet(it, e.currentTarget)}><${MoreDots} /></button>`}
				${it.dir && !showSel && html`<${Icon} n="right" k=${2} cls="chev" />`}</li>`; })}</ul></div>`}
		${selCount > 0 && !pick && html`<div class="selbar box surface" role="toolbar" aria-label="Selection"><div class="acts">
			<button class="btn sm primary sb" onClick=${() => playOn(s.target, selected)}><${Icon} n="play" k=${2} /><span class="lbl">Play</span></button>
			<button class="btn sm sb" onClick=${() => addToPlaylist(selected, selCount === 1 ? titleFor(selected[0]) : plural(selCount, 'item', 'items'))}><${Icon} n="pl-add" k=${2} /><span class="lbl">Add</span></button>
			<button class="btn sm sb" onClick=${() => openPicker(selected, path, refresh, false)}><${Icon} n="folder-move" k=${2} /><span class="lbl">Move</span></button>
			<button class="btn sm sb" onClick=${() => openPicker(selected, path, refresh, true)}><${Icon} n="copy" k=${2} /><span class="lbl">Copy</span></button>
			<button class="btn sm sb" onClick=${() => downloadPaths(selected)}><${Icon} n="download" k=${2} /><span class="lbl">Download</span></button>
			${canHide && selFolders.length > 0 && html`<button class="btn sm sb" onClick=${() => setHidden(selFolders, !selAllHidden, refresh)}><${Icon} n="eye" k=${2} /><span class="lbl">${selAllHidden ? 'Unhide' : 'Hide'}</span></button>`}
			<button class="btn sm danger sb" onClick=${() => deletePaths(selected, refresh, byName)}><${Icon} n="trash" k=${2} /><span class="lbl">Delete</span></button></div></div>`}
	</div>`;
}

export function Lightbox() {
	const lb = store.lightbox, sw = useRef(null);
	const { items, i } = lb, p = items[i];
	const step = d => set({ lightbox: { items, i: (i + d + items.length) % items.length } });
	const close = () => set({ lightbox: null });
	const ref = useRef();
	useEffect(() => {
		const prev = document.activeElement; ref.current && ref.current.focus();
		return () => prev && prev.focus && prev.focus();
	}, []);
	useEffect(() => {
		const k = e => { if (e.key === 'Escape') close(); if (e.key === 'ArrowLeft') step(-1); if (e.key === 'ArrowRight') step(1); };
		addEventListener('keydown', k); return () => removeEventListener('keydown', k);
	});
	// a sideways swipe moves to the next or previous photo
	const down = e => { sw.current = { x: e.clientX, y: e.clientY }; };
	const up = e => { const a = sw.current; sw.current = null; if (!a || items.length < 2) return; const dx = e.clientX - a.x, dy = e.clientY - a.y; if (Math.abs(dx) > 48 && Math.abs(dx) > Math.abs(dy) * 1.5) step(dx < 0 ? 1 : -1); };
	const show = async () => { try { await post('/api/play', { p }); toast('Showing it on your handheld'); } catch (e) { toast(e.status === 0 ? "Couldn't show it on your handheld" : errMsg(e)); } };
	return html`<div class="lightbox" role="dialog" aria-modal="true" aria-label=${baseName(p)} tabindex="-1" ref=${ref} onPointerDown=${down} onPointerUp=${up} onPointerCancel=${() => sw.current = null} onClick=${e => e.target === e.currentTarget && close()}>
		<img src="/api/file?p=${encodeURIComponent(p)}" alt=${baseName(p)} draggable="false" />
		<div class="lb-top"><span class="grow ell">${baseName(p)}${items.length > 1 ? ' · ' + (i + 1) + ' / ' + items.length : ''}</span>
			<button class="btn sm ghost" onClick=${show}><${Icon} n="card" k=${2} />Show on handheld</button>
			<button class="ib" aria-label="Close" onClick=${close}><${Icon} n="cross" k=${2} /></button></div>
		${items.length > 1 && html`<button class="nav l" aria-label="Previous photo" onClick=${() => step(-1)}><${Icon} n="left" k=${4} /></button><button class="nav r" aria-label="Next photo" onClick=${() => step(1)}><${Icon} n="right" k=${4} /></button>`}</div>`;
}

/* ================= uploads: the round key (bottom right) and its sheet =================
   Only while there's something to show: picking files and folders is the toolbar's Upload files / Upload folder and
   the drop zone. Sending: a dark key with the count and a ring of the
   bytes sent (a red stretch for what failed); done: a green ring and a tick, back to the arrow after 2.5 s. A tap while
   it sends shows each file (a sheet on a phone, a popover elsewhere). Shown in Files, and anywhere while uploads run
   or failed; never over the player or a selection. */
const isUp = i => i.state === 'up' || i.state === 'queued' || i.state === 'waiting';
function upStats() {
	const items = store.upload.items, n = items.length;
	const total = items.reduce((a, i) => a + i.size, 0) || 1;
	const loaded = items.reduce((a, i) => a + (i.state === 'done' ? i.size : i.state === 'failed' ? 0 : i.loaded), 0);
	const failedB = items.filter(i => i.state === 'failed').reduce((a, i) => a + i.size, 0);
	const done = items.filter(i => i.state === 'done').length, failed = items.filter(i => i.state === 'failed').length, running = items.filter(isUp).length;
	return { items, n, total, loaded, failedB, done, failed, running, frac: Math.min(1, loaded / total) };
}
function pickUpload(kind) { const i = document.getElementById(kind === 'dir' ? 'up-dir' : 'up-files'); if (i) i.click(); }
export function UploadFab({ down }) {
	const s = store, st = upStats(), picking = false;
	const state = st.running ? (st.failed ? 'error' : 'busy') : st.failed ? 'error' : s.upDone ? 'done' : 'idle';
	if (!down && (s.route.page === 'playing' || s.selecting || s.pick)) return null;
	if (state === 'idle') return null;
	const C = 2 * Math.PI * 35, yel = st.frac, red = st.failedB / st.total;
	const at = Math.min(st.done + st.failed + 1, st.n), cnt = at + '/' + st.n;
	const label = state === 'idle' ? (picking ? 'Close' : 'Upload') : state === 'done' ? 'Uploads done' : !st.running ? plural(st.failed, 'upload', 'uploads') + ' failed. Show uploads' : `Uploading ${at} of ${st.n}, ${Math.floor(st.frac * 100)}%. Show uploads`;
	const tap = e => { if (state === 'done') clearDone(); else openUploads(e.currentTarget); };
	const q = Math.floor(st.frac * 4) * 25;
	return html`
		<div class=${'fabwrap ' + state + (picking ? ' picking' : '')}>
			${state !== 'idle' && html`<svg class="ring" viewBox="0 0 80 80" aria-hidden="true"><circle cx="40" cy="40" r="35" fill="none" stroke="var(--e-track)" stroke-width="4" />
				<circle class="arc" cx="40" cy="40" r="35" fill="none" stroke=${state === 'done' ? 'var(--green)' : 'var(--yellow)'} stroke-width="4" stroke-linecap="round" stroke-dasharray=${C} stroke-dashoffset=${C * (1 - (state === 'done' ? 1 : yel))} transform="rotate(-90 40 40)" />
				${red > 0 && state !== 'done' && html`<circle cx="40" cy="40" r="35" fill="none" stroke="var(--red)" stroke-width="4" stroke-dasharray=${`${C * red} ${C}`} stroke-dashoffset=${-C * yel} transform="rotate(-90 40 40)" />`}</svg>`}
			<button class=${'box fab' + (state === 'idle' || state === 'done' ? ' yellow' : '')} aria-label=${label} aria-haspopup=${state === 'idle' ? 'menu' : 'dialog'} aria-expanded=${state === 'idle' ? picking : undefined} onClick=${tap}>
				${state === 'idle' ? html`<${Icon} n=${picking ? 'cross' : 'up'} k=${3} />` : state === 'done' ? html`<${Icon} n="check" k=${3} />` : html`<span class=${'cnt' + (cnt.length > 5 ? ' long' : '')}>${cnt}</span>`}</button>
			${st.failed > 0 && state !== 'done' && html`<span class="fbadge" aria-hidden="true"><${Icon} n="alert" k=${1} />${st.failed}</span>`}
			<span class="sr" aria-live="polite">${state === 'busy' || state === 'error' ? (q ? 'Uploaded ' + q + '%' : '') : state === 'done' ? 'Uploads done' : ''}</span></div>`;
}
function openUploads(anchor) {
	if (store.upOpen) return;
	let pos = null;
	if (!phoneNow() && anchor) { const r = anchor.getBoundingClientRect(); pos = `right:${Math.max(8, innerWidth - r.right - 10)}px;bottom:${innerHeight - r.top + 16}px`; }
	set({ upOpen: true });
	openModal(close => html`<${UploadSheet} pos=${pos} onClose=${() => { set({ upOpen: false }); close(); }} />`, () => set({ upOpen: false }));
}
function UploadSheet({ pos, onClose }) {
	useStore();
	const st = upStats(), items = st.items, u = store.upload;
	useEffect(() => { if (!items.length) onClose(); }, [items.length]);
	useEffect(() => {
		if (!pos) return;
		const k = e => { if (e.key === 'Escape') { e.preventDefault(); onClose(); } };
		document.addEventListener('keydown', k); return () => document.removeEventListener('keydown', k);
	}, []);
	const dirs = [...new Set(items.map(i => i.dir))];
	const to = dirs.length > 1 ? plural(dirs.length, 'folder', 'folders') : dirLabel(dirs[0] || '');
	const head = html`<div class="uphead"><div class="kicker y ell">${st.running ? 'Uploading to ' : 'Uploads to '}${to}</div>
		<div style="display:flex;align-items:baseline;gap:10px"><h2 class="grow">${st.running ? Math.min(st.done + st.failed + 1, st.n) + ' of ' + st.n : st.failed ? plural(st.failed, 'upload', 'uploads') + ' failed' : 'All uploaded'}</h2>
			<span class="num dim" style="font-size:13px">${fmtSize(st.loaded)} of ${fmtSize(st.total)} · ${Math.floor(st.frac * 100)}%</span></div>
		<div class="upbar"><i style=${'width:' + st.frac * 100 + '%'}></i></div>
		${store.conn !== 'ok' && html`<div class="note">Waiting for your handheld: they start again once it's back.</div>`}</div>`;
	const rows = html`<div class="uprows">${items.map(it => { const cls = it.state === 'done' ? 'done' : it.state === 'failed' ? 'err' : it.state === 'up' ? 'run' : 'wait'; return html`<div class=${'up ' + cls} key=${it.id}>
		<span class="nm ell">${joinPath(it.rel, it.name)}${dirs.length > 1 && html`<span class="dir ell">${dirLabel(it.dir)}</span>`}</span>
		<span class="st">${it.state === 'done' ? html`<${Icon} n="check" k=${1} />done`
			: it.state === 'failed' ? html`<span>${it.error}</span><button class="btn sm" style="min-height:28px;font-size:13px" onClick=${() => retryUpload(it)}>Retry</button>`
			: html`<span class=${it.state === 'up' ? 'y' : ''}>${it.state === 'up' ? Math.floor(it.loaded / (it.size || 1) * 100) + '%' : it.state === 'queued' ? 'next' : u.paused ? 'paused' : 'waiting'}</span><button class="ib sm" aria-label=${'Cancel ' + it.name} onClick=${() => cancelUpload(it)}><${Icon} n="cross" k=${1} /></button>`}</span>
		${it.state !== 'failed' && html`<div class="bar"><i style=${'width:' + (it.state === 'done' ? 100 : it.size ? it.loaded / it.size * 100 : 0) + '%'}></i></div>`}</div>`; })}</div>`;
	const foot = html`<div class="upfoot">${st.running ? html`<button class="btn sm" onClick=${() => pauseUploads(!u.paused)}>${u.paused ? 'Resume' : 'Pause'}</button><button class="btn sm danger" onClick=${cancelAllUploads}>Cancel all</button>`
		: html`<span></span><button class="btn sm" onClick=${() => { clearTimeout(doneTimer); set({ upload: { ...u, items: [] }, upDone: 0 }); }}>Clear</button>`}</div>`;
	if (pos) return html`<div class="popcatch" onMouseDown=${onClose}></div><div class="uppop box surface" role="dialog" aria-label="Uploads" style=${pos}>${head}${rows}${foot}</div>`;
	return html`<${Modal} cls="upsheet" label="Uploads" raw=${true} onClose=${onClose}>${head}${rows}${foot}</${Modal}>`;
}

