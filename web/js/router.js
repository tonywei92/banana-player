import { pinFromHash } from './login.js';
import { phoneNow, set, store, parseRoute } from './store.js';
import { view } from './browser-player.js';
import { videoHere } from './events.js';
import { reduceMotion } from './playing.js';

/* ================= routes: #/home, #/playing, #/files/<vpath>, #/playlists[/<id>] ================= */
export function go(hash) { location.hash = hash; }
/* which history entries this page made: Back (the player's chevron) only steps into one of them. An entry from an
   earlier load of the page (an update's reload, a tab iOS dropped, the QR's link opened again) would load the page
   anew, and whatever plays here would stop. Each entry carries this load's id and how many of ours lie behind it */
/* Safari also skips, going back, an entry that was left by a step made without a tap (the player opened by itself
   after the QR's link): back from there leaves the page. So each step says whether it came from a tap (bpOk), and
   the one before it too (bpOkPrev, for the sheet over the player) */
const DOC = Math.random().toString(36).slice(2);
let histIdx = 0, curOk = false, replacing = false, uaSwipe = false;
export const tapped = () => !navigator.userActivation || navigator.userActivation.isActive;
function stampHist(extra) { try { history.replaceState(Object.assign({}, history.state || {}, { bpOk: curOk }, extra || {}, { bpDoc: DOC, bpIdx: histIdx }), ''); } catch (x) {} }
export function pushHist(state, url) { const ok = tapped(), prev = curOk; histIdx++; curOk = ok; history.pushState(Object.assign({}, state || {}, { bpDoc: DOC, bpIdx: histIdx, bpOk: ok, bpOkPrev: prev }), '', url); }
export function replaceHist(state, url) { history.replaceState(Object.assign({}, state || {}, { bpDoc: DOC, bpIdx: histIdx, bpOk: curOk }), '', url); }
export function replaceHash(hash) { if (new URL(hash, location.href).hash === location.hash) return; replacing = true; location.replace(hash); }	// (the same address: no hashchange to clear the flag)
// n steps back stay on this page (and on entries Safari won't skip)
export function canGoBack(n) { const st = history.state; return !!st && st.bpDoc === DOC && st.bpIdx >= n && st.bpOk && (n < 2 || st.bpOkPrev); }
stampHist();
addEventListener('popstate', e => {
	// (iOS's edge swipe animates the change itself: no second animation of ours on top)
	uaSwipe = !!e.hasUAVisualTransition;
	const st = e.state;
	// (no state: a new entry for a changed #, which fires this too; hashchange counts it)
	if (st && st.bpDoc === DOC) { histIdx = st.bpIdx; curOk = !!st.bpOk; } else if (st) { histIdx = 0; curOk = false; stampHist(); }
});
export const filesHash = p => '#/files' + (p ? '/' + p.split('/').map(encodeURIComponent).join('/') : '');
addEventListener('hashchange', e => {
	// a new entry (a link, go()) is one more of ours; a replaced one (replaceHash) takes the same place
	if (!(history.state && history.state.bpDoc === DOC)) {
		if (replacing) stampHist();
		else { const ok = tapped(), prev = curOk; histIdx++; curOk = ok; stampHist({ bpOk: ok, bpOkPrev: prev }); }
	}
	replacing = false;
	const swiped = uaSwipe; uaSwipe = false;
	if (/^#pin=/.test(location.hash)) { pinFromHash(); return; }
	// where the player folds back to: the page it was opened from
	const was = store.route, r = parseRoute();
	const apply = () => {
		if (was.page !== 'playing' && r.page === 'playing') { let old = ''; try { old = new URL(e.oldURL).hash; } catch (x) {} set({ backHash: old || '', backNav: !!old, npAnim: Date.now(), sheetOpen: false }); }
		else if (r.page !== 'playing') set({ sheetOpen: false });
		set({ route: r });
	};
	navMotion(swiped ? '' : navKind(was, r), apply);
});
/* motion that tells where you went: deeper slides in from the right (not in Files or Playlists: they switch at once), back slides out to the right, a sibling tab
   cross-fades, the player rises from the small one (its cover, title and play button morph). View Transitions where
   the browser has them (Chrome, Safari 18); elsewhere the player's own slide, and the pages simply switch */
export const depth = r => r.page === 'files' ? (r.path ? r.path.split('/').length : 0) : r.page === 'playlists' ? (r.id ? 1 : 0) : 0;
function navKind(a, b) {
	const compact = phoneNow(), has = view().has;
	// (not over this browser's own video: WebKit froze it for seconds after a View Transition took it along; the
	// player's own slide then)
	if (compact && has && (a.page === 'playing') !== (b.page === 'playing')) return videoHere() ? '' : b.page === 'playing' ? 'player-open' : 'player-close';
	// (the same on a tablet or a computer: the video moves between the player and the bar's tile)
	if (has && videoHere() && (a.page === 'playing') !== (b.page === 'playing')) return '';
	if (a.page !== b.page) return 'fade';
	// folder to folder, the playlists to one and back: at once (a slide every step was too much while browsing)
	if (b.page === 'files' || b.page === 'playlists') return '';
	const d = depth(b) - depth(a);
	return d > 0 ? 'fwd' : d < 0 ? 'back' : (a.path !== b.path || a.id !== b.id) ? 'fade' : '';
}
let vtNow = null;
// a tap while a page changes: the change ends at once and the tap goes through (during a View Transition the browser
// would give the press to the document, not to what's under the finger)
addEventListener('pointerdown', () => { if (vtNow) { try { vtNow.skipTransition(); } catch (x) {} } }, true);
function navMotion(kind, apply) {
	if (!kind || !document.startViewTransition || reduceMotion() || document.hidden || store.authed !== true) { apply(); return; }
	if (vtNow) { try { vtNow.skipTransition(); } catch (x) {} }
	const root = document.documentElement;
	root.dataset.vt = kind;
	store.vtKind = kind;
	try {
		// (rendering is held while the page changes: Preact draws in a microtask, so a timer comes after it; no frames)
		const t = vtNow = document.startViewTransition(() => new Promise(res => { apply(); setTimeout(res, 0); }));
		t.ready.catch(() => {}); t.updateCallbackDone.catch(() => {});
		t.finished.catch(() => {}).then(() => { if (vtNow === t) { delete root.dataset.vt; store.vtKind = ''; vtNow = null; } });
	} catch (x) { delete root.dataset.vt; store.vtKind = ''; apply(); }
}

