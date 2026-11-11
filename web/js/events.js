import { DEVICE_NAME, NO_HERE, TAB_ID, set, store } from './store.js';
import { B, el, isPref, onSession, updateMediaSession } from './browser-player.js';
import { markUsed } from './plshared.js';
import { C, connFail, goDown, signOutHere } from './connection.js';
import { fetchT, get } from './api.js';

/* ================= live updates ================= */
let es = null;
export function applySnap(s) {
	const prev = store.snap;
	set({ snap: s, snapAt: Date.now() });
	// the Session: where it plays and where picks go. Out moving to this tab loads its file here; leaving, it stops
	onSession(s.session);
	if (!prev || prev.playlistSerial !== s.playlistSerial) set({ plTick: store.plTick + 1 });
	// a playlist playing on the handheld is a recent one (the Playlists page's Recent, the sidebar's)
	if (s.source && s.source.kind === 'playlist' && s.source.id && !s.paused && (!prev || prev.path !== s.path)) markUsed(s.source.id);
	updateMediaSession();
}
export function openSSE() {
	// hidden, the page listens only while it plays, or while it's where picks go (a song picked on the handheld comes
	// here, a locked phone too): the handheld taking over must stop it (two sounds otherwise)
	if (es || (document.hidden && !playingHere() && !isPref()) || !store.authed || store.conn !== 'ok') return;
	// (its name and whether it can play: the handheld's Play on lists it)
	const me = es = new EventSource('/api/events?' + new URLSearchParams({ tab: TAB_ID, name: DEVICE_NAME, play: NO_HERE ? '0' : '1' }));
	let heard = false;
	C.lastEvent = Date.now();
	const on = (name, fn) => es.addEventListener(name, e => { heard = true; C.sseRetry = 0; C.lastEvent = Date.now(); let d = {}; try { d = JSON.parse(e.data || '{}'); } catch (x) {} fn(d); });
	on('state', d => applySnap(d));
	on('ping', () => {});
	on('fs', d => set({ fsTick: store.fsTick + 1, fsDir: d.dir }));
	on('playlists', () => set({ plTick: store.plTick + 1, plExternal: true }));
	// the manga reader's state changed (library, repos, queue): the Manga pages reload
	on('manga', () => set({ mangaTick: (store.mangaTick || 0) + 1 }));
	// the handheld's history changed (played, removed, cleared, here or there): Recent loads again
	on('recent', () => set({ recentTick: (store.recentTick || 0) + 1 }));
	on('bye', d => {
		const r = d.reason || 'quit';
		if (r === 'pin') return signOutHere('pin');
		// the lid closed: the handheld freezes moments later. A lid closed with sleep off says "awake" soon after
		if (r === 'sleep') { clearTimeout(C.graceTimer); C.graceTimer = setTimeout(() => goDown('sleep'), 2500); return; }
		goDown(r);
	});
	on('awake', () => clearTimeout(C.graceTimer));
	es.onerror = () => {
		if (es !== me || store.conn !== 'ok') return;
		if (heard) return connFail();
		// refused before a word (the handheld has too many tabs open, say): if it still answers, try the stream again
		// later, quietly, instead of a reconnect loop
		closeSSE();
		C.sseRetry = Math.min(10000, (C.sseRetry || 1500) * 2);
		fetchT('/api/ping', { cache: 'no-store' }, 3000).then(({ r }) => {
			if (r.status >= 500) return connFail();
			refreshState();	// meanwhile, the picture as it is
			clearTimeout(C.sseTimer); C.sseTimer = setTimeout(openSSE, C.sseRetry);
		}).catch(connFail);
	};
	// no event for 35 s (the server sends one every 15): the stream is dead without saying so
	clearInterval(C.wd);
	C.wd = setInterval(() => { if (Date.now() - C.lastEvent > 35000) connFail(); }, 5000);
}
export function closeSSE() { if (es) { es.close(); es = null; } clearInterval(C.wd); C.wd = 0; }
export function playingHere() { return B.active && !el.paused; }
// this browser's video shows in the full player (the media element sits in its slot)
export const videoHere = () => store.target === 'browser' && !!B.meta && B.meta.kind === 'video';
export async function refreshState() { try { applySnap(await get('/api/state')); } catch (e) {} }

