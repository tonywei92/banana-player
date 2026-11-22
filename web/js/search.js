import { html, useEffect, useRef, useState } from '../vendor/htm-preact-standalone.module.js';
import { errMsg, get } from './api.js';
import { set, store, useStore } from './store.js';
import { fmtSize, fmtTime, plural } from './util.js';
import { AppMenu, Art, Banner, Empty, FileMore, Skeleton, bookCoverUrl, coverUrl, vthumbUrl } from './components.js';
import { Icon } from './icons.js';
import { canGoBack, go, replaceHash } from './router.js';
import { playHere, playPaths } from './browser-player.js';
import { readHere } from './books.js';

/* ================= search ================= */
// Search library (docs/redesign "search-results" and its web minis): what the handheld's own Search finds, from
// /api/search — the same sections in the same order (artists, albums, the folders inside the Music folders, songs;
// the folders inside the Videos folders, videos; books) under the same four tabs. The field is the sidebar's on a
// computer and the page's own elsewhere (a phone's headers and a tablet's sidebar have the magnifier). The query
// lives in the address (#/search?q=), so Back from an album a result opened lands on the results again.
const SECS = [
	{ k: 'artists', tab: 'music', all: 'Artists', own: 'Artists', many: 'artists' },
	{ k: 'albums', tab: 'music', all: 'Albums', own: 'Albums', many: 'albums' },
	{ k: 'musicFolders', tab: 'music', all: 'Music folders', own: 'Folders', many: 'music folders' },
	{ k: 'songs', tab: 'music', all: 'Songs', own: 'Songs', many: 'songs' },
	{ k: 'videoFolders', tab: 'videos', all: 'Video folders', own: 'Folders', many: 'video folders' },
	{ k: 'videos', tab: 'videos', all: 'Videos', own: 'Videos', many: 'videos' },
	{ k: 'books', tab: 'books', all: 'Books', own: 'Books', many: 'books' },
];
const TABS = [['all', 'All'], ['music', 'Music'], ['videos', 'Videos'], ['books', 'Books']];
const ALL_CAP = 3;	// (the handheld's lay::SEARCH_ALL_CAP)

export const searchHash = q => '#/search' + (q ? '?q=' + encodeURIComponent(q) : '');
// what a tab lists, section by section — the handheld's rule (lay::searchSections): under All three of each and a
// "See all" when another section found too (one more than three is simply shown); a kind's own tab whole
export function searchSections(data, tab) {
	const total = k => (data.totals && data.totals[k]) || 0;
	const found = SECS.filter(c => total(c.k) > 0).length;
	return SECS.filter(c => total(c.k) > 0 && (tab === 'all' || c.tab === tab)).map(c => {
		const cut = tab === 'all' && found > 1 && total(c.k) > ALL_CAP + 1;
		return { sec: c, total: total(c.k), rows: cut ? data[c.k].slice(0, ALL_CAP) : data[c.k], more: cut, head: tab !== 'books' };
	});
}

// a result that is a container: its own page of Music's or Videos' (they take it from the store), Back comes here
function openLib(g) {
	set({ libGoto: Object.assign({ back: location.hash }, g) });
	go(g.page === 'videos' ? '#/videos' : '#/music');
}
// from a page a result opened, back to the results (the entry before it when it is ours, else the address again)
export function backToResults(hash) { if (canGoBack(1)) history.back(); else go(hash || '#/search'); }
const playOne = p => playHere(t => playPaths([p], new Set(), t, true), p);

