import { html, render, useState, useEffect, useLayoutEffect, useRef, useMemo, useCallback } from '../vendor/htm-preact-standalone.module.js';
import { intent, B, S, clearFinished, finSource, finishedList, playFinished, playListOnHandheld, whereToPlay, playBlocked, CUE_SIZES, SPEEDS, SUBS, assBox, setSpeed, toggleCaptions, browserPlay, chooseAudio, chooseSub, directAudio, nativeAudio, playingAudio, streamWhy, continueHere, ctl, cueStyle, el, handoffToHandheld, playParked, probe1, probeOK, setBrowserVol, setCueStyle, setTarget, sizeCues, view } from './browser-player.js';
import { AppMenu, Art, Empty, MoreDots, Seek, coverUrl, openSheet, palOf, useHeld, useMedia, useTick } from './components.js';
import { NO_HERE, NO_HERE_WHY, Q_DESK, Q_WIDE, lsGet, lsSet, notify, phoneNow, set, store, useStore } from './store.js';
import { baseName, chapterTitle, dirName, fmtDate, fmtSize, fmtTime, isIOS, kindOfName, throttled, titleFor, touchOnly, userTap } from './util.js';
import { Icon } from './icons.js';
import { openModal, toast, trackDir, trackDirAt } from './toasts.js';
import { canGoBack, filesHash, go, pushHist, replaceHash, replaceHist, tapped } from './router.js';
import { LikeBtn, addToPlaylist, likable, showInFiles } from './plshared.js';
import { signOut } from './session.js';
import { videoHere } from './events.js';
import { errMsg, errToast, get, post } from './api.js';
import { durable, durationOf, useDurations } from './durations.js';
import { Files, downloadPaths, rootName } from './files.js';

/* ================= now playing =================
   Phone: a full-screen player the small one expands into (a chevron or a swipe down folds it away); its panel (Up next,
   Chapters, Tracks, Sound, Details) is a sheet from the bottom. Tablet: in the page, the cover beside the title and the
   controls, the panel a card underneath. Computer: the stage and a panel column; the bar at the bottom carries the
   transport, so there's one set of controls. */
export const reduceMotion = () => matchMedia('(prefers-reduced-motion: reduce)').matches;
const playableCache = {};
function useCanPlayHere(path) {
	const [r, setR] = useState(null);
	useEffect(() => {
		if (!path) { setR(null); return; }
		if (playableCache[path]) { setR(playableCache[path]); return; }
		let dead = false;
		probe1(path).then(m => { const ok = m ? probeOK(m) : true; playableCache[path] = ok ? 'yes' : 'no'; if (!dead) setR(playableCache[path]); });
		return () => { dead = true; };
	}, [path]);
	return r;
}
// elapsed on the left; on the right the time left ("−0:57"), or the length: a tap swaps them. skip: −5 s / +10 s between
function Times({ v, skip, off }) {
	useTick(1000);
	const [total, setTotal] = useState(lsGet('bpTimeTotal') === '1');
	const p = v.pos(), d = v.duration;
	const flip = () => { lsSet('bpTimeTotal', total ? '0' : '1'); setTotal(!total); };
	return html`<div class="times"><span>${fmtTime(p)}</span>
		${skip && html`<span class="skips"><button class="btn sm" aria-label="Back 5 seconds" onClick=${() => ctl.seekRel(-5)} disabled=${off}>−5 s</button><button class="btn sm" aria-label="Forward 10 seconds" onClick=${() => ctl.seekRel(10)} disabled=${off}>+10 s</button></span>`}
		${d > 0 ? html`<button class="tm-r" onClick=${flip} aria-label=${total ? 'Length ' + fmtTime(d) + '. Show the time left' : fmtTime(d - p) + ' left. Show the length'}>${total ? fmtTime(d) : '−' + fmtTime(d - p)}</button>` : html`<span></span>`}</div>`;
}

/* ---- where it plays: a two-segment switch on the player; in the bar, a key that opens a bubble ---- */
export const targetLabel = t => t === 'browser' ? 'In this browser' : 'On handheld';
// what the handheld plays can't play here (probed): the browser's choice says so
const noHereFor = () => { const sn = store.snap; return store.target === 'handheld' && !B.active && !!sn && !!sn.path && playableCache[sn.path] === 'no'; };
function DevSwitch({ small }) {
	const t = store.target, why = NO_HERE ? NO_HERE_WHY : noHereFor() ? 'Plays on the handheld only' : '';
	return html`<div class="seg dev" role="group" aria-label="Where it plays">
		<button aria-pressed=${t === 'handheld'} aria-label="On handheld" title="On handheld" onClick=${() => setTarget('handheld')}><${Icon} n="card" k=${2} />${!small && 'Handheld'}</button>
		<button aria-pressed=${t === 'browser'} aria-label=${'In this browser' + (why ? '. ' + why : '')} title=${why || 'In this browser'} aria-disabled=${why && t !== 'browser' ? 'true' : undefined} onClick=${() => setTarget('browser')}><${Icon} n="laptop" k=${2} />${!small && 'This browser'}</button></div>`;
}
export function DevButton() {
	const t = store.target;
	return html`<button class=${'btn sm devbtn' + (t === 'handheld' ? ' hh' : '')} aria-haspopup="dialog" aria-expanded=${!!store.devOpen} aria-label=${targetLabel(t) + '. Change where it plays'} title=${targetLabel(t)} onClick=${e => deviceBubble(e.currentTarget)}>
		<${Icon} n=${t === 'browser' ? 'laptop' : 'card'} k=${2} /><span class="lbl">${t === 'browser' ? 'Browser' : 'Handheld'}</span><${Icon} n="down" k=${1} /></button>`;
}
function deviceBubble(anchor) {
	const r = anchor.getBoundingClientRect(), W = 300;
	const right = Math.max(8, innerWidth - r.right), bottom = innerHeight - r.top + 14;
	const notch = Math.max(14, Math.min(W - 30, innerWidth - right - (r.left + r.right) / 2 - 7));
	set({ devOpen: true });
	openModal(close => html`<${DevBubble} pos=${`right:${right}px;bottom:${bottom}px;--notch:${notch}px`} onClose=${() => { set({ devOpen: false }); close(); }} />`, () => set({ devOpen: false }));
}
function DevBubble({ pos, onClose }) {
	const s = useStore(), t = s.target, v = view(), ref = useRef();
	useEffect(() => {
		const b = ref.current && ref.current.querySelector('.sheet-item'); if (b) b.focus();
		const k = e => { if (e.key === 'Escape') { e.preventDefault(); onClose(); } };
		document.addEventListener('keydown', k); return () => document.removeEventListener('keydown', k);
	}, []);
	const at = v.has ? fmtTime(v.pos()) : '', live = v.has && !v.finished;
	const hereSub = NO_HERE ? NO_HERE_WHY : noHereFor() ? 'Plays on the handheld only' : t === 'handheld' && live && v.kind !== 'photo' ? 'Pauses the handheld, carries on at ' + at : 'This phone or computer';
	const hhSub = t === 'browser' && live ? 'Pauses this browser, carries on there at ' + at : "The handheld's speakers or headphones";
	const pick = x => { onClose(); setTarget(x); };
	return html`<div class="popcatch" onMouseDown=${onClose}></div>
		<div class="bubble box surface" role="dialog" aria-label="Play on" ref=${ref} style=${pos}>
			<div class="kicker">Play on</div>
			<button class=${'sheet-item' + (t === 'handheld' ? ' on' : '')} aria-pressed=${t === 'handheld'} onClick=${() => pick('handheld')}><${Icon} n="card" k=${2} /><span class="grow"><b>On handheld</b><span class="sm">${hhSub}</span></span></button>
			<button class=${'sheet-item' + (t === 'browser' ? ' on' : '')} aria-pressed=${t === 'browser'} aria-disabled=${NO_HERE ? 'true' : undefined} onClick=${() => pick('browser')}><${Icon} n="laptop" k=${2} /><span class="grow"><b>In this browser</b><span class="sm">${hereSub}</span></span></button></div>`;
}

// "English" from "English · SRT", "Track 2" from "Track 2 · SRT"
const shortLabel = l => (l || '').split(' · ')[0];
function subChip(v) {
	if (v.here) { const s = SUBS.list.find(x => x.i === SUBS.chosen); return s ? shortLabel(s.label) : 'Off'; }
	const t = v.tracks; if (!t) return null;
	const s = (t.sub || []).find(x => x.selected); return s ? shortLabel(s.label) : 'Off';
}
/* the sheet and the full player answer Back (Android's button, iOS's swipe, the browser's): each open layer is a
   history entry of its own, with the same address, so the hash routes don't move */
