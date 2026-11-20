import { html, useState, useEffect } from '../vendor/htm-preact-standalone.module.js';
import { errMsg, get } from './api.js';
import { useStore } from './store.js';
import { fmtDate, fmtSize } from './util.js';
import { AppMenu, Banner, Empty, Skeleton } from './components.js';
import { Icon } from './icons.js';
import { openDoc } from './recent.js';

/* ================= books ================= */
// The Books tab (docs/redesign "books"): the cards' book shelf — EPUB, PDF, TXT and comics — as the index's own
// walk lists it. The dashboard can't read one, so a row asks the handheld to open it at its page, the same the
// Recent cards do (api_playlists.cpp's open); the reading places stay the handheld's own.
const GLYPH = { '.txt': 'page', '.text': 'page', '.epub': 'epub', '.pdf': 'pdf', '.cbz': 'comic', '.cbr': 'comic' };
const glyphOf = p => GLYPH[(p.match(/\.[^.]+$/) || [''])[0].toLowerCase()] || 'book';

export function Books() {
	const s = useStore();
	const [data, setData] = useState(null), [err, setErr] = useState('');
	useEffect(() => { setData(null); setErr(''); get('/api/library', null, { quiet: true }).then(setData).catch(e => setErr(errMsg(e))); }, [s.epoch]);
	if (err) return html`<div class="page"><div class="phead"><div class="row"><h1>Books</h1><${AppMenu} /></div></div><div class="banner box surface">${err}</div></div>`;
	if (!data) return html`<div class="page"><div class="phead"><div class="row"><h1>Books</h1><${AppMenu} /></div></div><${Skeleton} /></div>`;
	const reading = data.gen === 0;
	const books = data.books || [];
	return html`<div class="page">
		<div class="phead"><div class="row"><h1>Books</h1><${AppMenu} /></div></div>
		${reading ? html`<${Banner} title="Reading the cards…" />`
			: !books.length ? html`<${Empty} icon="book" title="No books here yet" sub="Books, PDFs, comics and text files in a folder named Books or Documents at the top of either card are shelved here. Anything you open from Files joins the shelf too." />`
			: html`<div class="box surface listbox"><ul class="flist">${books.map(b => html`<li class="frow" key=${b.p}>
				<button class="main-btn" aria-label=${'Open on the handheld: ' + b.n} onClick=${() => openDoc({ p: b.p, title: b.n })}>
					<span class="tile"><${Icon} n=${glyphOf(b.p)} k=${2} /></span>
					<span class="grow"><div class="fn ell">${b.n}</div>
						<div class="fm">${b.size ? fmtSize(b.size) : ''}${b.size && b.mtime ? ' \u00b7 ' : ''}${b.mtime ? fmtDate(b.mtime) : ''}</div></span>
					<span class="dur"><${Icon} n="card" k=${1} />Opens on handheld</span></button>
			</li>`)}</ul></div>`}
	</div>`;
}
