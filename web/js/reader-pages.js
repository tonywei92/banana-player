import { html, useState, useEffect, useLayoutEffect, useRef } from '../vendor/htm-preact-standalone.module.js';

/* ================= a book of pages: a comic, a PDF (docs/redesign/web-books.md: phone-read-comic, phone-read-pdf) =================
   A comic's page is the archive's picture as it is (the handheld unpacks it: /api/book/page); the next two are asked
   ahead. A PDF is drawn here by pdf.js (web/vendor/pdfjs, loaded only when a PDF opens; the owner's call over the
   handheld drawing each page), from the file as /api/file serves it (by ranges), at the screen's own pixels. A
   right-to-left comic turns the other way (the left third and ← are Next); Space is always Next. */

let pdfjs = null;
async function pdfLib() {
	if (pdfjs) return pdfjs;
	pdfjs = await import('../vendor/pdfjs/pdf.min.mjs');
	pdfjs.GlobalWorkerOptions.workerSrc = new URL('../vendor/pdfjs/pdf.worker.min.mjs', import.meta.url).href;
	return pdfjs;
}

// a PDF page drawn to fit the view (the page's whole), sharp at the device's pixels
function PdfPage({ doc, n, onFail }) {
	const box = useRef(), canvas = useRef();
	useLayoutEffect(() => {
		let task = null, gone = false;
		(async () => {
			try {
				const pg = await doc.getPage(n + 1); if (gone) return;
				const b = box.current, v1 = pg.getViewport({ scale: 1 });
				const scale = Math.min(b.clientWidth / v1.width, b.clientHeight / v1.height), dpr = Math.min(3, devicePixelRatio || 1);
				const vp = pg.getViewport({ scale: scale * dpr }), c = canvas.current;
				c.width = Math.floor(vp.width); c.height = Math.floor(vp.height);
				c.style.width = Math.floor(vp.width / dpr) + 'px'; c.style.height = Math.floor(vp.height / dpr) + 'px';
				task = pg.render({ canvasContext: c.getContext('2d'), viewport: vp });
				await task.promise;
			} catch (e) { if (!gone && e && e.name !== 'RenderingCancelledException') onFail(String(e && e.message || e)); }
		})();
		return () => { gone = true; if (task) task.cancel(); };
	}, [doc, n]);
	return html`<div class="rd-pg" ref=${box}><canvas ref=${canvas}></canvas></div>`;
}

export function PagesView({ book, p, ctl, onMoved, onTap }) {
	const pdf = book.kind === 'pdf';
	const [doc, setDoc] = useState(null), [count, setCount] = useState(pdf ? 0 : book.pages);
	const [page, setPage] = useState(() => book.start && book.start.t === 'pages' ? Math.floor(book.start.pos) : 0);
	const [fail, setFail] = useState(null), [tick, setTick] = useState(0);
	const rtl = !!book.rtl, view = useRef();
	const src = n => '/api/book/page?p=' + encodeURIComponent(p) + '&n=' + n;

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
	// where it is, once the count is known (a place past the end: the last page)
	useEffect(() => { if (count > 0) { const n = Math.min(page, count - 1); if (n !== page) setPage(n); onMoved({ t: 'pages', pos: n, share: count }); } }, [page, count]);
	// the next two asked ahead (a comic: the browser keeps them)
	useEffect(() => { if (!pdf) for (let k = 1; k <= 2 && page + k < count; k++) new Image().src = src(page + k); }, [page, count]);
	useEffect(() => { const r = () => setTick(t => t + 1); addEventListener('resize', r); return () => removeEventListener('resize', r); }, []);

	const step = d => { setFail(null); setPage(n => Math.max(0, Math.min(count - 1, n + d))); };
	ctl.current = {
		step,
		go: pl => { if (pl && pl.t === 'pages') { setFail(null); setPage(Math.max(0, Math.floor(pl.pos))); } },
		seekFrac: f => setPage(Math.max(0, Math.min(count - 1, Math.round(f * count) - 1))),
		key: e => {
			const k = e.key, fwd = rtl ? 'ArrowLeft' : 'ArrowRight', back = rtl ? 'ArrowRight' : 'ArrowLeft';
			if (k === fwd || k === 'PageDown' || (k === ' ' && !e.shiftKey)) { e.preventDefault(); step(1); }
			else if (k === back || k === 'PageUp' || (k === ' ' && e.shiftKey)) { e.preventDefault(); step(-1); }
			else if (k === 'Home') setPage(0);
			else if (k === 'End') setPage(count - 1);
		},
	};
	const touch = useRef(null);
	const click = e => { const r = view.current.getBoundingClientRect(), x = (e.clientX - r.left) / r.width; if (x < 1 / 3) step(rtl ? 1 : -1); else if (x > 2 / 3) step(rtl ? -1 : 1); else onTap(); };
	const tStart = e => { if (e.touches.length !== 1) { touch.current = null; return; } const t = e.touches[0]; touch.current = { x: t.clientX, y: t.clientY }; };
	const tEnd = e => { const s0 = touch.current, t = e.changedTouches[0]; touch.current = null; if (!s0 || (visualViewport && visualViewport.scale > 1.01)) return; const dx = t.clientX - s0.x; if (Math.abs(dx) > 50 && Math.abs(dx) > Math.abs(t.clientY - s0.y)) { e.preventDefault(); step((dx < 0) !== rtl ? 1 : -1); } };

	if (fail && fail.open) return html`<div class="rd-view rd-lost"><div class="rd-box"><div class="t">This book couldn’t be opened here.</div><div class="s">${fail.why}</div></div></div>`;
	return html`<div class="rd-view rd-pagesview" ref=${view} onClick=${click} onTouchStart=${tStart} onTouchEnd=${tEnd} key=${tick}>
		${fail ? html`<div class="rd-box"><div class="t">This page couldn’t be read.</div><div class="s sm">${fail.why}</div>
				<div class="rd-acts"><button class="btn primary" onClick=${e => { e.stopPropagation(); step(1); }}>Skip it</button><button class="btn" onClick=${e => { e.stopPropagation(); setFail(null); setTick(t => t + 1); }}>Try again</button></div></div>`
			: pdf ? (doc && html`<${PdfPage} doc=${doc} n=${page} onFail=${why => setFail({ why })} />`)
			: html`<div class="rd-pg"><img src=${src(page)} alt=${'Page ' + (page + 1)} onError=${() => setFail({ why: 'Page ' + (page + 1) + ' is still on the handheld: it isn’t answering, asleep or off Wi-Fi.' })} /></div>`}
	</div>`;
}