export const compactNow = () => phoneNow();
function openPanel(extra) {
	if (!store.sheetOpen && compactNow() && !(history.state && history.state.sheet)) pushHist({ sheet: 1 });
	set(Object.assign({ sheetOpen: compactNow() }, extra));
}
export function closePanel() {
	if (!store.sheetOpen) return;
	set({ sheetOpen: false });
	if (history.state && history.state.sheet) history.back();
}
addEventListener('popstate', e => { if (store.sheetOpen && !(e.state && e.state.sheet)) set({ sheetOpen: false }); });
// the panel's tab, opened (the sheet slides up on a phone)
export function showTab(tab, extra) {
	openPanel(Object.assign({ npTab: tab }, extra));
	requestAnimationFrame(() => { const c = document.querySelector('.np-panel .tabbody, .sheet2 .tabbody, .np-card .tabbody'); if (c) c.scrollTop = 0; });
}
const showTracks = tab => showTab('tracks', { tracksTab: tab });
function npTabs(v) {
	const vid = v.kind === 'video', t = [['queue', 'Up next']];
	if (v.chapters.length) t.push(['chapters', 'Chapters']);
	if (vid || (v.tracks && (v.tracks.audio || []).length > 1)) t.push(['tracks', 'Tracks']);	// (the tab's own switch says Audio / Subtitles)
	t.push(['sound', 'Sound'], ['details', 'Details']);
	return t;
}
const NP_GLYPH = { queue: 'playlist', chapters: 'chapters', tracks: 'cc', sound: 'eq', details: 'info' };
// the phone's three keys under the transport (the sheet has every tab)
function threeKeys(v, tabs) {
	const has = k => tabs.some(x => x[0] === k);
	const want = v.kind === 'video' ? ['queue', has('chapters') ? 'chapters' : 'sound', 'tracks'] : ['queue', 'sound', 'details'];
	return want.filter(has).map(k => tabs.find(x => x[0] === k));
}
function TabBody({ v, tab }) {
	return html`<div class="tabpane" key=${tab}>${TabBody1({ v, tab })}</div>`;
}
function TabBody1({ v, tab }) {
	return tab === 'details' ? html`<${DetailsTab} v=${v} />` : tab === 'chapters' ? html`<${ChaptersCard} v=${v} />` : tab === 'tracks' ? html`<${TracksCard} v=${v} />`
		: tab === 'sound' ? html`<${VolumeCard} />${v.here ? html`<${SoundHere} />` : html`<${SoundCard} v=${v} />`}` : html`<${UpNext} v=${v} />`;
}
function PanelTabs({ tabs, tab, open, onTab }) {
	return html`<div class="ptabs" role="tablist" aria-label="More about what's playing">${tabs.map(([k, l]) => html`<button role="tab" id=${'pt-' + k} aria-controls="np-tabbody" aria-selected=${open !== false && tab === k} onClick=${() => onTab(k)}>${l}</button>`)}</div>`;
}

/* ---- the media: the video here, the cover, or the handheld's video as a poster; taps ---- */
function useFullscreen() {
	const [fs, setFs] = useState(null);
	useEffect(() => { const f = () => setFs(document.fullscreenElement || document.webkitFullscreenElement || null); document.addEventListener('fullscreenchange', f); document.addEventListener('webkitfullscreenchange', f); return () => { document.removeEventListener('fullscreenchange', f); document.removeEventListener('webkitfullscreenchange', f); }; }, []);
	return fs;
}
function toggleFullscreen(box) {
	const cur = document.fullscreenElement || document.webkitFullscreenElement;
	if (cur) { (document.exitFullscreen || document.webkitExitFullscreen).call(document); return; }
	if (box.requestFullscreen) box.requestFullscreen().catch(() => {});
	else if (box.webkitRequestFullscreen) box.webkitRequestFullscreen();
	else if (el.webkitEnterFullscreen) el.webkitEnterFullscreen();	// iPhone: its own player, which keeps the subtitle tracks
}
// a track that changed while the page shows: in from the right (Next) or the left (Previous), else a fade
const seenTrack = { p: null };
function trackClass(p) {
	if (seenTrack.p === null) { seenTrack.p = p; return ''; }
	if (seenTrack.p !== p) { seenTrack.p = p; seenTrack.cls = Date.now() - trackDirAt < 4000 ? (trackDir < 0 ? 'trk-prev' : 'trk-next') : 'trk-fade'; seenTrack.at = Date.now(); }
	return Date.now() - (seenTrack.at || 0) < 600 ? seenTrack.cls || '' : '';
}
// the browser wouldn't start the sound without a tap (Play on from the handheld, mostly): one big button over the
// media (floating: over the page, away from the player). The handheld meanwhile says it waits, and its B takes it back
export function TapToPlay({ floating }) {
	const title = B.meta ? B.meta.title : '', self = Date.now() - (S.selfAt || 0) < 5000;
	const stop = e => e.stopPropagation();
	return html`<div class=${'tapplay' + (floating ? ' float' : '')} onPointerUp=${stop} onClick=${e => { stop(e); playBlocked(); }} role="group" aria-label="Tap to play">
		<button class="box yellow tap-btn" onClick=${e => { stop(e); playBlocked(); }}><${Icon} n="play" k=${3} />Tap to play</button>
		<div class="tap-t ell">${self ? title : 'The handheld sent ' + title + ' here'}</div>
		<div class="tap-s">Your browser needs one tap to start the sound</div></div>`;
}
/* the one <video> shows in one slot at a time: the newest on the page (the player's media box, or the small player
   while a video plays here and the player isn't shown); with none it waits in #holder, still in the document (a media
   element taken out of it is paused). A slot leaving gives it back before Preact takes the slot out (a ref is let go
   first; an effect's cleanup comes after). The ASS canvas (assBox) goes only into a slot that asks for it (the big
   box); the small one shows the picture alone: :root.vid-mini is set while the video sits in it, and style.css hides
   the picture overlays then */
const vslots = [];
function dockVideo() {
	const top = vslots[vslots.length - 1], holder = document.getElementById('holder'), host = top ? top.n : holder;
	const was = !el.paused && !!el.getAttribute('src');
	if (el.parentNode !== host) host.prepend(el);
	if (top && top.ass) { if (el.nextSibling !== assBox) el.after(assBox); }
	else if (assBox.parentNode !== holder) holder.appendChild(assBox);
	document.documentElement.classList.toggle('vid-mini', !!top && !top.ass);
	sizeCues();
	// (WebKit may pause a playing video moved within the page: the same element was let play, so it goes on, unless
	// the Session meanwhile says paused)
	if (was) {
		const on = () => { if (el.paused && B.active && !B.leaving && B.parked == null && el.getAttribute('src') && S.s && S.s.playing) el.play().catch(() => {}); };
		on(); setTimeout(on, 0);
	}
}
export function useVideoSlot(ass) {
	const me = useRef(null);
	return useCallback(n => {
		const i = vslots.indexOf(me.current);
		if (i >= 0) vslots.splice(i, 1);
		me.current = n ? { n, ass } : null;
		if (n) vslots.push(me.current);
		dockVideo();
	}, []);
}
function Media({ v, off, immersive, onCollapse }) {
	const box = useRef();
	const slot = useVideoSlot(true);
	const live = v.here && v.kind === 'video';
	const fsEl = useFullscreen(), fs = !!fsEl && fsEl === box.current;
	const full = immersive || fs;
	const [ov, setOv] = useState(false), [pulse, setPulse] = useState(null), [rip, setRip] = useState(null);
	const t = useRef({ last: 0, side: '', single: 0, hide: 0, n: 0, menu: null, closedAt: 0 });
	// the gear's menu (null, or its page: main, audio, subs, speed)
	const [menu, setMenu0] = useState(null);
	const setMenu = m => { t.current.menu = m; setMenu0(m); if (!m) { t.current.closedAt = Date.now(); showOv(); } };
	useEffect(() => () => { clearTimeout(t.current.single); clearTimeout(t.current.hide); }, []);
	useEffect(() => { if (!live && menu) setMenu(null); }, [live]);
	// the overlay fades out after 3 s while it plays; it stays while paused, and while the menu is open
	const showOv = () => { setOv(true); clearTimeout(t.current.hide); t.current.hide = setTimeout(() => { if (!view().paused && !t.current.menu) setOv(false); }, 3000); };
	const toggle = () => { if (off) return; const was = view().paused; ctl.toggle(); setPulse({ k: was ? 'play' : 'pause', id: ++t.current.n }); if (ov) showOv(); };
	const seek = (side) => { if (off) return; ctl.seekRel(side === 'l' ? -5 : 10); setRip(r => ({ side, id: ++t.current.n, n: r && r.side === side && Date.now() - r.at < 900 ? r.n + 1 : 1, at: Date.now() })); };
	const up = e => {
		if (e.button > 0 || e.target.closest('button,.seek,.vmenu') || box.current.dataset.swiped) return;
		// a tap beside the open menu only closes it
		if (t.current.menu || Date.now() - t.current.closedAt < 350) { if (t.current.menu) setMenu(null); return; }
		const r = box.current.getBoundingClientRect(), x = (e.clientX - r.left) / r.width;
		const side = x < 1 / 3 ? 'l' : x > 2 / 3 ? 'r' : 'c', tt = t.current, now = Date.now();
		// a mouse: a click plays or pauses at once, a double click is fullscreen
		if (e.pointerType === 'mouse') { if (now - tt.last < 300 && live) { tt.last = 0; toggleFullscreen(box.current); return; } tt.last = now; toggle(); return; }
		// a finger: a double tap on the left or right third seeks; a single tap shows the overlay over a video, and is
		// play / pause on a cover
		if (now - tt.last < 300 && side !== 'c' && side === tt.side) { clearTimeout(tt.single); tt.last = now; seek(side); return; }
		tt.last = now; tt.side = side;
		clearTimeout(tt.single);
		// a run of seeks keeps going without waiting
		if (rip && now - rip.at < 700 && side === rip.side) { seek(side); return; }
		tt.single = setTimeout(() => { if (live) { if (ov && !view().paused) setOv(false); else showOv(); } else toggle(); }, 260);
	};
	const move = e => { if (live && e.pointerType === 'mouse') showOv(); };
	const shown = live && (ov || v.paused || !!menu);
	const ccOn = SUBS.chosen >= 0, hasSubs = SUBS.list.length > 0;
	// a small picture (a phone, the page's player): the menu floats over the page beside it, in a root of its own
	// (out of the picture's clipping); fullscreen keeps it in the picture, the only thing showing then
	const floatMenu = live && !!menu && !full && !!box.current && box.current.clientHeight < 420;
	const host = useRef(null);
	useEffect(() => {
		if (floatMenu) {
			if (!host.current) { host.current = document.createElement('div'); document.body.appendChild(host.current); }
			render(html`<${VidMenu} page=${menu} setPage=${setMenu} fs=${fs} box=${box} float=${true} />`, host.current);
		} else if (host.current) { render(null, host.current); host.current.remove(); host.current = null; }
	});
	useEffect(() => () => { if (host.current) { render(null, host.current); host.current.remove(); host.current = null; } }, []);
	const cover = v.kind === 'video' ? v.path : v.cover;
	return html`<div class=${'media ' + (live ? 'is-video' : v.kind === 'video' ? 'is-poster' : 'is-cover') + (full ? ' full' : '') + (shown ? ' ov' : '')} ref=${box}
		onPointerUp=${up} onPointerMove=${move} onMouseLeave=${() => { if (!view().paused && !t.current.menu) setOv(false); }} role="group" aria-label=${v.title}>
		${live ? html`<div class="mvideo" ref=${slot}></div>` : html`<div class=${'mart ' + trackClass(v.path)} key=${v.path}><${Art} src=${coverUrl(cover)} seed=${v.path} kind=${v.kind} k=${v.kind === 'video' ? 6 : 10} /></div>`}
		${!live && v.kind === 'video' && !v.finished && html`<div class="poster-tag"><${Icon} n="card" k=${2} />Playing on your handheld</div>`}
		${!live && v.kind === 'video' && v.paused && !v.loading && html`<div class="mpaused" aria-hidden="true"><${Icon} n="play" k=${5} /></div>`}
		${live && html`<div class="mov" aria-hidden=${shown ? undefined : 'true'}>
			${full && html`<div class="mov-top">
				${immersive && !fs ? html`<button class="ib" aria-label="Close player" onClick=${onCollapse}><${Icon} n="down" k=${2} /></button>` : html`<span style="width:12px"></span>`}
				<div class="grow"><div class="mov-t ell">${v.title}</div>${v.artist && html`<div class="mov-a ell">${v.artist}</div>`}</div>
				</div>`}
			<div class="mov-mid">
				${full && html`<button class="mov-s" aria-label="Previous" onClick=${ctl.prev} disabled=${off}><${Icon} n="prev" k=${3} /></button>`}
				<button class="mov-play" aria-label=${v.paused ? 'Play' : 'Pause'} onClick=${toggle} disabled=${off}><${Icon} n=${v.paused ? 'play' : 'pause'} k=${4} /></button>
				${full && html`<button class="mov-s" aria-label="Next" onClick=${ctl.next} disabled=${off}><${Icon} n="next" k=${3} /></button>`}</div>
			<div class="mov-bot">
				${full && html`<div class="mov-seek"><${Seek} view=${v} onSeek=${ctl.seek} disabled=${off || v.finished} /><${Times} v=${v} /></div>`}
				<div class="mov-r">
					${hasSubs && html`<button class=${'ib mov-cc' + (ccOn ? ' on' : '')} aria-label="Subtitles" title=${'Subtitles: ' + (subChip(v) || 'Off') + ' (C)'} aria-pressed=${ccOn} onClick=${() => { toggleCaptions(); showOv(); }}><${Icon} n="cc" k=${2} /></button>`}
					<button class=${'ib mov-gear' + (menu ? ' on' : '')} aria-label="Settings" title="Settings" aria-haspopup="menu" aria-expanded=${!!menu} onClick=${() => setMenu(menu ? null : 'main')}><${Icon} n="gear" k=${2} /></button>
					<button class="ib mov-fs" aria-label=${fs ? 'Leave fullscreen' : 'Fullscreen'} onClick=${() => toggleFullscreen(box.current)}><${Icon} n=${fs ? 'fullscreen-exit' : 'fullscreen'} k=${2} /></button></div></div></div>`}
			${live && menu && !floatMenu && html`<${VidMenu} page=${menu} setPage=${setMenu} fs=${fs} box=${box} />`}
		${pulse && html`<div class="mpulse" key=${pulse.id} aria-hidden="true"><${Icon} n=${pulse.k} k=${4} /></div>`}
		${rip && html`<div class=${'mrip ' + rip.side} key=${rip.id} aria-hidden="true"><span>${rip.side === 'l' ? '−' + 5 * rip.n + ' s' : '+' + 10 * rip.n + ' s'}</span></div>`}
		${v.here && B.blocked && html`<${TapToPlay} />`}
	</div>`;
}
/* ---- the gear's menu on the video: Audio track, Subtitles, Speed, each a page of its own. It lives in the
   media box, so it shows in fullscreen too. Arrows move, Enter picks, Left or Esc go back, Esc on the first page closes */
