import { html, useState, useEffect } from '../vendor/htm-preact-standalone.module.js';
import { errMsg, get } from './api.js';
import { lsGet, lsSet, set, useStore } from './store.js';
import { fmtTime, plural } from './util.js';
import { AppMenu, Art, Banner, Empty, Skeleton, FileMore, coverUrl } from './components.js';
import { Icon } from './icons.js';
import { SectionUpload, uploadToSection } from './libupload.js';
import { Playlists } from './playlists.js';
import { playHere, playPaths, playRandom, view } from './browser-player.js';
import { backToResults } from './search.js';

/* ================= music ================= */
// The Music tab (docs/redesign "music-albums" and its web minis): the same three views the handheld has — the
// albums grid ("a click opens the album, with Play and Shuffle"), the artists, and the playlists ("no longer a
// tab of the dashboard: it is Music's third view there too"). The data is the index's (/api/library, its
// ?album= and ?artist= pages for the one under the tap).
const VIEWS = [
	{ k: 'albums', label: 'Albums' },
	{ k: 'artists', label: 'Artists' },
	{ k: 'folders', label: 'Folders' },
	{ k: 'playlists', label: 'Playlists' },
];

// Play and Shuffle for a whole list (an album's, an artist's): from the tapped song, or a random one with the
// session's own Shuffle on (playPaths turns the list into a play, as a playlist's)
async function playList(paths, at, shuffle) {
	const list = shuffle ? [] : paths;
	await playHere(t => playPaths(list.length ? list : paths, new Set(), t, true, shuffle, true, at), paths[Math.min(at || 0, paths.length - 1)]);
}

// the one under the tap: an album's or an artist's songs, with Play and Shuffle (docs/redesign "music-album")
function OneList({ data, kind, index, song, results, onBack }) {
	const s = useStore();
	const [page, setPage] = useState(null), [err, setErr] = useState('');
	useEffect(() => { setPage(null); setErr(''); get('/api/library', { [kind]: index }, { quiet: true }).then(setPage).catch(e => setErr(errMsg(e))); }, [kind, index, s.epoch]);
	const now = s.snap && s.snap.path;
	const play = (at, shuffle) => page && page.songs && playList(page.songs.filter(x => !x.gone).map(x => x.p), at, shuffle);
	return html`<div class="page">
		<div class="phead"><div class="row"><button class="ib" aria-label=${results ? 'Back to the results' : 'Back to Music'} title=${results ? 'Back to the results' : undefined} onClick=${onBack}><${Icon} n="left" k=${2} /></button><h1 class="ell">${page ? page.title : ''}</h1><${AppMenu} /></div></div>
		${err ? html`<div class="banner box surface">${err}</div>` : !page ? html`<${Skeleton} />` : html`
		<div class="onehead box surface">
			<span class="tile art-lg"><${Art} src=${kind === 'album' ? coverUrl(song || (data && data.albums[index] ? data.albums[index].song : '')) : ''} seed=${page.title} kind="audio" k=${4} /></span>
			<div class="grow"><h2 class="clamp2">${page.title}</h2><div class="dim" style="margin-top:6px">${page.sub}</div>
				<div class="acts" style="margin-top:14px"><button class="btn primary" onClick=${() => play(0, false)}><${Icon} n="play" k=${2} />Play</button>
					<button class="btn" onClick=${() => play(0, true)}><${Icon} n="shuffle" k=${2} />Shuffle</button></div></div></div>
		<div class="box surface listbox"><ul class="flist">${page.songs.map((x, i) => html`<li class=${'frow' + (x.p === now ? ' cur' : '')} key=${x.p}>
			<button class="main-btn" aria-label=${'Play ' + x.title} onClick=${() => play(i, false)}>
				<span class="tile xs dim-tile">${x.track || ''}</span>
				<span class="grow"><div class="fn ell">${x.title}${x.video && html` <span class="chip tiny">VIDEO</span>`}</div>
					${kind === 'artist' && x.album && html`<div class="fm ell">${x.album}</div>`}</span>
				<span class="dur">${x.dur > 0 ? fmtTime(x.dur) : ''}</span></button><${FileMore} p=${x.p} name=${x.title} glyph="note" /></li>`)}</ul></div>`}
	</div>`;
}

