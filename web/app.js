// Banana Player web dashboard: the entry point (shell, app and boot). Preact + htm, no build step; the rest is in js/.
import { html, render, useEffect, useMemo } from './vendor/htm-preact-standalone.module.js';
import { B, S, SUBS, browserPlay, browserStop, continueHere, ctl, el, hereNow, setBrowserVol, setTarget, toggleCaptions, view } from './js/browser-player.js';
import { applySnap, closeSSE, openSSE } from './js/events.js';
import { fetchT, get, post } from './js/api.js';
import { Q_DESK, Q_WIDE, notify, set, store, useStore } from './js/store.js';
import { Art, Modal, Seek, Storage, coverUrl, useHeld, useLater, useMedia, useTick } from './js/components.js';
import { depth, filesHash, go } from './js/router.js';
import { LikeBtn, addToPlaylist, likable, loadLikes, newPlaylistDialog, plUsed } from './js/plshared.js';
import { Icon, dimsCache } from './js/icons.js';
import { DevButton, LoopIcon, NowPlaying, TapToPlay, closePanel, loopName, showTab, targetLabel, useVideoSlot } from './js/playing.js';
import { cmpName, fmtTime, isIOS, throttled } from './js/util.js';
import { Playlists } from './js/playlists.js';
import { RECENT_ID } from './js/recent.js';
import { dirLabel, entriesFromDrop, entriesFromInput, startUpload } from './js/uploads.js';
import { Files, Lightbox, UploadFab } from './js/files.js';
import { Extensions } from './js/extensions.js';
import { DownScreen } from './js/down.js';
import { Login, loadExtras, pinFromHash, pingBoot } from './js/login.js';
import { C, connFail, goDown } from './js/connection.js';
import { boot, signOut } from './js/session.js';
import { toast } from './js/toasts.js';

/* ================= shell ================= */
const NAV = [['playing', 'Playing', 'note', '#/playing'], ['files', 'Files', 'folder', '#/files'], ['playlists', 'Playlists', 'playlist', '#/playlists'], ['extensions', 'Extensions', 'blocks', '#/extensions']];
async function loadPls() { try { set({ pls: (await get('/api/playlists', null, { quiet: true })).playlists || [] }); } catch (e) {} }

