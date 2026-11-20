import { html, useState, useEffect, useMemo } from '../vendor/htm-preact-standalone.module.js';
import { errMsg, get } from './api.js';
import { lsGet, lsSet, useStore } from './store.js';
import { cmpName, dirName, fmtDate, fmtSize, fmtTime, plural } from './util.js';
import { AppMenu, Art, Banner, Empty, Skeleton, coverUrl } from './components.js';
import { Icon } from './icons.js';
import { go } from './router.js';
import { playHere, playPaths, view } from './browser-player.js';

/* ================= videos ================= */
// The Videos tab (docs/redesign "videos"): the cards' films in one list, the same three views the handheld has,
// and a click plays "in the browser or on the handheld, as from Files" — with the view as the list, as the
// handheld's own Play does (Next goes to the next row). The data is the index's (/api/library).
const VIEWS = [
	{ k: 'all', label: 'All' },
	{ k: 'folders', label: 'Folders' },
	{ k: 'unfinished', label: 'Not finished' },
];

// the view's own list, the row under the finger first (so Next steps the view in its own order)
const asList = (paths, at) => at > 0 ? paths.slice(at).concat(paths.slice(0, at)) : paths;

export function Videos() {
	const s = useStore();
	const [data, setData] = useState(null), [err, setErr] = useState('');
	const [vk, setVk] = useState(lsGet('lib.vview') || 'all');
	const setView = k => { lsSet('lib.vview', k); setVk(k); };
	useEffect(() => { setData(null); setErr(''); get('/api/library', null, { quiet: true }).then(setData).catch(e => setErr(errMsg(e))); }, [s.epoch]);
	const v = view();
	const rows = useMemo(() => {
		if (!data || !data.videos) return [];
		if (vk === 'unfinished') return data.videos.filter(x => x.pos != null).sort((a, b) => a.at - b.at);
		if (vk === 'folders') {
			// the folders directly inside the Videos folders, each with how much is under it, then the loose
			// files directly inside them — the handheld's own Folders view
			const byFolder = new Map(), loose = [];
			for (const x of data.videos) {
				const d = dirName(x.p), root = d.includes('/') ? d.split('/')[0] : d;
				const rel = d.slice(root.length + 1);
				if (!rel) { loose.push(x); continue; }
				const folder = root + '/' + rel.split('/')[0];
				if (!byFolder.has(folder)) byFolder.set(folder, { folder: true, p: folder, n: rel.split('/')[0], count: 0 });
				byFolder.get(folder).count++;
			}
			return [...byFolder.values()].sort((a, b) => cmpName(a.n, b.n)).concat(loose);
		}
		return data.videos;
	}, [data, vk]);
	const openFolder = f => go('#/files/' + f.p.split('/').map(encodeURIComponent).join('/'));
	const playRow = x => {
		// the view is the list: the All view every video, Folders the loose files shown, Not finished those
		const list = vk === 'all' ? data.videos.map(r => r.p) : rows.filter(r => !r.folder).map(r => r.p);
		const at = list.indexOf(x.p);
		playHere(t => playPaths(asList(list, at >= 0 ? at : 0), new Set(), t, true), x.p);
	};
	if (err) return html`<div class="page"><div class="phead"><div class="row"><h1>Videos</h1><${AppMenu} /></div></div><div class="banner box surface">${err}</div></div>`;
	if (!data) return html`<div class="page"><div class="phead"><div class="row"><h1>Videos</h1><${AppMenu} /></div></div><${Skeleton} /></div>`;
	const reading = data.gen === 0;
	const empty = !reading && (!data.videos || !data.videos.length);
	const nFolders = vk === 'folders' && rows.filter(r => r.folder).length;
	const counts = { all: data.videos ? data.videos.length : 0, folders: nFolders, unfinished: vk === 'unfinished' ? rows.length : data.videos ? data.videos.filter(x => x.pos != null).length : 0 };
	const now = s.snap && s.snap.path;
	return html`<div class="page">
		<div class="phead"><div class="row"><h1>Videos</h1><${AppMenu} /></div>
			<div class="chips" role="tablist" aria-label="Views">${VIEWS.map(w => html`<button class="chipbtn" role="tab" aria-pressed=${vk === w.k} onClick=${() => setView(w.k)}>${w.label}${counts[w.k] ? ' ' + counts[w.k] : ''}</button>`)}</div></div>
		${empty ? html`<${Empty} icon="film" title="No videos here yet" sub="Videos and Movies folders at the top of either card are listed here." button="Open Files" onClick=${() => go('#/files')} />`
			: reading ? html`<${Banner} title="Reading the cards…" />`
			: html`<div class="box surface listbox"><ul class="flist">${rows.map(x => {
				const cur = x.p === now, part = x.pos != null;
				return html`<li class=${'frow' + (cur ? ' cur' : '')} key=${x.p}>
					<button class="main-btn" aria-label=${(x.folder ? 'Open ' : 'Play ') + x.n} onClick=${() => x.folder ? openFolder(x) : playRow(x)}>
						${x.folder ? html`<span class="tile folder"><${Icon} n="folder" k=${2} /></span>`
							: html`<span class="tile"><${Art} src=${coverUrl(x.p)} seed=${x.p} kind="video" /><span class="mark"><${Icon} n="film" k=${1} /></span></span>`}
						<span class="grow"><div class="fn ell">${x.n}${cur && html` <span class="chip y tiny"><${Icon} n="play" k=${1} />${v.paused ? 'Paused' : 'Playing'}</span>`}</div>
							<div class="fm">${x.folder ? '' : x.pos != null ? fmtTime(x.pos) + (x.dur > 0 ? ' / ' + fmtTime(x.dur) : '') : x.mtime ? fmtDate(x.mtime) : ''}</div></span>
						<span class=${'dur' + (part ? ' y' : '')}>${x.folder ? plural(x.count, 'video', 'videos') : x.size ? fmtSize(x.size) : ''}</span></button>
					${x.folder && html`<${Icon} n="right" k=${2} cls="chev" />`}</li>`;
			})}</ul></div>`}
	</div>`;
}
