import { html, useRef, useState } from '../vendor/htm-preact-standalone.module.js';
import { NO_HERE, NO_HERE_WHY, Q_WIDE, lsGet, lsSet, store, useStore } from './store.js';
import { baseName, dirName, fmtTime, isNetPath, plural, touchOnly } from './util.js';
import { ctl, playHere, view } from './browser-player.js';
import { AppMenu, Art, Empty, MoreDots, Skeleton, coverUrl, openSheet, palOf, useLater, useMedia, useTick } from './components.js';
import { filesHash, go } from './router.js';
import { LIKED_ID, addToPlaylist, isLiked, newPlaylistDialog, setLike, showInFiles } from './plshared.js';
import { toast } from './toasts.js';
import { Icon } from './icons.js';
import { DevSwitch, useVideoSlot } from './playing.js';
import { GLYPH } from './files.js';
import { playFromPl } from './playlists.js';
import { clearRecent, loadRecent, openDoc, playFromStart, playRecent, removeRecent, useRecent } from './recent.js';

/* ================= Home: the landing page. Continue (the newest thing played or read, on the handheld or here), then
   the rest of Recent. A book, a PDF or a comic opens on the handheld only, and a network file plays there only: the
   page says so on the item rather than failing after the tap ================= */
const NET_WHY = 'Network files play on the handheld only';
const DOC_WHY = 'Books, PDFs and comics open on the handheld’s screens';
const KIND = { video: 'Video', audio: 'Song', epub: 'Book', pdf: 'PDF', comic: 'Comic', text: 'Text' };
const isDoc = it => it.kind !== 'audio' && it.kind !== 'video';
const group = it => isDoc(it) ? 'doc' : it.kind;
// left part-way: Resume / Continue, the yellow bar and time
const inProg = it => !it.gone && (isDoc(it) ? !!it.started && it.frac < 1 : !it.played && it.position > 0);
const fracOf = it => it.gone ? 0 : isDoc(it) ? (it.started ? it.frac : 0) : it.played ? 0 : it.frac;
// "4:12 / 9:56", "Played · 3:10", "Page 7 of 18", "37% read", "Not started"
const noteOf = it => isDoc(it)
	? (!it.started ? (it.pages > 0 ? plural(it.pages, 'page', 'pages') : 'Not started') : it.pages > 0 ? 'Page ' + it.page + ' of ' + it.pages : Math.round(it.frac * 100) + '% read')
	: it.played ? 'Played' + (it.duration > 0 ? ' · ' + fmtTime(it.duration) : '') : it.position > 0 ? fmtTime(it.position) + (it.duration > 0 ? ' / ' + fmtTime(it.duration) : '') : it.duration > 0 ? fmtTime(it.duration) : 'Not played';
// where it is: its folder (the connection's name for a network file; the card's at its top)
const whereOf = it => { if (it.net) return it.where; const d = dirName(it.p); return d.includes('/') ? baseName(d) : it.where; };
const likable = it => !isDoc(it) && !it.net && !it.gone;

// a tap: what the item can do. A document goes to the handheld; a network file plays there whatever is chosen
function act(it) {
	if (it.gone) return toast(it.net ? 'That file is gone from its network folder' : 'That file is gone from the card');
	if (isDoc(it)) return openDoc(it);
	if (it.net) { if (store.target === 'browser') toast(NET_WHY); return playRecent(it, 'handheld'); }
	return playHere(t => playRecent(it, t), it.p);
}
const fromStart = it => it.net ? playFromStart(it, 'handheld') : playHere(t => playFromStart(it, t), it.p);
function menu(it, anchor) {
	const doc = isDoc(it), part = inProg(it), liked = isLiked(it.p), verb = part ? 'Resume' : 'Play';
	const rm = { label: 'Remove from Recent', icon: 'minus', sub: it.gone ? 'The file is gone' : doc ? 'Its page is kept' : 'The place it was left at is kept', fn: () => removeRecent(it) };
	const items = it.gone ? [rm] : doc ? [
		{ label: 'Open on handheld', icon: 'card', sub: noteOf(it), fn: () => openDoc(it) },
		!it.net && { label: 'Show in Files', icon: 'folder', fn: () => showInFiles(it.p) }, '-', rm]
	: [
		{ label: verb + ' on handheld', icon: 'card', sub: part ? 'From ' + fmtTime(it.position) : undefined, fn: () => playRecent(it, 'handheld') },
		{ label: verb + ' in this browser', icon: 'laptop', disabled: NO_HERE || it.net, sub: it.net ? NET_WHY : NO_HERE ? NO_HERE_WHY : part ? 'From ' + fmtTime(it.position) : undefined, fn: () => playHere(() => playRecent(it, 'browser'), it.p) },
		part && { label: 'Play from the start', icon: 'prev', fn: () => fromStart(it) }, '-',
		!it.net && { label: liked ? 'Unlike' : 'Like', icon: liked ? 'heart-o' : 'heart', fn: () => setLike(it.p, !liked, 'menu') },
		!it.net && { label: 'Add to playlist…', icon: 'pl-add', fn: () => addToPlaylist([it.p], it.title) },
		!it.net && { label: 'Show in Files', icon: 'folder', fn: () => showInFiles(it.p) }, '-', rm];
	openSheet(it.title, GLYPH[it.kind] || 'note', items, { anchor, sub: KIND[it.kind] + ' · ' + noteOf(it), seed: it.p, kind: it.kind, cover: it.gone || doc ? '' : coverUrl(it.p) });
}
const onCtx = it => e => { if (!touchOnly) { e.preventDefault(); menu(it, { x: e.clientX, y: e.clientY }); } };

