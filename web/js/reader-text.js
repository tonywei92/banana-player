import { html, useState, useEffect, useLayoutEffect, useRef } from '../vendor/htm-preact-standalone.module.js';
import { get } from './api.js';
import { lsGet, lsSet } from './store.js';
import { Modal } from './components.js';
import { openModal } from './toasts.js';

/* ================= a text: an EPUB or a TXT as the handheld's own blocks (docs/redesign/web-books.md: phone-read) =================
   The handheld sends a text a part at a time (an EPUB's chapter, a TXT's next 96 KB) as the blocks its text viewer
   wraps, each with the id that is the place. A part is laid out in CSS columns, a page a screen (two columns from
   900 px of page width), or as one long scroll (Scroll: the parts follow one another as the reader nears the end);
   the place is the block at the top of the page shown. The publisher's styling is not kept, as on the handheld. */

const THEMES = [['light', 'Light'], ['sepia', 'Sepia'], ['dark', 'Dark']];
const FONTS = [['serif', 'Serif'], ['sans', 'Sans']];
const SPACINGS = [['tight', 'Tight'], ['normal', 'Normal'], ['loose', 'Loose']];
const MARGINS = [['narrow', 'Narrow'], ['normal', 'Normal'], ['wide', 'Wide']];
const MODES = [['pages', 'Pages'], ['scroll', 'Scroll']];
const SIZES = [14, 16, 18, 20, 22, 25, 28, 32];
const LINE = { tight: 1.35, normal: 1.55, loose: 1.8 };
const one = (list, v, d) => list.some(([k]) => k === v) ? v : d;
export function textSettings() {
	let s = {}; try { s = JSON.parse(lsGet('bp-read-text') || '{}'); } catch (e) {}
	return { theme: s.theme || 'light', font: s.font || 'serif', size: SIZES.includes(s.size) ? s.size : 18,
		spacing: one(SPACINGS, s.spacing, 'normal'), margins: one(MARGINS, s.margins, 'normal'), mode: one(MODES, s.mode, 'pages') };
}
const saveSettings = s => lsSet('bp-read-text', JSON.stringify(s));

function TextSheet({ close, onChange }) {
	const [s, setS] = useState(textSettings);
	const put = patch => { const n = Object.assign({}, s, patch); setS(n); saveSettings(n); onChange(); };
	const i = SIZES.indexOf(s.size);
	const seg = (list, k) => html`<div class="rd-seg" role="group">${list.map(([v, l]) => html`<button class=${'btn sm' + (s[k] === v ? ' primary' : '')} aria-pressed=${s[k] === v} onClick=${() => put({ [k]: v })}>${l}</button>`)}</div>`;
	return html`<${Modal} title="Text" onClose=${close} actions=${html`<button class="btn primary" onClick=${close}>Done</button>`}>
		<div class="rd-set">${seg(THEMES, 'theme')}
			<div class="rd-seg"><button class="btn sm" aria-label="Smaller text" disabled=${i <= 0} onClick=${() => put({ size: SIZES[i - 1] })}>A−</button><span class="rd-size">${s.size}</span><button class="btn sm" aria-label="Larger text" disabled=${i >= SIZES.length - 1} onClick=${() => put({ size: SIZES[i + 1] })}>A+</button></div>
			${seg(FONTS, 'font')}
			<div class="rd-lab">Spacing</div>${seg(SPACINGS, 'spacing')}
			<div class="rd-lab">Margins</div>${seg(MARGINS, 'margins')}
			<div class="rd-lab">Layout</div>${seg(MODES, 'mode')}
			<p class="dim sm">Kept in this browser. The handheld keeps its own text settings; only your place and bookmarks are shared.</p></div></${Modal}>`;
}
export function textSettingsSheet(onChange) { openModal(close => html`<${TextSheet} close=${close} onChange=${onChange} />`); }

function Block({ b, p }) {
	const body = b.r ? b.r.map(([st, t]) => st & 1 && st & 2 ? html`<b><i>${t}</i></b>` : st & 1 ? html`<b>${t}</b>` : st & 2 ? html`<i>${t}</i>` : t) : b.t;
	switch (b.k) {
		case 1: return html`<h1 data-id=${b.id}>${body}</h1>`;
		case 2: return html`<h2 data-id=${b.id}>${body}</h2>`;
		case 3: return html`<h3 data-id=${b.id}>${body}</h3>`;
		case 4: return html`<blockquote data-id=${b.id}>${body}</blockquote>`;
		case 5: return html`<pre data-id=${b.id}>${body}</pre>`;
		case 6: return html`<figure data-id=${b.id}><img src=${'/api/book/img?p=' + encodeURIComponent(p) + '&n=' + encodeURIComponent(b.img)} alt="" /></figure>`;
		case 7: return html`<hr data-id=${b.id} />`;
		default: return html`<p data-id=${b.id}>${body || ' '}</p>`;
	}
}