// "Japanese · AAC Stereo · Commentary": the title first ("Commentary"), then "Japanese · AAC Stereo"
const trackName = l => { const p = (l || '').split(' · '); return p.length > 2 ? [p.slice(2).join(' · '), p[0] + ' · ' + p[1]] : [p[0], p[1] || '']; };
const speedName = r => r === 1 ? 'Normal' : r + '×';
// (its rows: components of their own, so a redraw keeps the focus on them)
function VmBack({ title, setPage }) {
	return html`<button class="vm-item vm-back" onClick=${() => setPage('main')}><${Icon} n="left" k=${2} /><span class="grow">${title}</span></button>`;
}
function VmRow({ on, label, sub, dim, onClick }) {
	return html`<button class=${'vm-item' + (dim ? ' dim' : '')} role="menuitemradio" aria-checked=${!!on} aria-disabled=${dim ? 'true' : undefined} onClick=${onClick}>
		<span class="vm-tick">${on && html`<${Icon} n="check" k=${2} />`}</span><span class="grow"><span class="vm-l">${label}</span>${sub && html`<span class="vm-s">${sub}</span>`}</span></button>`;
}
function VmNav({ k, label, val, show, setPage }) {
	if (show === false) return null;
	return html`<button class="vm-item vm-nav" role="menuitem" aria-haspopup="menu" onClick=${() => setPage(k)}>
		<span class="grow">${label}</span><span class="vm-v ell">${val}</span><${Icon} n="right" k=${2} /></button>`;
}
function VidMenu({ page, setPage, fs, box, float }) {
	useStore();
	const ref = useRef(), [pos, setPos] = useState('');
	// floating: beside the gear, on the side with more room
	useLayoutEffect(() => {
		if (!float) return;
		const f = () => {
			const g = box.current && (box.current.querySelector('.mov-gear') || box.current); if (!g) return;
			const r = g.getBoundingClientRect(), above = r.top - 12, below = innerHeight - r.bottom - 12;
			const right = 'right:' + Math.max(8, innerWidth - r.right) + 'px;';
			setPos(right + (below >= above ? `top:${r.bottom + 4}px;max-height:${below}px` : `bottom:${innerHeight - r.top + 4}px;max-height:${above}px`));
		};
		f(); addEventListener('resize', f); return () => removeEventListener('resize', f);
	}, [float]);
	const m = B.meta && B.meta.media, audio = m && m.audio || [];
	const native = nativeAudio(), playing = playingAudio(), def = directAudio(m), switching = B.switching && B.switching.a;
	const why = i => native || i === def ? '' : streamWhy(m, i);
	const subs = SUBS.list, cur = subs.find(x => x.i === SUBS.chosen);
	const rate = el.playbackRate || 1;
	useLayoutEffect(() => { const b = ref.current && ref.current.querySelector('.vm-item:not([aria-disabled="true"])'); if (b) b.focus({ preventScroll: true }); }, [page]);
	useEffect(() => {
		// a tap anywhere else closes it (the media's own tap handler sees that and does nothing more)
		const f = e => { if (ref.current && !ref.current.contains(e.target) && !e.target.closest('.mov-gear')) setPage(null); };
		document.addEventListener('pointerdown', f, true);
		return () => document.removeEventListener('pointerdown', f, true);
	}, []);
	const key = e => {
		const items = [...ref.current.querySelectorAll('.vm-item')], i = items.indexOf(document.activeElement);
		if (e.key === 'Escape' || (e.key === 'ArrowLeft' && page !== 'main')) {
			e.preventDefault(); e.stopPropagation();
			if (page !== 'main') setPage('main'); else { setPage(null); const g = box.current && box.current.querySelector('.mov-gear'); if (g) g.focus(); }
		} else if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
			e.preventDefault(); e.stopPropagation();
			const n = items.length, down = e.key === 'ArrowDown';
			if (n) items[i < 0 ? (down ? 0 : n - 1) : (i + (down ? 1 : -1) + n) % n].focus();
		} else if (e.key === 'ArrowRight' || e.key === 'ArrowLeft' || e.key === ' ') e.stopPropagation();
	};
	let body;
	if (page === 'audio') {
		const pick = (a, i) => {
			if (why(i)) { toast(trackName(a.label)[0] + ': ' + why(i), { label: 'Play on handheld', fn: () => handoffToHandheld({ a: a.i }) }, 8000); return; }
			chooseAudio(i); setPage(null);
		};
		body = html`<${VmBack} setPage=${setPage} title="Audio track" />${audio.map((a, i) => { const [l, c] = trackName(a.label); return html`<${VmRow} on=${switching != null ? i === switching : i === playing} label=${l}
			sub=${why(i) || (switching === i ? 'Switching…' : c)} dim=${!!why(i)} onClick=${() => pick(a, i)} />`; })}`;
	} else if (page === 'subs') {
		const pick = i => { lsSet('bpSubOff', i < 0 ? '1' : '0'); chooseSub(i); setPage(null); };
		body = html`<${VmBack} setPage=${setPage} title="Subtitles" /><${VmRow} on=${SUBS.chosen < 0} label="Off" onClick=${() => pick(-1)} />
			${subs.map(x => { const [l] = trackName(x.label), pic = x.kind === 'image';
				const sub = pic ? 'Shows on the handheld only' : x.i === SUBS.chosen && SUBS.progress != null ? 'Preparing… ' + Math.round(SUBS.progress * 100) + '%'
					: SUBS.failed[x.i] || [trackName(x.label)[1], x.external ? 'beside the video' : ''].filter(Boolean).join(' · ');
				return html`<${VmRow} on=${x.i === SUBS.chosen} label=${l} sub=${sub} dim=${pic} onClick=${() => pic ? toast(l + ': shows on the handheld only (picture subtitles)', { label: 'Watch on handheld', fn: () => handoffToHandheld({ s: x.i }) }, 8000) : pick(x.i)} />`; })}
			<button class="vm-item vm-more" role="menuitem" onClick=${() => { setPage(null); if (fs) toggleFullscreen(box.current); showTracks('sub'); }}><span class="vm-tick"></span><span class="grow">Size and position…</span></button>`;
	} else if (page === 'speed') {
		body = html`<${VmBack} setPage=${setPage} title="Speed" />${SPEEDS.map(r => html`<${VmRow} on=${Math.abs(rate - r) < .01} label=${speedName(r)} onClick=${() => { setSpeed(r); setPage(null); }} />`)}`;
	} else {
		const aCur = audio[switching != null ? switching : playing];
		body = html`<${VmNav} setPage=${setPage} k="audio" label="Audio track" val=${aCur ? trackName(aCur.label)[0] : '—'} show=${audio.length > 1} />
			<${VmNav} setPage=${setPage} k="subs" label="Subtitles" val=${cur ? trackName(cur.label)[0] : subs.length ? 'Off' : 'None'} show=${subs.length > 0} />
			<${VmNav} setPage=${setPage} k="speed" label="Speed" val=${speedName(Math.round(rate * 100) / 100)} />`;
	}
	return html`<div class=${'vmenu box surface' + (float ? ' float' : '')} style=${pos} role="menu" aria-label="Settings" ref=${ref} onKeyDown=${key} onPointerUp=${e => e.stopPropagation()}>${body}</div>`;
}