/* a cover cut to its kind's shape, so the kinds tell apart at a glance: a video fills the frame, a song is a square
   sleeve, a book, a PDF or a comic an upright page. frac: the yellow bar (given live for what plays) */
function Cover({ it, k, frac, children }) {
	const [a, b] = palOf(it.p), f = frac != null ? frac : fracOf(it);
	return html`<span class="hm-cv"><span class="amb" style=${`background:linear-gradient(135deg,${a},${b})`}></span>
		<span class=${'art ' + group(it)}><${Art} src=${it.gone || isDoc(it) ? '' : coverUrl(it.p)} seed=${it.p} kind=${it.kind} k=${k} /></span>${children}
		${f > 0 && html`<span class="bar" aria-hidden="true"><i style=${'width:' + (Math.min(1, f) * 100).toFixed(1) + '%'}></i></span>`}</span>`;
}
// what a row can't do, said on it
const Tags = ({ it, cur, short }) => html`${cur && html`<span class="chip y tiny"><${Icon} n="play" k=${1} />Playing</span>`}${it.gone && html`<span class="chip red tiny">Gone</span>`}
	${isDoc(it) && !it.gone && html`<span class="chip ghost tiny" title=${DOC_WHY}><${Icon} n="card" k=${1} />${short ? 'Handheld' : 'Opens on handheld'}</span>`}
	${it.net && html`<span class="chip ghost tiny" title=${NET_WHY}><${Icon} n="network" k=${1} />Network</span>`}`;
const Heart = ({ it }) => likable(it) && isLiked(it.p) ? html` <span class="likemark" role="img" aria-label="Liked"><${Icon} n="heart" k=${1} /></span>` : null;

// the video playing in this browser, live in the Now playing card while Home shows (the one <video> moves here, its
// subtitles with it, and back to the bar's tile when Home goes: playing.js's slots); a tap opens the player
function LiveVideo() {
	const slot = useVideoSlot(true, 1);	// (over the bar's tile, under the player's box)
	return html`<span class="mvideo hm-live" ref=${slot} role="button" aria-label="Open the player" onClick=${() => go('#/playing')}></span>`;
}

