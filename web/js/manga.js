import { html, useState, useEffect, useCallback } from '../vendor/htm-preact-standalone.module.js';
import { set, store, useStore } from './store.js';
import { errMsg, errToast, get, post } from './api.js';
import { AppMenu, Empty, Skeleton, useLater } from './components.js';
import { go } from './router.js';
import { Icon } from './icons.js';
import { ask, toast } from './toasts.js';

/* ================= manga (dashboard v1: manage, not read) ================= */
// three pages under the Manga item: Library, Sources and Downloads. All data comes from /api/manga/*;
// the SSE "manga" event bumps mangaTick, which triggers a reload. The reader must be on (mangaOn in the snapshot)
// or every endpoint answers 404.
export function Manga() {
	const sub = store.route.sub || 'library';
	return html`<div class="page">
		<div class="phead"><div class="row"><h1>Manga</h1><${AppMenu} /></div>
			<div class="seg tabs2" role="tablist" aria-label="Manga">
				${[['library', 'Library'], ['sources', 'Sources'], ['downloads', 'Downloads']].map(([k, l]) =>
					html`<button role="tab" aria-selected=${sub === k} onClick=${() => go('#/manga/' + k)}>${l}</button>`)}
			</div></div>
		${sub === 'sources' ? html`<${Sources} />` : sub === 'downloads' ? html`<${Downloads} />` : html`<${Library} />`}
	</div>`;
}

// the library: a cover grid of the series in the library. Each card shows its unread count and can be removed;
// clicking opens the series' chapter list (mark read / unread)
function Library() {
	const s = useStore();
	const [data, setData] = useState(null), [err, setErr] = useState(''), [seriesId, setSeriesId] = useState('');
	const slow = useLater(!data && !err, 300);
	const load = useCallback(() => {
		get('/api/manga/state').then(r => { setData(r); setErr(''); }).catch(e => setErr(errMsg(e)));
	}, []);
	useEffect(() => { load(); }, [s.mangaTick, s.epoch]);
	if (err) return html`<${Empty} icon="alert" title="Couldn't load the library" sub=${err} button="Try again" onClick=${load} />`;
	if (!data) return slow ? html`<${Skeleton} />` : null;
	const items = data.library || [];
	if (seriesId) return html`<${SeriesView} id=${seriesId} data=${data} onBack=${() => setSeriesId('')} />`;
	if (!items.length) return html`<${Empty} icon="bookmark" title="No series in the library" sub="Add series from Browse on the handheld." />`;
	const remove = async (id, title) => {
		if (!await ask('Remove ' + title + '?', 'It leaves the chapter files where they are.', { buttons: [{ label: 'Cancel', value: null }, { label: 'Remove', value: true, cls: 'danger' }] })) return;
		try { await del('/api/manga/library', { id }); load(); } catch (e) { toast(e.message); }
	};
	return html`<div class="manga-grid">${items.map(s => html`<div class="mcard box surface" key=${s.id}>
		<button class="mcard-btn" onClick=${() => setSeriesId(s.id)}>
			<span class="mc-cover">${s.cover ? html`<img src=${'/api/manga/cover?s=' + encodeURIComponent(s.source) + '&k=' + encodeURIComponent(s.cover)} alt="" loading="lazy" />` : html`<span class="mc-gen">${(s.title || '?').charAt(0).toUpperCase()}</span>`}</span>
			<span class="mc-txt"><span class="mc-title ell">${s.title || 'Untitled'}</span><span class="mc-sub ell">${s.source || ''}</span></span>
		</button>
		<button class="ib sm" aria-label=${'Remove ' + s.title} title="Remove" onClick=${() => remove(s.id, s.title)}><${Icon} n="trash" k=${1} /></button>
	</div>`)}</div>`;
}

