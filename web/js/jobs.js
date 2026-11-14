import { html, useEffect } from '../vendor/htm-preact-standalone.module.js';
import { phoneNow, set, store, useStore } from './store.js';
import { errMsg, get, post } from './api.js';
import { openModal, toast } from './toasts.js';
import { Modal } from './components.js';
import { Icon } from './icons.js';
import { baseName, fmtSize, joinPath, plural } from './util.js';
import { markFresh } from './uploads.js';
import { filesHash, go } from './router.js';

/* ================= Copy and Move jobs: the handheld copies, the page watches =================
   A copy, or a move to another card, runs on the handheld as a job (it goes on with this page closed). The page asks
   GET /api/jobs about once a second while one runs (the SSE "jobs" event says when one starts or ends, from any page),
   and shows them as a round key like the uploads': the share of bytes copied on its ring, a tap for each job with
   Cancel. A job that ends while the page watches says how it went in a toast; one that ended before isn't news. */
store.jobs = [];
const seen = new Map();	// id -> the job as last seen running (a toast when it ends)
let timer = 0, asking = false;
export const jobOver = j => j.state === 'done' || j.state === 'cancelled';
const running = () => store.jobs.filter(j => !jobOver(j));

export async function refreshJobs() {
	clearTimeout(timer);
	if (asking || !store.authed) return;
	asking = true;
	let jobs = null;
	try { jobs = (await get('/api/jobs', null, { quiet: true })).jobs; } catch (e) {}
	asking = false;
	if (jobs) {
		for (const j of jobs) {
			if (!jobOver(j)) seen.set(j.id, j);
			else if (seen.has(j.id)) { seen.delete(j.id); ended(j); }
		}
		set({ jobs });
	}
	if (running().length || !jobs) timer = setTimeout(refreshJobs, jobs ? 1000 : 3000);
}
// a Copy or Move answer that started a job
export function watchJob(j) { seen.set(j.id, j); set({ jobs: [...store.jobs.filter(x => x.id !== j.id), j] }); refreshJobs(); }
export async function cancelJob(j) { try { await post('/api/jobs/cancel', { id: j.id }); } catch (e) { toast(errMsg(e)); } refreshJobs(); }

const verb = (j, past) => j.kind === 'copy' ? (past ? 'Copied' : 'Copying') : (past ? 'Moved' : 'Moving');
const jobErr = f => f.error === 'exists' ? 'something there has that name' : f.error;
function ended(j) {
	const ok = j.ok.length, bad = j.failed.length, to = j.toName;
	j.ok.forEach(p => markFresh(joinPath(j.to, baseName(p))));
	// the folders shown follow (the handheld's "fs" news says it too; this page may have missed it)
	set({ fsTick: store.fsTick + 1, fsDir: '', jobDone: Date.now() });
	setTimeout(() => set({ jobDone: 0 }), 2500);
	const there = store.route.page === 'files' && store.route.path === j.to;
	const show = there ? null : { label: 'Show', fn: () => go(filesHash(j.to)) };
	if (j.state === 'cancelled') return toast((j.kind === 'copy' ? 'Copy' : 'Move') + ' cancelled' + (ok ? ': ' + plural(ok, 'item', 'items') + ' had gone to ' + to + ' already' : ': nothing changed'), ok ? show : null);
	if (!bad) return toast(verb(j, true) + ' ' + plural(ok, 'item', 'items') + ' to ' + to, show);
	toast((ok ? verb(j, true) + ' ' + ok + ' of ' + (ok + bad) + ' items to ' + to + '. ' : (j.kind === 'copy' ? 'Couldn’t copy' : 'Couldn’t move') + (bad === 1 ? ' “' + baseName(j.failed[0].p) + '”' : ' ' + plural(bad, 'item', 'items')) + ': ') + jobErr(j.failed[0]), ok ? show : null, 8000);
}