// Continue: the newest item, large, with the one thing to do next. What plays now is the newest item, so this card
// turns into Now playing (the small player and the bar stay the transport; this only opens the player and pauses)
function Continue({ it, v, cur }) {
	const [a, b] = palOf(it.p), doc = isDoc(it), part = inProg(it), liked = isLiked(it.p);
	const pos = cur ? v.pos() : it.position, dur = cur ? v.duration : it.duration, frac = cur ? (dur > 0 ? Math.min(1, pos / dur) : 0) : fracOf(it);
	const kick = cur ? html`<span class="kicker y hm-now"><span class=${'hm-eq' + (v.paused ? ' paused' : '')} aria-hidden="true"><i></i><i></i><i></i></span>${v.paused ? 'Paused' : 'Now playing'} · ${v.here ? 'in this browser' : 'on handheld'}</span>`
		: html`<span class="kicker y">${doc ? (part ? 'Continue reading' : 'Last opened') : part && it.kind === 'video' ? 'Continue watching' : part ? 'Continue listening' : 'Last played'}</span>`;
	const label = doc ? html`<${Icon} n="card" k=${2} />${!it.started ? 'Open on handheld' : it.pages > 0 ? 'Open at page ' + it.page : 'Open at ' + Math.round(it.frac * 100) + '%'}`
		: html`<${Icon} n="play" k=${2} />${part ? 'Resume at ' + fmtTime(it.position) : 'Play'}`;
	const where = doc ? html`<${Icon} n="card" k=${1} />${DOC_WHY}` : it.net ? html`<${Icon} n="network" k=${1} />A network file: it plays on the handheld`
		: store.target === 'browser' ? html`<${Icon} n="laptop" k=${1} />Plays in this browser` : html`<${Icon} n="card" k=${1} />Plays on the handheld`;
	return html`<section class="hm-hero box surface" aria-label=${cur ? 'Now playing' : 'Continue'} style=${`--a:${a};--b:${b}`} onContextMenu=${onCtx(it)}>
		<${Cover} it=${it} k=${6} frac=${frac}>${cur && v.here && v.kind === 'video' && html`<${LiveVideo} />`}<span class="tl"><${Tags} it=${it} /></span></${Cover}>
		<div class="txt">${kick}<h2 class="clamp2">${it.title}</h2>
			<div class="meta"><${Icon} n=${GLYPH[it.kind] || 'note'} k=${1} /><span class="ell">${KIND[it.kind]}${whereOf(it) ? ' · ' + whereOf(it) : ''}</span></div>
			${cur || part ? html`<div class="prog"><span class="y">${cur ? fmtTime(pos) + (dur > 0 ? ' / ' + fmtTime(dur) : '') : noteOf(it)}</span><span class="trk" aria-hidden="true"><i style=${'width:' + (frac * 100).toFixed(1) + '%'}></i></span>${!doc && dur > 0 && html`<span class="dim">${fmtTime(dur - pos)} left</span>`}</div>`
				: html`<div class="prog dim">${noteOf(it)}</div>`}
			<div class="acts">${cur ? html`<button class="btn primary" onClick=${() => go('#/playing')}><${Icon} n="note" k=${2} />Open player</button>
					<button class="btn start" onClick=${ctl.toggle}><${Icon} n=${v.paused ? 'play' : 'pause'} k=${2} />${v.paused ? 'Play' : 'Pause'}</button>`
				: html`<button class="btn primary" onClick=${() => act(it)}>${label}</button>
					${part && !doc && html`<button class="btn start" onClick=${() => fromStart(it)}>From the start</button>`}`}
				${likable(it) && html`<button class=${'btn icon' + (liked ? ' liked' : '')} aria-pressed=${liked} aria-label=${liked ? 'Unlike' : 'Like'} title=${liked ? 'Liked · click to unlike' : 'Like'} onClick=${() => setLike(it.p, !liked)}><${Icon} n=${liked ? 'heart' : 'heart-o'} k=${2} /></button>`}
				<button class="btn icon" aria-label=${'More about ' + it.title} aria-haspopup="dialog" onClick=${e => menu(it, e.currentTarget)}><${MoreDots} /></button></div>
			${!cur && html`<div class="plays">${where}</div>`}</div></section>`;
}
const verbOf = it => isDoc(it) ? 'Open' : inProg(it) ? 'Resume' : 'Play';
function Card({ it, cur }) {
	return html`<div class=${'hm-card box surface' + (it.gone ? ' gone' : '') + (cur ? ' cur' : '')} role="listitem">
		<button class="hit" aria-label=${verbOf(it) + ' ' + it.title} onClick=${() => cur ? go('#/playing') : act(it)} onContextMenu=${onCtx(it)}>
			<${Cover} it=${it} k=${4}>${(it.gone || cur) && html`<span class="tl">${cur ? html`<span class="chip y tiny"><${Icon} n="play" k=${1} />Playing</span>` : html`<span class="chip red tiny">Gone</span>`}</span>`}${!it.gone && html`<span class="ov" aria-hidden="true"><span><${Icon} n=${isDoc(it) ? 'card' : 'play'} k=${2} /></span></span>`}</${Cover}>
			<span class="body"><span class="ttl clamp2">${it.title}<${Heart} it=${it} /></span>
				<span class="where"><${Icon} n=${GLYPH[it.kind] || 'note'} k=${1} /><span class="ell">${whereOf(it) || KIND[it.kind]}</span></span>
				<span class="note"><span class=${inProg(it) ? 'y' : ''}>${noteOf(it)}</span>${!it.gone && html`<${Tags} it=${it} short=${true} />`}</span></span></button>
		<button class="ib more" aria-label=${'More about ' + it.title} aria-haspopup="dialog" onClick=${e => menu(it, e.currentTarget)}><${MoreDots} /></button></div>`;
}
function Row({ it, cur }) {
	return html`<div class=${'pitem nogrip hm-row' + (it.gone ? ' gone' : '') + (cur ? ' cur' : '')} role="listitem">
		<button class="main-btn" onClick=${() => cur ? go('#/playing') : act(it)} onContextMenu=${onCtx(it)}><${Cover} it=${it} k=${2} />
			<span class="grow"><div class="fn ell">${it.title}<${Heart} it=${it} /></div>
				<div class="fm"><${Tags} it=${it} cur=${cur} short=${true} /><${Icon} n=${GLYPH[it.kind] || 'note'} k=${1} /><span class="ell">${whereOf(it) || KIND[it.kind]}</span></div></span>
			<span class=${'dur' + (inProg(it) ? ' y' : '')}>${noteOf(it)}</span></button>
		<button class="ib more" aria-label=${'More about ' + it.title} aria-haspopup="dialog" onClick=${e => menu(it, e.currentTarget)}><${MoreDots} /></button></div>`;
}

