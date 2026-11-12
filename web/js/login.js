import { html, useState, useEffect, useRef } from '../vendor/htm-preact-standalone.module.js';
import { C, settle } from './connection.js';
import { set, store, useStore, parseRoute } from './store.js';
import { replaceHist } from './router.js';
import { openSSE, refreshState } from './events.js';
import { errMsg, fetchT, get, post } from './api.js';
import { Banner, HandheldArt, useTick } from './components.js';
import { fmtTime, touchOnly } from './util.js';
import { Icon } from './icons.js';
import { claimTab, otherTabHas } from './onetab.js';

/* ================= login ================= */
async function afterLogin() {
	settle();
	// one tab at a time: another tab of this browser has it, and this one asks "Switch here?" (onetab.js)
	const taken = await otherTabHas();
	set({ authed: true, signedOut: '', loginNote: '', signingIn: false, conn: 'ok', reconnecting: false, downReason: '', route: parseRoute(), elsewhere: taken });
	C.okAt = Date.now();
	await refreshState();
	if (!taken) { openSSE(); loadExtras(); claimTab(); }
	pingBoot();
}
// the player's start id, to tell a restart from a reconnect
export async function pingBoot() { try { const { r } = await fetchT('/api/ping', { cache: 'no-store' }, 4000); C.boot = r.headers.get('X-BP-Boot') || C.boot; } catch (e) {} }
// a link with the PIN opened while the page is open: sign in with it, the PIN gone from the address
export async function pinFromHash() {
	const m = location.hash.match(/^#pin=([A-Za-z0-9]{4})$/);
	replaceHist(null, location.pathname + location.search + (store.authed ? '#/playing' : ''));
	if (!m) return;
	if (store.authed) { set({ route: parseRoute() }); return; }
	set({ authed: false, signingIn: true, route: parseRoute() });
	try { await post('/api/login', { pin: m[1] }); await afterLogin(); }
	catch (e) { set({ signingIn: false, loginNote: e.status === 401 ? 'This QR code is out of date. Enter the PIN shown on your handheld.' : errMsg(e) }); }
}
export async function loadExtras() {
	try { set({ eq: (await get('/api/eq')).presets }); } catch (e) {}
	try { const r = (await get('/api/roots')).roots; set({ roots: r }); } catch (e) {}
}

export function Login() {
	const s = useStore();
	const [chars, setChars] = useState(['', '', '', '']), [status, setStatus] = useState('idle'), [msg, setMsg] = useState(''), [lockEnd, setLockEnd] = useState(0), [shake, setShake] = useState(0);
	const refs = useRef([]);
	useTick(1000, !!lockEnd);
	const left = lockEnd ? Math.max(0, Math.ceil((lockEnd - Date.now()) / 1000)) : 0;
	useEffect(() => { if (lockEnd && !left) { setLockEnd(0); setStatus('idle'); setMsg(''); setTimeout(() => refs.current[0] && refs.current[0].focus(), 0); } });
	useEffect(() => { if (!s.signingIn && refs.current[0] && !touchOnly) refs.current[0].focus(); }, [s.signingIn]);
	const locked = !!lockEnd && left > 0;
	async function submit(arr) {
		setStatus('busy'); setMsg('Signing in…');
		try { await post('/api/login', { pin: arr.join('') }); await afterLogin(); }
		catch (e) {
			const b = e.body || {};
			setChars(['', '', '', '']);
			if (b.retryAfter > 0) { setLockEnd(Date.now() + b.retryAfter * 1000); setStatus('locked'); setMsg(''); return; }
			if (e.status === 401) { setStatus('wrong'); setShake(x => x + 1); setMsg("That's not the PIN. " + plural(b.left == null ? 0 : b.left, 'try', 'tries') + ' left.'); }
			else { setStatus('idle'); setMsg(errMsg(e)); }
			setTimeout(() => refs.current[0] && refs.current[0].focus(), 0);
		}
	}
	const clean = v => v.toUpperCase().replace(/[^A-Z0-9]/g, '');
	function input(i, e) {
		const v = clean(e.target.value);
		if (!v) { const a = chars.slice(); a[i] = ''; setChars(a); return; }
		const a = chars.slice(); let j = i;
		for (const c of v) { if (j < 4) a[j++] = c; }
		setChars(a); setStatus('idle'); setMsg('');
		const next = Math.min(j, 3);
		if (refs.current[next]) refs.current[next].focus();
		if (a.every(Boolean)) submit(a);
	}
	function keydown(i, e) {
		if (e.key === 'Backspace' && !chars[i] && i > 0) { e.preventDefault(); const a = chars.slice(); a[i - 1] = ''; setChars(a); refs.current[i - 1].focus(); }
		if (e.key === 'ArrowLeft' && i > 0) refs.current[i - 1].focus();
		if (e.key === 'ArrowRight' && i < 3) refs.current[i + 1].focus();
	}
	function paste(e) { e.preventDefault(); const v = clean((e.clipboardData || window.clipboardData).getData('text')).slice(0, 4); if (!v) return; const a = ['', '', '', '']; [...v].forEach((c, i) => a[i] = c); setChars(a); if (a.every(Boolean)) submit(a); else refs.current[v.length].focus(); }
	const busy = status === 'busy' || s.signingIn;
	const dis = busy || locked;
	return html`<main class="login"><div class="signin">
		<div class="brand"><img src="/logo.png" alt="" width="64" height="64" />Banana Player</div>
		${s.signedOut === 'pin' && html`<${Banner} cls="info" title="Signed out: a new PIN was made on your handheld." />`}
		${s.signedOut === 'expired' && html`<${Banner} cls="info" title="You were signed out. Enter the PIN to continue." />`}
		${s.loginNote && html`<${Banner} cls="info" title=${s.loginNote} />`}
		<div class="box surface how"><${HandheldArt} mode="qr" /><div><div class="kicker y">Quickest</div><div class="t">Scan the QR on your handheld</div><p>Point your phone's camera at its bottom screen. It signs you in.</p></div></div>
		<div class="or" aria-hidden="true">OR TYPE THE PIN</div>
		<div class="pin ${status === 'wrong' ? 'wrong' : ''} ${busy ? 'busy' : ''} ${shake && status === 'wrong' ? 'shake' : ''}" key=${shake} role="group" aria-label="PIN">
			${chars.map((c, i) => html`<input ref=${r => refs.current[i] = r} class=${c ? 'filled' : ''} value=${c} disabled=${dis} inputmode="text" autocomplete=${i ? 'off' : 'one-time-code'} autocapitalize="characters" autocorrect="off" spellcheck="false" maxlength=${i === 0 ? 4 : 2}
				aria-label=${'PIN character ' + (i + 1)} onInput=${e => input(i, e)} onKeyDown=${e => keydown(i, e)} onPaste=${paste} onFocus=${e => e.target.select()} />`)}
		</div>
		<div class="msg ${status === 'wrong' || locked ? 'err' : 'dim'}" aria-live="polite">${s.signingIn ? 'Signing in…' : locked ? 'Too many tries. Try again in ' + fmtTime(left) + '.' : msg}</div>
		<p class="help">On the handheld: <span class="key">START</span> › Settings › Web dashboard</p>
		<p class="lnote"><${Icon} n="lock" k=${1} />Not encrypted: use it on your home Wi-Fi only.</p>
	</div></main>`;
}