// the handheld plays it behind its browser (its player left, not stopped): the bars say so
const bgShown = () => { const sn = store.snap, ss = sn && sn.session; return !!sn && !!sn.path && sn.screen === 'browser' && !(ss && ss.out && ss.out.kind === 'browser' && ss.index >= 0); };
// a video playing in this browser (not parked under a photo): away from the player it goes on, live, in the art tile
// of the phone's small player or of the bar (the one <video> moves there: playing.js's useVideoSlot)
const vidHere = v => v.has && v.here && v.kind === 'video' && v.parked == null;
// the phone's small player, floating over the tab bar
function MiniPlayer() {
	const v = view(), slot = useVideoSlot(false); useTick(1000);
	if (!v.has || store.route.page === 'playing' || store.selecting) return null;
	const frac = v.duration ? Math.min(1, v.pos() / v.duration) : 0;
	return html`<div class="mini box surface" onClick=${() => go('#/playing')} role="region" aria-label="Now playing">
		${vidHere(v) ? html`<div class="tile art-sm vthumb" key="v" ref=${slot}></div>` : html`<div class="tile art-sm" key="a"><${Art} src=${coverUrl(v.kind === 'video' ? v.path : v.cover)} seed=${v.path} kind=${v.kind} /></div>`}
		<div class="meta"><div class="t ell">${v.title}</div><div class="m ell">${v.finished ? html`<span class="dim">Finished</span>${v.source && v.source.title ? ' · ' + v.source.title : ''}` : v.here ? 'In this browser' : html`<span class="y">On handheld</span>`}${!v.here && bgShown() ? ' · in the background' : v.artist ? ' · ' + v.artist : ''}</div></div>
		${likable(v) && html`<${LikeBtn} v=${v} cls="ib" />`}
		<button class="ib play" aria-label=${v.finished ? 'Play the list again' : v.paused ? 'Play' : 'Pause'} onClick=${e => { e.stopPropagation(); ctl.toggle(); }}><${Icon} n=${v.paused ? 'play' : 'pause'} k=${2} /></button>
		<div class="prog" aria-hidden="true"><i style=${'width:' + frac * 100 + '%'}></i></div></div>`;
}
// the tablet's and the computer's player bar, on every page (on Playing too: on a computer the transport lives here)
function PlayerBar() {
	const v = view(), s = store, slot = useVideoSlot(false); useTick(1000);
	// (nothing playing: Shuffle and Loop still show and set the settings the next list plays with)
	if (!v.has) return html`<div class="pbar idle" role="region" aria-label="Player"><span class="dim" style="font-size:14px">Nothing playing</span><div class="right">
		<button class=${'ib opt' + (v.shuffled ? ' on' : '')} aria-label="Shuffle" title=${v.shuffled ? 'Shuffle: On' : 'Shuffle: Off'} aria-pressed=${!!v.shuffled} onClick=${ctl.shuffle} disabled=${s.reconnecting}><${Icon} n="shuffle" k=${2} /></button>
		<button class=${'ib opt' + (v.loop !== 'no' ? ' on' : '')} aria-label=${loopName(v.loop)} title=${loopName(v.loop)} aria-pressed=${v.loop !== 'no'} onClick=${ctl.loop} disabled=${s.reconnecting}><${LoopIcon} loop=${v.loop} k=${2} /></button>
		<${DevButton} /></div></div>`;
	const off = s.reconnecting && !v.here, onPage = s.route.page === 'playing', fin = !!v.finished;
	const open = () => { if (!onPage) go('#/playing'); };
	return html`<div class="pbar" role="region" aria-label="Player">
		<div class="pb-seek"><${Seek} view=${v} onSeek=${ctl.seek} disabled=${off || fin} /></div>
		<div class="tx"><button class="ib" aria-label=${fin ? 'Play the last one again' : 'Previous'} onClick=${ctl.prev} disabled=${off}><${Icon} n="prev" k=${2} /></button>
			<button class="box yellow play" aria-label=${fin ? 'Play the list again' : v.paused ? 'Play' : 'Pause'} onClick=${ctl.toggle} disabled=${off}><${Icon} n=${v.paused ? 'play' : 'pause'} k=${3} /></button>
			<button class="ib" aria-label="Next" onClick=${ctl.next} disabled=${off || fin}><${Icon} n="next" k=${2} /></button>
			<span class="ptime">${fin ? html`<span class="dim">Finished</span>` : html`${fmtTime(v.pos())} / ${fmtTime(v.duration)}`}</span></div>
		<div class=${'pb-now' + (onPage ? ' here' : '')}>
			<div class="go" onClick=${open} role=${onPage ? undefined : 'link'} tabindex=${onPage ? undefined : 0} aria-label=${onPage ? undefined : 'Open the player'} onKeyDown=${e => { if (e.key === 'Enter') open(); }}>
				${vidHere(v) && !onPage ? html`<span class="tile art-sm vthumb" key="v" ref=${slot}></span>` : html`<span class="tile art-sm" key="a"><${Art} src=${coverUrl(v.kind === 'video' ? v.path : v.cover)} seed=${v.path} kind=${v.kind} /></span>`}<span class="grow"><div class="t ell">${v.title}</div><div class="m ell">${fin ? 'Finished' + (v.source && v.source.title ? ' · ' + v.source.title : '') : v.artist || targetLabel(s.target)}${!v.here && bgShown() ? ' · in the background' : ''}</div></span></div>
			${likable(v) && html`<${LikeBtn} v=${v} cls="ib" /><button class=${'ib' + (s.addOpen ? ' open' : '')} aria-label="Add to playlist" title="Add to playlist" aria-haspopup="dialog" aria-expanded=${!!s.addOpen} onClick=${e => addToPlaylist([v.path], v.title, e.currentTarget)}><${Icon} n="pl-add" k=${2} /></button>`}</div>
		<div class="right">
			<button class=${'ib opt' + (v.shuffled ? ' on' : '')} aria-label="Shuffle" title=${v.shuffled ? 'Shuffle: On' : 'Shuffle: Off'} aria-pressed=${!!v.shuffled} onClick=${ctl.shuffle} disabled=${off}><${Icon} n="shuffle" k=${2} /></button>
			<button class=${'ib opt' + (v.loop !== 'no' ? ' on' : '')} aria-label=${loopName(v.loop)} title=${loopName(v.loop)} aria-pressed=${v.loop !== 'no'} onClick=${ctl.loop} disabled=${off}><${LoopIcon} loop=${v.loop} k=${2} /></button>
			<${DevButton} />
			<${BarVolume} /></div></div>`;
}
function BarVolume() {
	const here = store.target === 'browser', sn = store.snap;
	const [val, hold] = useHeld(here ? B.vol : sn ? sn.volume : 0);
	const send = useMemo(() => throttled(v => post('/api/volume', { v }).catch(() => {}), 120), []);
	if ((here && isIOS) || (!here && sn && sn.volume < 0)) return null;
	const change = e => { const v = +e.target.value; hold(v); if (here) setBrowserVol(v); else send(v); };
	return html`<div class="bar-vol"><${Icon} n="speaker" k=${2} /><input type="range" class="filled" style=${'--v:' + val + '%'} min="0" max="100" value=${val} onInput=${change} aria-label=${here ? 'Browser volume' : 'Handheld volume'} aria-valuetext=${val + '%'} /></div>`;
}
// the countdown ring only where there's a button to press in time (Undo, Show…); a notice just goes
function Toasts() {
	return html`<div class="toasts" role="status" aria-live="polite">${store.toasts.map(t => html`<div class=${'toast box' + (t.out ? ' out' : '')} key=${t.id} style=${'--ms:' + (t.ms || 4000) + 'ms'}>
		<span class="grow ttl">${t.text}</span>
		${t.action && html`<button class="btn sm primary" onClick=${() => { set({ toasts: store.toasts.filter(x => x.id !== t.id) }); t.action.fn(); }}>${t.action.label}</button>`}
		${t.action && html`<svg class="tring" viewBox="0 0 22 22" aria-hidden="true"><circle cx="11" cy="11" r="9" fill="none" stroke="#3a3842" stroke-width="3" /><circle cx="11" cy="11" r="9" fill="none" stroke="var(--yellow)" stroke-width="3" stroke-dasharray="56.55" stroke-dashoffset="0" /></svg>`}</div>`)}</div>`;
}
function HelpOverlay() {
	const keys = [['Space', 'Play / pause'], ['← →', 'Back 5 s / forward 10 s'], ['Shift + ← →', 'Previous / next'], ['[ ]', 'Previous / next chapter'], ['L', 'Loop'], ['C', 'Subtitles on / off (a video in this browser)'], ['↑ ↓', 'Volume'], ['/', 'Find (Files, Playlists)'], ['?', 'This help'], ['Esc', 'Close']];
	const close = () => set({ help: false });
	return html`<${Modal} title="Keyboard shortcuts" onClose=${close} actions=${html`<button class="btn primary" onClick=${close}>Close</button>`}><div class="helpkeys">${keys.map(([k, d]) => html`<kbd>${k}</kbd><span>${d}</span>`)}</div></${Modal}>`;
}

