import { TAB_ID, set, store } from './store.js';
import { parkHere } from './browser-player.js';
import { closeSSE, openSSE, refreshState, sayBye } from './events.js';
import { closeOverlays } from './toasts.js';

/* ================= one dashboard per browser ================= */
// As a messaging web app does: the tab opened last is the one connected; the one before steps aside ("open in another
// tab", Use here). Two tabs of one browser would be two outputs, two streams of the handheld's few, and two sets of
// picks. The tabs of this browser and this handheld (a channel is per origin) say so over a BroadcastChannel; a
// browser without one keeps every tab, as before.
const ch = typeof BroadcastChannel === 'function' ? new BroadcastChannel('bp-dashboard') : null;
let claimedAt = 0;

// this tab is the one: the others step aside (each claim carries its time, so of two tabs opened together the later wins)
export function claimTab() {
	if (!ch) return;
	claimedAt = Date.now();
	try { ch.postMessage({ claim: TAB_ID, at: claimedAt }); } catch (e) {}
}

if (ch)
	ch.onmessage = e => {
		const d = e.data || {};
		if (!d.claim || d.claim === TAB_ID || store.elsewhere || !store.authed) return;
		if (claimedAt && d.at < claimedAt) return;	// an older claim than this tab's own: it's the other one that goes
		stepAside();
	};

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

// Use here: this tab again, the other one aside
export function useHere() {
	set({ elsewhere: false });
	claimTab();
	openSSE();
	refreshState();
}