// a soft glow behind the player, tinted by the cover (blurred), or by the generated cover's colour
function Glow({ v }) {
	const src = v.kind === 'audio' ? coverUrl(v.cover) : '';
	const [ok, setOk] = useState(false);
	useEffect(() => setOk(false), [src]);
	if (v.kind === 'video') return null;
	const [a] = palOf(v.path);
	return html`<div class="glow" aria-hidden="true">${src && html`<img src=${src} alt="" style=${ok ? '' : 'display:none'} onLoad=${() => setOk(true)} />`}${!ok && html`<i style=${`background:radial-gradient(closest-side, ${a}, transparent)`}></i>`}</div>`;
}

// the player's ⋯: what to do with what plays, then the app's own
function npMore(v, anchor) {
	openSheet(v.title, v.kind === 'video' ? 'film' : 'note', [
		likable(v) && { label: 'Add to playlist…', icon: 'pl-add', fn: () => addToPlaylist([v.path], v.title) },
		v.path && { label: 'Show in Files', icon: 'folder', fn: () => showInFiles(v.path) },
		'-',
		!touchOnly && { label: 'Keyboard shortcuts', icon: 'info', fn: () => set({ help: true }) }, { label: 'Sign out', icon: 'power', fn: signOut }],
		{ anchor, seed: v.path, kind: v.kind, cover: coverUrl(v.kind === 'video' ? v.path : v.cover), sub: v.artist || targetLabel(store.target) });
}
// the title, what it's from, the subtitles chip; acts: the heart and ⋯ beside the title (the phone)
function NpMeta({ v, acts }) {
	const vid = v.kind === 'video', chip = vid ? subChip(v) : null;
	const src = v.source && v.source.kind && v.source.kind !== 'none' ? v.source : null;
	return html`<div class="np-meta">
		<div class="np-tl"><div class=${'grow ' + trackClass(v.path)} key=${v.path}><div class="np-title clamp2">${v.title}</div>
			${(v.artist || v.album) && html`<div class="np-artist ell">${[v.artist, v.album].filter(Boolean).join(' · ')}</div>`}</div>
			${acts && likable(v) && html`<${LikeBtn} v=${v} cls="np-ib" />`}
			${acts && html`<button class="np-ib" aria-label="More" aria-haspopup="dialog" onClick=${e => npMore(v, e.currentTarget)}><${MoreDots} /></button>`}</div>
		${(src || chip != null || v.chapters.length > 0) && html`<div class="np-lines">
			${(src || chip != null) && html`<div class="np-source">${src && html`<${Icon} n=${src.kind === 'playlist' ? 'playlist' : 'folder'} k=${2} /><span class="ell">From <b>${src.title}</b>${src.count ? ' · ' + (src.index + 1) + ' / ' + src.count : ''}</span>`}
				${chip != null && html`<button class=${'chip' + (chip === 'Off' ? ' ghost' : '')} aria-label=${'Subtitles: ' + chip} title=${'Subtitles: ' + chip} onClick=${() => showTracks('sub')}><${Icon} n="cc" k=${1} /><span class="ell" style="max-width:120px">${chip}</span></button>`}</div>`}
			${v.chapters.length > 0 && html`<button class="np-chap" onClick=${() => showTab('chapters')}><${Icon} n="chapters" k=${2} /><span class="ell">Chapter ${Math.max(1, v.chapter + 1)} of ${v.chapters.length}${v.chapter >= 0 ? ' · ' + chapterTitle(v.chapters[v.chapter], v.chapter) : ''}</span></button>`}</div>`}
	</div>`;
}
export function LoopIcon({ loop, k }) {
	return html`<span class="loopic"><${Icon} n="loop" k=${k} />${loop === 'one' && html`<b>1</b>`}</span>`;
}
export const loopName = l => 'Loop: ' + (l === 'one' ? 'One' : l === 'all' ? 'All' : 'Off');
// (a finished list: Play is the list again, Previous its last one again; Next has nothing to act on. Shuffle and Loop
// still set their settings: with Shuffle on, Play starts at a random row)
function Transport({ v, off }) {
	const fin = off || !!v.finished;
	return html`<div class="tp">
		<button class=${'tp-s' + (v.shuffled ? ' on' : '')} aria-label="Shuffle" title=${v.shuffled ? 'Shuffle: On' : 'Shuffle: Off'} aria-pressed=${!!v.shuffled} onClick=${ctl.shuffle} disabled=${off}><${Icon} n="shuffle" k=${2} /></button>
		<button class="tp-m" aria-label=${v.finished ? 'Play the last one again' : 'Previous'} onClick=${ctl.prev} disabled=${off}><${Icon} n="prev" k=${3} /></button>
		<button class="box yellow tp-play" aria-label=${v.finished ? 'Play the list again' : v.paused ? 'Play' : 'Pause'} onClick=${ctl.toggle} disabled=${off}><${Icon} n=${v.paused ? 'play' : 'pause'} k=${4} /></button>
		<button class="tp-m" aria-label="Next" onClick=${ctl.next} disabled=${fin}><${Icon} n="next" k=${3} /></button>
		<button class=${'tp-s' + (v.loop !== 'no' ? ' on' : '')} aria-label=${loopName(v.loop)} title=${loopName(v.loop)} aria-pressed=${v.loop !== 'no'} onClick=${ctl.loop} disabled=${off}><${LoopIcon} loop=${v.loop} k=${2} /></button></div>`;
}
// the volume as a speaker button that opens a small slider
function VolButton() {
	const [open, setOpen] = useState(false), ref = useRef();
	useEffect(() => {
		if (!open) return;
		const f = e => { if (ref.current && !ref.current.contains(e.target)) setOpen(false); };
		const k = e => { if (e.key === 'Escape') { e.stopPropagation(); setOpen(false); } };
		document.addEventListener('pointerdown', f, true); document.addEventListener('keydown', k, true);
		return () => { document.removeEventListener('pointerdown', f, true); document.removeEventListener('keydown', k, true); };
	}, [open]);
	return html`<div class="volwrap" ref=${ref}><button class=${'np-ib' + (open ? ' on' : '')} aria-label="Volume" aria-expanded=${open} onClick=${() => setOpen(!open)}><${Icon} n="speaker" k=${2} /></button>
		${open && html`<div class="volpop box surface" role="group" aria-label="Volume"><${VolumeRow} lab=${true} /></div>`}</div>`;
}

function Strips({ v, other, sn }) {
	// (the list that ended: Play plays it again, Up next any row of it; Clear puts it away)
	const src = v.finished && v.source && v.source.title;
	return html`${v.finished && html`<div class="strip box surface"><span>${src ? html`The end of <b>${src}</b>` : 'The end of the list'}</span>
			<button class="btn sm" onClick=${clearFinished}>Clear</button></div>`}
		${v.here && v.parked != null && html`<div class="strip box surface"><span>Stopped when the connection to your handheld dropped.</span><button class="btn sm primary" onClick=${playParked}><${Icon} n="play" k=${2} />Play here from ${fmtTime(v.parked)}</button></div>`}
		${other && !NO_HERE && html`<div class="strip box surface"><span>Your handheld is playing <b>${titleFor(sn.path, sn.title)}</b></span><button class="btn sm primary" onClick=${continueHere}>Continue here</button></div>`}`;
}