// the part holding a block id (the last that starts at or before it)
const partOf = (parts, id) => { let i = 0; while (i + 1 < parts.length && parts[i + 1][0] <= id) i++; return i; };

// the paper the text lies on: the browser's theme, font, size, spacing and margins
const paperClass = s => 'rd-paper th-' + s.theme + ' f-' + s.font + ' m-' + s.margins;
const paperStyle = s => '--rd-size:' + s.size + 'px;--rd-lh:' + LINE[s.spacing];
// a part's share of the book: [from, to)
const partSpan = (parts, i) => [parts[i][1], i + 1 < parts.length ? parts[i + 1][1] : 1];
// a part from the handheld, the last few kept (a map per view)
async function fetchPart(cache, p, i) {
	let blocks = cache.get(i);
	if (blocks) return blocks;
	blocks = (await get('/api/book/part', { p, i }, { quiet: true, ms: 20000 })).blocks;
	cache.set(i, blocks);
	if (cache.size > 4) cache.delete(cache.keys().next().value);
	return blocks;
}
const LostPart = ({ onRetry, onBack }) => html`<div class="rd-lost"><div class="rd-box"><div class="t">This part is still on the handheld</div>
	<div class="s">It isn’t answering: asleep, or off Wi-Fi. Wake it and try again. Your place is sent when it is back.</div>
	<div class="rd-acts"><button class="btn primary" onClick=${onRetry}>Try again</button>${onBack && html`<button class="btn" onClick=${onBack}>Back to the page</button>`}</div></div></div>`;

export function TextView(props) {
	return textSettings().mode === 'scroll' ? html`<${ScrollText} ...${props} />` : html`<${PagedText} ...${props} />`;
}

