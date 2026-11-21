import { html, useState, useEffect } from '../vendor/htm-preact-standalone.module.js';
import { errMsg, get } from './api.js';
import { useStore } from './store.js';
import { cmpName } from './util.js';
import { fmtDate, fmtSize } from './util.js';
import { AppMenu, Art, Banner, Empty, Skeleton, FileMore, bookCoverUrl } from './components.js';
import { Icon } from './icons.js';
import { SectionUpload, uploadToSection } from './libupload.js';
import { openDoc } from './recent.js';

/* ================= books ================= */
// The Books tab (docs/redesign "books"): the cards' book shelf — EPUB, PDF and comics — as the index's own
// walk lists it. The dashboard can't read one, so a row asks the handheld to open it at its page, the same the
// Recent cards do (api_playlists.cpp's open); the reading places stay the handheld's own.
const kindOf = p => ({ '.epub': 'epub', '.pdf': 'pdf', '.cbz': 'comic', '.cbr': 'comic' })[(p.match(/\.[^.]+$/) || [''])[0].toLowerCase()] || 'book';
const progress = b => !b.started ? '' : b.pages > 0 ? 'Page ' + b.page + ' of ' + b.pages : Math.round(b.frac * 100) + '% read';

export function Books() {
	const s = useStore();
	const [data, setData] = useState(null), [err, setErr] = useState('');
	useEffect(() => { setData(null); setErr(''); get('/api/library', null, { quiet: true }).then(setData).catch(e => setErr(errMsg(e))); }, [s.epoch]);
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
				<button class="main-btn" aria-label=${'Open on the handheld: ' + b.n} onClick=${() => openDoc({ p: b.p, title: b.n })}>
					<span class="tile cover"><${Art} src=${bookCoverUrl(b.p)} seed=${b.p} kind=${kindOf(b.p)} k=${2} /></span>
					<span class="grow"><div class="fn ell">${b.n}</div>
						<div class="fm">${progress(b) || (b.size ? fmtSize(b.size) : '')}${b.mtime && !b.started ? ' \u00b7 ' + fmtDate(b.mtime) : ''}</div>
						${b.started && b.frac > 0 && html`<span class="mprog" aria-hidden="true"><i style=${'width:' + Math.round(100 * b.frac) + '%'}></i></span>`}</span>
					<span class="dur"><${Icon} n="card" k=${1} />Opens on handheld</span></button>
				<${FileMore} p=${b.p} name=${b.n} glyph=${kindOf(b.p)} />
			</li>`)}</ul></div>`}
	</div>`;
}
