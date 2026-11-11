import { html } from '../vendor/htm-preact-standalone.module.js';
import { baseName, dirName, fmtSize, joinPath, plural, stem } from './util.js';
import { ask, toast } from './toasts.js';
import { errMsg, get } from './api.js';
import { notify, set, store } from './store.js';
import { connFail, signOutHere } from './connection.js';
import { filesHash, go } from './router.js';

/* ================= uploads ================= */
const UP_MAX = 2, SPACE_MARGIN = 64 * 1024 * 1024;
// any kind of file goes up; a name with a dot in front is the player's own (the handheld refuses it too)
const uploadable = n => !n.startsWith('.');
let upSeq = 0;

export async function entriesFromDrop(dt) {
	// webkitGetAsEntry must be called synchronously, before the first await
	const entries = [...(dt.items || [])].map(i => i.kind === 'file' && i.webkitGetAsEntry ? i.webkitGetAsEntry() : null).filter(Boolean);
	const plain = [...(dt.files || [])];
	const out = [];
	if (!entries.length) { plain.forEach(f => out.push({ file: f, rel: '' })); return out; }
	for (const e of entries) await walkEntry(e, '', out);
	return out;
}
async function walkEntry(entry, rel, out) {
	if (entry.isFile) { const f = await new Promise(r => entry.file(r, () => r(null))); if (f) out.push({ file: f, rel }); return; }
	if (!entry.isDirectory) return;
	const sub = joinPath(rel, entry.name), reader = entry.createReader();
	for (;;) {
		const batch = await new Promise(r => reader.readEntries(r, () => r([])));
		if (!batch.length) break;
		for (const c of batch) await walkEntry(c, sub, out);
	}
}
export function entriesFromInput(files) {
	return [...files].map(f => ({ file: f, rel: f.webkitRelativePath ? dirName(f.webkitRelativePath) : '' }));
}
export function dirLabel(dir) { const n = baseName(dir); return n && dir.includes('/') ? n : 'This card'; }

