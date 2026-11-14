import { html, useState, useEffect, useRef } from '../vendor/htm-preact-standalone.module.js';
import { view } from './browser-player.js';
import { chapterTitle, fmtSize, fmtTime } from './util.js';
import { Icon } from './icons.js';
import { LIKED_ID } from './plshared.js';
import { Q_WIDE, phoneNow, store } from './store.js';
import { openModal } from './toasts.js';
import { signOut } from './session.js';

export function useTick(ms, on) {
	const [, n] = useState(0);
	useEffect(() => {
		if (on === false) return;
		let t = 0;
		const run = () => { clearInterval(t); t = document.hidden ? 0 : setInterval(() => n(x => x + 1), ms); if (!document.hidden) n(x => x + 1); };
		run(); document.addEventListener('visibilitychange', run);
		return () => { clearInterval(t); document.removeEventListener('visibilitychange', run); };
	}, [ms, on]);
}
export function useMedia(q) {
	const [m, setM] = useState(matchMedia(q).matches);
	useEffect(() => { const mq = matchMedia(q), f = () => setM(mq.matches); mq.addEventListener('change', f); return () => mq.removeEventListener('change', f); }, [q]);
	return m;
}
// the local value while the user drags, the server's otherwise
export function useHeld(serverVal) {
	const [held, setHeld] = useState(null), timer = useRef(0);
	const hold = v => { setHeld(v); clearTimeout(timer.current); timer.current = setTimeout(() => setHeld(null), 900); };
	return [held == null ? serverVal : held, hold];
}

// pos: a menu or popover placed by its anchor (no scrim then); raw: the children are the whole inside
export function Modal({ title, sub, label, onClose, actions, children, cls, pos, raw }) {
	const ref = useRef();
	useEffect(() => {
		const prev = document.activeElement;
		const q = x => ref.current.querySelector(x);
		const f = q('[data-focus]') || q('input') || q('.sheet-item:not(:disabled)') || q('.btn.primary') || q('button');
		if (f) f.focus();
		return () => prev && prev.focus && prev.focus();
	}, []);
	const key = e => {
		if (e.key === 'Escape') { e.stopPropagation(); onClose(); }
		if (e.key === 'Tab') {
			const f = [...ref.current.querySelectorAll('button:not(:disabled),input,select,[tabindex="0"]')]; if (!f.length) return;
			const first = f[0], last = f[f.length - 1];
			if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); } else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
		}
	};
	return html`<div class=${'scrim' + (pos ? ' clear' : '')} onMouseDown=${e => { if (e.target === e.currentTarget) onClose(); }} onKeyDown=${key}>
		<div class="modal ${cls || ''}" style=${pos} role="dialog" aria-modal="true" aria-label=${label || title} ref=${ref}>
			${title && html`<h2>${title}</h2>`}${sub && html`<div class="sub">${sub}</div>`}
			${children && (raw ? children : html`<div class="body">${children}</div>`)}
			${actions && html`<div class="acts">${actions}</div>`}
		</div></div>`;
}