// the Folders view: the music folders on the cards, then folders inside folders, then the songs (the handheld's
// Music > Folders); a song plays with the folder's songs as the list
function MusicFolders() {
	const s = useStore();
	const [dir, setDir] = useState(lsGet('lib.mdir') || ''), [page, setPage] = useState(null), [err, setErr] = useState('');
	const go = d => { lsSet('lib.mdir', d); setDir(d); };
	useEffect(() => {
		setErr('');
		get('/api/library/music', dir ? { dir } : null, { quiet: true }).then(setPage).catch(e => { if (dir) go(''); else setErr(errMsg(e)); });
	}, [dir, s.epoch, s.libTick]);
	if (err) return html`<div class="banner box surface">${err}</div>`;
	if (!page) return html`<${Skeleton} />`;
	if (page.reading) return html`<${Banner} title="Reading the cards…" />`;
	const now = s.snap && s.snap.path, songs = (page.songs || []).filter(x => !x.gone);
	const label = f => dir ? f.n : (page.folders.length > 1 ? 'SD' + (+f.p.split('/')[0] + 1) + ' \u00b7 ' : '') + f.n;
	if (!page.folders.length && !songs.length) return html`<${Empty} icon="note" title="No music here" sub=${dir ? 'This folder has no songs.' : 'Music folders at the top of either card are listed here.'} />`;
	return html`<div class="box surface listbox"><ul class="flist">
		${dir && html`<li class="frow"><button class="main-btn" aria-label="Up one folder" onClick=${() => go(page.up || '')}><span class="tile folder"><${Icon} n="left" k=${2} /></span><span class="grow"><div class="fn ell">${page.name}</div><div class="fm">Up</div></span></button></li>`}
		${page.folders.map(f => html`<li class="frow" key=${f.p}><button class="main-btn" aria-label=${'Open ' + f.n} onClick=${() => go(f.p)}>
			<span class="tile folder"><${Icon} n="folder" k=${2} /></span>
			<span class="grow"><div class="fn ell">${label(f)}</div><div class="fm">${plural(f.count, 'song', 'songs')}</div></span></button><${Icon} n="right" k=${2} cls="chev" /></li>`)}
		${songs.map((x, i) => html`<li class=${'frow' + (x.p === now ? ' cur' : '')} key=${x.p}><button class="main-btn" aria-label=${'Play ' + x.title} onClick=${() => playList(songs.map(y => y.p), i, false)}>
			<span class="tile xs dim-tile">${x.track || ''}</span>
			<span class="grow"><div class="fn ell">${x.title}${x.video && html` <span class="chip tiny">VIDEO</span>`}</div>${x.album && html`<div class="fm ell">${x.album}</div>`}</span>
			<span class="dur">${x.dur > 0 ? fmtTime(x.dur) : ''}</span></button><${FileMore} p=${x.p} name=${x.title} glyph="note" /></li>`)}
	</ul></div>`;
}

export function Music() {
	const s = useStore();
	const [data, setData] = useState(null), [err, setErr] = useState('');
	const [vk, setVk] = useState(lsGet('lib.mview') || 'albums');
	const setView = k => { lsSet('lib.mview', k); setVk(k); setFrom(''); };
	const [one, setOne] = useState(null);	// { kind, index } while an album's or an artist's page is open
	// a result of Search's opened here (search.js's openLib): the album or the artist, or Folders at that folder;
	// `from` is the results' address while that folder shows (the header's way back)
	const [from, setFrom] = useState(''), [folderKey, setFolderKey] = useState(0);
	useEffect(() => {
		const g = s.libGoto;
		if (!g || g.page !== 'music') return;
		set({ libGoto: null });
		if (g.kind === 'folder') { lsSet('lib.mdir', g.p); lsSet('lib.mview', 'folders'); setVk('folders'); setOne(null); setFolderKey(k => k + 1); setFrom(g.back); }
		else setOne({ kind: g.kind, index: g.index, song: g.song, back: g.back });
	}, [s.libGoto]);
	useEffect(() => { setData(null); setErr(''); get('/api/library', null, { quiet: true }).then(setData).catch(e => setErr(errMsg(e))); }, [s.epoch]);
	useEffect(() => { if (s.libTick) get('/api/library', null, { quiet: true }).then(setData).catch(() => {}); }, [s.libTick]);
	const v = view();
	if (one) return html`<${OneList} data=${data} kind=${one.kind} index=${one.index} song=${one.song} results=${!!one.back} onBack=${() => { const b = one.back; setOne(null); if (b) backToResults(b); }} />`;
	if (err) return html`<div class="page"><div class="phead"><div class="row"><h1>Music</h1><${AppMenu} /></div></div><div class="banner box surface">${err}</div></div>`;
	if (!data) return html`<div class="page"><div class="phead"><div class="row"><h1>Music</h1><${AppMenu} /></div></div><${Skeleton} /></div>`;
	const reading = data.gen === 0;
	const noFolder = !reading && !data.musicFrom;
	const albums = data.albums || [], artists = data.artists || [];
	// the empty Music page's upload: the same as the header's (the card is asked, the folder is made)
	const upMusic = () => uploadToSection('music', data.cards);
	return html`<div class="page">
		<div class="phead"><div class="row">${from && vk === 'folders' && html`<button class="ib" aria-label="Back to the results" title="Back to the results" onClick=${() => backToResults(from)}><${Icon} n="left" k=${2} /></button>`}<h1>Music</h1><${SectionUpload} kind="music" cards=${data.cards} /><${AppMenu} /></div>
			<div class="chips" role="tablist" aria-label="Views">${VIEWS.map(w => html`<button class="chipbtn" role="tab" aria-pressed=${vk === w.k} onClick=${() => setView(w.k)}>${w.label} ${w.k === 'albums' ? albums.length : w.k === 'artists' ? artists.length : w.k === 'folders' ? '' : (s.pls || []).length}</button>`)}
				${!noFolder && !reading && vk !== 'playlists' && html`<span class="grow"></span><button class="btn sm" id="play-random" onClick=${playRandom} title="50 songs from your whole library, in a random order"><${Icon} n="shuffle" k=${2} />Play random</button>`}</div></div>
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
			: vk === 'folders' ? html`<${MusicFolders} key=${folderKey} />`
			: html`<${Playlists} />`}
	</div>`;
}