// Upload: to Files, where the folder it goes in is chosen (there its Upload files button uploads into it)
const pickFiles = () => { go(filesHash(store.lastDir || '')); toast('Open the folder it goes in, then Upload files'); };
const newPlaylist = () => newPlaylistDialog(id => go('#/playlists/' + encodeURIComponent(id)));
// things to do, not a second set of tabs
function Quick() {
	const n = store.likesCount || 0;
	const q = (art, t, sub, fn, off) => html`<button class="hm-qa box surface" disabled=${off} onClick=${fn}><span class="tile">${art}</span><span class="grow"><div class="t">${t}</div><div class="s ell">${sub}</div></span><${Icon} n="right" k=${2} /></button>`;
	return html`<section class="hm-sec"><div class="band"><span class="kicker">Quick actions</span><span class="rule"></span></div><div class="hm-qgrid">
		${q(html`<span class="liked-art gc"><${Icon} n="heart" k=${2} /></span>`, 'Shuffle Liked', n ? plural(n, 'item', 'items') + ' you hearted' : 'Tap ♥ on anything playing', () => playFromPl({ id: LIKED_ID }, store.target, true), !n)}
		${q(html`<${Icon} n="up" k=${2} />`, 'Upload files', 'In Files: pick the folder, then upload', pickFiles)}
		${q(html`<${Icon} n="pl-add" k=${2} />`, 'New playlist', 'Line up songs or episodes', newPlaylist)}</div></section>`;
}
// where to begin when there is nothing to continue (nothing played yet, or the history is off)
function Starts() {
	const s = (icon, t, d, fn, extra) => html`<${fn ? 'button' : 'div'} class=${'hm-start box surface' + (fn ? '' : ' static')} onClick=${fn}><span class="tile"><${Icon} n=${icon} k=${2} /></span><span class="grow"><div class="t">${t}</div><div class="s">${d}</div>${extra}</span><//>`;
	return html`<div class="hm-sgrid">
		${s('folder', 'Browse Files', 'Everything on the handheld’s card. Tap a file and it plays.', () => go(filesHash(store.lastDir || '')))}
		${s('up', 'Upload from this device', 'Send videos, music, books and comics over Wi-Fi. On a computer, drop them anywhere.', pickFiles)}
		${s('playlist', 'Make a playlist', 'Line up songs or episodes. Liked collects everything you heart.', newPlaylist)}
		${s('network', 'Network folders', 'Play from a shared folder (SMB or FTP). They play on the handheld.', null, html`<div class="hm-path"><span class="chip ghost tiny"><${Icon} n="card" k=${1} />On the handheld</span><span class="key">Network</span><${Icon} n="right" k=${1} /><span class="key">Add connection</span></div>`)}</div>`;
}
// the picture: the handheld's two screens and this browser, on the same Wi-Fi
const Duo = () => html`<div class="hm-duo" aria-hidden="true"><div class="hh"><div class="scr"><${Icon} n="film" k=${2} /></div><div class="scr b"><i></i><i></i><i></i></div></div>
	<div class="link"><span class="dd"><i></i><i></i><i></i><i></i></span>Same Wi-Fi</div>
	<div class="br"><div class="top"><i></i><i></i><i></i></div><div class="pg"><b></b><span><i></i><i></i></span></div><div class="pg"><b class="two"></b><span><i></i><i></i></span></div></div></div>`;