export function Seek({ view: v, onSeek, disabled }) {
	useTick(500);
	const ref = useRef(), [drag, setDrag] = useState(null), [hover, setHover] = useState(null);
	const dur = v.duration || 0, pos = drag != null ? drag : Math.min(v.pos(), dur || v.pos());
	const frac = dur ? Math.max(0, Math.min(1, pos / dur)) : 0;
	const at = e => { const r = ref.current.getBoundingClientRect(); return Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)) * dur; };
	const down = e => { if (disabled || !dur) return; ref.current.setPointerCapture(e.pointerId); setDrag(at(e)); };
	// a mouse over the bar: the time under it (and its chapter) in a tip above; touch shows it while dragging
	const move = e => { if (drag != null) setDrag(at(e)); if (e.pointerType === 'mouse' && dur && !disabled) setHover(at(e)); };
	const tipT = drag != null ? drag : hover;
	let tip = null;
	if (tipT != null && dur) {
		const ch = v.chapters || [];
		let k = -1;
		for (let i = 0; i < ch.length; i++) if (ch[i].start <= tipT + 0.05) k = i;
		const name = ch.length > 1 && k >= 0 ? chapterTitle(ch[k], k) : '';
		tip = html`<div class="seek-tip" style=${'left:' + (tipT / dur * 100) + '%;transform:translateX(-' + (tipT / dur * 100) + '%)'} aria-hidden="true">${name && html`<span class="seek-tip-ch">${name}</span>`}<span class="seek-tip-t">${fmtTime(tipT)}</span></div>`;
	}
	const up = e => { if (drag != null) { const t = at(e); setDrag(null); onSeek(t); } };
	const key = e => {
		const d = { ArrowLeft: -5, ArrowRight: 10, ArrowDown: -5, ArrowUp: 10 }[e.key];
		if (d != null) { e.preventDefault(); e.stopPropagation(); onSeek(Math.max(0, Math.min(dur, pos + d))); }
	};
	return html`<div class="seek" ref=${ref} role="slider" tabindex=${disabled ? -1 : 0} aria-label="Seek" aria-valuemin="0" aria-valuemax=${Math.floor(dur)} aria-valuenow=${Math.floor(pos)}
		aria-valuetext=${fmtTime(pos) + ' of ' + fmtTime(dur)} aria-disabled=${disabled ? 'true' : undefined}
		onPointerDown=${down} onPointerMove=${move} onPointerUp=${up} onPointerCancel=${() => setDrag(null)} onPointerLeave=${() => setHover(null)} onKeyDown=${key}>
		${tip}
		<div class="track"><div class="fill" style="width:${frac * 100}%"></div>
			${dur > 0 && (v.chapters || []).slice(1).map(c => html`<i class=${'tick' + (c.start <= pos ? ' past' : '')} style="left:${c.start / dur * 100}%"></i>`)}
			${dur > 0 && html`<div class="thumb" style="left:${frac * 100}%"></div>`}</div></div>`;
}

/* covers: the file's own art when it has one, over a generated one (a gradient keyed on the path, a pixel motif) that
   shows while it loads and stays when there's none. No artwork of ours: the handheld's files only */
const PAL = [['#E8823A', '#6B1E4A'], ['#3AA7A0', '#18234F'], ['#F2C14E', '#B0413E'], ['#6C5CE7', '#120F2E'], ['#2E86AB', '#0B1E2D'], ['#D96C9A', '#2B193D'], ['#7BBF6A', '#1C3A2E'], ['#F0645A', '#2A0E14']];
const MOTIFS = ['note', 'sun', 'moon', 'eq', 'vis-waves', 'vis-bars', 'vis-spectrum', 'note'];
function hashOf(s) { let h = 5381; s = s || ''; for (let i = 0; i < s.length; i++) h = (h * 33 + s.charCodeAt(i)) | 0; return h >>> 0; }
export const palOf = s => PAL[hashOf(s) % PAL.length];
// (a book, a PDF, a comic: its own glyph, as in Files)
const DOC_MOTIF = { epub: 'epub', pdf: 'pdf', comic: 'comic', text: 'page' };
function GenCover({ seed, kind, k, r }) {
	const h = hashOf(seed), [a, b] = PAL[h % PAL.length];
	const n = kind === 'video' ? 'film' : kind === 'photo' ? 'picture' : DOC_MOTIF[kind] || MOTIFS[(h >>> 5) % MOTIFS.length];
	return html`<span class="gc" ref=${r} style=${`background:linear-gradient(135deg,${a},${b})`}><${Icon} n=${n} k=${k || 2} /></span>`;
}
// a cover asked for only once its tile comes near the screen (a long list asks for the rows you scroll to). Not the
// browser's loading="lazy": in a scrolling part of the page (the shell's lists) Safari and Chrome could leave those
// images unrequested until a reload. One observer for every tile
const nearFns = new WeakMap();
let nearObs = null;
function observeNear(el, fn) {
	if (typeof IntersectionObserver === 'undefined') { fn(); return () => {}; }
	if (!nearObs) nearObs = new IntersectionObserver(es => { for (const e of es) if (e.isIntersecting) { const f = nearFns.get(e.target); if (f) { nearFns.delete(e.target); nearObs.unobserve(e.target); f(); } } }, { rootMargin: '300px 0px' });
	nearFns.set(el, fn); nearObs.observe(el);
	return () => { nearFns.delete(el); nearObs.unobserve(el); };
}
export const coverUrl = p => p && !p.startsWith('bpnet://') ? '/api/cover?p=' + encodeURIComponent(p) : '';	// (a network file's art: the handheld's only)
export function Art({ src, seed, kind, k }) {
	const [st, setSt] = useState(''), [near, setNear] = useState(false), gc = useRef(null);
	useEffect(() => setSt(''), [src]);
	useEffect(() => { if (!src || near || !gc.current) return; return observeNear(gc.current, () => setNear(true)); }, [src, near]);
	return html`<${GenCover} seed=${seed} kind=${kind} k=${k} r=${gc} />${src && near && st !== 'bad' && html`<img class=${'cvimg' + (st === 'ok' ? ' on' : '')} src=${src} alt="" decoding="async" onLoad=${() => setSt('ok')} onError=${() => setSt('bad')} />`}`;
}
// a playlist's cover: Liked's heart on dark gold; else a 2×2 mosaic
export function PlArt({ p, k }) {
	if (p.builtin || p.id === LIKED_ID) return html`<span class="liked-art gc"><${Icon} n="heart" k=${k || 2} /></span>`;
	return html`<span class="mosaic gc">${[0, 1, 2, 3].map(i => html`<span><${GenCover} seed=${p.id + '/' + i} kind="audio" k=${k ? Math.max(1, k - 1) : 1} /></span>`)}</span>`;
}