function stats() {
	const js = running(), total = js.reduce((a, j) => a + j.total, 0), done = js.reduce((a, j) => a + j.done, 0);
	return { js, total, done, frac: total ? Math.min(1, done / total) : 0 };
}
// the key: above the uploads' when both show
export function JobFab() {
	const s = useStore(), st = stats();
	if (s.route.page === 'playing' || s.selecting || s.pick) return null;
	if (!st.js.length && !s.jobDone) return null;
	const doneNow = !st.js.length;
	const stack = s.upload.items.length > 0 || !!s.upDone;
	const C = 2 * Math.PI * 35, pct = Math.floor(st.frac * 100);
	const one = st.js.length === 1 && st.js[0];
	const label = doneNow ? 'Copying done' : (one ? verb(one) + ' to ' + one.toName : plural(st.js.length, 'copy', 'copies')) + ', ' + pct + '%. Show';
	return html`<div class=${'fabwrap jobfab ' + (doneNow ? 'done' : 'busy') + (stack ? ' stack' : '')}>
		<svg class="ring" viewBox="0 0 80 80" aria-hidden="true"><circle cx="40" cy="40" r="35" fill="none" stroke="var(--e-track)" stroke-width="4" />
			<circle class="arc" cx="40" cy="40" r="35" fill="none" stroke=${doneNow ? 'var(--green)' : 'var(--yellow)'} stroke-width="4" stroke-linecap="round" stroke-dasharray=${C} stroke-dashoffset=${C * (1 - (doneNow ? 1 : st.frac))} transform="rotate(-90 40 40)" /></svg>
		<button class=${'box fab' + (doneNow ? ' yellow' : '')} aria-label=${label} aria-haspopup="dialog" onClick=${e => doneNow ? set({ jobDone: 0 }) : openJobs(e.currentTarget)}>
			${doneNow ? html`<${Icon} n="check" k=${3} />` : html`<span class="cnt">${pct}%</span>`}</button>
		<span class="fbadge job" aria-hidden="true"><${Icon} n=${one && one.kind === 'move' ? 'folder-move' : 'copy'} k=${1} /></span></div>`;
}
function openJobs(anchor) {
	if (store.jobsOpen) return;
	let pos = null;
	if (!phoneNow() && anchor) { const r = anchor.getBoundingClientRect(); pos = `right:${Math.max(8, innerWidth - r.right - 10)}px;bottom:${innerHeight - r.top + 16}px`; }
	set({ jobsOpen: true });
	openModal(close => html`<${JobSheet} pos=${pos} onClose=${() => { set({ jobsOpen: false }); close(); }} />`, () => set({ jobsOpen: false }));
}
function JobSheet({ pos, onClose }) {
	useStore();
	const st = stats(), js = st.js;
	useEffect(() => { if (!js.length) onClose(); }, [js.length]);
	useEffect(() => {
		if (!pos) return;
		const k = e => { if (e.key === 'Escape') { e.preventDefault(); onClose(); } };
		document.addEventListener('keydown', k); return () => document.removeEventListener('keydown', k);
	}, []);
	const head = html`<div class="uphead"><div class="kicker y ell">On your handheld</div>
		<div style="display:flex;align-items:baseline;gap:10px"><h2 class="grow">${js.length === 1 ? verb(js[0]) + ' ' + plural(js[0].n, 'item', 'items') : plural(js.length, 'copy', 'copies')}</h2>
			<span class="num dim" style="font-size:13px">${fmtSize(st.done)} of ${fmtSize(st.total)} · ${Math.floor(st.frac * 100)}%</span></div>
		<div class="upbar"><i style=${'width:' + st.frac * 100 + '%'}></i></div>
		<div class="note">It carries on with this page closed.</div></div>`;
	const rows = html`<div class="uprows">${js.map(j => { const f = j.total ? j.done / j.total : 0; return html`<div class=${'up ' + (j.state === 'copying' ? 'run' : 'wait')} key=${j.id}>
		<span class="nm ell">${verb(j)} to ${j.toName}<span class="dir ell">${j.state === 'waiting' ? 'Waits for the copy before it' : j.state === 'counting' ? 'Measuring…' : j.cur || '…'}</span></span>
		<span class="st"><span class=${j.state === 'copying' ? 'y' : ''}>${j.state === 'copying' ? fmtSize(j.done) + ' of ' + fmtSize(j.total) : j.state === 'waiting' ? 'next' : ''}</span>
			<button class="btn sm" style="min-height:28px;font-size:13px" onClick=${() => cancelJob(j)}>Cancel</button></span>
		<div class="bar"><i style=${'width:' + f * 100 + '%'}></i></div></div>`; })}</div>`;
	const foot = html`<div class="upfoot"><span class="dim" style="font-size:13px;align-self:center">Cancel removes what's half copied.</span><button class="btn sm" onClick=${onClose}>Close</button></div>`;
	if (pos) return html`<div class="popcatch" onMouseDown=${onClose}></div><div class="uppop box surface" role="dialog" aria-label="Copying" style=${pos}>${head}${rows}${foot}</div>`;
	return html`<${Modal} cls="upsheet" label="Copying" raw=${true} onClose=${onClose}>${head}${rows}${foot}</${Modal}>`;
}