// the field: what is typed goes to the address a moment after the last key (the page asks the handheld from there)
let typing = 0;
export function SearchField({ auto, slash, cls }) {
	const s = useStore(), ref = useRef(null);
	const text = s.searchText != null ? s.searchText : (s.route.page === 'search' ? s.route.q || '' : '');
	const show = v => { const h = searchHash(v.trim()); if (store.route.page === 'search') replaceHash(h); else go(h); };
	const input = v => { set({ searchText: v, searchTab: 'all' }); clearTimeout(typing); typing = setTimeout(() => show(v), 180); };
	useEffect(() => { if (auto && ref.current && !text) ref.current.focus(); }, []);
	return html`<label class=${'find sfind' + (cls ? ' ' + cls : '')}><${Icon} n="search" k=${2} />
		<input class="field" ref=${ref} type="search" data-slash=${slash ? '1' : undefined} data-search="1" value=${text} placeholder="Search library" aria-label="Search library"
			autocomplete="off" autocapitalize="off" enterkeyhint="search" maxlength="120" onInput=${e => input(e.target.value)}
			onKeyDown=${e => { if (e.key === 'Enter') { clearTimeout(typing); show(e.target.value); if (slash) e.target.blur(); } else if (e.key === 'Escape') { if (text) input(''); else e.target.blur(); } }} /></label>`;
}

// the part of a name the query's first word matched, in yellow (the handheld's ACCENT mark)
function Marked({ text, q }) {
	const w = (q || '').trim().split(/\s+/)[0] || '', at = w ? text.toLowerCase().indexOf(w.toLowerCase()) : -1;
	if (at < 0) return text;
	return html`${text.slice(0, at)}<span class="y">${text.slice(at, at + w.length)}</span>${text.slice(at + w.length)}`;
}

function Row({ sec, x, q, now }) {
	const k = sec.k, chev = html`<${Icon} n="right" k=${2} cls="chev" />`;
	const row = (label, onClick, tile, name, sub, end, after, cur) => html`<li class=${'frow' + (cur ? ' cur' : '')}>
		<button class="main-btn" aria-label=${label} onClick=${onClick}>${tile}
			<span class="grow"><div class="fn ell"><${Marked} text=${name} q=${q} />${k === 'songs' && x.video && html` <span class="chip tiny">VIDEO</span>`}</div>${sub && html`<div class="fm ell">${sub}</div>`}</span>
			${end && html`<span class="dur">${end}</span>`}</button>${after}</li>`;
	const under = (f, one, many) => (f.in ? 'in ' + f.in + ' · ' : '') + plural(f.count, one, many);
	if (k === 'artists') return row('Open ' + x.name, () => openLib({ page: 'music', kind: 'artist', index: x.i }),
		html`<span class="tile round"><${Art} src="" seed=${x.name} kind="audio" /></span>`, x.name, plural(x.albums, 'album', 'albums') + ' · ' + plural(x.songs, 'song', 'songs'), '', chev);
	if (k === 'albums') return row('Open ' + x.name, () => openLib({ page: 'music', kind: 'album', index: x.i, song: x.song }),
		html`<span class="tile"><${Art} src=${coverUrl(x.song)} seed=${x.song} kind="audio" /></span>`, x.name, x.artist + ' · ' + plural(x.n, 'song', 'songs'), '', chev);
	if (k === 'musicFolders') return row('Open ' + x.n, () => openLib({ page: 'music', kind: 'folder', p: x.p }),
		html`<span class="tile folder"><${Icon} n="folder" k=${2} /></span>`, x.n, under(x, 'song', 'songs'), '', chev);
	if (k === 'videoFolders') return row('Open ' + x.n, () => openLib({ page: 'videos', kind: 'folder', p: x.p }),
		html`<span class="tile folder"><${Icon} n="folder" k=${2} /></span>`, x.n, under(x, 'video', 'videos'), '', chev);
	if (k === 'songs') return row('Play ' + x.title, () => playOne(x.p),
		html`<span class="tile"><${Art} src=${coverUrl(x.p)} seed=${x.album || x.p} kind="audio" /></span>`, x.title, [x.artist, x.album].filter(Boolean).join(' · '), x.dur > 0 ? fmtTime(x.dur) : '',
		html`<${FileMore} p=${x.p} name=${x.title} glyph="note" />`, x.p === now);
	if (k === 'videos') return row('Play ' + x.n, () => playOne(x.p),
		html`<span class="tile wide"><${Art} src=${vthumbUrl(x.p)} seed=${x.p} kind="video" k=${1} /></span>`, x.n, '', x.size ? fmtSize(x.size) : '',
		html`<${FileMore} p=${x.p} name=${x.n} glyph="film" />`, x.p === now);
	return row('Read: ' + x.n, () => readHere(x),
		html`<span class="tile cover"><${Art} src=${bookCoverUrl(x.p)} seed=${x.p} kind="book" k=${2} /></span>`, x.n, x.size ? fmtSize(x.size) : '', '', chev);
}