export function Banner({ title, sub, children, cls }) {
	return html`<div class="banner box surface ${cls || ''}" role="status"><div class="grow"><div class="bt">${title}</div>${sub && html`<div class="bs">${sub}</div>`}</div>${children}</div>`;
}
export function Empty({ icon, title, sub, button, onClick }) {
	return html`<div class="empty"><${Icon} n=${icon} k=${6} /><h2>${title}</h2><p>${sub}</p>${button && html`<button class="btn primary" onClick=${onClick}>${button}</button>`}</div>`;
}
export const Skeleton = () => html`<div aria-hidden="true">${[1, 2, 3, 4, 5, 6].map(() => html`<div class="skel"></div>`)}</div>`;
export function useLater(loading, ms) { const [s, setS] = useState(false); useEffect(() => { if (!loading) { setS(false); return; } const t = setTimeout(() => setS(true), ms); return () => clearTimeout(t); }, [loading]); return s; }

export function MoreDots() { return html`<span class="dots" aria-hidden="true"><i></i><i></i><i></i></span>`; }
export function Storage({ free, total }) {
	const cls = free < 200e6 ? 'crit' : free < 1e9 ? 'low' : '';
	return html`<span class=${'storage ' + cls}>${total > 0 && html`<span class="meter" aria-hidden="true"><i style=${'width:' + Math.max(0, Math.min(100, (1 - free / total) * 100)) + '%'}></i></span>`}<span class="num">${cls ? 'Low on space · ' : ''}${fmtSize(free)} free</span></span>`;
}

// where a menu goes on a computer: beside what opened it (an element, or a point), below or above it
function menuPos(a, w) {
	if (!a || phoneNow()) return null;
	w = w || 280;
	const r = a.getBoundingClientRect ? a.getBoundingClientRect() : { left: a.x, right: a.x, top: a.y, bottom: a.y };
	const left = Math.max(8, Math.min(innerWidth - w - 8, r.right - w));
	// (the side with more room; a long menu scrolls inside)
	const above = r.top - 12, below = innerHeight - r.bottom - 12;
	return above > below ? `left:${left}px;bottom:${innerHeight - r.top + 4}px;max-height:${Math.min(520, above)}px` : `left:${left}px;top:${r.bottom + 4}px;max-height:${Math.min(520, below)}px`;
}
// an action sheet (a phone) or a menu beside its row (a computer): items [{label, icon, sub, danger, disabled, fn}], '-'
// between groups. opts: {anchor, sub, seed, kind, cover} (the header's tile: a cover when seed is given)
export function openSheet(title, glyph, items, opts) {
	opts = opts || {};
	const pos = menuPos(opts.anchor);
	const list = items.filter(Boolean).filter((it, i, a) => it !== '-' || (i > 0 && i < a.length - 1 && a[i - 1] !== '-'));
	const close = openModal(close => html`<${Modal} cls=${'sheet' + (pos ? ' menu-pop' : '')} pos=${pos} label=${title} raw=${true} onClose=${close}>
		<div class="sheet-head"><span class=${'tile xs' + (glyph === 'folder' ? ' folder' : '')}>${opts.seed != null ? html`<${Art} src=${opts.cover} seed=${opts.seed} kind=${opts.kind} k=${1} />` : html`<${Icon} n=${glyph || 'note'} k=${2} />`}</span>
			<div class="grow"><div class="t clamp2">${title}</div>${opts.sub && html`<div class="s">${opts.sub}</div>`}</div></div>
		<div class="sheet-items">${list.map(it => it === '-' ? html`<div class="sheet-sep" role="separator"></div>` : html`<button class="sheet-item ${it.danger ? 'danger' : ''}" disabled=${it.disabled} onClick=${() => { close(); it.fn && it.fn(); }}>
			${it.icon ? html`<${Icon} n=${it.icon} k=${2} cls=${it.y ? 'y' : ''} />` : html`<span style="width:22px"></span>`}<span class="grow">${it.label}${it.sub && html`<span class="sm">${it.sub}</span>`}</span>${it.on && html`<${Icon} n="check" k=${2} cls="on" label="Chosen" />`}</button>`)}</div></${Modal}>`);
	return close;
}
// the phone's menu for the app itself (the sidebar's card on wider screens): the handheld, Sign out
function appMenu() {
	const sn = store.snap, r = store.roots && store.roots[0];
	const bits = [store.reconnecting ? 'Reconnecting…' : 'Connected', sn && sn.battery >= 0 ? 'battery ' + sn.battery + '%' : '', r && r.free != null ? fmtSize(r.free) + ' free' : ''].filter(Boolean).join(' · ');
	openSheet('Your handheld', 'card', [{ label: 'Sign out', icon: 'power', fn: signOut }], { sub: bits });
}
// incognito mode's mark while the handheld is in it (switched on the handheld only, in its Quick settings)
export function IncognitoMark() {
	if (!(store.snap && store.snap.incognito)) return null;
	const t = 'Incognito mode is on: nothing played or read goes into the history. Switch it in the handheld\u2019s Quick settings (F).';
	return html`<span class="incog" role="img" aria-label="Incognito mode is on" title=${t}><${Icon} n="incognito" k=${2} /></span>`;
}

