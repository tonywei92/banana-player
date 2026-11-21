import { html, useState, useEffect, useLayoutEffect, useRef } from '../vendor/htm-preact-standalone.module.js';
import { lsGet, lsSet } from './store.js';
import { Modal } from './components.js';
import { openModal } from './toasts.js';

/* ================= a book of pages: a comic, a PDF (docs/redesign/web-books.md: phone-read-comic, phone-read-pdf,
   desk-read-comic) =================
   A comic's page is the archive's picture as it is (the handheld unpacks it: /api/book/page); the next ones are asked
   ahead. A PDF is drawn here by pdf.js (web/vendor/pdfjs, loaded only when a PDF opens; the owner's call over the
   handheld drawing each page), from the file as /api/file serves it (by ranges), at the screen's own pixels.
   View (per browser): One page / Two (side by side, only in a landscape window: the cover alone, a wide page alone,
   the place is the first of the pair) / Strip (one long scroll); Fit page / width / height / 100%; zoom over the fit
   (pinch, Ctrl+wheel, + −, a double tap) to 4×. A right-to-left comic turns the other way (the left third and ← are
   Next); Space is always Next. A page's size is learnt as it arrives (unknown: an upright page). */

let pdfjs = null;
async function pdfLib() {
	if (pdfjs) return pdfjs;
	pdfjs = await import('../vendor/pdfjs/pdf.min.mjs');
	pdfjs.GlobalWorkerOptions.workerSrc = new URL('../vendor/pdfjs/pdf.worker.min.mjs', import.meta.url).href;
	return pdfjs;
}

const LAYOUTS = [['one', 'One'], ['two', 'Two'], ['strip', 'Strip']];
const FITS = [['page', 'Page'], ['width', 'Width'], ['height', 'Height'], ['actual', '100%']];
const COVERS = [['alone', 'Alone'], ['paired', 'Paired']];
const DIRS = [['ltr', 'Left to right'], ['rtl', 'Right to left']];
const ZMAX = 4, UPRIGHT = { w: 700, h: 1000 };
const one = (list, v, d) => list.some(([k]) => k === v) ? v : d;
// the View settings, this browser's: layout 'auto' is Two in a landscape window ≥ 1000 px, else One
export function pageSettings() {
	let s = {}; try { s = JSON.parse(lsGet('bp-read-pages') || '{}'); } catch (e) {}
	return { layout: s.layout === 'auto' ? 'auto' : one(LAYOUTS, s.layout, 'auto'), fit: one(FITS, s.fit, 'page'), cover: one(COVERS, s.cover, 'alone') };
}
const saveSettings = s => lsSet('bp-read-pages', JSON.stringify(s));
const wideWindow = () => innerWidth >= 1000 && innerWidth > innerHeight;
// what the layout is now: 'one', 'two' (a landscape window only) or 'strip'
export const layoutNow = s => s.layout === 'strip' ? 'strip' : (s.layout === 'two' || s.layout === 'auto') && wideWindow() ? 'two' : 'one';
// the reading direction: the book's own unless this browser chose one for it
const dirOf = (p, book) => lsGet('bp-read-dir:' + p) || (book.rtl ? 'rtl' : 'ltr');

// the pages shown together: each alone, or in pairs (the cover alone unless Paired; a wide page alone)
function spreadsOf(count, sizeOf, two, coverAlone) {
	const out = [];
	let i = 0;
	if (two && coverAlone && count > 0) { out.push([0]); i = 1; }
	const wide = n => { const z = sizeOf(n); return z.w > z.h; };
	while (i < count) {
		if (two && !wide(i) && i + 1 < count && !wide(i + 1)) { out.push([i, i + 1]); i += 2; }
		else { out.push([i]); i += 1; }
	}
	return out;
}
const spreadAt = (spreads, page) => Math.max(0, spreads.findIndex(s => s.includes(page)));