// the sidebar's playlists: a shortcut, not the library (Liked, up to 5 recent, then all of them on their page)
function SidePls() {
	const s = store, list = s.pls;
	if (!list) return html`<div class="plblock"></div>`;
	const liked = list.find(p => p.builtin), rest = list.filter(p => !p.builtin);
	const used = plUsed(), few = rest.length <= 6;
	const shown = few ? rest.slice().sort((a, b) => cmpName(a.name, b.name)) : rest.filter(p => used[p.id]).sort((a, b) => used[b.id] - used[a.id]).slice(0, 5);
	const cur = s.route.page === 'playlists' ? s.route.id : null;
	const open = id => go('#/playlists/' + encodeURIComponent(id));
	return html`<div class="plblock" role="group" aria-label="Playlists">
		<div class="plhead"><span class="kicker">Playlists</span><span class="num">${list.length}</span><span class="grow"></span><button class="ib sm" aria-label="New playlist" title="New playlist" onClick=${() => newPlaylistDialog(id => open(id))}><${Icon} n="plus" k=${1} /></button></div>
		${liked && html`<button class="plnav" aria-current=${cur === liked.id ? 'page' : undefined} onClick=${() => open(liked.id)}><span class="y"><${Icon} n="heart" k=${1} /></span>Liked</button>`}
		<button class="plnav" aria-current=${cur === RECENT_ID ? 'page' : undefined} onClick=${() => go('#/recent')}><span class="y"><${Icon} n="clock" k=${1} /></span>Recent</button>
		${!few && shown.length > 0 && html`<div class="kicker plsub">Recently used</div>`}
		${shown.map(p => html`<button class="plnav side-pl" key=${p.id} aria-current=${cur === p.id ? 'page' : undefined} onClick=${() => open(p.id)}><${Icon} n="playlist" k=${1} /><span class="ell">${p.name}</span></button>`)}
		${!few && html`<button class="plnav all" onClick=${() => go('#/playlists')}>All ${list.length} playlists<span class="grow"></span><${Icon} n="right" k=${1} /></button>`}</div>`;
}
function Side({ nav }) {
	const s = store, r = s.route, sn = s.snap, root = s.roots && s.roots[0];
	const bat = sn && sn.battery >= 0 ? sn.battery : null, conn = !s.reconnecting;
	return html`<aside class="side" aria-label="Sections">
		<div class="logo"><img class="logo32" src="/logo.svg" width="32" height="32" alt="" /><b>Banana Player</b></div>
		<nav aria-label="Main" style="display:contents">${NAV.map(([k, l, ic, h]) => html`<button class="nav" aria-current=${r.page === k ? 'page' : undefined} onClick=${() => nav(k, h)}><${Icon} n=${ic} k=${2} />${l}</button>`)}</nav>
		<${SidePls} />
		<div class="box surface hhcard" role="group" aria-label="Your handheld">
			<div class="hrow"><span class=${'cdot' + (conn ? '' : ' off')}></span><span class="hname">${conn ? 'Handheld' : 'Reconnecting…'}</span><span class="grow"></span>${bat != null && html`<span class="num dim" style="font-size:13px">${bat}%</span>`}</div>
			${root && root.free != null && html`<${Storage} free=${root.free} total=${root.total} />`}
			<button class="btn sm" onClick=${signOut}>Sign out</button>
			<div class="lnote"><${Icon} n="lock" k=${1} />Not encrypted: home Wi-Fi only</div></div>
		<div class="hh-mini"><span class=${'cdot' + (conn ? '' : ' off')} title=${conn ? 'Handheld connected' : 'Reconnecting…'}></span>${bat != null && html`<span class="num">${bat}%</span>`}
			<button class="ib" aria-label="Sign out" title="Sign out" onClick=${signOut}><${Icon} n="power" k=${2} /></button></div>
	</aside>`;
}

