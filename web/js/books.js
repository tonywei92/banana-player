import { html, useState, useEffect } from '../vendor/htm-preact-standalone.module.js';
import { errMsg, get } from './api.js';
import { useStore } from './store.js';
import { cmpName } from './util.js';
import { fmtDate, fmtSize } from './util.js';
import { AppMenu, Art, Banner, Empty, MoreDots, Skeleton, bookCoverUrl, openSheet } from './components.js';
import { go } from './router.js';
import { showInFiles } from './plshared.js';
import { Icon } from './icons.js';
import { SectionUpload, uploadToSection } from './libupload.js';
import { openDoc } from './recent.js';

/* ================= books ================= */
// The Books tab (docs/redesign "books"): the cards' book shelf — EPUB, PDF and comics — as the index's own
// walk lists it. The dashboard can't read one, so a row asks the handheld to open it at its page, the same the
// Recent cards do (api_playlists.cpp's open); the reading places stay the handheld's own.
const kindOf = p => ({ '.epub': 'epub', '.pdf': 'pdf', '.cbz': 'comic', '.cbr': 'comic' })[(p.match(/\.[^.]+$/) || [''])[0].toLowerCase()] || 'book';
const progress = b => !b.started ? '' : b.pages > 0 ? 'Page ' + b.page + ' of ' + b.pages : Math.round(b.frac * 100) + '% read';

export const readHere = b => go('#/books/read?p=' + encodeURIComponent(b.p));
// a book's sheet: who read it last, Continue here / Open on handheld
function bookSheet(b, anchor) {
	const who = b.started ? (b.by && b.by !== 'the handheld' ? 'Last read in ' + b.by : 'Last read on the handheld') + ' \u00b7 ' + progress(b) : '';
	openSheet(b.n, null, [
		{ label: b.started ? 'Continue here' : 'Read here', icon: 'book', fn: () => readHere(b) },
		{ label: 'Open on handheld', icon: 'card', fn: () => openDoc({ p: b.p, title: b.n }) },
		{ label: 'Show in Files', icon: 'folder', fn: () => showInFiles(b.p) },
		{ label: 'Download', icon: 'download', fn: () => { location.href = '/api/file?dl=1&p=' + encodeURIComponent(b.p); } },
	], { anchor, sub: who, seed: b.p, cover: bookCoverUrl(b.p), kind: kindOf(b.p) });
}

export function Books() {
	const s = useStore();
	const [data, setData] = useState(null), [err, setErr] = useState('');
	useEffect(() => { setData(null); setErr(''); get('/api/library', null, { quiet: true }).then(setData).catch(e => setErr(errMsg(e))); }, [s.epoch]);
	// a place written anywhere: the progress again (quietly, the list stays)
	useEffect(() => { if (s.bookTick) get('/api/library', null, { quiet: true }).then(setData).catch(() => {}); }, [s.bookTick]);
	if (err) return html`<div class="page"><div class="phead"><div class="row"><h1>Books</h1><${AppMenu} /></div></div><div class="banner box surface">${err}</div></div>`;
	if (!data) return html`<div class="page"><div class="phead"><div class="row"><h1>Books</h1><${AppMenu} /></div></div><${Skeleton} /></div>`;
	const reading = data.gen === 0;
	// the ones being read first (the handheld's order: last opened first), then the rest by name
	const books = (data.books || []).slice().sort((a, b) => (a.at ? a.at : 1e9) - (b.at ? b.at : 1e9) || cmpName(a.n, b.n));
	return html`<div class="page">
		<div class="phead"><div class="row"><h1>Books</h1><${SectionUpload} kind="books" cards=${data.cards} /><${AppMenu} /></div></div>
		${reading ? html`<${Banner} title="Reading the cards…" />`
			: !books.length ? html`<${Empty} icon="book" title="No books here yet" sub="Books, PDFs and comics in a folder named Books, Ebook or Ebooks at the top of either card are shelved here. Anything you open from Files joins the shelf too." button="Upload to Books" onClick=${() => uploadToSection('books', data.cards)} />`
			: html`<div class="box surface listbox"><ul class="flist">${books.map(b => html`<li class="frow" key=${b.p}>
				<button class="main-btn" aria-label=${'Read: ' + b.n} onClick=${() => readHere(b)}>
					<span class="tile cover"><${Art} src=${bookCoverUrl(b.p)} seed=${b.p} kind=${kindOf(b.p)} k=${2} /></span>
					<span class="grow"><div class="fn ell">${b.n}</div>
						<div class="fm">${progress(b) || (b.size ? fmtSize(b.size) : '')}${b.mtime && !b.started ? ' \u00b7 ' + fmtDate(b.mtime) : ''}</div>
						${b.started && b.frac > 0 && html`<span class="mprog" aria-hidden="true"><i style=${'width:' + Math.round(100 * b.frac) + '%'}></i></span>`}</span>
				</button>
				<button class="ib more" aria-label=${'More: ' + b.n} onClick=${e => bookSheet(b, e.currentTarget)}><${MoreDots} /></button>
			</li>`)}</ul></div>`}
	</div>`;
}
