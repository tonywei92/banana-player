import { TAB_ID, set, store } from './store.js';
import { parkHere } from './browser-player.js';
import { closeSSE, openSSE, refreshState, sayBye } from './events.js';
import { closeOverlays } from './toasts.js';
import { loadExtras } from './login.js';

/* ================= one dashboard per browser ================= */
// One tab at a time is connected: two tabs of one browser would be two outputs, two streams of the handheld's few,
// and two sets of picks. A tab just opened asks the others first; if one has the dashboard, the new one doesn't
// connect and offers "Switch here" (then the other steps aside and offers the same). The tabs of this browser and
// this handheld (a channel is per origin) talk over a BroadcastChannel; a browser without one keeps every tab.
const ch = typeof BroadcastChannel === 'function' ? new BroadcastChannel('bp-dashboard') : null;
let claimedAt = 0;

// this tab is the one: the others step aside (each claim carries its time, so of two tabs opened together the later wins)
export function claimTab() {
	if (!ch) return;
	claimedAt = Date.now();
	try { ch.postMessage({ claim: TAB_ID, at: claimedAt }); } catch (e) {}
}

// a tab just opened: does another one have the dashboard? (the one that has it answers at once; a frozen background
// tab may not, and then this one connects and the frozen one steps aside when it wakes to the claim)
export function otherTabHas() {
	if (!ch) return Promise.resolve(false);
	return new Promise(resolve => {
		const ask = Math.random().toString(36).slice(2);
		const done = v => { clearTimeout(t); ch.removeEventListener('message', on); resolve(v); };
		const on = e => { const d = e.data || {}; if (d.here && d.ask === ask) done(true); };
		const t = setTimeout(() => done(false), 600);
		ch.addEventListener('message', on);
		try { ch.postMessage({ ask, from: TAB_ID }); } catch (e) { done(false); }
	});
}

if (ch)
	ch.addEventListener('message', e => {
		const d = e.data || {};
		// asked by a new tab: this one has it (signed in, connected, not aside), and says so
		if (d.ask && d.from !== TAB_ID) {
			if (store.authed && !store.elsewhere) try { ch.postMessage({ here: TAB_ID, ask: d.ask }); } catch (x) {}
			return;
		}
		if (!d.claim || d.claim === TAB_ID || store.elsewhere || !store.authed) return;
		if (claimedAt && d.at < claimedAt) return;	// an older claim than this tab's own: it's the other one that goes
		stepAside();
	});

// another tab has it: nothing plays here (stopped at its place), no stream, and the handheld told at once. Uploads
// under way here carry on to their end (the screen says so): taking over must never lose a file being sent
function stepAside() {
	parkHere();
	closeOverlays();
	closeSSE();
	sayBye();
	claimedAt = 0;
	set({ elsewhere: true });
}

// Switch here: this tab, the other one aside
export function useHere() {
	set({ elsewhere: false });
	claimTab();
	openSSE();
	refreshState();
	loadExtras();	// (a tab that asked first never connected: its likes and the rest come now)
}