export function Search() {
	const s = useStore(), q = s.route.q || '', tab = s.searchTab || 'all';
	const [data, setData] = useState(null), [err, setErr] = useState('');
	// the address moved without the field (Back, a link): the field says what is searched
	useEffect(() => { if (store.searchText == null || store.searchText.trim() !== q) set({ searchText: q }); }, [q]);
	useEffect(() => {
		setErr('');
		if (!q) { setData(null); return; }
		let live = true;
		get('/api/search', { q }, { quiet: true }).then(d => { if (live) setData(d); }).catch(e => { if (live) setErr(errMsg(e)); });
		return () => { live = false; };
	}, [q, s.epoch, s.libTick]);
	const total = k => (data && data.totals && data.totals[k]) || 0;
	const count = t => SECS.filter(c => t === 'all' || c.tab === t).reduce((n, c) => n + total(c.k), 0);
	const secs = data && q ? searchSections(data, tab) : [];
	const now = s.snap && s.snap.path;
	const seeAll = c => { set({ searchTab: c.tab }); requestAnimationFrame(() => { const h = document.getElementById('sr-' + c.k); if (h) h.scrollIntoView({ block: 'start' }); }); };
	return html`<div class="page search">
		<div class="phead"><div class="row"><h1>Search</h1><${AppMenu} /></div>
			<${SearchField} auto=${true} slash=${true} cls="inpage" />
			${q && data && count('all') > 0 && html`<div class="chips" role="tablist" aria-label="Kinds">${TABS.map(([k, l]) => html`<button class="chipbtn" role="tab" aria-pressed=${tab === k} onClick=${() => set({ searchTab: k })}>${l} ${count(k)}</button>`)}</div>`}</div>
		${err ? html`<div class="banner box surface">${err}</div>`
			: !q ? html`<${Empty} icon="search" title="Search your library" sub="Find an artist, an album, a song, a video, a book, or a folder inside Music or Videos by any part of its name. A file anywhere else is in Files." />`
			: !data ? html`<${Skeleton} />`
			: data.gen === 0 ? html`<${Banner} title="Reading the cards…" />`
			: count('all') === 0 ? html`<${Empty} icon="search" title=${'Nothing called “' + q + '”'} sub="Your library was searched: artists, albums, songs, videos, books and the folders inside Music and Videos. Files' other folders are not." button="Browse Files" onClick=${() => go('#/files')} />`
			: secs.length === 0 ? html`<div class="banner box surface dim">Nothing in ${TABS.find(t => t[0] === tab)[1]} by that name</div>`
			: secs.map(g => html`<section key=${g.sec.k} aria-label=${g.sec.all}>
				${g.head && html`<div class="sr-head" id=${'sr-' + g.sec.k}><span class="kicker">${tab === 'all' ? g.sec.all : g.sec.own}</span><span class="num dim">${g.total}</span></div>`}
				<div class="box surface listbox"><ul class="flist">${g.rows.map(x => html`<${Row} key=${g.sec.k + (x.p || x.i)} sec=${g.sec} x=${x} q=${q} now=${now} />`)}</ul>
					${g.more && html`<button class="sr-more" onClick=${() => seeAll(g.sec)}>See all ${g.total} ${g.sec.many}<${Icon} n="right" k=${1} /></button>`}
					${!g.more && g.total > g.rows.length && html`<div class="sr-more dim">The first ${g.rows.length} of ${g.total}: type more of the name</div>`}</div></section>`)}
	</div>`;
}