function PagedText({ book, p, ctl, onMoved, onTap }) {
	const parts = book.parts || [];
	const [part, setPart] = useState(null);	// {i, blocks}
	const [page, setPage] = useState(0), [pages, setPages] = useState(1);
	const [tick, setTick] = useState(0);	// a relayout asked (settings, a picture loaded, the window's size)
	const [lost, setLost] = useState(null);	// a part the handheld didn't send
	const view = useRef(), cols = useRef(), want = useRef(null), cache = useRef(new Map()), geo = useRef({ stride: 1, per: 1 });
	const s = textSettings();

	// a part, from the handheld (the last few kept); want: where in it ({id}, {end}, {frac})
	async function load(i, w) {
		if (i < 0 || i >= parts.length) return;
		want.current = w;
		let blocks;
		try { blocks = await fetchPart(cache.current, p, i); }
		catch (e) { setLost({ i, w, page: (part ? part.i : 0) }); return; }
		setLost(null);
		// an empty part (a chapter with no text): the next one in the way of travel
		if (!blocks.length) return load(w && w.end ? i - 1 : i + 1, w);
		setPart({ i, blocks });
		setTick(t => t + 1);
	}
	useEffect(() => { const id = startId(book); load(partOf(parts, id), { id }); }, []);

	// the block at the top of page n: the first whose box reaches past the page's left edge
	function topBlock(n) {
		const c = cols.current; if (!c || !c.children.length) return null;
		const base = c.getBoundingClientRect().left, left = n * geo.current.stride + 1, kids = c.children;
		let lo = 0, hi = kids.length - 1;
		while (lo < hi) { const m = (lo + hi) >> 1; if (kids[m].getBoundingClientRect().right - base > left) hi = m; else lo = m + 1; }
		return kids[lo];
	}
	function pageOfId(id) {
		const c = cols.current; if (!c) return 0;
		const kids = [...c.children]; let el = kids[0];
		for (const k of kids) { if (+k.dataset.id <= id) el = k; else break; }
		if (!el) return 0;
		return Math.max(0, Math.floor((el.getBoundingClientRect().left - c.getBoundingClientRect().left + 2) / geo.current.stride / 1));
	}
	// lay the part out in columns and find the page asked
	useLayoutEffect(() => {
		const v = view.current, c = cols.current; if (!v || !c || !part) return;
		const W = v.clientWidth, H = v.clientHeight, G = 48, per = W >= 900 ? 2 : 1;
		const colW = per === 2 ? (W - G) / 2 : W;
		c.style.height = H + 'px'; c.style.width = W + 'px'; c.style.columnWidth = colW + 'px'; c.style.columnGap = G + 'px';
		c.style.transform = 'none';
		geo.current = { stride: W + G, per };
		const n = Math.max(1, Math.round((c.scrollWidth + G) / (colW + G))), np = Math.max(1, Math.ceil(n / per));
		setPages(np);
		const w = want.current; want.current = null;
		let pg = page;
		if (w && w.id != null) pg = pageOfId(w.id);
		else if (w && w.end) pg = np - 1;
		else if (w && w.frac != null) pg = Math.floor(w.frac * np);
		pg = Math.max(0, Math.min(np - 1, pg));
		setPage(pg);
		c.style.transform = 'translateX(' + (-pg * geo.current.stride) + 'px)';
		report(pg, np);
	}, [part, tick]);
	useLayoutEffect(() => { if (cols.current) cols.current.style.transform = 'translateX(' + (-page * geo.current.stride) + 'px)'; }, [page]);

	function report(pg, np) {
		const el = topBlock(pg); if (!el || !part) return;
		const i = part.i, [f0, f1] = partSpan(parts, i);
		onMoved({ t: 'text', pos: +el.dataset.id, share: Math.min(1, f0 + (f1 - f0) * (np > 1 ? pg / (np - 1) : 0)) });
	}
	// a relayout keeps the block at the top where it is
	const relayout = () => { const el = topBlock(page); want.current = el ? { id: +el.dataset.id } : null; setTick(t => t + 1); };
	useEffect(() => { let t = 0; const r = () => { clearTimeout(t); t = setTimeout(relayout, 150); }; addEventListener('resize', r); return () => { removeEventListener('resize', r); clearTimeout(t); }; });

	function step(d) {
		if (!part) return;
		const n = page + d;
		if (n >= 0 && n < pages) { setPage(n); report(n, pages); return; }
		if (d > 0 && part.i + 1 < parts.length) load(part.i + 1, { frac: 0 });
		else if (d < 0 && part.i > 0) load(part.i - 1, { end: true });
	}
	ctl.current = {
		step, relayout,
		go: pl => { if (pl && pl.t === 'text') load(partOf(parts, pl.pos), { id: pl.pos }); },
		seekFrac: f => { const i = partAtFrac(parts, f), [f0, f1] = partSpan(parts, i); load(i, { frac: f1 > f0 ? (f - f0) / (f1 - f0) : 0 }); },
		snippet: () => { const el = topBlock(page); return el ? (el.textContent || '').trim().split(/\s+/).slice(0, 6).join(' ').slice(0, 60) || 'Bookmark' : 'Bookmark'; },
		key: e => {
			const k = e.key;
			if (k === 'ArrowRight' || k === 'PageDown' || (k === ' ' && !e.shiftKey)) { e.preventDefault(); step(1); }
			else if (k === 'ArrowLeft' || k === 'PageUp' || (k === ' ' && e.shiftKey)) { e.preventDefault(); step(-1); }
			else if (k === ']' && part && part.i + 1 < parts.length) load(part.i + 1, { frac: 0 });
			else if (k === '[' && part) load(Math.max(0, part.i - 1), { frac: 0 });
			else if (k === 'Home') { setPage(0); report(0, pages); }
			else if (k === 'End') { setPage(pages - 1); report(pages - 1, pages); }
			else if (k === '+' || k === '=' || k === '-') { const ss = textSettings(), i = SIZES.indexOf(ss.size) + (k === '-' ? -1 : 1); if (i >= 0 && i < SIZES.length) { saveSettings(Object.assign(ss, { size: SIZES[i] })); relayout(); } }
		},
	};
	// a tap: the left third back, the right third on, the middle the bars; a swipe turns too
	const touch = useRef(null);
	const click = e => { const r = view.current.getBoundingClientRect(), x = (e.clientX - r.left) / r.width; if (x < 1 / 3) step(-1); else if (x > 2 / 3) step(1); else onTap(); };
	const tStart = e => { const t = e.touches[0]; touch.current = { x: t.clientX, y: t.clientY }; };
	const tEnd = e => { const s0 = touch.current, t = e.changedTouches[0]; touch.current = null; if (!s0) return; const dx = t.clientX - s0.x; if (Math.abs(dx) > 50 && Math.abs(dx) > Math.abs(t.clientY - s0.y)) { e.preventDefault(); step(dx < 0 ? 1 : -1); } };
	return html`<div class=${paperClass(s)} style=${paperStyle(s)}>
		<div class=${'rd-view' + (book.para ? '' : ' lines')} ref=${view} onClick=${click} onTouchStart=${tStart} onTouchEnd=${tEnd}>
			<div class="rd-cols" ref=${cols} onLoadCapture=${relayout}>${part && part.blocks.map(b => html`<${Block} key=${b.id} b=${b} p=${p} />`)}</div>
		</div>
		${lost && html`<${LostPart} onRetry=${() => load(lost.i, lost.w)} onBack=${part ? () => setLost(null) : null} />`}
	</div>`;
}