function Shell() {
	const s = useStore(), r = s.route, v = view();
	// the liked files, for the hearts on rows and menus: again whenever a playlist changes anywhere
	useEffect(() => { loadLikes(); }, [s.plTick, s.epoch]);
	const wide = useMedia(Q_WIDE), desk = useMedia(Q_DESK);
	// the sidebar's playlists and the handheld's free space (a computer)
	useEffect(() => { if (desk) loadPls(); }, [desk, s.plTick, s.epoch]);
	useEffect(() => { if (!desk || !s.fsTick) return; const t = setTimeout(() => get('/api/roots', null, { quiet: true }).then(x => set({ roots: x.roots })).catch(() => {}), 2000); return () => clearTimeout(t); }, [desk, s.fsTick]);
	// something playing: on a phone the player takes the whole screen
	const full = r.page === 'playing' && v.has && !wide;
	// drag and drop onto the whole page
	useEffect(() => {
		let depth = 0;
		const hasFiles = e => e.dataTransfer && [...e.dataTransfer.types].includes('Files');
		const enter = e => { if (hasFiles(e)) { e.preventDefault(); depth++; set({ dropping: true }); } };
		const over = e => { if (hasFiles(e)) e.preventDefault(); };
		const leave = e => { if (hasFiles(e) && --depth <= 0) { depth = 0; set({ dropping: false }); } };
		const drop = e => { if (!hasFiles(e)) return; e.preventDefault(); depth = 0; set({ dropping: false }); const dir = store.lastDir || (store.roots && store.roots[0] && store.roots[0].path); if (!dir) return; entriesFromDrop(e.dataTransfer).then(x => startUpload(x, dir)); };
		addEventListener('dragenter', enter); addEventListener('dragover', over); addEventListener('dragleave', leave); addEventListener('drop', drop);
		return () => { removeEventListener('dragenter', enter); removeEventListener('dragover', over); removeEventListener('dragleave', leave); removeEventListener('drop', drop); };
	}, []);
	// keys
	useEffect(() => {
		const key = e => {
			const t = e.target, tag = t.tagName, role = t.getAttribute && t.getAttribute('role');
			if (e.ctrlKey || e.metaKey || e.altKey || store.modals.length || store.lightbox) return;
			if (/^(INPUT|SELECT|TEXTAREA)$/.test(tag) && !(tag === 'INPUT' && t.type === 'range')) return;	// typing
			const slider = role === 'slider' || t.type === 'range';	// arrows belong to a focused slider
			const button = tag === 'BUTTON' || tag === 'A';	// Space belongs to a focused button
			const sn = store.snap, here = hereNow();
			if (e.key === 'Escape' && store.help) return set({ help: false });
			if (e.key === 'Escape' && store.sheetOpen && store.route.page === 'playing') return closePanel();
			if (e.key === '?') return set({ help: !store.help });
			if (e.key === '/') { const f = document.querySelector('[data-slash]'); if (f && f.offsetParent) { e.preventDefault(); f.focus(); } return; }
			if (e.key === ' ' && !button) { e.preventDefault(); ctl.toggle(); }
			else if ((e.key === 'ArrowLeft' || e.key === 'ArrowRight') && !slider) { e.preventDefault(); const fwd = e.key === 'ArrowRight'; if (e.shiftKey) fwd ? ctl.next() : ctl.prev(); else ctl.seekRel(fwd ? 10 : -5); }
			else if (e.key === '[' || e.key === ']') ctl.chapterStep(e.key === ']' ? 1 : -1);
			else if ((e.key === 'l' || e.key === 'L') && (sn || here)) ctl.loop();
			else if ((e.key === 'c' || e.key === 'C') && here && B.meta.kind === 'video') toggleCaptions();
			else if ((e.key === 'ArrowUp' || e.key === 'ArrowDown') && !slider && !button) {
				e.preventDefault();
				const cur = here ? B.vol : sn ? sn.volume : 0, nv = Math.max(0, Math.min(100, cur + (e.key === 'ArrowUp' ? 5 : -5)));
				if (here) setBrowserVol(nv); else { post('/api/volume', { v: nv }).catch(() => {}); if (sn) { sn.volume = nv; notify(); } }
			}
		};
		addEventListener('keydown', key); return () => removeEventListener('keydown', key);
	}, []);
	const page = r.page === 'files' ? html`<${Files} />` : r.page === 'playlists' ? html`<${Playlists} />` : r.page === 'extensions' ? html`<${Extensions} />` : html`<${NowPlaying} />`;
	const miniShown = !wide && v.has && r.page !== 'playing' && !s.selecting;
	const fabShown = r.page !== 'playing' && !s.selecting && !s.pick && (s.upload.items.length > 0 || (r.page === 'files' && !!r.path));
	const nav = (k, h) => go(k === 'files' ? filesHash(store.lastDir || '') : h);
	const upIn = e => { startUpload(entriesFromInput(e.target.files), store.lastDir || (store.roots && store.roots[0] && store.roots[0].path) || ''); e.target.value = ''; };
	return html`<div class=${'shell' + (miniShown ? ' has-mini' : '') + (fabShown ? ' has-fab' : '') + (r.page === 'playing' ? ' on-playing' : '') + (full ? ' fullplayer' + (s.sheetOpen ? ' sheet-open' : '') : '')}>
		<${Side} nav=${nav} />
		<div class="mainwrap">
			<main class="main" id="main">${s.updated && !s.reconnecting && html`<div class="reconnecting updated" role="status"><span>Banana Player was updated</span><button class="btn sm primary" onClick=${() => location.reload()}>Reload</button></div>`}${s.reconnecting && html`<div class="reconnecting" role="status"><span class="cdot off"></span>Reconnecting to your handheld…</div>`}${page}</main>
			<${UploadFab} />
			<${Toasts} />
		</div>
		${!wide && html`<${MiniPlayer} />`}
		<nav class="tabs" aria-label="Main">${NAV.map(([k, l, ic, h]) => html`<button aria-current=${r.page === k ? 'page' : undefined} onClick=${() => nav(k, h)}><${Icon} n=${ic} k=${2} />${l}</button>`)}</nav>
		${wide && html`<${PlayerBar} />`}
		<input type="file" multiple hidden id="up-files" onChange=${upIn} />
		<input type="file" hidden webkitdirectory id="up-dir" onChange=${upIn} />
		${B.blocked && r.page !== 'playing' && html`<${TapToPlay} floating=${true} />`}
		${s.lightbox && html`<${Lightbox} />`}
		${s.help && html`<${HelpOverlay} />`}
		${s.modals.map(m => html`<div class=${'mwrap' + (m.out ? ' out' : '')} key=${m.id}>${m.render(m.close)}</div>`)}
		${s.dropping && !s.overZone && html`<div class="dropover"><div>Drop to upload to ${dirLabel(store.lastDir || 'This card')}</div></div>`}
	</div>`;
}
const Fragment = ({ children }) => children;

