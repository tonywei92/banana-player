import { applySnap, closeSSE, openSSE } from './events.js';
import { get, post } from './api.js';
import { C, goDown } from './connection.js';
import { loadExtras, pinFromHash, pingBoot } from './login.js';
import { browserStop } from './browser-player.js';
import { set } from './store.js';
import { claimTab, otherTabHas } from './onetab.js';

// signing out, and the page's first request
export async function signOut() { browserStop(); closeSSE(); try { await post('/api/logout'); } catch (e) {} set({ authed: false, signedOut: '', snap: null }); }

export async function boot() {
	if (/^#pin=/.test(location.hash)) return pinFromHash();
	pingBoot();
	try {
		const st = await get('/api/state');
		// one tab at a time: another tab of this browser has it, and this one asks "Switch here?" (onetab.js)
		const taken = await otherTabHas();
		set({ authed: true, elsewhere: taken }); C.okAt = Date.now(); applySnap(st);
		if (taken) return;
		openSSE(); loadExtras(); claimTab();
	}
	catch (e) {
		if (e.status === 401) set({ authed: false });
		else goDown('none');	// nothing to show yet: say so at once, and keep trying
	}
}
