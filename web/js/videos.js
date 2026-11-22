import { html, useState, useEffect, useMemo } from '../vendor/htm-preact-standalone.module.js';
import { errMsg, get } from './api.js';
import { lsGet, lsSet, set, useStore } from './store.js';
import { cmpName, dirName, fmtDate, fmtSize, fmtTime, plural } from './util.js';
import { AppMenu, Art, Banner, Empty, Skeleton, FileMore, vthumbUrl } from './components.js';
import { Icon } from './icons.js';
import { SectionUpload, uploadToSection } from './libupload.js';
import { playHere, playPaths, view } from './browser-player.js';
import { backToResults } from './search.js';

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
	const setView = k => { lsSet('lib.vview', k); setVk(k); setDir(''); setFrom(''); };
	const [dir, setDir] = useState('');	// the folder open in Folders ('' the top)
	// a folder Search's results opened (search.js's openLib): Folders at it; `from` is the results' address (the
	// header's way back) until another view is picked
	const [from, setFrom] = useState('');
	useEffect(() => {
		const g = s.libGoto;
		if (!g || g.page !== 'videos') return;
		set({ libGoto: null });
		lsSet('lib.vview', 'folders'); setVk('folders'); setDir(g.p); setFrom(g.back);
	}, [s.libGoto]);
	useEffect(() => { setData(null); setErr(''); get('/api/library', null, { quiet: true }).then(setData).catch(e => setErr(errMsg(e))); }, [s.epoch]);
	useEffect(() => { if (s.libTick) get('/api/library', null, { quiet: true }).then(setData).catch(() => {}); }, [s.libTick]);
	const v = view();
	const rows = useMemo(() => {
		if (!data || !data.videos) return [];
		if (vk === 'unfinished') return data.videos.filter(x => x.pos != null).sort((a, b) => a.at - b.at);
		if (vk === 'folders') {
			// the handheld's Folders: the Videos folders on the cards, then what is inside each (folders with how
			// many videos under them, then the videos directly in it). One Videos folder opens straight
			const roots = data.videoDirs || [];
			let at = dir;
			if (!at && roots.length === 1) at = roots[0];
			if (!at) return roots.map(r => ({ folder: true, p: r, n: (roots.length > 1 ? 'SD' + (+r.split('/')[0] + 1) + ' \u00b7 ' : '') + r.split('/').pop(), count: data.videos.filter(x => x.p.startsWith(r + '/')).length }));
			const sub = new Map(), loose = [];
			for (const x of data.videos) {
				if (!x.p.startsWith(at + '/')) continue;
				const rest = x.p.slice(at.length + 1), i = rest.indexOf('/');
				if (i < 0) { loose.push(x); continue; }
				const n = rest.slice(0, i);
				if (!sub.has(n)) sub.set(n, { folder: true, p: at + '/' + n, n, count: 0 });
				sub.get(n).count++;
			}
			return [...sub.values()].sort((a, b) => cmpName(a.n, b.n)).concat(loose);
		}
		return data.videos;
	}, [data, vk, dir]);
	const roots = (data && data.videoDirs) || [];
	const upDir = !dir ? null : roots.includes(dir) ? (roots.length > 1 ? '' : null) : dir.slice(0, dir.lastIndexOf('/'));	// null: already at the top
	const openFolder = f => setDir(f.p);
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
		<div class="phead"><div class="row">${from && vk === 'folders' && html`<button class="ib" aria-label="Back to the results" title="Back to the results" onClick=${() => backToResults(from)}><${Icon} n="left" k=${2} /></button>`}<h1>Videos</h1><${SectionUpload} kind="videos" cards=${data.cards} /><${AppMenu} /></div>
			<div class="chips" role="tablist" aria-label="Views">${VIEWS.map(w => html`<button class="chipbtn" role="tab" aria-pressed=${vk === w.k} onClick=${() => setView(w.k)}>${w.label}${counts[w.k] ? ' ' + counts[w.k] : ''}</button>`)}</div></div>
		${empty ? html`<${Empty} icon="film" title="No videos here yet" sub="Videos and Movies folders at the top of either card are listed here. Upload into Videos and they appear here." button="Upload to Videos" onClick=${() => uploadToSection('videos', data.cards)} />`
			: reading ? html`<${Banner} title="Reading the cards…" />`
			: html`<div class="box surface listbox"><ul class="flist">
				${vk === 'folders' && upDir != null && html`<li class="frow"><button class="main-btn" aria-label="Up one folder" onClick=${() => setDir(upDir)}><span class="tile wide folder"><${Icon} n="left" k=${2} /></span><span class="grow"><div class="fn ell">${dir.split('/').pop()}</div><div class="fm">Up</div></span></button></li>`}
				${rows.map(x => {
				const cur = x.p === now, part = x.pos != null;
				return html`<li class=${'frow' + (cur ? ' cur' : '')} key=${x.p}>
					<button class="main-btn" aria-label=${(x.folder ? 'Open ' : 'Play ') + x.n} onClick=${() => x.folder ? openFolder(x) : playRow(x)}>
						${x.folder ? html`<span class="tile wide folder"><${Icon} n="folder" k=${2} /></span>`
							: html`<span class="tile wide"><${Art} src=${vthumbUrl(x.p)} seed=${x.p} kind="video" k=${1} /></span>`}
						<span class="grow"><div class="fn ell">${x.n}${cur && html` <span class="chip y tiny"><${Icon} n="play" k=${1} />${v.paused ? 'Paused' : 'Playing'}</span>`}</div>
							<div class="fm">${x.folder ? '' : part ? (x.dur > 0 ? fmtTime(Math.max(0, x.dur - x.pos)) + ' left' : fmtTime(x.pos)) : x.mtime ? fmtDate(x.mtime) : ''}</div>
							${part && x.dur > 0 && html`<span class="mprog" aria-hidden="true"><i style=${'width:' + Math.round(100 * Math.min(1, x.pos / x.dur)) + '%'}></i></span>`}</span>
						<span class=${'dur' + (part ? ' y' : '')}>${x.folder ? plural(x.count, 'video', 'videos') : x.size ? fmtSize(x.size) : ''}</span></button>
					${x.folder ? html`<${Icon} n="right" k=${2} cls="chev" />` : html`<${FileMore} p=${x.p} name=${x.n} glyph="film" />`}</li>`;
			})}</ul></div>`}
	</div>`;
}