export function NowPlaying() {
	const s = useStore(), v = view(), sn = s.snap;
	const wide = useMedia(Q_WIDE), desk = useMedia(Q_DESK);
	useCanPlayHere(s.target === 'handheld' && sn ? sn.path : null);
	const off = s.reconnecting && !v.here;
	// the handheld plays something this page doesn't have: offer it here
	const other = s.target === 'browser' && sn && sn.path && sn.kind !== 'photo' && (!B.meta || (sn.path !== B.meta.p && !sn.paused));
	const top = html`<div class="np-top"><span class="kicker">Now playing</span><${DevSwitch} /><span style="width:44px"></span></div>`;
	if (!v.has) return html`<div class="np idle">
		<div class="np-idlehead"><span class="kicker">Now playing</span><${AppMenu} /></div>
		<div style="display:flex;justify-content:center"><${DevSwitch} /></div>
		<${Strips} v=${v} other=${other} sn=${sn} />
		<${Empty} icon="note" title="Nothing playing" sub="Pick something in Files or Playlists." button="Open Files" onClick=${() => go(filesHash(store.lastDir || ''))} />
		${!wide && html`<div class="tp" style="justify-content:center;gap:24px" aria-label="Play mode for the next list">
			<button class=${'tp-s' + (v.shuffled ? ' on' : '')} aria-label="Shuffle" title=${v.shuffled ? 'Shuffle: On' : 'Shuffle: Off'} aria-pressed=${!!v.shuffled} onClick=${ctl.shuffle} disabled=${off}><${Icon} n="shuffle" k=${2} /></button>
			<button class=${'tp-s' + (v.loop !== 'no' ? ' on' : '')} aria-label=${loopName(v.loop)} title=${loopName(v.loop)} aria-pressed=${v.loop !== 'no'} onClick=${ctl.loop} disabled=${off}><${LoopIcon} loop=${v.loop} k=${2} /></button></div>`}</div>`;
	const tabs = npTabs(v), tab = tabs.some(x => x[0] === s.npTab) ? s.npTab : 'queue';
	const vid = v.kind === 'video';
	const panel = html`<${PanelTabs} tabs=${tabs} tab=${tab} onTab=${k => set({ npTab: k })} />
		<div class="tabbody" id="np-tabbody" role="tabpanel" aria-labelledby=${'pt-' + tab}><${TabBody} v=${v} tab=${tab} /></div>`;
	if (desk) return html`<div class=${'np desk' + (vid ? ' vid' : '')}>
		<div class="np-stagecol"><${Glow} v=${v} />${top}
			<${Strips} v=${v} other=${other} sn=${sn} />
			<section class="np-stage" aria-label="Now playing"><div class="stage-in"><${Media} v=${v} off=${off} /><${NpMeta} v=${v} /></div></section></div>
		<aside class="np-panel" aria-label="Queue and more">${panel}</aside></div>`;
	if (wide) return html`<div class=${'np wide' + (vid ? ' vid' : '')}><${Glow} v=${v} />${top}
		<${Strips} v=${v} other=${other} sn=${sn} />
		<div class="np-hero"><${Media} v=${v} off=${off} />
			<div class="np-side"><${NpMeta} v=${v} /><div class="np-seek"><${Seek} view=${v} onSeek=${ctl.seek} disabled=${off || v.finished} /><${Times} v=${v} skip=${true} off=${off || v.finished} /></div><${Transport} v=${v} off=${off} /></div></div>
		<section class="np-card box surface" aria-label="Queue and more">${panel}</section></div>`;
	return html`<${FullPlayer} v=${v} off=${off} other=${other} sn=${sn} tabs=${tabs} tab=${tab} />`;
}

// the phone's player: it fills the screen; the panel is a sheet that rises from the bottom
function FullPlayer({ v, off, other, sn, tabs, tab }) {
	const s = store, root = useRef();
	const short = useMedia('(orientation: landscape) and (max-height: 520px)');
	const immersive = short && v.here && v.kind === 'video';
	const [leaving, setLeaving] = useState(false);
	const entering = useMemo(() => Date.now() - (s.npAnim || 0) < 600 && store.vtKind !== 'player-open', []);
	useEffect(() => {
		if (store.backNav || !tapped()) return;	// (not tapped: Safari would skip the entry under it; the chevron replaces instead)
		// opened straight at #/playing: Files goes under it, so Back folds the player instead of leaving the app
		const under = filesHash(store.lastDir || '');
		replaceHist(null, under); pushHist(null, '#/playing');
		set({ backHash: under, backNav: true });
	}, []);
	const collapse = () => {
		if (leaving) return;
		const sheet = history.state && history.state.sheet ? 1 : 0;
		if (s.sheetOpen) set({ sheetOpen: false });
		// back when what's behind is ours; else the page under it takes the player's place (no load: it keeps playing)
		const leave = () => { if (store.backNav && canGoBack(1 + sheet)) history.go(-(1 + sheet)); else replaceHash(store.backHash || filesHash(store.lastDir || '')); };
		if (reduceMotion() || (document.startViewTransition && !videoHere())) { leave(); return; }	// (a View Transition morphs it back)
		setLeaving(true); setTimeout(leave, 260);
	};
	// a swipe down on the header or the media folds the player away; it follows the finger
	const sw = useRef(null);
	const down = e => {
		if (e.pointerType === 'mouse' || e.target.closest('.seek,.sheet2,.sh-scrim,.np-ctl,input,select,.volpop,.vmenu') || immersive || s.sheetOpen) return;
		sw.current = { y: e.clientY, x: e.clientX, t: Date.now(), dy: 0, on: false, box: e.target.closest('.media') };
	};
	const move = e => {
		const w = sw.current; if (!w) return;
		const dy = e.clientY - w.y, dx = e.clientX - w.x;
		if (!w.on && dy > 12 && dy > Math.abs(dx) * 1.5) { w.on = true; root.current.classList.add('dragging'); if (w.box) w.box.dataset.swiped = '1'; }
		if (w.on) { w.dy = Math.max(0, dy); root.current.style.transform = `translateY(${w.dy}px)`; }
	};
	const up = () => {
		const w = sw.current; sw.current = null; if (!w || !w.on) return;
		root.current.classList.remove('dragging');
		setTimeout(() => { if (w.box) delete w.box.dataset.swiped; }, 0);
		const fast = w.dy / Math.max(1, Date.now() - w.t) > .5;
		if (w.dy > 140 || (fast && w.dy > 40)) { root.current.style.transform = ''; collapse(); }
		else root.current.style.transform = '';
	};
	return html`<div class=${'fp' + (entering ? ' entering' : '') + (leaving ? ' leaving' : '') + (short ? ' short' : '') + (immersive ? ' immersive' : '') + (v.kind === 'video' ? ' vid' : '')} ref=${root}
		onPointerDown=${down} onPointerMove=${move} onPointerUp=${up} onPointerCancel=${up}>
		<${Glow} v=${v} />
		<header class="fp-head">
			<button class="ib" aria-label="Close player" onClick=${collapse}><${Icon} n="down" k=${3} /></button>
			<div class="grow fp-mid"><${DevSwitch} small=${true} /></div>
			<${VolButton} /></header>
		<${Strips} v=${v} other=${other} sn=${sn} />
		<section class="fp-body" aria-label="Now playing">
			<div class="fp-media"><${Media} v=${v} off=${off} immersive=${immersive} onCollapse=${collapse} /></div>
			<div class="np-ctl">
				<${NpMeta} v=${v} acts=${true} />
				<div class="np-seek"><${Seek} view=${v} onSeek=${ctl.seek} disabled=${off || v.finished} /><${Times} v=${v} skip=${true} off=${off || v.finished} /></div>
				<${Transport} v=${v} off=${off} />
				<div class="np-tabs">${threeKeys(v, tabs).map(([k, l]) => html`<button class="btn" aria-haspopup="dialog" onClick=${() => showTab(k)}><${Icon} n=${NP_GLYPH[k]} k=${2} />${l}</button>`)}</div></div></section>
		<${Sheet} v=${v} tabs=${tabs} tab=${tab} />
	</div>`;
}

// the panel as a sheet: a key or a tab opens it; a drag down, the scrim, the grip, Esc or Back closes it
function Sheet({ v, tabs, tab }) {
	const s = store, open = !!s.sheetOpen, ref = useRef(), d = useRef(null);
	const down = e => { if (e.button > 0 || !open) return; d.current = { y: e.clientY, t: Date.now(), on: false, cur: 0 }; };
	const move = e => {
		const g = d.current; if (!g) return;
		const dy = e.clientY - g.y;
		if (!g.on && dy > 8) { g.on = true; ref.current.classList.add('dragging'); try { ref.current.setPointerCapture(e.pointerId); } catch (x) {} }
		if (g.on) { g.cur = Math.max(0, dy); ref.current.style.transform = `translateY(${g.cur}px)`; e.preventDefault(); }
	};
	const up = e => {
		const g = d.current; d.current = null; if (!g || !g.on) return;
		ref.current.classList.remove('dragging'); ref.current.style.transform = '';
		sheetDragged = Date.now();
		const vel = (e.clientY - g.y) / Math.max(1, Date.now() - g.t);
		if (vel > .4 || g.cur > ref.current.offsetHeight * .3) closePanel();
	};
	const onTab = k => { if (Date.now() - sheetDragged < 250) return; openPanel({ npTab: k }); };
	return html`${open && html`<div class="sh-scrim" onClick=${closePanel}></div>`}
		<section class=${'sheet2 box surface' + (open ? ' open' : '')} ref=${ref} aria-label="Queue and more" role="dialog" aria-modal=${open ? 'true' : undefined} inert=${open ? undefined : true}>
			<div class="sh-peek" onPointerDown=${down} onPointerMove=${move} onPointerUp=${up} onPointerCancel=${up}>
				<button class="sh-grip" aria-label="Close the panel" onClick=${() => { if (Date.now() - sheetDragged > 250) closePanel(); }}><i></i></button>
				<${PanelTabs} tabs=${tabs} tab=${tab} onTab=${onTab} /></div>
			<div class="tabbody" id="np-tabbody" role="tabpanel" aria-labelledby=${'pt-' + tab}><${TabBody} v=${v} tab=${tab} /></div>
		</section>`;
}
let sheetDragged = 0;

