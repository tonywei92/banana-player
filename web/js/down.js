import { html, useEffect, useRef } from '../vendor/htm-preact-standalone.module.js';
import { store } from './store.js';
import { Art, HandheldArt, useTick } from './components.js';
import { B } from './browser-player.js';
import { fmtTime } from './util.js';
import { probe } from './connection.js';
import { uploading } from './uploads.js';
import { useHere } from './onetab.js';
import { Icon } from './icons.js';

/* ================= down screen: the handheld closed, off, asleep or out of reach ================= */
const DOWN = {
	quit: ['Banana Player was closed on your handheld', 'Open Banana Player on your handheld. This page reconnects by itself.'],
	off: ['The web dashboard was turned off', 'Turn it on again on your handheld: START › Settings › Web dashboard. This page reconnects by itself.'],
	sleep: ['Your handheld is asleep', 'Open its lid to carry on. This page reconnects by itself.'],
	none: ["Can't reach your handheld", 'It may be asleep, or off this Wi-Fi. This page reconnects by itself.'],
};
// (all drawn here: when the handheld is gone, nothing more can be fetched from it)
export function DownScreen() {
	const s = store; useTick(1000);
	const [t, sub] = DOWN[s.downReason] || DOWN.none;
	const left = Math.max(0, Math.ceil((s.nextTry - Date.now()) / 1000));
	const trying = s.probing || !left;
	const ref = useRef();
	useEffect(() => { ref.current && ref.current.focus(); }, [s.downReason]);
	const sleep = s.downReason === 'sleep';
	return html`<main class="down" aria-labelledby="down-t"><div class="downbox">
		<div class="zz"><${HandheldArt} mode="off" />${sleep && html`<span style="right:-6px;top:-4px" aria-hidden="true">z</span><span style="right:-18px;top:-14px;animation-delay:.8s" aria-hidden="true">Z</span>`}</div>
		<div class="kicker">Disconnected</div>
		<h1 id="down-t" tabindex="-1" ref=${ref}>${t}</h1>
		<p class="down-sub">${sub}</p>
		${B.meta && B.parkedPlaying && html`<div class="box surface down-card"><span class="tile"><${Art} seed=${B.meta.p} kind=${B.meta.kind} /></span><div class="grow"><div class="t">${B.meta.title}</div><div class="dim" style="font-size:13px">Was playing here · stopped at <span class="num">${fmtTime(B.parked)}</span></div></div></div>`}
		<p class="down-status" role="status" aria-live="polite"><span class="wdots" aria-hidden="true"><i></i><i></i><i></i></span>${trying ? 'Trying again…' : 'Next try in ' + fmtTime(left)}</p>
		<button class="btn" disabled=${s.probing} onClick=${() => probe()}><${Icon} n="refresh" k=${2} />Try again</button>
	</div></main>`;
}

/* ================= another tab has it: one dashboard per browser (onetab.js) ================= */
export function ElsewhereScreen() {
	const s = store;
	const ref = useRef();
	useEffect(() => { ref.current && ref.current.focus(); }, []);
	const left = s.upload.items.filter(i => i.state === 'up' || i.state === 'queued' || i.state === 'waiting').length;
	return html`<main class="down" aria-labelledby="else-t"><div class="downbox">
		<div class="zz"><${HandheldArt} mode="off" /></div>
		<div class="kicker">Already open</div>
		<h1 id="else-t" tabindex="-1" ref=${ref}>Banana Player is open in another tab</h1>
		<p class="down-sub">Only one tab at a time can be connected to your handheld. Switch here? The other tab steps aside.</p>
		${B.meta && B.parkedPlaying && html`<div class="box surface down-card"><span class="tile"><${Art} seed=${B.meta.p} kind=${B.meta.kind} /></span><div class="grow"><div class="t">${B.meta.title}</div><div class="dim" style="font-size:13px">Was playing here · stopped at <span class="num">${fmtTime(B.parked)}</span></div></div></div>`}
		${uploading() && html`<p class="down-status" role="status" aria-live="polite">Finishing ${left} upload${left === 1 ? '' : 's'} here first</p>`}
		<button class="btn" onClick=${() => useHere()}><${Icon} n="refresh" k=${2} />Switch here</button>
	</div></main>`;
}
