import { html, useState, useEffect, useRef } from '../vendor/htm-preact-standalone.module.js';
import { errMsg, get, post } from './api.js';
import { Q_DESK, TAB_ID, lsGet, lsSet } from './store.js';
import { canGoBack, go } from './router.js';
import { Icon } from './icons.js';
import { ask } from './toasts.js';
import { openSheet, useMedia } from './components.js';
import { BookPlace, localPlace, placeFrac, samePlace } from './bookplace.js';
import { TextView, textSettings, textSettingsSheet } from './reader-text.js';
import { PagesView } from './reader-pages.js';

/* ================= the reader (docs/redesign/web-books.md: phone-read, phone-read-comic, phone-read-pdf) =================
   The whole page: the book, and bars a tap in the middle shows (‹ back, the title, ⋯; the slider, where, the sync
   line). A text is laid out here from the handheld's own blocks (reader-text.js); a comic's pages are the archive's
   pictures and a PDF is drawn by pdf.js (reader-pages.js). The place is bookplace.js's.
   On a computer (desk-read, desk-read-comic) the bars stay (hidden in full screen until the pointer moves), with the
   keys in the top one, and a text's Contents and Bookmarks pinned at the side when there is room (C). */

const pct = f => Math.round(Math.max(0, Math.min(1, f || 0)) * 100) + '%';
// a place in words: "page 5", "Chapter 2 · 11%"
function where(book, pl) {
	if (!pl) return '';
	if (pl.t === 'pages') return 'page ' + (Math.floor(pl.pos) + 1);
	const c = (book.contents || []).filter(x => x.id <= pl.pos).pop();
	return (c ? c.t + ' · ' : '') + pct(pl.share);
}
const deviceName = by => !by || by === 'the handheld' ? 'the handheld' : by;
const SYNC = { shared: 'Shared with the handheld', saving: 'Saving…', unsent: 'Not sent yet' };