export function AppMenu() {
	const wide = useMedia(Q_WIDE);
	if (wide) return null;
	// (the mark and the button together at the header's end: the header spreads its children apart)
	return html`<span class="hdr-end"><${IncognitoMark} /><button class="ib" aria-label="Your handheld" aria-haspopup="dialog" onClick=${appMenu}><${Icon} n="card" k=${2} /></button></span>`;
}

/* the handheld, drawn small: two panels, the bottom one showing the QR (sign-in) or dark (the down screen) */
export function HandheldArt({ mode }) {
	let bottom = '';
	if (mode === 'qr') {
		const q = ['1111101011111', '1000101010001', '1011101110101', '1011100011101', '1000101010001', '1111101011111', '0000000100000', '1011011101101', '0110100010110', '1111101101011', '1000100110100', '1011101011011', '1111101101111'];
		bottom = '<rect x="31" y="44.5" width="25" height="25" rx="1" fill="#fff"/>' + q.map((r, y) => [...r].map((c, x) => c === '1' ? `<rect x="${33 + x * 1.6}" y="${46.5 + y * 1.6}" width="1.6" height="1.6" fill="#000"/>` : '').join('')).join('') +
			'<rect x="60" y="48" width="18" height="2" fill="#FFD23F"/><rect x="60" y="53" width="14" height="2" fill="#8F8A80"/><rect x="60" y="58" width="16" height="3" rx=".5" fill="#26242E"/>';
	}
	const dark = mode !== 'qr';
	const top = dark ? '<rect x="14" y="8" width="64" height="28" fill="#060608"/>' : '<rect x="14" y="8" width="64" height="28" fill="#000"/><rect x="36" y="14" width="20" height="2" fill="#FFD23F"/><rect x="30" y="20" width="32" height="2" fill="#E8E2D3"/><rect x="34" y="26" width="24" height="2" fill="#8F8A80"/>';
	const svg = `<svg viewBox="0 0 92 84" class="dsmini" shape-rendering="crispEdges" aria-hidden="true"><rect x="8" y="2" width="76" height="38" rx="6" fill="#26242E" stroke="#605D60" stroke-width="1.5"/>${top}<rect x="20" y="40" width="52" height="3" fill="#1B1920"/><rect x="8" y="42" width="76" height="40" rx="6" fill="#26242E" stroke="#605D60" stroke-width="1.5"/><rect x="14" y="43" width="64" height="35" fill="${dark ? '#060608' : '#000'}"/>${bottom}</svg>`;
	return html`<span style="display:block;line-height:0" dangerouslySetInnerHTML=${{ __html: svg }}></span>`;
}