const startId = book => { const st = book.start, parts = book.parts || []; return st && st.t === 'text' ? st.pos : parts.length ? parts[0][0] : 0; };
const partAtFrac = (parts, f) => { let i = 0; while (i + 1 < parts.length && parts[i + 1][1] <= f) i++; return i; };
const KEEP_PARTS = 3;	// (Scroll: the parts kept laid out; one further away goes, the view kept where it is)

/* Scroll: the parts one under another, the next one asked when the end is a screen away, the previous one from the
   key over the first. The place is the block at the top of the view. A change of the parts shown keeps the block at
   the top where it was on the screen (anchor) */
function ScrollText({ book, p, ctl, onMoved, onTap }) {
	const parts = book.parts || [];
	const [shown, setShown] = useState([]);	// [{i, blocks}], parts in a row
	const [lost, setLost] = useState(null);
	const view = useRef(), cache = useRef(new Map()), busy = useRef(false), anchor = useRef(null), want = useRef(null);
	const s = textSettings();

	// the parts from i alone (a jump): want, where in it ({id}, {frac}, {end})
	async function jump(i, w) {
		if (i < 0 || i >= parts.length) return;
		busy.current = true;
		try { const blocks = await fetchPart(cache.current, p, i); setLost(null); want.current = w; anchor.current = null; setShown([{ i, blocks }]); }
		catch (e) { setLost({ i, w }); }
		busy.current = false;
	}
	// one more part at an end (d 1: after the last, -1: before the first)
	async function grow(d) {
		if (busy.current || !shown.length) return;
		const i = d > 0 ? shown[shown.length - 1].i + 1 : shown[0].i - 1;
		if (i < 0 || i >= parts.length) return;
		busy.current = true;
		try {
			const blocks = await fetchPart(cache.current, p, i);
			anchor.current = topAnchor();
			setShown(list => { const n = d > 0 ? list.concat([{ i, blocks }]) : [{ i, blocks }].concat(list); return n.length > KEEP_PARTS ? (d > 0 ? n.slice(1) : n.slice(0, KEEP_PARTS)) : n; });
		} catch (e) { setLost({ i, w: { frac: 0 } }); }
		busy.current = false;
	}
	useEffect(() => { const id = startId(book); jump(partOf(parts, id), { id }); }, []);

	const blocksEls = () => view.current ? view.current.querySelectorAll('[data-id]') : [];
	// the first block whose bottom is under the view's top
	function topBlock() {
		const v = view.current; if (!v) return null;
		const top = v.getBoundingClientRect().top + 1, els = blocksEls();
		let lo = 0, hi = els.length - 1;
		if (hi < 0) return null;
		while (lo < hi) { const m = (lo + hi) >> 1; if (els[m].getBoundingClientRect().bottom > top) hi = m; else lo = m + 1; }
		return els[lo];
	}
	const topAnchor = () => { const el = topBlock(); return el ? { id: el.dataset.id, off: el.getBoundingClientRect().top - view.current.getBoundingClientRect().top } : null; };
	const elOf = id => view.current && view.current.querySelector('[data-id="' + id + '"]');
	useLayoutEffect(() => {
		const v = view.current; if (!v || !shown.length) return;
		const a = anchor.current, w = want.current;
		anchor.current = null; want.current = null;
		if (a) { const el = elOf(a.id); if (el) v.scrollTop += el.getBoundingClientRect().top - v.getBoundingClientRect().top - a.off; }
		else if (w) {
			const sec = v.querySelector('section[data-part="' + shown[0].i + '"]');
			if (w.id != null) { const els = [...sec.querySelectorAll('[data-id]')]; let el = els[0]; for (const k of els) { if (+k.dataset.id <= w.id) el = k; else break; } v.scrollTop = el ? el.offsetTop - sec.offsetTop : 0; }
			else if (w.end) v.scrollTop = Math.max(0, sec.offsetHeight - v.clientHeight);
			else v.scrollTop = Math.round((w.frac || 0) * Math.max(0, sec.offsetHeight - v.clientHeight));
		}
		report();
	}, [shown]);

	function report() {
		const el = topBlock(), v = view.current; if (!el || !v) return;
		const sec = el.closest('section'), i = +sec.dataset.part, [f0, f1] = partSpan(parts, i);
		const within = Math.max(0, Math.min(1, (v.getBoundingClientRect().top - sec.getBoundingClientRect().top) / Math.max(1, sec.offsetHeight)));
		onMoved({ t: 'text', pos: +el.dataset.id, share: Math.min(1, f0 + (f1 - f0) * within) });
	}
	const onScroll = () => {
		const v = view.current; report();
		if (v.scrollHeight - v.scrollTop - v.clientHeight < v.clientHeight) grow(1);
	};
	const relayout = () => { anchor.current = topAnchor(); setShown(l => l.slice()); };
	useEffect(() => { let t = 0; const r = () => { clearTimeout(t); t = setTimeout(relayout, 150); }; addEventListener('resize', r); return () => { removeEventListener('resize', r); clearTimeout(t); }; });
	const by = d => { const v = view.current; if (v) v.scrollBy({ top: d * (v.clientHeight - 48), behavior: 'smooth' }); };
	const curPart = () => { const el = topBlock(); return el ? +el.closest('section').dataset.part : shown.length ? shown[0].i : 0; };
	ctl.current = {
		step: by, relayout,
		go: pl => { if (pl && pl.t === 'text') jump(partOf(parts, pl.pos), { id: pl.pos }); },
		seekFrac: f => { const i = partAtFrac(parts, f), [f0, f1] = partSpan(parts, i); jump(i, { frac: f1 > f0 ? (f - f0) / (f1 - f0) : 0 }); },
		snippet: () => { const el = topBlock(); return el ? (el.textContent || '').trim().split(/\s+/).slice(0, 6).join(' ').slice(0, 60) || 'Bookmark' : 'Bookmark'; },
		key: e => {
			const k = e.key;
			if (k === 'ArrowRight' || k === 'PageDown' || (k === ' ' && !e.shiftKey)) { e.preventDefault(); by(1); }
			else if (k === 'ArrowLeft' || k === 'PageUp' || (k === ' ' && e.shiftKey)) { e.preventDefault(); by(-1); }
			else if (k === ']' && curPart() + 1 < parts.length) jump(curPart() + 1, { frac: 0 });
			else if (k === '[') jump(Math.max(0, curPart() - 1), { frac: 0 });
			else if (k === 'Home') jump(curPart(), { frac: 0 });
			else if (k === 'End') jump(curPart(), { end: true });
			else if (k === '+' || k === '=' || k === '-') { const ss = textSettings(), i = SIZES.indexOf(ss.size) + (k === '-' ? -1 : 1); if (i >= 0 && i < SIZES.length) { saveSettings(Object.assign(ss, { size: SIZES[i] })); relayout(); } }
		},
	};
	const first = shown.length ? shown[0].i : 0;
	return html`<div class=${paperClass(s)} style=${paperStyle(s)}>
		<div class=${'rd-view rd-scroll' + (book.para ? '' : ' lines')} ref=${view} onScroll=${onScroll} onClick=${e => { if (!e.target.closest('button')) onTap(); }}>
			${first > 0 && html`<button class="btn sm rd-prevpart" onClick=${() => grow(-1)}>Previous part</button>`}
			${shown.map(pt => html`<section class="rd-cols" data-part=${pt.i} key=${pt.i} onLoadCapture=${relayout}>${pt.blocks.map(b => html`<${Block} key=${b.id} b=${b} p=${p} />`)}</section>`)}
			${shown.length > 0 && shown[shown.length - 1].i === parts.length - 1 && html`<p class="rd-end">The end</p>`}
		</div>
		${lost && html`<${LostPart} onRetry=${() => jump(lost.i, lost.w)} onBack=${shown.length ? () => setLost(null) : null} />`}
	</div>`;
}
