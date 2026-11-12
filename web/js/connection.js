import { boot } from './session.js';
import { set, store } from './store.js';
import { B, dropBrowser, el, isPref, parkHere } from './browser-player.js';
import { applySnap, closeSSE, openSSE, playingHere, refreshState } from './events.js';
import { fetchT, get } from './api.js';
import { closeOverlays, seeVersion, toast } from './toasts.js';
import { holdUploads, resumeUploads } from './uploads.js';
import { loadExtras } from './login.js';
import { claimTab } from './onetab.js';

/* ================= connection =================
   ok -> blip (a request or the live stream failed: the app stays, "Reconnecting…" after 2 s) -> down (a goodbye from
   the handheld, or 12 s of failing: the down screen). Probes of /api/ping bring it back; a hidden tab runs no timers. */
export const C = { since: 0, tries: 0, timer: 0, bannerTimer: 0, graceTimer: 0, boot: null, okAt: 0, lastEvent: 0, wd: 0, reason: '', busy: false };
export function connFail() {
	if (store.authed === false || store.conn !== 'ok') return;
	C.since = Date.now(); C.tries = 0; C.reason = '';
	B.wasPlaying = playingHere();	// (the banner pauses it: the down screen still says it was playing)
	closeSSE();
	set({ conn: 'blip' });
	clearTimeout(C.bannerTimer);
	C.bannerTimer = setTimeout(() => { if (store.conn === 'blip') { el.pause(); set({ reconnecting: true }); } }, 2000);
	schedule(400);
}
// a request answered while failing: try the whole way back now
export function connAnswered() { if (store.conn === 'blip' && !C.busy) schedule(0); }
function nextDelay() {
	const t = Date.now() - C.since;
	if (C.reason === 'sleep' ? t < 30e3 : C.tries < 3) return 3000;
	return t < 300e3 ? 10000 : 30000;
}
function schedule(ms) {
	clearTimeout(C.timer); C.timer = 0;
	if (document.hidden) return;
	C.timer = setTimeout(probe, ms);
	if (store.conn === 'down') set({ nextTry: Date.now() + ms });
}
export async function probe() {
	clearTimeout(C.timer); C.timer = 0;
	if (C.busy || document.hidden || store.conn === 'ok') return;
	C.busy = true; set({ probing: true });
	const started = Date.now();
	let ok = false, boot = null;
	try {
		const { r } = await fetchT('/api/ping', { cache: 'no-store' }, 3000);
		boot = r.headers.get('X-BP-Boot');
		seeVersion(r.headers.get('X-BP-Version'));
		if (r.status === 404) ok = (await fetchT('/api/state', { cache: 'no-store' }, 4000)).r.status < 500;	// a player without /api/ping
		else ok = r.status >= 200 && r.status < 300;
	} catch (e) {}
	if (ok) ok = await reconnect(boot);
	C.busy = false;
	if (ok) return;
	C.tries++;
	if (store.conn === 'blip' && Date.now() - C.since >= 12000 && C.tries >= 3) goDown('none');
	set({ probing: false });
	schedule(Math.max(500, nextDelay() - (Date.now() - started)));
}
async function reconnect(boot) {
	let s;
	try { s = await get('/api/state'); }
	catch (e) {
		if (e.status === 401) { C.busy = false; settle(); signOutHere(store.authed === null ? '' : 'expired'); return true; }
		return false;
	}
	const restarted = !!(boot && C.boot && boot !== C.boot);
	if (boot) C.boot = boot;
	const wasDown = store.conn === 'down', first = store.authed === null;
	settle();
	C.okAt = Date.now();
	if (restarted) dropBrowser();
	set({ authed: true, conn: 'ok', reconnecting: false, downReason: '', probing: false, epoch: store.epoch + 1, plTick: store.plTick + (restarted ? 1 : 0), toasts: store.toasts.filter(t => !t.offline) });
	applySnap(s);
	openSSE();
	resumeUploads();
	if (wasDown || first) loadExtras();
	if (first) claimTab();	// this tab has the dashboard now: one opened before steps aside
	if (wasDown && !first) toast('Connected again');
	return true;
}
export function settle() { clearTimeout(C.timer); clearTimeout(C.bannerTimer); clearTimeout(C.graceTimer); C.timer = 0; }
export function goDown(reason) {
	clearTimeout(C.bannerTimer); clearTimeout(C.graceTimer);
	if (store.conn === 'ok') { C.since = Date.now(); C.tries = 0; }
	if (reason !== 'none') { C.since = Date.now(); C.tries = 0; }
	C.reason = reason;
	closeSSE();
	parkHere();
	closeOverlays();
	holdUploads();
	if ('mediaSession' in navigator) { try { navigator.mediaSession.playbackState = 'paused'; navigator.mediaSession.metadata = null; } catch (e) {} }
	// what was said before is past news on this screen ("Continuing on the handheld…" under "asleep")
	set({ conn: 'down', downReason: reason, reconnecting: false, toasts: [] });
	if (!C.busy) schedule(3000);
}
function onVisible() {
	if (document.hidden || store.authed === false || store.elsewhere) return;
	if (store.conn !== 'ok') probe();
	else if (store.authed) { openSSE(); refreshState(); }
}
document.addEventListener('visibilitychange', () => {
	if (document.hidden) { clearTimeout(C.timer); C.timer = 0; if (!playingHere() && !isPref()) closeSSE(); }
	else onVisible();
});
addEventListener('online', onVisible);
addEventListener('pageshow', e => { if (e.persisted) onVisible(); });

// signed out by the handheld (a new PIN) or by expiry: the sign-in page, the route kept in the address
export function signOutHere(why) {
	dropBrowser(true);
	closeSSE(); closeOverlays(); settle();
	set({ authed: false, signedOut: why, snap: null, conn: 'ok', reconnecting: false, downReason: '' });
}

