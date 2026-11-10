import { html } from '../vendor/htm-preact-standalone.module.js';
import { notify, store, useStore } from './store.js';
import { errToast, post } from './api.js';
import { AppMenu } from './components.js';
import { Icon } from './icons.js';

/* ================= settings ================= */
// the manga reader's switch, in the dashboard's Manage extensions card: it is the handheld's own setting, so the
// page sends it and the handheld applies it (the other switches there work the same way)
function setManga(on) {
	const sn = store.snap, was = sn && sn.mangaOn;
	if (sn) { sn.mangaOn = on; notify(); }
	post('/api/setting', { key: 'manga-reader', value: on ? 1 : 0 }).catch(e => {
		if (sn) { sn.mangaOn = was; notify(); }	// (the handheld didn't take it: the switch goes back)
		errToast(e);
	});
}

export function Settings() {
	const s = useStore(), sn = s.snap, on = !!(sn && sn.mangaOn);
	return html`<div class="page">
		<div class="phead"><div class="row"><h1>Settings</h1><${AppMenu} /></div></div>
		<section class="box surface card" aria-label="Manage extensions">
			<div class="card-head"><h2>Manage extensions</h2></div>
			<div class="line">
				<span class="tt"><span class="ell" style="display:block">Manga reader</span><span class="m">Comics and manga from files and servers</span></span>
				<button class=${'ib sm' + (on ? ' on' : '')} role="switch" aria-checked=${on} aria-label="Manga reader" title=${on ? 'Manga reader: On' : 'Manga reader: Off'}
					disabled=${!sn} onClick=${() => setManga(!on)}><${Icon} n=${on ? 'box-on' : 'box'} k=${2} /></button>
			</div>
			<p class="note">Repositories are added by you. Banana Player doesn’t provide or check their content.</p>
		</section></div>`;
}
