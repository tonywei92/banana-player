import { useEffect } from '../vendor/htm-preact-standalone.module.js';
import { set, store, useStore } from './store.js';
import { errMsg, errToast, get, post } from './api.js';
import { playAt } from './browser-player.js';
import { ask, toast } from './toasts.js';

/* ================= Recent: the handheld's history, newest first, with the places kept (Home shows it: home.js) ================= */
// the list, shared by whoever shows it; asked again on every "recent" event
store.recent = null; store.recentTick = 0;
// rows taken out here and not yet on the handheld (Undo's few seconds): paths
store.recentHidden = {};
let seq = 0;
export async function loadRecent() {
	const n = ++seq;
	try { const r = await get('/api/recent', null, { quiet: true }); if (n === seq) set({ recent: r }); }
	catch (e) { if (n === seq && !store.recent) set({ recent: { error: errMsg(e) } }); }
}
export function useRecent() {
	const s = useStore();
	useEffect(() => { loadRecent(); }, [s.recentTick, s.epoch]);
	return s.recent;
}
// a song or a video, from where it was left (the handheld finds its own place; this browser is told it)
// (from Home: it plays in Home's Now playing card, the page staying, a video too)
export const playRecent = (it, target) => playAt(it.p, it.played ? 0 : it.position, target, 'always');
// the same from its beginning. /api/play has no "from the start": the handheld opens it at its place, and is sent
// back to 0 once it shows the file
export async function playFromStart(it, target) {
	if (target === 'browser') return playAt(it.p, 0, 'browser', 'always');
	if (!await playAt(it.p, 0, 'handheld', 'always')) return false;
	for (let i = 0; i < 40; i++) {
		await new Promise(r => setTimeout(r, 150));
		const sn = store.snap;
		if (sn && sn.path === it.p && !sn.loading) { post('/api/cmd', { c: 'seek', t: 0 }).catch(() => {}); break; }
	}
	return true;
}
// a book, a PDF or a comic: only the handheld shows those, so it is asked to open it at its page
export async function openDoc(it) {
	try { await post('/api/recent/open', { p: it.p }); toast('Opened on the handheld: ' + it.title); } catch (e) { errToast(e); }
}

/* Remove, with Undo: the server can't put a row back, so the row only hides here while the toast shows, and the
   handheld is told when it has gone (or as the page closes) */
const waiting = {};
const hide = (p, on) => { const h = Object.assign({}, store.recentHidden); if (on) h[p] = 1; else delete h[p]; set({ recentHidden: h }); };
async function flush(p) {
	if (!waiting[p]) return;
	clearTimeout(waiting[p]); delete waiting[p];
	try { await post('/api/recent/remove', { p }); await loadRecent(); } catch (e) { errToast(e); }
	hide(p, false);
}
export function removeRecent(it) {
	const p = it.p;
	hide(p, true);
	clearTimeout(waiting[p]); waiting[p] = setTimeout(() => flush(p), 6000);
	toast('Removed from Recent', { label: 'Undo', fn: () => { clearTimeout(waiting[p]); delete waiting[p]; hide(p, false); } }, 6000);
}
addEventListener('pagehide', () => Object.keys(waiting).forEach(flush));
export async function clearRecent() {
	if (!await ask('Clear Recent?', 'The places your videos were left at are kept.', { buttons: [{ label: 'Cancel', value: null }, { label: 'Clear', value: true, cls: 'danger' }] })) return;
	try { await post('/api/recent/clear', {}); toast('Recent cleared'); loadRecent(); } catch (e) { errToast(e); }
}