export function Reader({ p }) {
	const [book, setBook] = useState(null), [err, setErr] = useState(null);
	const [bars, setBars] = useState(() => !lsGet('bp-read-tip'));
	const [here, setHere] = useState(null);	// the place shown: {t, pos, share}
	const [sync, setSync] = useState('shared');
	const [note, setNote] = useState(null);	// {text, label, fn}
	const [there, setThere] = useState(null);	// opened on the handheld: its place
	const [marks, setMarks] = useState([]);
	const desk = useMedia(Q_DESK), [fs, setFs] = useState(false), [idle, setIdle] = useState(false);
	const [side, setSide] = useState(() => lsGet('bp-read-side') !== '0'), [sideTab, setSideTab] = useState('contents');
	const [mode, setMode] = useState(() => textSettings().mode), [pv, setPv] = useState({ layout: 'one', rtl: false });
	const ctl = useRef({}), place = useRef(null), noteT = useRef(0), hereRef = useRef(null);
	hereRef.current = here;
	const say = (text, label, fn) => { clearTimeout(noteT.current); setNote({ text, label, fn }); noteT.current = setTimeout(() => setNote(null), 8000); };
	const leave = () => { if (canGoBack(1)) history.back(); else go('#/books'); };

	useEffect(() => {
		let gone = false;
		get('/api/book', { p }, { quiet: true, ms: 20000 }).then(b => {
			if (gone) return;
			setMarks(b.marks || []);
			const mine = localPlace(p), stored = b.place;
			place.current = new BookPlace(p, stored, {
				status: setSync,
				// another device turned: a note, never a jump (and the page "there" follows it)
				remote: pl => {
					if (place.current.there) { ctl.current.go && ctl.current.go(pl); setThere(pl); return; }
					say(deviceName(pl.by) === 'the handheld' ? 'The handheld turned to ' + where(b, pl) : deviceName(pl.by) + ' is at ' + where(b, pl), 'Go there', () => { place.current.takeTheirs(pl); ctl.current.go(pl); });
				},
				conflict: (mineNow, theirs) => twoPlaces(b, mineNow, theirs),
			});
			// open where it was last read, on either device; picked up from another one: a note back to this one's
			const start = stored || mine;
			if (stored && mine && !samePlace(stored, mine))
				setTimeout(() => say('Picked up from ' + deviceName(stored.by) + ': ' + where(b, stored), 'Back to ' + where(b, mine), () => ctl.current.go(mine)), 600);
			setBook(Object.assign({ start }, b));
		}).catch(e => { if (!gone) setErr(e.status === 404 ? 'This book is no longer on the card.' : errMsg(e)); });
		return () => { gone = true; if (place.current) place.current.close(); place.current = null; clearTimeout(noteT.current); };
	}, [p]);

	// rule 5: both read on unseen. The furthest is the first choice; the other is kept as "Other place" (a bookmark
	// in a text) or offered back for a moment (a page)
	async function twoPlaces(b, mine, theirs) {
		const mineFirst = placeFrac(mine) >= placeFrac(theirs);
		const keepMine = { label: 'Continue', value: 'mine' }, goThere = { label: 'Go there', value: 'theirs' };
		const v = await ask('Two places in this book', 'You read on here while the handheld couldn’t be reached, and the book was read on the handheld too.',
			{ buttons: mineFirst ? [goThere, Object.assign({ cls: 'primary' }, keepMine)] : [keepMine, Object.assign({ cls: 'primary' }, goThere)] });
		const other = v === 'theirs' ? mine : theirs;
		if (v === 'theirs') { place.current.takeTheirs(theirs); ctl.current.go(theirs); }
		else place.current.keepMine(mine);
		if (other.t === 'text') addMark(other.pos, other.share, 'Other place');
		else say('Back to ' + where(b, other), 'Go', () => ctl.current.go(other));
	}
	function addMark(id, frac, label) {
		post('/api/book/mark', { p, id, frac, label }).then(() => setMarks(m => m.filter(x => x.id !== id).concat([{ id, frac, label }]).sort((a, c) => a.id - c.id))).catch(() => {});
	}
	// the view's place changed (a turn, a jump)
	const moved = pl => { setHere(pl); if (place.current) place.current.turn(pl); };

	async function openThere() {
		const pl = here;
		try {
			await post('/api/recent/open', Object.assign({ p, tab: TAB_ID }, pl ? { t: pl.t, pos: pl.pos, share: pl.share } : {}));
			if (place.current) { place.current.unsent = null; place.current.there = true; }
			setThere(pl || {});
		} catch (e) { say(e.status === 0 || e.status === 503 ? 'The handheld isn’t answering. Wake it and try again.' : errMsg(e)); }
	}
	function more(anchor) {
		const text = book && book.kind === 'text';
		openSheet(book ? book.title : '', 'book', [
			{ label: 'Open on handheld', icon: 'card', fn: openThere },
			text && { label: 'Contents', icon: 'chapters', fn: contents },
			text && { label: 'Bookmarks', icon: 'bookmark', fn: bookmarks },
			text && { label: 'Text', icon: 'page', fn: textSheet },
			!text && { label: 'View', icon: 'page', fn: () => ctl.current.view && ctl.current.view() },
			!text && { label: 'Pages', icon: 'blocks', fn: () => ctl.current.grid && ctl.current.grid() },
			'-',
			{ label: 'Read from the start', icon: 'prev', fn: fromStart },
			{ label: 'Download', icon: 'download', fn: () => { location.href = '/api/file?dl=1&p=' + encodeURIComponent(p); } },
		], { anchor });
	}
	// the Text sheet: a change of layout (Pages / Scroll) opens the other view at the place shown
	function textSheet() {
		textSettingsSheet(() => {
			const m = textSettings().mode;
			if (m !== mode) { if (hereRef.current) book.start = hereRef.current; setMode(m); }
			else if (ctl.current.relayout) ctl.current.relayout();
		});
	}
	// Contents: pinned at the side on a computer with room (C shows or hides it), else a sheet
	const pinned = () => desk && innerWidth >= 1200;
	function toggleContents(tab) {
		if (!pinned()) return tab === 'marks' ? bookmarks() : contents();
		const on = !(side && sideTab === (tab || 'contents'));
		setSideTab(tab || 'contents'); setSide(on); lsSet('bp-read-side', on ? '1' : '0');
		setTimeout(() => ctl.current.relayout && ctl.current.relayout(), 0);
	}
	function contents() {
		const list = (book.contents || []);
		if (!list.length) return openSheet('Contents', 'chapters', [{ label: 'This book has no contents list', disabled: true }]);
		openSheet('Contents', 'chapters', list.map(c => ({ label: ' '.repeat(Math.min(c.d, 4) * 3) + c.t, fn: () => ctl.current.go({ t: 'text', pos: c.id }) })));
	}
	function bookmarks() {
		const pl = here;
		openSheet('Bookmarks', 'bookmark', [
			pl && { label: 'Add a bookmark here', icon: 'plus', fn: () => addMark(pl.pos, pl.share, ctl.current.snippet ? ctl.current.snippet() : 'Bookmark') },
			marks.length ? '-' : null,
			...marks.map(m => ({ label: m.label, sub: pct(m.frac), icon: 'bookmark', fn: () => ctl.current.go({ t: 'text', pos: m.id }) })),
			marks.length ? '-' : null,
			...marks.map(m => ({ label: 'Remove “' + m.label + '”', icon: 'trash', danger: true, fn: () => post('/api/book/mark/remove', { p, id: m.id }).then(() => setMarks(x => x.filter(y => y.id !== m.id))).catch(() => {}) })),
		]);
	}
	// rule 7: the start written; the old place kept as "Other place" (a text's)
	function fromStart() {
		const old = here;
		const start = book.kind === 'text' ? { t: 'text', pos: book.parts && book.parts.length ? book.parts[0][0] : 0, share: 0 } : { t: 'pages', pos: 0, share: here ? here.share : 0 };
		ctl.current.go(start);
		if (old && old.t === 'text' && old.share > 0.01) addMark(old.pos, old.share, 'Other place');
	}
	const tap = () => { if (!desk) setBars(b => !b); if (!lsGet('bp-read-tip')) lsSet('bp-read-tip', '1'); };
	// D: this page's bookmark (a computer); on a phone the Bookmarks sheet holds the same key
	function markHere() {
		const pl = hereRef.current;
		if (!pl) return;
		if (!desk) return bookmarks();
		addMark(pl.pos, pl.share, ctl.current.snippet ? ctl.current.snippet() : 'Bookmark');
		say('Bookmarked: ' + where(book, pl));
	}
	// full screen on a computer: the bars go until the pointer moves
	useEffect(() => {
		const f = () => setFs(!!document.fullscreenElement);
		document.addEventListener('fullscreenchange', f); return () => document.removeEventListener('fullscreenchange', f);
	}, []);
	useEffect(() => {
		if (!fs) { setIdle(false); return; }
		let t = 0;
		const wake = () => { setIdle(false); clearTimeout(t); t = setTimeout(() => setIdle(true), 2500); };
		wake(); addEventListener('pointermove', wake);
		return () => { removeEventListener('pointermove', wake); clearTimeout(t); };
	}, [fs]);

	// keys (a computer): the view's own (turns) and these
	useEffect(() => {
		const key = e => {
			if (e.ctrlKey || e.metaKey || e.altKey || /^(INPUT|SELECT|TEXTAREA)$/.test(e.target.tagName) || document.querySelector('.scrim')) return;
			if (e.key === 'Escape') { e.preventDefault(); if (!(ctl.current.esc && ctl.current.esc())) leave(); }
			else if (e.key === 'c' || e.key === 'C') { if (book && book.kind === 'text') toggleContents('contents'); }
			else if (e.key === 'd' || e.key === 'D') { if (book && book.kind === 'text') markHere(); }
			else if (e.key === 't' || e.key === 'T') { if (book && book.kind === 'text') textSheet(); }
			else if (e.key === 'f' || e.key === 'F') { if (document.fullscreenElement) document.exitFullscreen(); else document.documentElement.requestFullscreen && document.documentElement.requestFullscreen().catch(() => {}); }
			else if (ctl.current.key) ctl.current.key(e);
		};
		addEventListener('keydown', key); return () => removeEventListener('keydown', key);
	});

	if (err) return html`<div class="rd rd-msg"><div class="rd-box"><div class="t">This book couldn’t be opened here.</div><div class="s">${err}</div>
		<div class="rd-acts"><button class="btn" onClick=${leave}>Back to Books</button>${err.indexOf('no longer') < 0 && html`<button class="btn" onClick=${openThere}>Open on handheld</button><a class="btn" href=${'/api/file?dl=1&p=' + encodeURIComponent(p)}>Download</a>`}</div></div></div>`;
	if (!book) return html`<div class="rd rd-opening" aria-busy="true"></div>`;
	const frac = placeFrac(here), text = book.kind === 'text';
	const pos = !here ? '' : here.t === 'pages' ? (here.to > here.pos ? 'Pages ' + (here.pos + 1) + '–' + (here.to + 1) : 'Page ' + (Math.floor(here.pos) + 1)) + ' of ' + here.share : where(book, here);
	const showBars = desk ? !idle : bars, showSide = text && desk && side && innerWidth >= 1200;
	const key = (n, label, fn, on) => html`<button class=${'ib' + (on ? ' on' : '')} aria-label=${label} title=${label} aria-pressed=${on == null ? undefined : !!on} onClick=${fn}><${Icon} n=${n} k=${2} /></button>`;
	return html`<div class=${'rd' + (showBars ? ' bars' : '') + (text ? ' rd-text' : ' rd-pages') + (desk ? ' desk' : '') + (fs ? ' fs' : '')}>
		<div class="rd-main">
			${showSide && html`<${Side} book=${book} here=${here} marks=${marks} tab=${sideTab} setTab=${setSideTab} ctl=${ctl} onMark=${markHere} />`}
			${text ? html`<${TextView} key=${mode} book=${book} p=${p} ctl=${ctl} onMoved=${moved} onTap=${tap} />`
				: html`<${PagesView} book=${book} p=${p} ctl=${ctl} onMoved=${moved} onTap=${tap} onLayout=${setPv} />`}
		</div>
		<header class="rd-top">
			<button class=${desk ? 'btn sm rd-back' : 'ib'} aria-label="Back to Books" onClick=${leave}><${Icon} n="back" k=${2} />${desk && 'Books'}</button>
			<div class="grow ell rd-title">${book.title}${desk && !text && pos && html`<span class="rd-sub">${pos}${pv.rtl ? ' · right to left' : ''}</span>`}</div>
			${desk && text && html`${key('bookmark', 'Bookmark this page (D)', markHere)}${key('page', 'Text (T)', textSheet)}${key('chapters', 'Contents (C)', () => toggleContents('contents'), showSide)}`}
			${!text && html`${key('page', 'View', () => ctl.current.view && ctl.current.view())}${key('blocks', 'Pages (G)', () => ctl.current.grid && ctl.current.grid())}`}
			${desk && html`<button class="btn sm" onClick=${openThere}><${Icon} n="card" k=${2} />Open on handheld</button>`}
			<button class="ib" aria-label="More" onClick=${e => more(e.currentTarget)}><${Icon} n="more" k=${2} /></button>
		</header>
		<footer class="rd-bot">
			<input type="range" class="rd-slider" min="0" max="1000" value=${Math.round(frac * 1000)} aria-label="Where in the book"
				dir=${!text && pv.rtl ? 'rtl' : 'ltr'} onChange=${e => ctl.current.seekFrac && ctl.current.seekFrac(e.target.value / 1000)} />
			<div class="rd-line"><span>${pos}</span><span class="rd-sync">${SYNC[sync]}</span></div>
		</footer>
		${!desk && !lsGet('bp-read-tip') && bars && html`<div class="rd-tip">Sides turn the page · middle shows the controls</div>`}
		${note && html`<div class="rd-note" role="status"><span class="grow">${note.text}</span>${note.label && html`<button class="btn sm" onClick=${() => { setNote(null); note.fn(); }}>${note.label}</button>`}</div>`}
		${there && html`<div class="rd-there"><div class="rd-box"><${Icon} n="card" k=${3} /><div class="t">Open on the handheld</div>
			<div class="s">${there.t ? where(book, there) + ', where you were here.' : 'At its place.'}</div>
			<div class="rd-acts"><button class="btn primary" onClick=${() => { place.current.there = false; setThere(null); }}>Read here instead</button><button class="btn" onClick=${leave}>Back to Books</button></div></div></div>`}
	</div>`;
}