/* ---- what the handheld reads from the file ---- */
const FORMATS = { matroska: 'Matroska (MKV)', 'matroska,webm': 'Matroska (MKV)', webm: 'WebM', mov: 'MP4', 'mov,mp4,m4a,3gp,3g2,mj2': 'MP4', mp3: 'MP3', flac: 'FLAC', ogg: 'Ogg', wav: 'WAV', avi: 'AVI', asf: 'ASF (WMA / WMV)', aac: 'AAC', ape: 'APE', mpeg: 'MPEG', mpegts: 'MPEG-TS' };
const CODECS = { h264: 'H.264', hevc: 'H.265 (HEVC)', vp8: 'VP8', vp9: 'VP9', av1: 'AV1', mpeg4: 'MPEG-4', mpeg2video: 'MPEG-2', aac: 'AAC', mp3: 'MP3', mp3float: 'MP3', flac: 'FLAC', opus: 'Opus', vorbis: 'Vorbis', ac3: 'Dolby Digital (AC-3)', eac3: 'Dolby Digital Plus', dts: 'DTS', truehd: 'Dolby TrueHD', alac: 'ALAC', wmav2: 'WMA', ape: 'APE' };
const codecName = c => !c ? '' : CODECS[c] || (/^pcm_/.test(c) ? 'PCM' : c.toUpperCase());
const formatName = (c, p) => FORMATS[c] || (c ? c.split(',')[0].toUpperCase() : (p.split('.').pop() || '').toUpperCase());
const chName = n => n === 1 ? 'Mono' : n === 2 ? 'Stereo' : n === 6 ? '5.1' : n === 8 ? '7.1' : n ? n + ' channels' : '';
// what /api/media says (tags, format, codecs; more as the server sends it), with the size and date from the folder's list
function DetailsTab({ v }) {
	const p = v.path, [m, setM] = useState(null), [row, setRow] = useState(null);
	useEffect(() => {
		let dead = false;
		const known = v.here && B.meta && B.meta.p === p && B.meta.media;
		setM(known || null); setRow(null);
		if (!known) get('/api/media', { p }, { ms: 8000, quiet: true }).then(x => { if (!dead) setM(x || {}); }).catch(() => { if (!dead) setM({}); });
		get('/api/ls', { p: dirName(p) }, { quiet: true }).then(r => { if (!dead) setRow((r.items || []).find(i => i.name === baseName(p)) || {}); }).catch(() => { if (!dead) setRow({}); });
		return () => { dead = true; };
	}, [p, store.epoch]);
	if (!m) return html`<div aria-hidden="true">${[1, 2, 3, 4].map(() => html`<div class="skel" style="height:40px"></div>`)}</div>`;
	const vid = v.kind === 'video', segs = dirName(p).split('/').filter(Boolean);
	const join = (...a) => a.filter(Boolean).join(' · ');
	// "3/12" as the tag has it: "3 of 12"
	const ofN = x => x ? String(x).replace(/^\s*0*(\d+)\s*\/\s*0*(\d+)\s*$/, '$1 of $2') : '';
	const kbps = b => b ? Math.round(b / 1000).toLocaleString() + ' kbps' : '';
	const res = m.width && m.height ? m.width + ' × ' + m.height : '';
	const rows = [
		['Title', v.title], ['Artist', m.artist || v.artist], ['Album artist', m.albumArtist !== (m.artist || v.artist) && m.albumArtist], ['Album', m.album || v.album],
		['Composer', m.composer], ['Year', m.year], ['Genre', m.genre], ['Track', ofN(m.track)], ['Disc', ofN(m.disc)],
		['Length', (m.duration || v.duration) ? fmtTime(m.duration || v.duration) : ''],
		['Format', formatName(m.container, p)],
		vid && ['Video', join(codecName(m.vcodec), res, m.fps ? +(+m.fps).toFixed(2) + ' fps' : '', kbps(m.videoBitrate))],
		['Audio', join(codecName(m.acodec), m.sampleRate ? m.sampleRate / 1000 + ' kHz' : '', m.channelLayout ? (m.channelLayout === 'stereo' || m.channelLayout === 'mono' ? m.channelLayout[0].toUpperCase() + m.channelLayout.slice(1) : m.channelLayout) : chName(m.channels), kbps(m.audioBitrate))],
		['Bitrate', kbps(m.bitrate)],
		(vid || (m.audio || []).length > 1) && ['Audio tracks', m.audio ? '' + m.audio.length : ''],
		vid && ['Subtitles', m.subs ? (m.subs.length ? '' + m.subs.length : 'None') : ''],
		vid && v.here && B.meta && B.meta.p === p && ['Playing here', B.remux ? 'Direct Stream (the video and audio track ' + (B.remux.a + 1) + ' copied, not converted)' : 'Direct Play (the file as it is)'],
		v.chapters.length > 0 && ['Chapters', '' + v.chapters.length],
		['Size', row && row.size ? fmtSize(row.size) : m.size ? fmtSize(m.size) : ''],
		['Modified', row && row.mtime ? fmtDate(row.mtime) : ''],
	].filter(r => r && r[1]);
	const onCard = /^\d+\//.test(p);	// (a network file isn't in the page's tree)
	return html`<section class="box surface card" aria-label="Details"><dl class="details">${rows.map(([k, val]) => html`<dt>${k}</dt><dd>${val}</dd>`)}</dl></section>
		<button class="box surface det-path" onClick=${() => go(filesHash(dirName(p)))} aria-label=${'Location: ' + segs.join(' › ') + '. Show in Files'}><${Icon} n="folder" k=${2} />
			<span class="grow">${segs.map((sg, i) => html`${i > 0 && html`<span class="sep">›</span>`}<span>${i === 0 ? rootName(sg) : sg}</span>`)}<span class="sep">›</span><b>${baseName(p)}</b></span><span class="chip">Show in Files</span></button>
		<div class="det-acts">${onCard && html`<button class="btn sm" onClick=${() => downloadPaths([p])}><${Icon} n="download" k=${2} />Download</button>`}
			${likable(v) && html`<button class="btn sm" onClick=${e => addToPlaylist([p], v.title, e.currentTarget)}><${Icon} n="pl-add" k=${2} />Add to playlist</button>`}</div>`;
}

// lab: says whose volume (the speaker button's pop-up; the Sound tab's card has it in its head)
function VolumeRow({ lab }) {
	const s = store, here = s.target === 'browser', sn = s.snap;
	const [val, hold] = useHeld(here ? B.vol : sn ? sn.volume : 0);
	const send = useMemo(() => throttled(v => post('/api/volume', { v }).catch(() => {}), 120), []);
	const change = e => { const v = +e.target.value; hold(v); if (here) setBrowserVol(v); else send(v); };
	if (here && isIOS) return html`<div class="vol-note">Use your phone's volume buttons</div>`;
	// the server sends -1 when the handheld has no volume control to read (a desktop test run)
	if (!here && sn && sn.volume < 0) return html`<div class="vol-note">The handheld's volume can't be changed from here.</div>`;
	return html`${lab && html`<div class="vol-lab">${here ? 'Browser volume' : 'Handheld volume'}</div>`}<div class="vol"><${Icon} n="speaker" k=${2} /><input type="range" class="filled" style=${'--v:' + val + '%'} min="0" max="100" value=${val} onInput=${change} aria-label=${here ? 'Browser volume' : 'Handheld volume'} aria-valuetext=${val + '%'} disabled=${!here && s.reconnecting} />
		<span class="pct">${val}</span></div>`;
}
function VolumeCard() {
	const here = store.target === 'browser';
	return html`<section class="box surface card" aria-label="Volume"><div class="card-head"><h2 class="grow">Volume</h2><span class="chip ghost"><${Icon} n=${here ? 'laptop' : 'card'} k=${1} />${here ? 'this browser' : 'handheld'}</span></div><${VolumeRow} /></section>`;
}

function ChaptersCard({ v }) {
	return html`<section id="chapters-card" aria-label="Chapters"><div class="list-lines">
		${v.chapters.map((c, i) => html`<button class="line ${i === v.chapter ? 'cur' : ''}" aria-current=${i === v.chapter ? 'true' : undefined} onClick=${() => ctl.chapterTo(i)}>
			<span class="n">${String(i + 1).padStart(2, '0')}</span><span class="tt ell">${chapterTitle(c, i)}</span><span class="tm">${fmtTime(c.start)}</span></button>`)}</div></section>`;
}

// the queue, scrolled so the one playing is near the top when it's out of sight
function UpNext({ v }) {
	const s = store, sn = s.snap;
	const [items, setItems] = useState(null), [idx, setIdx] = useState(0);
	const box = useRef();
	const fin = v.finished && finishedList();
	const key = fin ? 'fin|' + fin.index + '|' + fin.queue.join('|') : v.here ? B.queue.length + ':' + B.index + ':' + (B.queue[0] && B.queue[0].p) : (sn && sn.path) + '|' + s.epoch + '|' + s.plTick;
	useEffect(() => {
		if (fin) { setItems(fin.queue.map(p => ({ p, title: titleFor(p) }))); setIdx(fin.index); return; }
		if (v.here) { setItems(B.queue.map(q => ({ p: q.p, title: titleFor(q.p, q.title), duration: q.duration }))); setIdx(B.index); return; }
		let dead = false;
		get('/api/source').then(r => { if (!dead) { setItems((r.items || []).map(q => ({ p: q.p, title: titleFor(q.p, q.title), duration: q.duration }))); setIdx(r.index); } }).catch(() => { if (!dead) setItems([]); });
		return () => { dead = true; };
	}, [key]);
	// a folder's or this browser's queue has no lengths: asked for the rows that come into view
	useDurations(box, [items]);
	useLayoutEffect(() => {
		const b = box.current, c = b && b.querySelector('.cur'), sc = b && b.closest('.tabbody');
		if (!c || !sc) return;
		// only when the one playing is out of sight (opened on a long queue, or the list moved on past the view): a tap
		// on a row you can see leaves the list where it is
		const r = c.getBoundingClientRect(), v2 = sc.getBoundingClientRect();
		if (r.top >= v2.top && r.bottom <= v2.bottom) return;
		sc.scrollTop = Math.max(0, sc.scrollTop + r.top - v2.top - 8 - (c.offsetHeight || 0));
	}, [idx, items]);
	if (items === null) return html`<div class="skel" aria-hidden="true"></div>`;
	if (items.length < 2 && !fin) return html`<p class="note pad">Nothing else is queued. Play a folder or a playlist to fill it.</p>`;
	const play = async i => {
		if (!userTap()) return;
		// (a row this browser can't play, with the browser chosen: asked first; yes plays the list from it on the handheld)
		if (store.target === 'browser' && (v.here || fin)) {
			const where = await whereToPlay(items[i].p);
			if (!where) return;
			if (where === 'handheld') {
				const f = fin && finishedList();
				return playListOnHandheld(f ? f.queue : B.queue.map(q => q.p), i, f ? finSource(f) : B.source);
			}
		}
		// (a finished list: from that row, where picks go)
		if (fin) return playFinished(i);
		// (with Shuffle on, what follows the row is random again: the session's new order from it)
		if (v.here) return browserPlay(B.queue, i, B.source, 0);
		const src = sn.source;
		try { if (src && src.kind === 'playlist' && src.id) await post('/api/play', { playlist: src.id, index: i }); else await post('/api/play', { p: items[i].p }); } catch (e) { toast(errMsg(e)); }
	};
	const src = v.source && v.source.title, pad = n => String(n).padStart(2, '0');
	return html`<section aria-label="Queue"><div class="band queue-band"><span class="kicker ell" style="max-width:70%">${src ? 'From ' + src : 'Up next'}</span><span class="rule"></span><span class="num">${pad(idx + 1)} / ${pad(items.length)}</span></div>
		<div class="upnext list-lines" ref=${box}>
		${items.map((it, i) => { const d = it.duration > 0 ? it.duration : durationOf(it.p); return html`<button class="line ${i === idx ? 'cur' : ''} ${i < idx ? 'past' : ''}" data-dur=${!d && durable(it.p) ? it.p : undefined} onClick=${() => play(i)} aria-current=${i === idx ? 'true' : undefined}>
			<span class="tile sm"><${Art} src=${kindOfName(it.p) === 'audio' ? coverUrl(it.p) : ''} seed=${it.p} kind=${kindOfName(it.p)} k=${1} /></span>
			<span class="tt"><span class="ell" style="display:block">${it.title}</span>${i === idx && html`<span class="m">${fin ? html`<span class="chip tiny">Finished</span>` : html`<span class="chip y tiny"><${Icon} n="play" k=${1} />Now</span>`}</span>`}</span>
			${d > 0 && html`<span class="tm">${fmtTime(d)}</span>`}</button>`; })}</div></section>`;
}