// a PDF page drawn at a CSS size, sharp at the device's pixels (capped, so a 4× zoom stays in memory); lazy: drawn
// only once it nears the view (the strip, the grid)
function PdfCanvas({ doc, n, w, h, lazy, onFail }) {
	const canvas = useRef();
	const [near, setNear] = useState(!lazy);
	useEffect(() => {
		if (near || !canvas.current) return;
		const io = new IntersectionObserver(es => { if (es.some(e => e.isIntersecting)) { setNear(true); io.disconnect(); } }, { rootMargin: '800px' });
		io.observe(canvas.current);
		return () => io.disconnect();
	}, [near]);
	useLayoutEffect(() => {
		if (!near || !doc || !(w > 0)) return;
		let task = null, gone = false;
		(async () => {
			try {
				const pg = await doc.getPage(n + 1); if (gone) return;
				const v1 = pg.getViewport({ scale: 1 }), dpr = Math.min(3, devicePixelRatio || 1);
				let scale = w / v1.width * dpr;
				const px = v1.width * v1.height * scale * scale;
				if (px > 16e6) scale *= Math.sqrt(16e6 / px);
				const vp = pg.getViewport({ scale }), c = canvas.current;
				c.width = Math.floor(vp.width); c.height = Math.floor(vp.height);
				task = pg.render({ canvasContext: c.getContext('2d'), viewport: vp });
				await task.promise;
			} catch (e) { if (!gone && onFail && e && e.name !== 'RenderingCancelledException') onFail(String(e && e.message || e)); }
		})();
		return () => { gone = true; if (task) task.cancel(); };
	}, [doc, n, near, Math.round(w)]);
	return html`<canvas ref=${canvas} style=${'width:' + Math.round(w) + 'px;height:' + Math.round(h) + 'px'}></canvas>`;
}

// View: the layout, the fit, the direction, the cover (a sheet; the keys 1 2 S, W H 0 do the same)
function ViewSheet({ close, onChange, p, book }) {
	const [s, setS] = useState(pageSettings), [dir, setDir] = useState(() => dirOf(p, book));
	const now = layoutNow(s);
	const put = patch => { const n = Object.assign({}, s, patch); setS(n); saveSettings(n); onChange(); };
	const seg = (list, v, fn) => html`<div class="rd-seg" role="group">${list.map(([k, l]) => html`<button class=${'btn sm' + (v === k ? ' primary' : '')} aria-pressed=${v === k} onClick=${() => fn(k)}>${l}</button>`)}</div>`;
	return html`<${Modal} title="View" onClose=${close} actions=${html`<button class="btn primary" onClick=${close}>Done</button>`}>
		<div class="rd-set">
			<div class="rd-lab">Pages</div>${seg(LAYOUTS, now, k => put({ layout: k }))}
			<div class="rd-lab">Fit</div>${seg(FITS, s.fit, k => put({ fit: k }))}
			<div class="rd-lab">Reads</div>${seg(DIRS, dir, k => { setDir(k); lsSet('bp-read-dir:' + p, k); onChange(); })}
			<div class="rd-lab">Cover</div>${seg(COVERS, s.cover, k => put({ cover: k }))}
			<p class="dim sm">Two pages only when the window is wider than it is tall. A wide page (a spread scan) is shown alone. Kept in this browser.</p></div></${Modal}>`;
}

// the pages grid: every page small, the one read marked; a tap goes there
function Grid({ count, cur, thumb, onPick, onClose }) {
	const box = useRef();
	useLayoutEffect(() => { const c = box.current && box.current.querySelector('.cur'); if (c) c.scrollIntoView({ block: 'center' }); }, []);
	return html`<div class="rd-grid" role="dialog" aria-label="Pages">
		<div class="rd-grid-head"><span class="grow">Pages · ${count}</span><button class="btn sm" onClick=${onClose}>Close</button></div>
		<div class="rd-grid-body" ref=${box}>${Array.from({ length: count }, (_, n) => html`<button class=${'rd-th' + (n === cur ? ' cur' : '')} key=${n} aria-label=${'Page ' + (n + 1)} aria-current=${n === cur ? 'true' : undefined} onClick=${() => onPick(n)}>
			${thumb(n)}<span class="num">${n + 1}</span></button>`)}</div></div>`;
}