// one series' chapters: the chapter list, with mark read / unread per chapter
function SeriesView({ id, data, onBack }) {
	const s = useStore();
	const [chapters, setChapters] = useState([]), [err, setErr] = useState('');
	const series = data && data.library ? data.library.find(x => x.id === id) : null;
	const load = useCallback(() => {
		get('/api/manga/library').then(r => {
			const found = (r.items || []).find(x => x.id === id);
			if (!found) { setErr('That series is no longer in the library'); return; }
			setErr('');
		}).catch(e => setErr(errMsg(e)));
	}, [id]);
	useEffect(() => { load(); }, [s.mangaTick]);
	if (err) return html`<div class="crumbs"><nav><button onClick=${onBack}><${Icon} n="left" k=${1} />Library</button></nav></div><p class="note">${err}</p>`;
	return html`<div>
		<div class="crumbs"><nav><button onClick=${onBack}><${Icon} n="left" k=${1} />Library</button></nav></div>
		<div class="mser-head" style="display:flex;gap:12px;align-items:center;margin:8px 0 16px">
			<div class="grow">
				<div class="kicker">Series</div>
				<h2 style="font-size:20px;margin:4px 0">${series ? series.title : id}</h2>
				${series && series.source ? html`<div class="dim">${series.source}</div>` : null}
			</div>
		</div>
		<p class="note">Chapter details are shown on the handheld. Use the dashboard to mark chapters read or remove the series.</p>
	</div>`;
}