/* ---- audio and subtitles ---- */
const SUB_COLORS = { white: '#FFFFFF', yellow: '#FFE45C', cyan: '#6EE7F2', green: '#8EF08A', pink: '#FF9BD2', grey: '#C8C8C8', black: '#000000', 'dark-grey': '#404040', 'dark-blue': '#10204A' };
const SUB_FONTS = { inter: ['Inter', 400], 'inter-bold': ['Inter', 700], pixelify: ['"Pixelify Sans"', 400], 'fusion-pixel': ['"Pixelify Sans"', 400] };	// (Fusion Pixel is 7 MB, for CJK: the preview's Latin looks the same in Pixelify)
const SUB_SIZES = { small: 36, medium: 46, large: 56, 'extra-large': 68 };
const SUB_BORDERS = { none: 0, thin: 1.5, normal: 3, thick: 5 };

function TracksCard({ v }) {
	const s = useStore(), tab = s.tracksTab || 'audio';
	const here = v.here;
	if (!here && !v.tracks) return null;	// a player too old to say
	const tabs = [['audio', 'Audio'], ['sub', 'Subtitles']];
	return html`<section class="box surface card tracks" id="tracks-card" tabindex="-1" aria-label="Audio and subtitles">
		${here && html`<div class="card-head"><span class="grow"></span><span class="chip ghost"><${Icon} n="laptop" k=${1} />this browser</span></div>`}
		<div class="seg tabs2" role="tablist">${tabs.map(([k, l]) => html`<button role="tab" id=${'tt-' + k} aria-controls="tracks-panel" aria-selected=${tab === k} onClick=${() => set({ tracksTab: k })}>${l}</button>`)}</div>
		<div id="tracks-panel" role="tabpanel" aria-labelledby=${'tt-' + tab}>
		${here ? (tab === 'audio' ? html`<${BrowserAudio} />` : html`<${BrowserSubs} />`) : (tab === 'audio' ? html`<${HandheldAudio} t=${v.tracks} />` : html`<${HandheldSubs} t=${v.tracks} />`)}
		</div></section>`;
}
// the choice made here shows at once; the handheld's own word comes with the next state
function useChosen(serverId) {
	const [held, setHeld] = useState(null), t = useRef(0);
	useEffect(() => { setHeld(null); }, [serverId]);
	const hold = id => { setHeld(id); clearTimeout(t.current); t.current = setTimeout(() => setHeld(null), 4000); };
	return [held == null ? serverId : held, hold];
}
function TrackRow({ on, label, sub, onClick, disabled, tag }) {
	return html`<button class="radio track" role="radio" aria-checked=${!!on} disabled=${disabled} onClick=${onClick}><${Icon} n=${on ? 'radio-on' : 'radio'} k=${2} />
		<span class="grow"><span class="lab">${label}</span>${sub && html`<span class="sm">${sub}</span>`}</span>${tag && html`<span class="chip">${tag}</span>`}</button>`;
}
// the handheld's track (its list as the state has it, by mpv's id; sub 0 off) as the Session's pick: the handheld
// plays it, and so does this browser if it takes over
function sendTrack(type, list, id) {
	const k = list.findIndex(x => x.id === id), x = list[k];
	return intent(type, k < 0 ? { off: 1, of: list.length } : { ord: k, of: list.length, lang: x.lang || '', label: x.label, external: x.external ? 1 : 0 });
}
function HandheldAudio({ t }) {
	const list = t.audio || [];
	const [cur, hold] = useChosen(t.aid);
	if (!list.length) return html`<p class="note pad">This video has no sound.</p>`;
	if (list.length === 1) return html`<div class="radios"><${TrackRow} on=${true} label=${list[0].label} /></div><p class="note pad">Only one audio track</p>`;
	const pick = id => { if (id === cur) return; hold(id); sendTrack('SetAudio', list, id).catch(e => { hold(null); errToast(e); }); };
	return html`<div class="radios" role="radiogroup" aria-label="Audio track">${list.map(a => html`<${TrackRow} on=${a.id === cur} label=${a.label} onClick=${() => pick(a.id)} />`)}</div>`;
}
function HandheldSubs({ t }) {
	const list = t.sub || [];
	const [cur, hold] = useChosen(t.sid || 0);
	const [styleOpen, setStyleOpen] = useState(false);
	const pick = id => { if (id === cur) return; hold(id); sendTrack('SetSub', list, id).catch(e => { hold(null); errToast(e); }); };
	return html`${list.length ? html`<div class="radios" role="radiogroup" aria-label="Subtitles">
			<${TrackRow} on=${!cur} label="Off" onClick=${() => pick(0)} />
			${list.map(x => html`<${TrackRow} on=${x.id === cur} label=${x.label} sub=${x.forced ? 'Forced' : x.external ? 'From a file beside the video' : ''} onClick=${() => pick(x.id)} />`)}</div>`
		: html`<p class="note pad">No subtitles in this file</p>`}
		<button class="line more-row" aria-expanded=${styleOpen} onClick=${() => setStyleOpen(!styleOpen)}><${Icon} n="pencil" k=${2} /><span class="grow">Subtitle style</span><${Icon} n=${styleOpen ? 'up' : 'down'} k=${2} /></button>
		${styleOpen && html`<${SubStyle} />`}`;
}
function SubStyle() {
	const [st, setSt] = useState(null), [err, setErr] = useState('');
	useEffect(() => { get('/api/substyle').then(setSt).catch(e => setErr(e.status === 404 ? 'Change it on the handheld: START › Settings › Subtitles.' : errMsg(e))); }, [store.epoch]);
	if (err) return html`<p class="note pad">${err}</p>`;
	if (!st) return html`<div class="skel" style="height:120px" aria-hidden="true"></div>`;
	const val = k => { const x = (st.settings || []).find(y => y.key === k); return x && x.value; };
	const change = (k, value) => {
		setSt({ ...st, settings: st.settings.map(x => x.key === k ? { ...x, value } : x) });
		post('/api/substyle', { key: k, value }).then(r => r && r.settings && setSt(r)).catch(errToast);
	};
	const f = SUB_FONTS[val('font')] || SUB_FONTS.inter, size = (SUB_SIZES[val('size')] || 46) * .42, bw = (SUB_BORDERS[val('border')] || 0) * .42;
	const prev = `font-family:${f[0]};font-weight:${f[1]};font-size:${size.toFixed(1)}px;color:${SUB_COLORS[val('color')] || '#fff'};-webkit-text-stroke:${(bw * 2).toFixed(2)}px ${SUB_COLORS[val('border-color')] || '#000'}`;
	return html`<div class="substyle">
		${st.styled && html`<div class="note warn">This video's subtitles are styled (ASS): they keep their own look.</div>`}
		<div class="sub-preview" aria-hidden="true"><span style=${prev}>Subtitles look like this</span></div>
		${(st.settings || []).map(x => html`<div class="ss-row"><div class="ss-lab" id=${'ss-' + x.key}>${x.label}</div>
			${x.key === 'font' || x.key === 'encoding' ? html`<select class="sel" aria-labelledby=${'ss-' + x.key} value=${x.value} onChange=${e => change(x.key, e.target.value)}>${x.choices.map(c => html`<option value=${c.id}>${c.label}</option>`)}</select>`
			: /color/.test(x.key) ? html`<div class="swatches" role="radiogroup" aria-labelledby=${'ss-' + x.key}>${x.choices.map(c => html`<button class="sw" role="radio" aria-checked=${c.id === x.value} onClick=${() => change(x.key, c.id)}><i style="background:${SUB_COLORS[c.id] || '#888'}"></i><span>${c.label}</span></button>`)}</div>`
			: html`<div class="chips" role="radiogroup" aria-labelledby=${'ss-' + x.key}>${x.choices.map(c => html`<button class="chipbtn" role="radio" aria-checked=${c.id === x.value} onClick=${() => change(x.key, c.id)}>${c.label}</button>`)}</div>`}</div>`)}
		<p class="note">How plain subtitles (SRT, VTT…) look. Styled ones (ASS) keep theirs. Text encoding is how a subtitle file’s letters are read: if letters like š, č or ж come out wrong, pick the file’s.</p></div>`;
}
function BrowserAudio() {
	useStore();
	const m = B.meta && B.meta.media, list = m && m.audio || [];
	const native = nativeAudio(), playing = playingAudio(), def = directAudio(m);
	const [want, setWant] = useState(-1);
	if (!list.length) return html`<p class="note pad">${m ? 'This video has no sound.' : 'Loading…'}</p>`;
	if (list.length === 1) return html`<div class="radios"><${TrackRow} on=${true} label=${list[0].label} tag="Playing here" /></div><p class="note pad">Only one audio track</p>`;
	// Safari switches in the file itself; elsewhere the default track is the file as it is (Direct Play) and the others
	// come as a Direct Stream; one the browser can't play is the handheld's
	const why = i => native || i === def ? '' : streamWhy(m, i);
	const switching = B.switching && B.switching.a;
	const pick = i => { if (why(i)) { setWant(want === i ? -1 : i); return; } setWant(-1); chooseAudio(i); };
	const subOf = (a, i) => why(i) ? why(i) : switching === i ? 'Switching…' : native ? '' : i === def ? 'Direct Play' : 'Direct Stream';
	const blocked = want >= 0 && why(want);
	return html`<div class="radios" role="radiogroup" aria-label="Audio track">${list.map((a, i) => html`<${TrackRow} on=${want >= 0 ? i === want : switching != null ? i === switching : i === playing}
			label=${a.label} sub=${subOf(a, i)} tag=${i === playing && switching == null ? 'Playing here' : ''} onClick=${() => pick(i)} />`)}</div>
		${blocked && html`<div class="track-note"><p class="note">${list[want].label}: ${why(want)}.</p>
			<button class="btn sm primary" onClick=${() => handoffToHandheld({ a: list[want].i })}><${Icon} n="card" k=${2} />Play this one on the handheld</button></div>`}
		${!native && B.remux && html`<p class="note pad">Direct Stream: the handheld sends the video and this audio track as they are, without converting them. Seeking takes a moment.</p>`}`;
}
function BrowserSubs() {
	useStore();
	const list = SUBS.list, cur = SUBS.chosen;
	const text = list.filter(x => x.kind !== 'image'), pics = list.filter(x => x.kind === 'image');
	const pick = i => { lsSet('bpSubOff', i < 0 ? '1' : '0'); chooseSub(i); };
	const failed = Object.values(SUBS.failed)[0];
	return html`${list.length ? html`<div class="radios" role="radiogroup" aria-label="Subtitles">
			<${TrackRow} on=${cur < 0} label="Off" onClick=${() => pick(-1)} />
			${text.map(x => html`<${TrackRow} on=${x.i === cur} label=${x.label} sub=${x.i === cur && SUBS.progress != null ? 'Preparing subtitles… ' + Math.round(SUBS.progress * 100) + '%'
				: [x.styled ? (SUBS.noAss[x.i] ? 'Styled (ASS), shown plain here' : 'Styled (ASS)') : '', x.external ? 'From a file beside the video' : ''].filter(Boolean).join(' · ')} onClick=${() => pick(x.i)} />`)}
			${pics.map(x => html`<${TrackRow} on=${false} disabled=${true} label=${x.label} sub="Shows on the handheld only (picture subtitles)" />`)}</div>`
		: html`<p class="note pad">${B.meta && B.meta.media ? 'No subtitles in this file' : 'Loading…'}</p>`}
		${failed && html`<p class="note pad err" role="alert">${failed}</p>`}
		${pics.length > 0 && html`<div class="track-note"><button class="btn sm" onClick=${() => handoffToHandheld()}><${Icon} n="card" k=${2} />Watch on handheld</button></div>`}
		<div class="substyle here">
			<div class="ss-row"><div class="ss-lab" id="cue-size">Size</div><div class="chips" role="radiogroup" aria-labelledby="cue-size">${CUE_SIZES.map(([k, l]) => html`<button class="chipbtn" role="radio" aria-checked=${cueStyle.size === k} onClick=${() => setCueStyle('size', k)}>${l}</button>`)}</div></div>
			<div class="ss-row"><div class="ss-lab" id="cue-pos">Position</div><div class="chips" role="radiogroup" aria-labelledby="cue-pos">${[['bottom', 'Bottom'], ['raised', 'Raised']].map(([k, l]) => html`<button class="chipbtn" role="radio" aria-checked=${cueStyle.pos === k} onClick=${() => setCueStyle('pos', k)}>${l}</button>`)}</div></div>
			<p class="note">${SUBS.ass ? 'These subtitles are styled (ASS): they keep their own look. Size and position apply to plain ones (SRT, VTT…).' : 'How subtitles look in this browser, on this device.'}</p></div>`;
}

/* ---- sound: equalizer and effects ---- */
const BANDS = [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000];
const bandLabel = f => f >= 1000 ? f / 1000 + 'k' : '' + f;
const FX = [['none', 'None'], ['room', 'Room'], ['hall', 'Concert hall'], ['church', 'Church'], ['cave', 'Cave'], ['wide', 'Wide stereo'], ['headphones', 'Headphones']];
const dbText = g => (g > 0 ? '+' : g < 0 ? '−' : '') + Math.abs(g).toFixed(1);
// a song in this browser: the equalizer and effects are the handheld's
function SoundHere() {
	return html`<section class="box surface card sound-here" aria-label="Sound"><span class="note">Equalizer and effects play on the handheld</span><button class="btn sm" onClick=${() => setTarget('handheld')}><${Icon} n="card" k=${2} />Switch to handheld</button></section>`;
}
// one band: a slot with a cream knob, yellow from the 0 dB line; drag, arrows (Page keys ±3 dB), a double click or 0 resets
function EqBand({ f, g, onSet }) {
	const ref = useRef(), drag = useRef(false);
	const at = e => { const r = ref.current.querySelector('.slot').getBoundingClientRect(); const fr = 1 - (e.clientY - r.top) / r.height; return Math.round((Math.max(0, Math.min(1, fr)) * 24 - 12) * 2) / 2; };
	const down = e => { if (e.button > 0) return; drag.current = true; try { ref.current.setPointerCapture(e.pointerId); } catch (x) {} onSet(at(e)); };
	const move = e => { if (drag.current) onSet(at(e)); };
	const up = () => { drag.current = false; };
	const key = e => {
		const d = { ArrowUp: .5, ArrowRight: .5, ArrowDown: -.5, ArrowLeft: -.5, PageUp: 3, PageDown: -3 }[e.key];
		if (d != null) { e.preventDefault(); e.stopPropagation(); onSet(g + d); }
		else if (e.key === 'Home' || e.key === 'End' || e.key === '0') { e.preventDefault(); onSet(e.key === 'Home' ? 12 : e.key === 'End' ? -12 : 0); }
	};
	const top = 50 - g / 12 * 50;
	return html`<div class="eqb"><span class="val">${g ? dbText(g).replace('.0', '') : ''}</span>
		<div class="slotwrap" ref=${ref} role="slider" tabindex="0" aria-label=${bandLabel(f) + ' Hz'} aria-orientation="vertical" aria-valuemin="-12" aria-valuemax="12" aria-valuenow=${g} aria-valuetext=${dbText(g) + ' dB'}
			onPointerDown=${down} onPointerMove=${move} onPointerUp=${up} onPointerCancel=${up} onKeyDown=${key} onDblClick=${() => onSet(0)}>
			<div class="slot"><div class="lvl" style=${`top:${Math.min(top, 50)}%;bottom:${100 - Math.max(top, 50)}%`}></div><div class="knob" style=${`top:${top}%`}></div></div></div>
		<small>${bandLabel(f)}</small></div>`;
}
function SoundCard({ v }) {
	const sn = store.snap, eq = sn && sn.eq || { on: false, preset: 'Flat', gains: Array(10).fill(0) };
	const [gains, holdGains] = useHeld(eq.gains);
	const sendGains = useMemo(() => throttled(g => post('/api/eq', { gains: g.join(',') }).catch(e => errToast(e)), 120), []);
	const setGain = (i, val) => { const g = gains.slice(); g[i] = Math.max(-12, Math.min(12, val)); if (g[i] === gains[i]) return; holdGains(g); sendGains(g); };
	const vid = v.kind === 'video';
	const presets = store.eq || [{ name: 'Flat' }];
	const cd = eq.preset === '', fx = sn && sn.fx || 'none';
	return html`<section class="box surface card sound" aria-label="Equalizer" aria-disabled=${vid ? 'true' : undefined}>
		<div class="eqhead"><h2>Equalizer</h2>
			<button class="btn sm ghost" disabled=${vid} onClick=${() => { holdGains(Array(10).fill(0)); post('/api/eq', { preset: 'Flat' }).catch(() => {}); }}>Reset</button>
			<button class=${'ib sm' + (eq.on ? ' on' : '')} role="switch" aria-checked=${!!eq.on} aria-label="Equalizer on" disabled=${vid} onClick=${() => post('/api/eq', { on: eq.on ? 0 : 1 }).catch(e => errToast(e))}><${Icon} n=${eq.on ? 'box-on' : 'box'} k=${2} /></button></div>
		${vid && html`<p class="note" style="margin-bottom:var(--s3)">Sound changes music only.</p>`}
		<div class=${vid ? 'greyed' : ''} inert=${vid ? true : undefined}>
			<div class="chips" role="radiogroup" aria-label="Preset">${cd && html`<button class="chipbtn" role="radio" aria-checked="true">Custom</button>`}${presets.map(p => html`<button class="chipbtn" role="radio" aria-checked=${eq.preset === p.name} onClick=${() => post('/api/eq', { preset: p.name }).catch(x => errToast(x))}>${p.name}</button>`)}</div>
			<div class=${'eq' + (eq.on ? '' : ' greyed')} role="group" aria-label="Bands">${BANDS.map((f, i) => html`<${EqBand} f=${f} g=${gains[i]} onSet=${val => setGain(i, val)} />`)}</div></div></section>
	<section class="box surface card" aria-label="Effect"><div class="card-head"><h2>Effect</h2></div>
		<div class=${vid ? 'greyed' : ''} inert=${vid ? true : undefined}><div class="chips" role="radiogroup" aria-label="Effect">${FX.map(([k, l]) => html`<button class="chipbtn" role="radio" aria-checked=${fx === k} onClick=${() => post('/api/fx', { key: k }).catch(e => errToast(e))}>${l}</button>`)}</div></div></section>`;
}