// a computer's side panel (desk-read): Contents and Bookmarks, the chapter read marked
function Side({ book, here, marks, tab, setTab, ctl, onMark }) {
	const list = book.contents || [], at = here && here.t === 'text' ? here.pos : -1;
	let cur = -1;
	list.forEach((c, i) => { if (c.id <= at) cur = i; });
	const chip = (k, label) => html`<button class=${'chip' + (tab === k ? ' y' : '')} aria-pressed=${tab === k} onClick=${() => setTab(k)}>${label}</button>`;
	return html`<aside class="rd-side" aria-label="Contents and bookmarks">
		<div class="rd-side-tabs">${chip('contents', 'Contents · ' + list.length)}${chip('marks', 'Bookmarks · ' + marks.length)}</div>
		<div class="rd-side-list">${tab === 'contents'
			? (list.length ? list.map((c, i) => html`<button class=${'rd-ch' + (i === cur ? ' cur' : '')} style=${'padding-left:' + (10 + Math.min(c.d, 4) * 14) + 'px'} aria-current=${i === cur ? 'true' : undefined} onClick=${() => ctl.current.go({ t: 'text', pos: c.id })}><span class="ell">${c.t}</span></button>`)
				: html`<p class="dim sm">This book has no contents list.</p>`)
			: html`<button class="btn sm" onClick=${onMark}><${Icon} n="plus" k=${2} />Bookmark this page</button>
				${marks.map(m => html`<button class="rd-ch" onClick=${() => ctl.current.go({ t: 'text', pos: m.id })}><span class="ell grow">${m.label}</span><span class="dim">${pct(m.frac)}</span></button>`)}`}</div>
	</aside>`;
}