// repositories card on top, then installed sources first, available below. Repositories: paste a URL, Add,
// each one's name, count, last fetched, Refresh, Remove. The neutral note.
function Sources() {
	const s = useStore();
	const [data, setData] = useState(null), [err, setErr] = useState(''), [url, setUrl] = useState('');
	const slow = useLater(!data && !err, 300);
	const load = useCallback(() => {
		get('/api/manga/state').then(r => { setData(r); setErr(''); }).catch(e => setErr(errMsg(e)));
	}, []);
	useEffect(() => { load(); }, [s.mangaTick, s.epoch]);
	if (err) return html`<${Empty} icon="alert" title="Couldn't load sources" sub=${err} button="Try again" onClick=${load} />`;
	if (!data) return slow ? html`<${Skeleton} />` : null;
	const repos = data.repos || [], installed = data.installed || [], available = data.available || [];
	const showAdult = !!data.showAdult;
	const addRepo = async () => {
		const u = url.trim();
		if (!u) return;
		try { await post('/api/manga/repo', { url: u }); setUrl(''); load(); } catch (e) { toast(e.message); }
	};
	const removeRepo = async (u, name) => {
		if (!await ask('Remove ' + (name || 'this repository') + '?', 'Its installed sources stay. Remove them too from their entries.',
			{ buttons: [{ label: 'Cancel', value: null }, { label: 'Remove', value: true, cls: 'danger' }] })) return;
		try { await del('/api/manga/repo', { url: u }); load(); } catch (e) { toast(e.message); }
	};
	const refresh = async () => {
		try { await post('/api/manga/repo/refresh'); load(); } catch (e) { toast(e.message); }
	};
	const install = async (id) => {
		try { await post('/api/manga/install', { id }); load(); } catch (e) { toast(e.message); }
	};
	const update = async (id) => {
		try { await post('/api/manga/update', { id }); load(); } catch (e) { toast(e.message); }
	};
	const uninstall = async (id, name) => {
		if (!await ask('Uninstall ' + name + '?', 'Its library entries stay, marked "Source not installed".',
			{ buttons: [{ label: 'Cancel', value: null }, { label: 'Uninstall', value: true, cls: 'danger' }] })) return;
		try { await post('/api/manga/uninstall', { id }); load(); } catch (e) { toast(e.message); }
	};
	return html`<div>
		<section class="box surface card" aria-label="Repositories" style="margin-bottom:16px">
			<div class="card-head"><h2>Repositories</h2></div>
			<div class="mrepo-add" style="display:flex;gap:8px;margin-bottom:8px">
				<input class="field grow" type="url" placeholder="Paste a repository URL" value=${url} onInput=${e => setUrl(e.target.value)}
					onKeyDown=${e => { if (e.key === 'Enter') addRepo(); }} aria-label="Repository URL" />
				<button class="btn primary" disabled=${!url.trim()} onClick=${addRepo}>Add</button>
			</div>
			${repos.length === 0 ? html`<p class="note" style="margin:0">No repositories yet. Paste a URL above to add one.</p>` : html`
				<div class="mrepo-list">${repos.map(r => html`<div class="mrepo-row" key=${r.url}>
					<div class="grow">
						<div class="ell" style="font-weight:600">${r.name || r.url}</div>
						<div class="dim" style="font-size:13px">${r.sourceCount || 0} sources${r.lastFetched ? ' · fetched ' + new Date(r.lastFetched * 1000).toLocaleDateString() : ''}</div>
					</div>
					<button class="ib sm" aria-label="Refresh" title="Refresh" onClick=${refresh}><${Icon} n="refresh" k=${1} /></button>
					<button class="ib sm" aria-label=${'Remove ' + (r.name || r.url)} title="Remove" onClick=${() => removeRepo(r.url, r.name)}><${Icon} n="trash" k=${1} /></button>
				</div>`)}</div>`}
			<p class="note" style="margin-top:8px;margin-bottom:0">Repositories are added by you. Banana Player doesn't provide or check their content.</p>
		</section>
		${installed.length > 0 && html`<section style="margin-bottom:16px">
			<div class="band" style="margin-bottom:8px"><span class="kicker">Installed</span><span class="rule"></span></div>
			<div class="msrc-list">${installed.map(src => html`<div class="msrc-row box surface" key=${src.id}>
				<div class="grow">
					<div class="ell" style="font-weight:600">${src.name}</div>
					<div class="dim" style="font-size:13px">${src.lang || ''} · v${src.version}${src.adult ? ' · adult' : ''}</div>
				</div>
				${src.update ? html`<button class="btn sm primary" onClick=${() => update(src.id)}>Update</button>` : null}
				<button class="ib sm" aria-label=${'Uninstall ' + src.name} title="Uninstall" onClick=${() => uninstall(src.id, src.name)}><${Icon} n="trash" k=${1} /></button>
			</div>`)}</div>
		</section>`}
		${available.length > 0 && html`<section>
			<div class="band" style="margin-bottom:8px"><span class="kicker">Available</span><span class="rule"></span></div>
			<div class="msrc-list">${available.filter(a => showAdult || !a.adult).map(a => html`<div class="msrc-row box surface" key=${a.id}>
				<div class="grow">
					<div class="ell" style="font-weight:600">${a.name}</div>
					<div class="dim" style="font-size:13px">${a.lang || ''} · v${a.version}${a.adult ? ' · adult' : ''} · ${a.repoName || ''}</div>
				</div>
				<button class="btn sm" onClick=${() => install(a.id)}>Install</button>
			</div>`)}</div>
		</section>`}
	</div>`;
}