/* ================= app and boot ================= */
function App() {
	const s = useStore();
	const slow = useLater(s.authed === null && s.conn !== 'down', 600);
	if (s.conn === 'down' && s.authed !== false) return html`<div class="downwrap"><${DownScreen} />${s.upload.items.length > 0 && html`<${UploadFab} down=${true} />`}<${Toasts} />
		${s.modals.map(m => html`<div class=${'mwrap' + (m.out ? ' out' : '')} key=${m.id}>${m.render(m.close)}</div>`)}</div>`;
	if (s.authed === null) return html`<main class="login"><div class="signin"><div class="brand"><img src="/logo.svg" alt="" width="64" height="64" />Banana Player</div><p class="dim" aria-live="polite">${slow ? 'Connecting…' : ' '}</p></div></main>`;
	if (!s.authed) return html`<${Login} />`;
	return html`<${Shell} />`;
}
// headless and simulator checks: ?debug&do=sheet:queue,scroll:600,click:New folder (one step every 1.5 s, after boot)
if (location.search.includes('debug')) {
	const steps = (new URLSearchParams(location.search).get('do') || '').split(',').filter(Boolean);
	steps.forEach((st, i) => setTimeout(() => {
		const [k, ...rest] = st.split(':'), v = rest.join(':');
		if (k === 'sheet') showTab(v);
		else if (k === 'scroll') { const m = document.querySelector('.main'); if (m) m.scrollTop = +v; }
		else if (k === 'click') { const b = [...document.querySelectorAll('button')].find(x => x.offsetParent && (x.textContent.trim() === v || x.getAttribute('aria-label') === v)); if (b) b.click(); }
	}, 2500 + i * 1500));
}
if (location.search.includes('debug')) window.__bp = { store, set, closePanel, setTarget, continueHere, B, C, SUBS, startUpload, entriesFromDrop, browserPlay, toast, go, goDown, connFail, el, ctl, view, S };	// for the headless tests
// the icons first (their sizes come from the sprite), but never a blank page waiting for them
(async () => {
	let rendered = false;
	const go1 = () => { if (rendered) return; rendered = true; render(html`<${App} />`, document.getElementById('root')); boot(); };
	const late = setTimeout(go1, 1500);
	try {
		const { r, text } = await fetchT('/sprite.svg', {}, 10000);
		if (r.ok) { document.body.insertAdjacentHTML('afterbegin', text); for (const k in dimsCache) delete dimsCache[k]; if (rendered) notify(); }
	} catch (e) {}
	clearTimeout(late); go1();
})();