export function PagesView({ book, p, ctl, onMoved, onTap, onLayout }) {
	const pdf = book.kind === 'pdf';
	const [doc, setDoc] = useState(null), [count, setCount] = useState(pdf ? 0 : book.pages);
	const [page, setPage] = useState(() => book.start && book.start.t === 'pages' ? Math.floor(book.start.pos) : 0);
	const [fail, setFail] = useState(null), [tick, setTick] = useState(0);
	const [vs, setVs] = useState(pageSettings), [zoom, setZoom] = useState(1), [grid, setGrid] = useState(false);
	const [box, setBox] = useState({ w: innerWidth, h: innerHeight });
	const view = useRef(), sizes = useRef(new Map()), asked = useRef(new Set()), zoomAt = useRef(null), strip = useRef({ jump: true });
	const rtl = dirOf(p, book) === 'rtl', layout = layoutNow(vs);
	const src = n => '/api/book/page?p=' + encodeURIComponent(p) + '&n=' + n;
	const sizeOf = n => sizes.current.get(n) || UPRIGHT;
	const learn = (n, w, h) => { if (w > 0 && h > 0 && !sizes.current.has(n)) { sizes.current.set(n, { w, h }); setTick(t => t + 1); } };

	useEffect(() => {
		if (!pdf) return;
		let gone = false, task = null;
		pdfLib().then(lib => {
			if (gone) return;
			task = lib.getDocument({ url: '/api/file?p=' + encodeURIComponent(p), withCredentials: true, isEvalSupported: false, rangeChunkSize: 262144 });
			return task.promise.then(d => { if (!gone) { setDoc(d); setCount(d.numPages); } });
		}).catch(e => { if (!gone) setFail({ open: true, why: String(e && e.message || e) }); });
		return () => { gone = true; if (task) task.destroy(); };
	}, [p]);
	// the sizes of the pages around the place (a PDF: its viewports; a comic: the pictures asked ahead, which the
	// browser then keeps)
	useEffect(() => {
		for (let n = Math.max(0, page - 2); n < Math.min(count, page + 5); n++) {
			if (asked.current.has(n)) continue;
			asked.current.add(n);
			if (pdf) { if (doc) doc.getPage(n + 1).then(pg => { const v = pg.getViewport({ scale: 1 }); learn(n, v.width, v.height); }).catch(() => {}); else asked.current.delete(n); }
			else if (n !== page) { const im = new Image(); im.onload = () => learn(n, im.naturalWidth, im.naturalHeight); im.src = src(n); }
		}
	}, [page, count, doc]);
	// the view's size, again on a resize (Two follows the window's shape)
	useLayoutEffect(() => {
		const measure = () => { const v = view.current; if (v) setBox({ w: v.clientWidth, h: v.clientHeight }); };
		measure();
		const r = () => { measure(); setVs(pageSettings()); };
		addEventListener('resize', r); return () => removeEventListener('resize', r);
	}, [layout, grid]);

	const spreads = spreadsOf(count, sizeOf, layout === 'two', vs.cover === 'alone');
	const si = spreadAt(spreads, Math.min(page, Math.max(0, count - 1))), spread = spreads[si] || [0];
	// where it is, once the count is known (a place past the end: the last page). The place is the spread's first
	useEffect(() => {
		if (count <= 0) return;
		const n = Math.min(page, count - 1);
		if (n !== page) { setPage(n); return; }
		const first = layout === 'strip' ? n : spread[0];
		onMoved({ t: 'pages', pos: first, share: count, to: layout === 'strip' ? first : spread[spread.length - 1] });
	}, [page, count, si, layout]);
	useEffect(() => { if (onLayout) onLayout({ layout, rtl }); }, [layout, rtl]);

	const go = n => { setFail(null); setZoom(1); strip.current.jump = true; setPage(Math.max(0, Math.min(count - 1, n))); };
	const step = d => {
		if (layout === 'strip') { const v = view.current; if (v) v.scrollBy({ top: d * (v.clientHeight - 48), behavior: 'smooth' }); return; }
		const t = spreads[Math.max(0, Math.min(spreads.length - 1, si + d))];
		if (t) go(t[0]);
	};
	const setFit = fit => { const n = Object.assign(pageSettings(), { fit }); saveSettings(n); setVs(n); setZoom(1); };
	const setLayout = l => { const n = Object.assign(pageSettings(), { layout: l }); saveSettings(n); setVs(n); setZoom(1); strip.current.jump = true; };
	const zoomBy = (f, at) => setZoom(z => { const nz = Math.max(1, Math.min(ZMAX, z * f)); if (nz !== z) zoomAt.current = { from: z, to: nz, at }; return nz; });

	// the size a page is drawn at: the spread's height by the fit, times the zoom
	const fitH = (ns, W, H) => {
		const ar = ns.reduce((a, n) => a + sizeOf(n).w / sizeOf(n).h, 0);
		const z = sizeOf(ns[0]), actual = pdf ? z.h * 4 / 3 : z.h;
		return vs.fit === 'width' ? W / ar : vs.fit === 'height' ? H : vs.fit === 'actual' ? actual : Math.min(H, W / ar);
	};
	// a zoom keeps the point under the fingers (or the middle) where it is
	useLayoutEffect(() => {
		const a = zoomAt.current, v = view.current; zoomAt.current = null;
		if (!a || !v) return;
		const r = a.to / a.from, x = a.at ? a.at.x : v.clientWidth / 2, y = a.at ? a.at.y : v.clientHeight / 2;
		v.scrollLeft = (v.scrollLeft + x) * r - x; v.scrollTop = (v.scrollTop + y) * r - y;
	}, [zoom]);
	// Strip: the place is the page at the top; a jump (open, Contents, the grid, a layout change) scrolls to it
	useLayoutEffect(() => {
		if (layout !== 'strip' || !strip.current.jump || !view.current) return;
		const el = view.current.querySelector('[data-n="' + page + '"]');
		if (el) { view.current.scrollTop = el.offsetTop; strip.current.jump = false; }
	}, [layout, page, count, box.w]);
	const stripScroll = () => {
		const v = view.current, top = v.getBoundingClientRect().top + 1, els = v.querySelectorAll('[data-n]');
		for (const el of els) if (el.getBoundingClientRect().bottom > top) { const n = +el.dataset.n; if (n !== page) setPage(n); break; }
	};

	// touch: a swipe turns (at 1× only), two fingers zoom, a double tap zooms to the width and back
	const touch = useRef(null), lastTap = useRef(0), tapT = useRef(0), drag = useRef(null), zoomNow = useRef(1);
	zoomNow.current = zoom;
	useEffect(() => {
		const v = view.current; if (!v) return;
		const dist = t => Math.hypot(t[0].clientX - t[1].clientX, t[0].clientY - t[1].clientY);
		const mid = t => { const r = v.getBoundingClientRect(); return { x: (t[0].clientX + t[1].clientX) / 2 - r.left, y: (t[0].clientY + t[1].clientY) / 2 - r.top }; };
		const start = e => { if (e.touches.length === 2) touch.current = { pinch: dist(e.touches), z: zoomNow.current }; else if (e.touches.length === 1) touch.current = { x: e.touches[0].clientX, y: e.touches[0].clientY }; };
		const move = e => {
			const t = touch.current;
			if (!t || !t.pinch || e.touches.length !== 2) return;
			e.preventDefault();
			const want = Math.max(1, Math.min(ZMAX, t.z * dist(e.touches) / t.pinch));
			if (Math.abs(want - zoomNow.current) > 0.02) zoomBy(want / zoomNow.current, mid(e.touches));
		};
		const wheel = e => { if (!e.ctrlKey) return; e.preventDefault(); const r = v.getBoundingClientRect(); zoomBy(Math.exp(-e.deltaY * 0.01), { x: e.clientX - r.left, y: e.clientY - r.top }); };
		v.addEventListener('touchstart', start, { passive: true });
		v.addEventListener('touchmove', move, { passive: false });
		v.addEventListener('wheel', wheel, { passive: false });
		return () => { v.removeEventListener('touchstart', start); v.removeEventListener('touchmove', move); v.removeEventListener('wheel', wheel); };
	}, [layout, grid, fail && fail.open]);
	const tEnd = e => {
		const s0 = touch.current, t = e.changedTouches[0]; touch.current = null;
		if (!s0 || s0.pinch || layout === 'strip') return;
		const dx = t.clientX - s0.x, v = view.current;
		// (zoomed: only from the page's edge, where the scroll can't go further that way)
		const atEdge = zoom <= 1.01 || (dx < 0 ? v.scrollLeft + v.clientWidth >= v.scrollWidth - 2 : v.scrollLeft <= 1);
		if (atEdge && Math.abs(dx) > 50 && Math.abs(dx) > Math.abs(t.clientY - s0.y)) { e.preventDefault(); step((dx < 0) !== rtl ? 1 : -1); }
	};
	const tapAt = (x, w) => { if (layout === 'strip') return onTap(); if (x < w / 3) step(rtl ? 1 : -1); else if (x > w * 2 / 3) step(rtl ? -1 : 1); else onTap(); };
	const click = e => {
		if (drag.current && drag.current.moved) { drag.current = null; return; }
		const r = view.current.getBoundingClientRect(), x = e.clientX - r.left;
		if (e.pointerType === 'mouse' || !('ontouchstart' in window)) return tapAt(x, r.width);
		// (a touch: a second tap soon after is a double tap: the width and back)
		const now = Date.now();
		if (now - lastTap.current < 300) {
			clearTimeout(tapT.current); lastTap.current = 0;
			if (zoom > 1.01) { setZoom(1); return; }
			const W = view.current.clientWidth, H = view.current.clientHeight, h = fitH(spread, W, H), wide = W / spread.reduce((a, n) => a + sizeOf(n).w / sizeOf(n).h, 0);
			zoomBy(wide / h > 1.05 ? wide / h : 2, { x, y: e.clientY - r.top });
			return;
		}
		lastTap.current = now;
		tapT.current = setTimeout(() => tapAt(x, r.width), 260);
	};
	// a mouse drags a zoomed page about
	const pDown = e => { if (e.pointerType === 'mouse' && e.button === 0) drag.current = { x: e.clientX, y: e.clientY, l: view.current.scrollLeft, t: view.current.scrollTop, moved: false }; };
	const pMove = e => {
		const d = drag.current; if (!d || !(e.buttons & 1)) return;
		const dx = e.clientX - d.x, dy = e.clientY - d.y;
		if (Math.abs(dx) + Math.abs(dy) > 5) d.moved = true;
		if (d.moved) { view.current.scrollLeft = d.l - dx; view.current.scrollTop = d.t - dy; }
	};

	ctl.current = {
		step, go: pl => { if (pl && pl.t === 'pages') go(Math.floor(pl.pos)); },
		seekFrac: f => go(Math.round(f * count) - 1),
		grid: () => setGrid(true),
		view: () => openModal(close => html`<${ViewSheet} close=${close} p=${p} book=${book} onChange=${() => { setVs(pageSettings()); setZoom(1); setTick(t => t + 1); }} />`),
		esc: () => { if (grid) { setGrid(false); return true; } return false; },
		key: e => {
			const k = e.key, fwd = rtl ? 'ArrowLeft' : 'ArrowRight', back = rtl ? 'ArrowRight' : 'ArrowLeft';
			if (grid) return;
			if (k === fwd || k === 'PageDown' || (k === ' ' && !e.shiftKey)) { e.preventDefault(); step(1); }
			else if (k === back || k === 'PageUp' || (k === ' ' && e.shiftKey)) { e.preventDefault(); step(-1); }
			else if (k === 'Home') go(0);
			else if (k === 'End') go(count - 1);
			else if (k === '+' || k === '=') zoomBy(1.25);
			else if (k === '-') zoomBy(0.8);
			else if (k === 'w' || k === 'W') setFit('width');
			else if (k === 'h' || k === 'H') setFit('height');
			else if (k === '0') setFit('actual');
			else if (k === '1') setLayout('one');
			else if (k === '2') setLayout('two');
			else if (k === 's' || k === 'S') setLayout('strip');
			else if (k === 'g' || k === 'G') setGrid(true);
		},
	};

	if (fail && fail.open) return html`<div class="rd-view rd-lost"><div class="rd-box"><div class="t">This book couldn’t be opened here.</div><div class="s">${fail.why}</div></div></div>`;
	const W = box.w, H = box.h;
	// one page at a size: a comic's picture, a PDF's canvas
	const pageEl = (n, w, h, lazy) => pdf ? (doc && html`<${PdfCanvas} doc=${doc} n=${n} w=${w} h=${h} lazy=${lazy} onFail=${lazy ? null : why => setFail({ why })} />`)
		: html`<img src=${src(n)} alt=${'Page ' + (n + 1)} loading=${lazy ? 'lazy' : undefined} draggable="false" style=${'width:' + Math.round(w) + 'px;height:' + Math.round(h) + 'px'}
			onLoad=${e => learn(n, e.target.naturalWidth, e.target.naturalHeight)} onError=${lazy ? undefined : () => setFail({ why: 'Page ' + (n + 1) + ' is still on the handheld: it isn’t answering, asleep or off Wi-Fi.' })} />`;
	const thumb = n => { const z = sizeOf(n), w = 112, h = w * z.h / z.w; return pageEl(n, w, h, true); };
	let body;
	if (fail) body = html`<div class="rd-box"><div class="t">This page couldn’t be read.</div><div class="s sm">${fail.why}</div>
		<div class="rd-acts"><button class="btn primary" onClick=${e => { e.stopPropagation(); step(1); }}>Skip it</button><button class="btn" onClick=${e => { e.stopPropagation(); setFail(null); setTick(t => t + 1); }}>Try again</button></div></div>`;
	else if (layout === 'strip') {
		const pw = Math.min(W, 1000) * zoom;
		body = html`<div class="rd-strip">${Array.from({ length: count }, (_, n) => { const z = sizeOf(n); return html`<div class="rd-sp" data-n=${n} key=${n}>${pageEl(n, pw, pw * z.h / z.w, Math.abs(n - page) > 1)}</div>`; })}</div>`;
	} else {
		const h = fitH(spread, W, H) * zoom;
		body = html`<div class=${'rd-spread' + (rtl ? ' rtl' : '')}>${spread.map(n => html`<div class="rd-pg" key=${n}>${pageEl(n, h * sizeOf(n).w / sizeOf(n).h, h, false)}</div>`)}</div>`;
	}
	return html`<div class="rd-pagesbox">
		<div class=${'rd-view rd-pagesview' + (layout === 'strip' ? ' strip' : '') + (zoom > 1.01 ? ' zoomed' : '')} ref=${view} onClick=${click} onTouchEnd=${tEnd}
			onPointerDown=${pDown} onPointerMove=${pMove} onScroll=${layout === 'strip' ? stripScroll : undefined}>${body}</div>
		${grid && html`<${Grid} count=${count} cur=${page} thumb=${thumb} onPick=${n => { setGrid(false); go(n); }} onClose=${() => setGrid(false)} />`}
	</div>`;
}