function Welcome() {
	const h = (icon, t, s) => html`<div class="h"><${Icon} n=${icon} k=${2} /><div><div class="t">${t}</div><div class="s">${s}</div></div></div>`;
	const g = (icon, t, s, w) => html`<div class="hm-gh"><div class="g" style=${'--w:' + w}><${Icon} n=${icon} k=${3} /></div><div><b>${t}</b>${s}</div></div>`;
	return html`<section class="hm-welcome box surface"><div><span class="kicker y">Welcome</span><h2>Nothing played yet</h2>
			<p>Play a video or a song, on the handheld or from here. It will wait on this page with its place kept, ready to pick up on either one.</p>
			<div class="acts"><button class="btn primary" onClick=${() => go(filesHash(store.lastDir || ''))}><${Icon} n="folder" k=${2} />Browse Files</button><button class="btn" onClick=${pickFiles}><${Icon} n="up" k=${2} />Upload files</button></div></div><${Duo} /></section>
		<section class="hm-sec"><div class="band"><span class="kicker">Ways to start</span><span class="rule"></span></div><${Starts} /></section>
		<section class="hm-sec"><div class="band"><span class="kicker">How the two work together</span><span class="rule"></span></div><div class="box surface hm-how">
			${h('laptop', 'Two places to play', html`Pick <b>Handheld</b> in the player and this page is its remote. Pick <b>This browser</b> and the file plays right here.`)}
			${h('clock', 'Your place is kept', 'Stop a video on one and carry on from the same second on the other. Books keep their page.')}
			${h('book', 'Books stay on the handheld', 'Books, PDFs and comics open on its two screens, and its own Home keeps your page.')}</div></section>
		<section class="hm-sec"><div class="band"><span class="kicker">What shows up here</span><span class="rule"></span></div><div class="hm-ghs" aria-hidden="true">
			${g('film', 'A video', 'left at 4:12', '42%')}${g('vis-bars', 'An album', 'track 3', '30%')}${g('note', 'A song', 'played', '0%')}</div></section>`;
}
// the history is off (the handheld's setting; it also empties it): why Home is bare, and where the switch is
const HistoryOff = () => html`<div class="hm-off box surface" role="status"><span class="tile"><${Icon} n="clock" k=${2} /></span><div class="grow"><div class="bt">Play history is off</div>
		<div class="bs">Nothing you play or read is being kept, so there is nothing to continue from. Turn it back on from the handheld:</div>
		<div class="hm-path"><span class="chip ghost tiny"><${Icon} n="card" k=${1} />On the handheld</span><span class="key">Settings</span><${Icon} n="right" k=${1} /><span class="key">Files</span><${Icon} n="right" k=${1} /><span class="key">Keep play history</span></div></div></div>
	<section class="hm-sec"><div class="band"><span class="kicker">Everything else works</span><span class="rule"></span></div><${Starts} /></section>`;