export async function startUpload(entries, dir) {
	if (!entries.length) return;
	const ok = entries.filter(e => uploadable(e.file.name));
	if (ok.length < entries.length) toast('Skipped ' + plural(entries.length - ok.length, 'file', 'files') + ' with a name starting with a dot');
	if (!ok.length) return;
	let list = ok, free = null;
	try { free = (await get('/api/ls', { p: dir })).free; } catch (e) { if (e.status !== 404) return toast(errMsg(e)); }
	const total = list.reduce((a, e) => a + e.file.size, 0);
	if (free != null && total + SPACE_MARGIN > free) {
		const fit = []; let used = SPACE_MARGIN;
		for (const e of list) if (used + e.file.size <= free) { fit.push(e); used += e.file.size; }
		const r = await ask('Not enough space on ' + dirLabel(dir), `These files need ${fmtSize(total)}; there's ${fmtSize(free)} free.`, fit.length ? { buttons: [{ label: 'Cancel', value: null }, { label: 'Upload what fits', value: true, cls: 'primary' }] } : { buttons: [{ label: 'OK', value: null, cls: 'primary' }] });
		if (!r || !fit.length) return;
		list = fit;
	}
	// clashes: ask once per batch, from the target folders' listings
	const names = new Map();
	for (const rel of new Set(list.map(e => e.rel))) {
		try { names.set(rel, new Set((await get('/api/ls', { p: joinPath(dir, rel) })).items.map(i => i.name))); } catch (e) { names.set(rel, new Set()); }
	}
	const clashes = list.filter(e => names.get(e.rel).has(e.file.name));
	let policy = 'skip';
	if (clashes.length) {
		const n = clashes.length, shown = clashes.slice(0, 5).map(e => joinPath(e.rel, e.file.name));
		policy = await ask(n === 1 ? 'A file with this name is already here' : plural(n, 'file', 'files') + ' with these names are already here', null,
			{ body: html`<ul class="names">${shown.map(x => html`<li class="ell">${x}</li>`)}${n > 5 && html`<li class="dim">and ${(n - 5).toLocaleString()} more</li>`}</ul>`,
				buttons: [{ label: 'Skip', value: 'skip' }, { label: 'Replace', value: 'replace', cls: 'danger' }, { label: 'Keep both', value: 'both', cls: 'primary', focus: true }] });
		if (!policy) return;
	}
	const items = [];
	for (const e of list) {
		let name = e.file.name, overwrite = false;
		if (names.get(e.rel).has(name)) {
			if (policy === 'skip') continue;
			if (policy === 'replace') overwrite = true;
			else { let n = 2; const ext = name.includes('.') ? '.' + name.split('.').pop() : '', base = stem(name); while (names.get(e.rel).has(`${base} (${n})${ext}`)) n++; name = `${base} (${n})${ext}`; names.get(e.rel).add(name); }
		}
		items.push({ id: ++upSeq, file: e.file, name, rel: e.rel, dir, size: e.file.size, loaded: 0, state: 'queued', overwrite, error: '', xhr: null });
	}
	if (!items.length) return;
	clearTimeout(doneTimer);
	set({ upDone: 0, upload: { ...store.upload, items: [...store.upload.items.filter(i => i.state !== 'done' && i.state !== 'cancelled'), ...items] } });
	pumpUploads();
}
function pumpUploads() {
	if (store.conn !== 'ok' || store.upload.paused) return;
	let running = store.upload.items.filter(i => i.state === 'up').length;
	for (const it of store.upload.items) {
		if (running >= UP_MAX) break;
		if (it.state === 'queued') { running++; sendUpload(it); }
	}
}
let lastNote = 0;
function sendUpload(it) {
	const x = new XMLHttpRequest(); it.xhr = x; it.state = 'up'; it.error = '';
	const q = new URLSearchParams({ dir: it.dir, name: it.name }); if (it.rel) q.set('rel', it.rel); if (it.overwrite) q.set('overwrite', '1');
	x.open('PUT', '/api/upload?' + q);
	x.setRequestHeader('X-BP', '1');
	x.upload.onprogress = e => { it.loaded = e.loaded; if (Date.now() - lastNote > 150) { lastNote = Date.now(); notify(); } };
	x.onload = () => {
		it.xhr = null;
		if (x.status >= 200 && x.status < 300) { it.state = 'done'; it.loaded = it.size; markFresh(joinPath(joinPath(it.dir, it.rel), it.name)); set({ fsTick: store.fsTick + 1, fsDir: it.dir }); checkUploadsDone(); }
		else { it.state = 'failed'; let b = {}; try { b = JSON.parse(x.responseText); } catch (e) {} it.error = x.status === 409 ? 'Already there' : b.error || 'Failed (' + x.status + ')'; if (x.status === 401) signOutHere('expired'); }
		notify(); pumpUploads();
	};
	// the connection, not the file: it waits, and starts again from the beginning once the handheld is back
	x.onerror = () => { it.xhr = null; it.state = 'waiting'; it.loaded = 0; connFail(); notify(); };
	x.onabort = () => { it.xhr = null; };
	x.send(it.file);
	notify();
}
export function cancelUpload(it) { if (it.xhr) it.xhr.abort(); it.state = 'cancelled'; set({ upload: { ...store.upload, items: store.upload.items.filter(i => i !== it) } }); pumpUploads(); checkUploadsDone(); }
export function cancelAllUploads() { store.upload.items.forEach(i => { if (i.state === 'up' || i.state === 'queued' || i.state === 'waiting') { if (i.xhr) i.xhr.abort(); i.state = 'cancelled'; } }); set({ upload: { items: store.upload.items.filter(i => i.state === 'failed'), paused: false } }); }
export function retryUpload(it) { it.state = 'queued'; it.loaded = 0; notify(); pumpUploads(); }
export function holdUploads() { store.upload.items.forEach(i => { if (i.state === 'up' || i.state === 'queued') { const x = i.xhr; i.xhr = null; i.state = 'waiting'; i.loaded = 0; if (x) x.abort(); } }); notify(); }
export function resumeUploads() { if (store.upload.paused) return; let any = false; store.upload.items.forEach(i => { if (i.state === 'waiting') { i.state = 'queued'; any = true; } }); if (any) { notify(); pumpUploads(); } }
// Pause: what's on its way stops (it starts again from the beginning: the handheld can't take a file in parts)
export function pauseUploads(on) { store.upload.paused = on; if (on) holdUploads(); else resumeUploads(); notify(); }
// all sent: the key says so (a tick) and a toast says where; 2.5 s later the key is the arrow again
export let doneTimer = 0;
function checkUploadsDone() {
	const its = store.upload.items;
	if (!its.length || its.some(i => i.state !== 'done')) return;
	const dirs = [...new Set(its.map(i => i.dir))], d = dirs[0];
	const there = store.route.page === 'files' && store.route.path === d;
	set({ upDone: Date.now() });
	toast(plural(its.length, 'file', 'files') + ' uploaded to ' + (dirs.length > 1 ? plural(dirs.length, 'folder', 'folders') : dirLabel(d)), dirs.length === 1 && !there ? { label: 'Show', fn: () => go(filesHash(d)) } : null);
	clearTimeout(doneTimer); doneTimer = setTimeout(clearDone, 2500);
}
export function clearDone() { clearTimeout(doneTimer); set({ upDone: 0, upload: { ...store.upload, items: store.upload.items.filter(i => i.state !== 'done') } }); }
// rows that just arrived glow for 2 s
export function markFresh(p) { set({ fresh: Object.assign({}, store.fresh, { [p]: Date.now() }) }); setTimeout(() => { const f = Object.assign({}, store.fresh); delete f[p]; set({ fresh: f }); }, 2200); }
export const uploading = () => store.upload.items.some(i => i.state === 'up' || i.state === 'queued' || i.state === 'waiting');
addEventListener('beforeunload', e => { if (uploading()) { e.preventDefault(); e.returnValue = ''; } });

