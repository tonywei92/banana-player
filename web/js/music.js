import { html, useState, useEffect } from '../vendor/htm-preact-standalone.module.js';
import { errMsg, get } from './api.js';
import { lsGet, lsSet, useStore } from './store.js';
import { fmtTime, plural } from './util.js';
import { AppMenu, Art, Banner, Empty, Skeleton, coverUrl } from './components.js';
import { Icon } from './icons.js';
import { SectionUpload, uploadToSection } from './libupload.js';
import { Playlists } from './playlists.js';
import { playHere, playPaths, view } from './browser-player.js';

/* ================= music ================= */
// The Music tab (docs/redesign "music-albums" and its web minis): the same three views the handheld has — the
// albums grid ("a click opens the album, with Play and Shuffle"), the artists, and the playlists ("no longer a
// tab of the dashboard: it is Music's third view there too"). The data is the index's (/api/library, its
// ?album= and ?artist= pages for the one under the tap).
const VIEWS = [
	{ k: 'albums', label: 'Albums' },
	{ k: 'artists', label: 'Artists' },
	{ k: 'playlists', label: 'Playlists' },
];

// Play and Shuffle for a whole list (an album's, an artist's): from the tapped song, or a random one with the
// session's own Shuffle on (playPaths turns the list into a play, as a playlist's)
async function playList(paths, at, shuffle) {
	const list = shuffle ? [] : paths;
	await playHere(t => playPaths(list.length ? list : paths, new Set(), t, true, shuffle), paths[Math.min(at || 0, paths.length - 1)]);
}

// the one under the tap: an album's or an artist's songs, with Play and Shuffle (docs/redesign "music-album")
function OneList({ data, kind, index, onBack }) {
	const s = useStore();
	const [page, setPage] = useState(null), [err, setErr] = useState('');
	useEffect(() => { setPage(null); setErr(''); get('/api/library', { [kind]: index }, { quiet: true }).then(setPage).catch(e => setErr(errMsg(e))); }, [kind, index, s.epoch]);
	const now = s.snap && s.snap.path;
	const play = (at, shuffle) => page && page.songs && playList(page.songs.filter(x => !x.gone).map(x => x.p), at, shuffle);
	return html`<div class="page">
		<div class="phead"><div class="row"><button class="ib" aria-label="Back to Music" onClick=${onBack}><${Icon} n="left" k=${2} /></button><h1 class="ell">${page ? page.title : ''}</h1><${AppMenu} /></div></div>
		${err ? html`<div class="banner box surface">${err}</div>` : !page ? html`<${Skeleton} />` : html`
		<div class="onehead box surface">
			<span class="tile art-lg"><${Art} src=${kind === 'album' && data && data.albums[index] ? coverUrl(data.albums[index].song) : ''} seed=${page.title} kind="audio" k=${4} /></span>
			<div class="grow"><h2 class="clamp2">${page.title}</h2><div class="dim" style="margin-top:6px">${page.sub}</div>
				<div class="acts" style="margin-top:14px"><button class="btn primary" onClick=${() => play(0, false)}><${Icon} n="play" k=${2} />Play</button>
					<button class="btn" onClick=${() => play(0, true)}><${Icon} n="shuffle" k=${2} />Shuffle</button></div></div></div>
		<div class="box surface listbox"><ul class="flist">${page.songs.map((x, i) => html`<li class=${'frow' + (x.p === now ? ' cur' : '')} key=${x.p}>
			<button class="main-btn" aria-label=${'Play ' + x.title} onClick=${() => play(i, false)}>
				<span class="tile xs dim-tile">${x.track || ''}</span>
				<span class="grow"><div class="fn ell">${x.title}${x.video && html` <span class="chip tiny">VIDEO</span>`}</div>
					${kind === 'artist' && x.album && html`<div class="fm ell">${x.album}</div>`}</span>
				<span class="dur">${x.dur > 0 ? fmtTime(x.dur) : ''}</span></button></li>`)}</ul></div>`}
	</div>`;
}

export function Music() {
	const s = useStore();
	const [data, setData] = useState(null), [err, setErr] = useState('');
	const [vk, setVk] = useState(lsGet('lib.mview') || 'albums');
	const setView = k => { lsSet('lib.mview', k); setVk(k); };
	const [one, setOne] = useState(null);	// { kind, index } while an album's or an artist's page is open
	useEffect(() => { setData(null); setErr(''); get('/api/library', null, { quiet: true }).then(setData).catch(e => setErr(errMsg(e))); }, [s.epoch]);
	const v = view();
	if (one) return html`<${OneList} data=${data} kind=${one.kind} index=${one.index} onBack=${() => setOne(null)} />`;
	if (err) return html`<div class="page"><div class="phead"><div class="row"><h1>Music</h1><${AppMenu} /></div></div><div class="banner box surface">${err}</div></div>`;
	if (!data) return html`<div class="page"><div class="phead"><div class="row"><h1>Music</h1><${AppMenu} /></div></div><${Skeleton} /></div>`;
	const reading = data.gen === 0;
	const noFolder = !reading && !data.musicFrom;
	const albums = data.albums || [], artists = data.artists || [];
	// the empty Music page's upload: the same as the header's (the card is asked, the folder is made)
	const upMusic = () => uploadToSection('music', data.cards);
	return html`<div class="page">
		<div class="phead"><div class="row"><h1>Music</h1><${SectionUpload} kind="music" cards=${data.cards} /><${AppMenu} /></div>
			<div class="chips" role="tablist" aria-label="Views">${VIEWS.map(w => html`<button class="chipbtn" role="tab" aria-pressed=${vk === w.k} onClick=${() => setView(w.k)}>${w.label} ${w.k === 'albums' ? albums.length : w.k === 'artists' ? artists.length : (s.pls || []).length}</button>`)}</div></div>
		${noFolder ? html`<${Empty} icon="note" title="No music here yet" sub="Albums and artists come from a folder named Music at the top of either card. Upload into it and they appear here." button="Upload to Music" onClick=${upMusic} />`
			: reading ? html`<${Banner} title="Reading the cards…" />`
			: vk === 'albums' ? html`<div class="lib-grid">${albums.map((a, i) => html`<button class="lib-tile" key=${a.i} onClick=${() => setOne({ kind: 'album', index: a.i })} aria-label=${'Open ' + a.name}>
				<span class="tile art-sq"><${Art} src=${coverUrl(a.song)} seed=${a.song} kind="audio" k=${5} /></span>
				<span class="lt ell">${a.name}</span><span class="lm ell">${a.artist}</span></button>`)}</div>`
			: vk === 'artists' ? html`<div class="box surface listbox"><ul class="flist">${artists.map((a, i) => html`<li class="frow" key=${a.name}>
					<button class="main-btn" aria-label=${'Open ' + a.name} onClick=${() => setOne({ kind: 'artist', index: i })}>
						<span class="tile"><${Art} src="" seed=${a.name} kind="audio" /></span>
						<span class="grow"><div class="fn ell">${a.name}</div><div class="fm">${plural(a.albums, 'album', 'albums')} \u00b7 ${plural(a.songs, 'song', 'songs')}</div></span></button>
					<${Icon} n="right" k=${2} cls="chev" /></li>`)}</ul></div>`
			: html`<${Playlists} />`}
	</div>`;
}