const FILTERS = [['all', 'All'], ['video', 'Videos'], ['audio', 'Music']];
export function Home() {
	const s = useStore(), r = useRecent(), v = view(), wide = useMedia(Q_WIDE);
	const [filter, setFilter] = useState('all'), [pref, setPref] = useState(lsGet('bpHomeView') || ''), [all, setAll] = useState(false);
	const slow = useLater(!r, 300);
	const nowPath = v.has && !v.finished ? v.path : '';
	// (the time on the Now playing card)
	useTick(1000, !!nowPath);
	// the order as Home was opened: playing from it (or the next song starting) doesn't shuffle the rows under the
	// finger; their progress still updates, a removed one goes, and a newly played one comes when Home is opened again
	const order = useRef(null);
	if (r && r.items && !r.items.length) order.current = null;	// (cleared: what plays next starts it afresh)
	else if (r && r.items && !order.current) order.current = r.items.map(it => it.p);
	// (what plays only: books, PDFs and comics are read on the handheld, and its own Home lists them)
	let items = (r && r.items || []).filter(it => !s.recentHidden[it.p] && !isDoc(it));
	if (order.current) {
		const at = new Map(order.current.map((p, i) => [p, i]));
		items = items.filter(it => at.has(it.p)).sort((a, b) => at.get(a.p) - at.get(b.p));
	}
	const has = items.length > 0;
	const playOn = html`<div class="hm-playon"><span class="kicker">Play on</span><${DevSwitch} /></div>`;
	const head = html`<div class="phead hm-head"><div class="row"><h1>Home</h1>${has && wide && playOn}
		${has && html`<button class="btn icon" aria-label="More about Recent" aria-haspopup="dialog" onClick=${e => openSheet('Recent', 'clock', [{ label: 'Clear Recent…', icon: 'trash', danger: true, sub: 'On the handheld too', fn: clearRecent }], { anchor: e.currentTarget, sub: plural(items.length, 'item', 'items') + ' · kept on the handheld' })}><${MoreDots} /></button>`}<${AppMenu} /></div>
		${has && !wide && playOn}</div>`;
	if (r && r.error) return html`<div class="page hm">${head}<${Empty} icon="alert" title="Couldn't load Home" sub=${r.error} button="Try again" onClick=${loadRecent} /></div>`;
	if (!r) return html`<div class="page hm">${head}${slow ? html`<${Skeleton} />` : null}</div>`;
	if (r.on === false) return html`<div class="page hm">${head}<${HistoryOff} /></div>`;
	if (!has) return html`<div class="page hm">${head}<${Welcome} /></div>`;
	// the top card: what plays now (from wherever it was started, in Recent or not), and Recent below it whole; with
	// nothing playing, the newest still there to continue, and Recent the rest
	const nowIt = nowPath ? items.find(it => it.p === nowPath) || { p: nowPath, title: v.title, kind: v.kind === 'audio' ? 'audio' : 'video',
		where: '', net: isNetPath(nowPath), position: 0, duration: v.duration, frac: 0, played: false, gone: false } : null;
	const first = nowIt || items.find(it => !it.gone) || items[0], rest = nowIt ? items : items.filter(it => it !== first);
	const count = g => rest.filter(it => g === 'all' || group(it) === g).length;
	const list = rest.filter(it => filter === 'all' || group(it) === filter);
	const mode = pref || (wide ? 'grid' : 'list'), cap = wide ? 8 : 6, shown = all ? list : list.slice(0, cap);
	const pickView = k => { setPref(k); lsSet('bpHomeView', k); };
	return html`<div class="page hm">${head}
		<${Continue} it=${first} v=${v} cur=${!!nowIt} />
		${rest.length > 0 && html`<section class="hm-sec"><div class="band"><span class="kicker">Recent</span><span class="num dim cnt">${rest.length}</span><span class="rule"></span>
				<div class="hm-filters" role="group" aria-label="Show">${FILTERS.map(([k, l]) => html`<button class="hm-f" aria-pressed=${filter === k} disabled=${k !== 'all' && !count(k)} onClick=${() => { setFilter(k); setAll(false); }}>${l}<span class="n num">${count(k)}</span></button>`)}</div>
				<div class="seg" role="group" aria-label="View"><button aria-pressed=${mode === 'grid'} aria-label="Grid" title="Grid" onClick=${() => pickView('grid')}><${Icon} n="blocks" k=${1} /></button><button aria-pressed=${mode === 'list'} aria-label="List" title="List" onClick=${() => pickView('list')}><${Icon} n="playlist" k=${1} /></button></div></div>
			${!shown.length ? html`<p class="note pad">Nothing of that kind in Recent yet.</p>`
				: mode === 'grid' ? html`<div class="hm-grid" role="list">${shown.map(it => html`<${Card} key=${it.p} it=${it} cur=${it.p === nowPath} />`)}</div>`
				: html`<div class="box surface listbox"><div class="flist" role="list">${shown.map(it => html`<${Row} key=${it.p} it=${it} cur=${it.p === nowPath} />`)}</div></div>`}
			${list.length > cap && html`<div class="hm-more"><button class="btn" onClick=${() => setAll(!all)}>${all ? 'Show fewer' : 'Show all ' + list.length}</button></div>`}</section>`}
		<${Quick} /></div>`;
}