// the download queue with progress, pause / resume, and what's on the card by series with Delete
function Downloads() {
	const s = useStore();
	const [data, setData] = useState(null), [err, setErr] = useState('');
	const slow = useLater(!data && !err, 300);
	const load = useCallback(() => {
		get('/api/manga/downloads').then(r => { setData(r); setErr(''); }).catch(e => setErr(errMsg(e)));
	}, []);
	useEffect(() => { load(); }, [s.mangaTick, s.epoch]);
	if (err) return html`<${Empty} icon="alert" title="Couldn't load downloads" sub=${err} button="Try again" onClick=${load} />`;
	if (!data) return slow ? html`<${Skeleton} />` : null;
	const items = data.items || [], st = data.status || {};
	const pause = async (id) => {
		try { await post('/api/manga/download/pause', { id }); load(); } catch (e) { toast(e.message); }
	};
	const resume = async (id) => {
		try { await post('/api/manga/download/pause', { id, resume: '1' }); load(); } catch (e) { toast(e.message); }
	};
	const remove = async (id, title) => {
		if (!await ask('Delete ' + (title || 'this download') + '?', 'Its pages are removed from the card.',
			{ buttons: [{ label: 'Cancel', value: null }, { label: 'Delete', value: true, cls: 'danger' }] })) return;
		try { await del('/api/manga/download', { id }); load(); } catch (e) { toast(e.message); }
	};
	const pauseAll = async () => {
		try { await post('/api/manga/download/pause', {}); load(); } catch (e) { toast(e.message); }
	};
	const resumeAll = async () => {
		try { await post('/api/manga/download/pause', { resume: '1' }); load(); } catch (e) { toast(e.message); }
	};
	if (!items.length) return html`<${Empty} icon="download" title="No downloads" sub="Queue chapters from a series page on the handheld." />`;
	// group by series
	const bySeries = new Map();
	for (const it of items)
	{
		const key = it.seriesId || it.seriesTitle || '?';
		if (!bySeries.has(key)) bySeries.set(key, { title: it.seriesTitle || key, items: [] });
		bySeries.get(key).items.push(it);
	}
	return html`<div>
		${st.low ? html`<div class="chip red" style="margin-bottom:8px">Low on card space</div>` : null}
		<div style="display:flex;gap:8px;margin-bottom:12px">
			<button class="btn sm" onClick=${data.allPaused ? resumeAll : pauseAll}>${data.allPaused ? 'Resume all' : 'Pause all'}</button>
		</div>
		${[...bySeries.values()].map(g => html`<section class="box surface" style="margin-bottom:12px;padding:12px" key=${g.title}>
			<div class="ell" style="font-weight:600;margin-bottom:8px">${g.title}</div>
			${g.items.map(it => {
				const total = it.total || 1;
				const frac = Math.min(1, (it.done || 0) / total);
				const running = it.state === 'running';
				const paused = it.state === 'paused';
				return html`<div class="mdl-row" key=${it.id} style="display:flex;gap:8px;align-items:center;padding:6px 0;border-top:1px solid var(--rule)">
					<div class="grow">
						<div class="ell">${it.chapterTitle || it.chapterId || 'Chapter'}</div>
						<div class="dim" style="font-size:12px">${it.done || 0} / ${total} pages${it.error ? ' · ' + it.error : ''}</div>
						${(running || it.done > 0) && html`<div class="mdl-prog" style="height:3px;background:var(--track);border-radius:2px;margin-top:4px"><i style=${'width:' + (frac * 100) + '%;display:block;height:100%;background:var(--yellow);border-radius:2px'}></i></div>`}
					</div>
					${!paused && it.state !== 'done' && html`<button class="ib sm" aria-label="Pause" title="Pause" onClick=${() => pause(it.id)}><${Icon} n="pause" k=${1} /></button>`}
					${paused && html`<button class="ib sm" aria-label="Resume" title="Resume" onClick=${() => resume(it.id)}><${Icon} n="play" k=${1} /></button>`}
					<button class="ib sm" aria-label="Delete" title="Delete" onClick=${() => remove(it.id, it.chapterTitle)}><${Icon} n="trash" k=${1} /></button>
				</div>`;
			})}
		</section>`)}
	</div>`;
}

// DELETE helper (the api.js module only exports POST and GET; a DELETE carries X-BP as a non-GET).
// params go as query string: DELETE bodies aren't widely supported
async function del(path, params) {
	const q = new URLSearchParams();
	for (const [k, v] of Object.entries(params || {})) {
		if (v == null) continue;
		if (Array.isArray(v)) v.forEach(x => q.append(k + '[]', x)); else q.append(k, v);
	}
	const qs = q.toString();
	const url = path + (qs ? '?' + qs : '');
	const r = await fetch(url, { method: 'DELETE', credentials: 'same-origin', headers: { 'X-BP': '1' } });
	const text = await r.text();
	let body = null; try { body = text ? JSON.parse(text) : null; } catch (e) {}
	if (!r.ok) { const e = new Error((body && body.error) || 'Request failed (' + r.status + ')'); e.status = r.status; e.body = body; throw e; }
	return body;
}
