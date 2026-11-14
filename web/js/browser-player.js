import { NO_HERE, NO_HERE_WHY, TAB_ID, lsGet, lsSet, notify, set, store } from './store.js';
import { MATCH_OFF, matchTrack } from './match.js';
import { closeSSE, openSSE } from './events.js';
import { errMsg, errToast, fetchT, get, post } from './api.js';
import { ask, markTrack, toast } from './toasts.js';
import { baseName, cmpName, dirName, fmtTime, joinPath, kindOfName, plural, titleFor, userTap } from './util.js';
import { C } from './connection.js';
import { go, tapped } from './router.js';
import { markUsed } from './plshared.js';

/* ================= browser player ================= */
export const el = document.createElement('video');
el.playsInline = true; el.preload = 'auto'; el.controls = false;
document.getElementById('holder').appendChild(el);
// parked: unloaded when the connection went, at that position (the page never plays it again by itself)
// tok: grows with each item loaded here (an answer for an older one is dropped: a Direct Stream started for it)
export const B = { blocked: false, active: false, queue: [], index: -1, source: null, meta: null, loop: 'no', error: false, leaving: false, parked: null, parkedPlaying: false, vol: Number(lsGet('bpVol') || 80), tok: 0 };
el.volume = B.vol / 100;
// playing here with the page hidden (a locked phone): keep listening; paused and hidden: stop (battery)
el.addEventListener('play', () => {
	openSSE();
	// a Play from anywhere (the mini player, Space, the headset) of a file this tab isn't the output of yet: it is now
	// (the handheld pauses: one sound only)
	if (B.meta && S.s && !isOutput()) intent('SetOutput', { to: 'browser' }).catch(() => {});
});
// (the selected output keeps its stream while hidden: a pick on the handheld comes here, a locked phone included)
el.addEventListener('pause', () => { if (document.hidden && !isPref()) closeSSE(); });
// every browser-play and browser-pos carries this tab and a number that only grows (across reloads too): two posts can
// overtake each other on the way, and the handheld drops the older (it would open an older file after a newer one)
// where this page is in its file, for the handheld: its own Play resumes from here (not from its stale paused copy)
let posTimer = 0;
// (the session's Pos report: only the output's, for its load. A play or pause made here first says so: Play / Pause)
function sendPos() {
	if (B.skipping) return;	// (a Direct Stream running on to where it was asked: not a play or pause of the viewer's)
	if (!B.active || !B.meta || B.leaving || B.parked != null || store.conn !== 'ok' || !el.getAttribute('src') || !isOutput() || B.meta.p !== S.s.file) return;
	// (waiting for a tap: still waiting, at its place now; a Pos would say it's paused)
	if (B.blocked && el.paused) { intent('Blocked', { load: S.s.load, t: bPos().toFixed(2) }).catch(() => {}); return; }
	intent('Pos', { load: S.s.load, t: bPos().toFixed(2), playing: el.paused ? 0 : 1 }).catch(() => {});
}
for (const ev of ['play', 'pause', 'seeked']) el.addEventListener(ev, () => {
	if (B.skipping) return;
	// (a file's end fires pause before ended: not the viewer's pause, its Ended follows and the next one plays. A Pos
	// saying "paused" here had the next song load paused)
	if (ev === 'pause' && (el.ended || (el.duration > 0 && el.duration - el.currentTime < 0.25))) return;
	// (not the pause a file's end brings: that's its Ended, and the next one plays)
	if (ev === 'play' && B.blocked) { B.blocked = false; notify(); }	// (the tap came: Pos says it plays)
	if (ev !== 'seeked' && !el.ended && !B.blocked && isOutput() && B.active && B.meta && B.meta.p === S.s.file && S.s.playing === el.paused && !B.leaving)
		intent(el.paused ? 'Pause' : 'Play').catch(() => {});
	sendPos();
	clearInterval(posTimer); posTimer = 0;
	if (!el.paused) posTimer = setInterval(() => { if (el.paused || !B.active) { clearInterval(posTimer); posTimer = 0; } else sendPos(); }, 10000);
});
el.addEventListener('emptied', () => { clearInterval(posTimer); posTimer = 0; });
// it sounds: the tap the browser wanted came (the session hears it by the Pos above)
el.addEventListener('playing', () => { if (B.blocked) { B.blocked = false; notify(); } });
// play() refused. No tap yet (Safari, a Chrome page nobody touched: their autoplay rules) while the session wants it to
// play here: Tap to play shows, and the session hears it waits for one (Blocked: the handheld says so, and its B plays
// there instead). Anything else: the session hears it's paused, at its place once the element has it
function refused(e) {
	if (e && e.name === 'NotAllowedError' && isOutput() && S.s.playing && B.active && B.meta && B.meta.p === S.s.file && B.parked == null) {
		if (!B.blocked) { B.blocked = true; notify(); }
		intent('Blocked', Object.assign({ load: S.s.load }, el.readyState >= 1 ? { t: bPos().toFixed(2) } : {})).catch(() => {});
		return;
	}
	if (el.readyState >= 1) sendPos(); else el.addEventListener('loadedmetadata', () => setTimeout(sendPos, 0), { once: true });
}
// Tap to play: within the tap (Safari wants play() inside it)
export function playBlocked() {
	if (!B.meta || B.parked != null) return;
	el.play().then(() => { if (B.blocked) { B.blocked = false; notify(); } }).catch(e => toast(e && e.name === 'NotAllowedError' ? 'Your browser still won\u2019t start the sound' : "Couldn't play it here"));
}
for (const ev of ['play', 'pause', 'loadedmetadata', 'durationchange', 'waiting', 'playing', 'seeked']) el.addEventListener(ev, () => { updateMediaSession(); notify(); });
el.addEventListener('ended', () => {
	// a Direct Stream cut short (the connection dropped while paused, say): on from where it stopped
	if (B.remux && store.conn === 'ok' && bDur() && bPos() < bDur() - 2) { remuxAt(B.remux.a, bPos(), true); return; }
	// the session's Ended: its next, a wrap, or the end (it says so)
	const title = B.source && B.source.title;
	if (store.conn === 'ok' && isOutput()) { S.quiet = true; intent('Ended', { load: S.s.load }).finally(() => { S.quiet = false; }).then(r => { if (r && r.session && r.session.index < 0) toast('End of ' + (title || 'the list')); }).catch(() => {}); }
	else notify();
});
el.addEventListener('error', () => {
	if (!B.active || !el.getAttribute('src')) return;
	// the Direct Stream failed: back to the file as it is (its default sound)
	if (B.remux) { remuxFailed(el.error); return; }
	B.error = true; notify();
	// (this tab the output: the session skips it, said in the page, and the list goes on here)
	if (isOutput() && S.s && B.meta && B.meta.p === S.s.file) { skippedToast(B.meta.p); intent('CantPlay', { load: S.s.load, reason: 'format' }).catch(() => {}); return; }
	toast("This browser can't play this file", null, 6000); });
// a file of the list this browser can't play: skipped (Next, Previous, a file's end), said here
function skippedToast(p) { toast((p ? baseName(p) : 'This one') + " isn't supported in this browser · skipped", null, 5000); }
export function probeOK(m) {
	if (m.playable === 'no') return false;
	if (m.playable === 'maybe' && m.mime) { const hint = m.mime + (m.codecs ? '; codecs="' + m.codecs + '"' : ''); return !!el.canPlayType(hint) || !!el.canPlayType(m.mime); }
	return true;
}
export async function probe1(p) {
	// not worth a long wait: unknown, the browser just tries the file
	try { return await get('/api/media', { p }, { ms: 4000, quiet: true }); } catch (e) { return null; }
}
export const fileUrl = p => '/api/file?p=' + encodeURIComponent(p);
// items: [{p}] picked in this page to play here: the session's Open (this tab the output, and where picks go), and the
// renderer loads it as the answer comes (onSession). One this browser can't play plays on the handheld, said so
export async function browserPlay(items, index, source, pos) {
	if (store.conn !== 'ok') return false;
	if (NO_HERE) { toast(NO_HERE_WHY + ': it plays on the handheld'); return false; }
	const src = source || {};
	try {
		const r = await intent('Open', { q: items.map(x => x.p), index, t: (pos || 0).toFixed(2), skind: src.kind || 'folder', stitle: src.title || '', sid: src.id || '', to: 'browser' });
		return !!r && !r.rejected;
	} catch (e) { toast(e.status === 0 ? "Couldn't reach your handheld" : errMsg(e)); return false; }
}
// Next / Previous here: the session's Step (Previous RESTART_S in restarts first; the end of the list says so)
export function browserNext(dir) {
	intent('Step', { n: dir < 0 ? -1 : 1 }).then(r => { if (r && r.rejected && (r.reason === 'first' || r.reason === 'last')) toast(r.reason === 'first' ? 'This is the first one' : 'This is the last one'); }).catch(() => {});
}
// one item into the element: m (its probe, null: not known), from pos, with tracks w {a: an audio index of m.audio,
// s: a subtitle's i (-1 off)}, null each: this browser's choice; play: start it, else paused there
function startItem(items, i, source, m, pos, w, play) {
	const it = items[i];
	B.tok++;
	const kind = kindOfName(it.p) === 'video' || (m && m.vcodec) ? 'video' : 'audio';
	Object.assign(B, { active: true, queue: items, index: i, source, error: false, leaving: false, parked: null, parkedPlaying: false,
		meta: { p: it.p, title: titleFor(it.p, m && m.title), artist: m && m.artist || '', album: m && m.album || '', kind, chapters: m && m.chapters || [], duration: m && m.duration || 0, media: m } });
	B.remux = null; clearTimeout(remuxTimer);
	subsForFile(m, w.s);
	const a = w.a != null && m && w.a !== directAudio(m) ? w.a : null;
	// (refused, no tap: the session hears it's paused, at its place once the element has it)
	const start = () => el.play().catch(refused);
	// another audio track: Safari switches it in the file (once it knows its tracks); elsewhere a Direct Stream
	// from pos, never the file first (its clock is the stream's)
	if (a != null && !('audioTracks' in el) && !streamWhy(m, a)) {
		// (meanwhile the position is pos; no stream: the file as it is)
		B.remux = { a, base: pos, pending: pos };
		remuxAt(a, pos, play).then(() => { if (B.meta && B.meta.p === it.p && !el.getAttribute('src') && B.parked == null) directPlay(pos, play); });
	}
	else {
		el.src = fileUrl(it.p);
		if (pos > 0) el.addEventListener('loadedmetadata', () => { el.currentTime = pos; }, { once: true });
		if (a != null) el.addEventListener('loadedmetadata', () => {
			if (nativeAudio() && el.audioTracks[a]) [...el.audioTracks].forEach((t, j) => t.enabled = j === a);
			else { const l = m.audio[a] ? m.audio[a].label : 'That audio track'; toast(l.split(' · ')[0] + ' ' + (streamWhy(m, a) || 'plays on the handheld only').toLowerCase() + ': the default track here', null, 6000); }
			notify();
		}, { once: true });
		if (play) start();
	}
	// no word on it in time: playing anyway; its tags, chapters and tracks when they come
	if (!m) probe1(it.p).then(m2 => { if (!m2 || !B.meta || B.meta.p !== it.p) return; Object.assign(B.meta, { title: titleFor(it.p, m2.title), artist: m2.artist || '', album: m2.album || '', chapters: m2.chapters || [], duration: m2.duration || 0, media: m2 }); subsForFile(m2); notify(); });
	updateMediaSession(); notify();
}
function unload() { el.pause(); B.blocked = false; B.remux = null; clearTimeout(remuxTimer); el.removeAttribute('src'); clearSubs(); el.load(); }
export function browserStop() {
	B.leaving = true; unload();
	Object.assign(B, { active: false, meta: null, queue: [], index: -1, error: false, parked: null, parkedPlaying: false, tok: B.tok + 1 });
	// the session's Stop: everywhere (the handheld lets go of its copy); this tab stays where picks go
	if (isOutput()) { S.quiet = true; intent('Stop').then(r => { if (r && r.rejected && r.reason === 'stale') return intent('Stop'); }).catch(() => {}).finally(() => { S.quiet = false; }); }
	notify();
}
// forgets this page's file without telling the handheld (signed out, or the player started again)
export function dropBrowser() {
	B.leaving = true; unload();
	Object.assign(B, { active: false, meta: null, queue: [], index: -1, error: false, parked: null, parkedPlaying: false, tok: B.tok + 1 });
	notify();
}
// the connection went: stop at once (the stream can't go on), keep what and where
export function parkHere() {
	if (!B.meta || B.parked != null) return;
	const playing = !el.paused || (store.conn === 'blip' && B.wasPlaying);
	B.parked = bPos(); B.parkedPlaying = playing;
	el.pause(); el.removeAttribute('src'); el.load();
	notify();
}
export function playParked() {
	if (!B.meta || B.parked == null) return;
	// the handheld took it meanwhile (this tab gone too long): this tab the output again, at the session's place
	if (S.s && !isOutput()) { Object.assign(B, { parked: null, parkedPlaying: false }); unload(); B.meta = null; B.active = false; notify(); continueHere(true); return; }
	const pos = B.parked;
	Object.assign(B, { parked: null, parkedPlaying: false, error: false });
	restoreSubs();
	if (B.remux) { remuxAt(B.remux.a, pos, true); return; }
	el.src = fileUrl(B.meta.p);
	if (pos > 0) el.addEventListener('loadedmetadata', () => { el.currentTime = pos; }, { once: true });
	el.play().catch(() => {});
	notify();
}
// fromHandheld: its volume keys moved it (nothing to tell back). Else the handheld is told, so its keys go on from here
let volSent = null;
export function setBrowserVol(v, fromHandheld) {
	B.vol = v; el.volume = v / 100; lsSet('bpVol', v); notify();
	if (fromHandheld) return;
	clearTimeout(volSent);
	volSent = setTimeout(() => post('/api/browser-volume', { v: Math.round(v) }).catch(() => {}), 250);
}
export function bPos() {
	if (B.parked != null) return B.parked;
	if (B.remux) return B.remux.pending != null ? B.remux.pending : B.remux.base + (el.currentTime || 0);
	return el.currentTime || 0;
}
// (a Direct Stream doesn't know its own length: the file's)
// the file's length as the handheld reads it (its chapters, its bar) when known: a browser's own for a Matroska file
// can differ by a little (the chapter marks then sat off their times); the element's only when the file says none
function bDur() { return (B.meta && B.meta.duration) || (isFinite(el.duration) && el.duration) || 0; }
// to a place in the file, whichever way it plays: a Direct Stream can't seek, so it starts again from there
export function seekTo(t) {
	if (B.remux) {
		// (a drag along the bar asks once, where it stops)
		const r = B.remux;
		if (r.pending == null) r.play = !el.paused;
		r.pending = t; notify();
		clearTimeout(remuxTimer); remuxTimer = setTimeout(() => remuxAt(r.a, t, r.play), 250);
		return;
	}
	el.currentTime = t;
}

/* ---- audio tracks: Safari switches them in the element (audioTracks); elsewhere the server's Direct Stream copies
   the video and the chosen track into a fragmented MP4 from a time on (/api/remux), and the original file is back for
   the default track. The page counts the stream's time from where it starts (B.remux.base) ---- */
let remuxTimer = 0;
const VC = { h264: 'avc1.640028', hevc: 'hvc1.1.6.L120.90', vp9: 'vp09.00.40.08', av1: 'av01.0.08M.08' };
const AC = { aac: 'mp4a.40.2', mp3: 'mp4a.69', opus: 'opus', flac: 'flac', ac3: 'ac-3', eac3: 'ec-3' };
const typeOk = (v, a) => { try { return !!el.canPlayType('video/mp4; codecs="' + v + ', ' + a + '"') || !!(window.MediaSource && MediaSource.isTypeSupported('video/mp4; codecs="' + v + ', ' + a + '"')); } catch (e) { return false; } };
// the track the browser plays from the file itself (Direct Play)
export const directAudio = m => Math.max(0, (m && m.audio || []).findIndex(a => a.default));
// '' when audio track i can play here by Direct Stream, else why not
export function streamWhy(m, i) {
	const a = m && m.audio && m.audio[i];
	if (!a) return 'No such track';
	if (B.noStream && B.meta && B.meta.media === m && B.noStream === B.meta.p && i !== directAudio(m)) return "This browser can't play this video's format in a new stream";
	if (!VC[m.vcodec] || !typeOk(VC[m.vcodec], 'mp4a.40.2') && !typeOk(VC[m.vcodec], 'opus')) return "This video can't be streamed to this browser";
	if (!AC[a.codec] || !typeOk(VC[m.vcodec], AC[a.codec])) return 'Plays on the handheld only';
	return '';
}
// Safari: the file's own tracks, switched in place
export const nativeAudio = () => el.audioTracks && el.audioTracks.length > 1;
export function playingAudio() {
	const m = B.meta && B.meta.media;
	if (nativeAudio()) return [...el.audioTracks].findIndex(t => t.enabled);
	return B.remux ? B.remux.a : directAudio(m);
}
// the chosen track: in place (Safari), the file itself (its default track), else a Direct Stream from here
// quiet: the session's pick followed here (made on the handheld), not sent back
export async function chooseAudio(i, quiet) {
	const m = B.meta && B.meta.media; if (!m || B.parked != null) return;
	if (!quiet) sendPick('SetAudio', audioPick(m, i));
	if (nativeAudio()) { [...el.audioTracks].forEach((t, j) => t.enabled = j === i); notify(); return; }
	if (i === playingAudio() && !B.switching) return;
	if (i === directAudio(m)) { directPlay(bPos(), !el.paused); return; }
	await remuxAt(i, bPos(), !el.paused);
}
// the Direct Stream didn't play: back to the file as it is (its default sound), saying why, and the handheld's log
// hears what the browser said. A format it can't decode is remembered for this file (the other tracks say so)
function remuxFailed(err) {
	const pos = bPos(), r = B.remux, code = err ? err.code : 0;
	const format = code === 3 || code === 4;
	const why = format ? "This browser can't play this video's format in a new stream" : 'The stream from your handheld broke off';
	if (format && B.meta) B.noStream = B.meta.p;
	post('/api/remux-failed', { p: B.meta ? B.meta.p : '', a: r ? r.a : '', t: pos.toFixed(1), why: 'MediaError ' + code + ' ' + (err && err.message || ''), ua: navigator.userAgent }).catch(() => {});
	toast("Couldn't switch the audio track: " + why.charAt(0).toLowerCase() + why.slice(1), null, 8000);
	directPlay(pos, true);
}
function directPlay(pos, play) {
	B.remux = null; B.switching = null; clearTimeout(remuxTimer);
	el.src = fileUrl(B.meta.p);
	if (pos > 0) el.addEventListener('loadedmetadata', () => { el.currentTime = pos; }, { once: true });
	if (play) el.play().catch(e => { if (e && e.name === 'NotAllowedError') refused(e); });
	shiftSubs();
	notify();
}
async function remuxAt(a, t, play) {
	const p = B.meta && B.meta.p, tok = B.tok;
	if (!p) return;
	B.switching = { a }; notify();
	let r;
	try { r = await get('/api/remux-start', { p, a, t: t.toFixed(2) }, { ms: 15000, quiet: true }); }
	catch (e) { if (B.meta && B.meta.p === p && tok === B.tok) { B.switching = null; if (B.remux) B.remux.pending = null; notify(); toast(e.status === 0 ? "Couldn't switch the audio track" : errMsg(e)); } return; }
	if (!B.meta || B.meta.p !== p || tok !== B.tok || (B.switching && B.switching.a !== a)) return;	// moved on meanwhile
	// the stream starts at the keyframe at or before t (x265 puts them seconds apart). Played from there it showed the
	// seconds before t (a chapter's start, a spot on the bar: "it jumps back"). So it plays on, muted and unseen, fast,
	// until it reaches t, then shows: this stream can't be seeked (a jump inside it went back to its start), and a
	// browser fetches it only a little ahead of what it shows (paused, it stops fetching). The clock says t meanwhile
	// (pending); the session hears nothing of it (B.skipping)
	const skip = t - r.start;
	const rx = B.remux = { a, base: r.start, pending: skip > 0.3 ? t : null };
	B.switching = null;
	el.src = '/api/remux?' + new URLSearchParams({ p, a, t: r.start.toFixed(3), tab: TAB_ID });
	const start = () => { if (play) el.play().catch(e => { if (e && e.name === 'NotAllowedError') refused(e); }); };
	if (skip > 0.3) {
		const wasMuted = el.muted, rate = el.playbackRate, t0 = Date.now();
		B.skipping = true; el.muted = true; el.style.visibility = 'hidden';
		const done = () => {
			if (!B.skipping) return;
			el.playbackRate = rate; el.muted = wasMuted; el.style.visibility = '';
			if (!play && B.remux === rx) el.pause();
			B.skipping = false;
			if (B.remux !== rx) return;
			rx.pending = null; notify();
			if (play && el.paused) start();
		};
		// each frame: 16x while far, slower as it nears t, so it stops within a frame or two of it
		const step = () => {
			if (!B.skipping) return;
			if (B.remux !== rx || Date.now() - t0 > 8000) return done();	// (a stream slow to come: from where it got to)
			const left = skip - el.currentTime;
			if (el.readyState >= 1 && left <= 0.04) return done();
			el.playbackRate = Math.max(1, Math.min(16, left * 6));
			requestAnimationFrame(step);
		};
		el.play().then(() => requestAnimationFrame(step)).catch(done);
	} else start();
	shiftSubs();
	notify();
}

/* ---- subtitles in this browser: one <track> per text track, fetched as WebVTT when chosen ---- */
export const SUBS = { file: '', list: [], chosen: -1, urls: {}, progress: null, failed: {}, seq: 0, noAss: {}, ass: false };
function clearSubs() {
	assOff(); picOff();
	[...el.querySelectorAll('track')].forEach(t => t.remove());
	Object.values(SUBS.urls).forEach(u => URL.revokeObjectURL(u));
	Object.assign(SUBS, { file: '', list: [], chosen: -1, last: -1, urls: {}, progress: null, failed: {}, seq: SUBS.seq + 1, noAss: {}, ass: false });
}
// s: the one to show (-1 none), else the usual choice
function subsForFile(m, s) {
	clearSubs();
	SUBS.file = B.meta ? B.meta.p : '';
	SUBS.list = m && m.subs || [];
	const want = s != null ? s : lsGet('bpSubOff') === '1' ? -1 : m && m.subDefault != null ? m.subDefault : -1;
	const x = SUBS.list.find(y => y.i === want);
	if (s != null && x && x.kind === 'image') toast(x.label.split(' · ')[0] + ' subtitles show on the handheld only', null, 6000);
	const sub = SUBS.list.find(x => x.i === want);
	if (sub && sub.kind !== 'image') chooseSub(want, true);
}
// the subtitles last shown (their label), for CC: the same ones again, in this file or the next
const SIGNS = /sign|song|karaoke|forced/i;
function rememberSub(i) { const x = SUBS.list.find(y => y.i === i); if (x) { SUBS.last = i; lsSet('bpSubLast', x.label); } }
// CC on the video (and the C key): off, or back on with the last ones; in a new file the same label, else the same
// language, else the file's default, else the first that isn't signs only
export function toggleCaptions() {
	if (SUBS.chosen >= 0) { lsSet('bpSubOff', '1'); chooseSub(-1); return; }
	const text = SUBS.list.filter(x => x.kind !== 'image');
	if (!text.length) { toast(SUBS.list.length ? "This video's subtitles show on the handheld only" : 'No subtitles in this file'); return; }
	const label = lsGet('bpSubLast') || '', lang = label.split(' · ')[0];
	const m = B.meta && B.meta.media;
	const pick = text.find(x => x.i === SUBS.last) || text.find(x => x.label === label)
		|| text.find(x => lang && x.label.split(' · ')[0] === lang && !SIGNS.test(x.label)) || text.find(x => m && x.i === m.subDefault)
		|| text.find(x => !SIGNS.test(x.label)) || text[0];
	lsSet('bpSubOff', '0'); chooseSub(pick.i);
}
// playback speed (the gear's Speed): it stays for the next file
export const SPEEDS = [0.5, 0.75, 1, 1.25, 1.5, 2];
export function setSpeed(r) { el.defaultPlaybackRate = r; el.playbackRate = r; notify(); }
function restoreSubs() { const c = SUBS.chosen; if (c >= 0) { SUBS.chosen = -1; chooseSub(c, true); } }
// quiet: this browser's own choice as a file starts (else the viewer's, which the session keeps for the handheld)
export async function chooseSub(i, quiet) {
	const file = SUBS.file, seq = SUBS.seq;
	SUBS.chosen = i; SUBS.progress = null; delete SUBS.failed[i];
	if (!quiet && file && B.meta && B.meta.p === file) sendPick('SetSub', subPick(i));
	if (i >= 0) rememberSub(i);
	[...el.textTracks].forEach(t => t.mode = 'disabled');
	assOff(); picOff();
	notify();
	if (i < 0) return;
	const sx = SUBS.list.find(x => x.i === i);
	// pictures (VobSub, PGS, DVB): the handheld decodes them a window at a time
	if (sx && sx.kind === 'picture') { choosePic(i, file); return; }
	// styled (ASS/SSA): drawn by libass in the page, in its own look; plain cues if that can't run here
	if (sx && sx.styled && !SUBS.noAss[i] && assCan()) { if (await chooseAss(i, file, seq)) return; if (SUBS.seq !== seq || SUBS.chosen !== i) return; }
	if (!SUBS.urls[i]) {
		for (;;) {
			let r, text;
			try { ({ r, text } = await fetchT('/api/subs?' + new URLSearchParams({ p: file, i }), {}, 60000)); } catch (e) { r = null; }
			if (SUBS.seq !== seq || SUBS.chosen !== i) return;
			if (r && r.status === 202) {
				let pr = 0; try { pr = JSON.parse(text).progress || 0; } catch (e) {}
				SUBS.progress = pr; notify();
				await new Promise(res => setTimeout(res, 1000));
				if (SUBS.seq !== seq || SUBS.chosen !== i) return;
				continue;
			}
			SUBS.progress = null;
			if (!r || !r.ok) { SUBS.failed[i] = r && r.status === 415 ? "These subtitles can't be shown here" : "Couldn't load these subtitles"; SUBS.chosen = -1; notify(); return; }
			SUBS.urls[i] = URL.createObjectURL(new Blob([text], { type: 'text/vtt' }));
			const t = document.createElement('track');
			t.kind = 'subtitles'; t.src = SUBS.urls[i]; t.dataset.i = i;
			const s = SUBS.list.find(x => x.i === i); t.label = s ? s.label : 'Subtitles'; if (s && s.lang) t.srclang = s.lang;
			t.addEventListener('load', () => placeCues());
			el.appendChild(t);
			break;
		}
	}
	const tr = el.querySelector('track[data-i="' + i + '"]');
	if (tr) { tr.track.mode = 'showing'; placeCues(); }
	notify();
}
/* ---- styled subtitles (ASS/SSA) by JASSUB (libass in WebAssembly, web/vendor/jassub/), loaded the first time one is
   chosen. It draws on a canvas over the picture (assBox, which goes where the video goes), with the file's own fonts
   (its Matroska attachments) and Inter for what they lack ---- */
export const assBox = document.createElement('div');
assBox.className = 'assbox';
document.getElementById('holder').appendChild(assBox);
const ASS = { jas: null, mod: null };
const abs = u => new URL(u, location.href).href;
const assCan = () => typeof WebAssembly === 'object' && typeof Worker === 'function';
function assOff() {
	if (ASS.jas) { try { ASS.jas.destroy(); } catch (e) {} ASS.jas = null; }
	assBox.querySelectorAll('canvas').forEach(c => c.remove());	// (psubBox stays)
	SUBS.ass = false;
}
// true: it shows; false: use the plain cues instead
async function chooseAss(i, file, seq) {
	const live = () => SUBS.seq === seq && SUBS.chosen === i;
	let text = SUBS.assText && SUBS.assText[i];
	if (text == null) {
		for (;;) {
			let r, t;
			try { ({ r, text: t } = await fetchT('/api/ass?' + new URLSearchParams({ p: file, i }), {}, 60000)); } catch (e) { r = null; }
			if (!live()) return true;
			if (r && r.status === 202) {
				let pr = 0; try { pr = JSON.parse(t).progress || 0; } catch (e) {}
				SUBS.progress = pr; notify();
				await new Promise(res => setTimeout(res, 1000));
				if (!live()) return true;
				continue;
			}
			SUBS.progress = null;
			if (!r || !r.ok) { SUBS.noAss[i] = 1; return false; }
			text = t; (SUBS.assText = SUBS.assText || {})[i] = t;
			break;
		}
	}
	let fonts = [];
	try { const f = await get('/api/fonts', { p: file }, { ms: 8000, quiet: true }); fonts = (f.fonts || []).map(x => abs('/api/font?' + new URLSearchParams({ p: file, i: x.i }))); } catch (e) {}
	if (!live()) return true;
	try {
		if (!ASS.mod) ASS.mod = (await import('../vendor/jassub/jassub.es.js')).default;
		if (!live()) return true;
		assOff();
		const canvas = document.createElement('canvas');
		assBox.appendChild(canvas);
		ASS.jas = new ASS.mod({ video: el, canvas, subContent: text, fonts,
			workerUrl: abs('/vendor/jassub/jassub-worker.js'), wasmUrl: abs('/vendor/jassub/jassub-worker.wasm'),
			availableFonts: { inter: abs('/fonts/Inter-Regular.ttf') }, fallbackFont: 'inter', timeOffset: B.remux ? B.remux.base : 0, libassMemoryLimit: 48, libassGlyphLimit: 24 });
		ASS.jas.addEventListener('error', () => { if (!live() || !SUBS.ass) return; console.log('jassub failed: plain cues'); SUBS.noAss[i] = 1; chooseSub(i, true); });
		SUBS.ass = true;
		notify();
		return true;
	} catch (e) {
		console.log('jassub: ' + e);
		assOff(); SUBS.noAss[i] = 1;
		return false;
	}
}

/* ---- picture subtitles (VobSub, PGS, DVB): the handheld decodes them a window of the film at a time into PNGs
   (/api/picsubs: about 60 s, cues in file seconds, rects in the track's canvas, VobSub's 720×480 or PGS's 1920×1080),
   asked ahead of the clock and again on a seek. They go over the picture where it shows (object-fit: contain), in
   psubBox inside assBox (which moves with the video). One shows at a time: a later picture replaces the one before ---- */
const psubBox = document.createElement('div');
psubBox.className = 'psub-box';
assBox.appendChild(psubBox);
const PIC_WIN = 60, PIC_AHEAD = 25;
const PIC = { i: -1, file: '', seq: 0, cues: [], from: 0, to: 0, eof: false, w: 0, h: 0, busy: false, fails: 0, raf: 0, shown: null, lay: '' };
function picOff() {
	PIC.seq++; PIC.i = -1; PIC.cues = []; PIC.from = PIC.to = 0; PIC.eof = false; PIC.busy = false; PIC.shown = null; PIC.lay = '';
	cancelAnimationFrame(PIC.raf); PIC.raf = 0;
	psubBox.textContent = '';
}
function choosePic(i, file) {
	picOff();
	Object.assign(PIC, { i, file, fails: 0 });
	picFrame();
}
// a window from t on: `fresh` drops what was there (a seek away from it), else it goes on after it
async function picFetch(t, fresh) {
	const seq = PIC.seq, from = fresh ? Math.max(0, Math.floor(t / 10) * 10) : PIC.to;
	PIC.busy = true;
	let d = null;
	try { d = await get('/api/picsubs', { p: PIC.file, i: PIC.i, from: from.toFixed(3), to: (from + PIC_WIN).toFixed(3) }, { ms: 20000, quiet: true }); } catch (e) {}
	if (seq !== PIC.seq) return;
	PIC.busy = false;
	if (!d || !Array.isArray(d.cues)) {
		// (the handheld busy, the connection blinking): again a bit later, then said so
		if (++PIC.fails >= 4) { SUBS.failed[PIC.i] = "Couldn't load these subtitles"; picOff(); SUBS.chosen = -1; notify(); return; }
		PIC.busy = true; setTimeout(() => { if (seq === PIC.seq) PIC.busy = false; }, 1500 * PIC.fails);
		return;
	}
	PIC.fails = 0;
	if (fresh) { PIC.cues = []; PIC.from = d.from; }
	PIC.to = d.to; PIC.eof = !!d.eof; PIC.w = d.w || 720; PIC.h = d.h || 480;
	// a cue in two windows: once, with the later end (where the first one stopped reading is all it knew)
	for (const c of d.cues) {
		const o = PIC.cues.find(x => Math.abs(x.s - c.s) < .001);
		if (o) { o.e = Math.max(o.e, c.e); continue; }
		PIC.cues.push(c);
	}
	const now = bPos();
	PIC.cues = PIC.cues.filter(c => c.e > now - 120).sort((a, b) => a.s - b.s);
	PIC.shown = undefined;
}
function picFrame() {
	PIC.raf = requestAnimationFrame(picFrame);
	if (PIC.i < 0) return;
	if (el.readyState < 1 && !B.remux && B.parked == null) return;	// (the file's start position isn't there yet)
	const t = bPos(), inside = t >= PIC.from && (t < PIC.to || PIC.eof) && PIC.to > PIC.from;
	if (!PIC.busy) {
		if (!inside) picFetch(t, true);
		else if (!PIC.eof && t > PIC.to - PIC_AHEAD) picFetch(t, false);
	}
	// the picture showing now: the latest to start (only once its window is here: not after a seek, meanwhile)
	let cur = null;
	if (inside) for (const c of PIC.cues) { if (c.s > t) break; if (t < c.e) cur = c; }
	picPlace();
	if (cur === PIC.shown) return;
	PIC.shown = cur;
	psubBox.textContent = '';
	if (!cur) return;
	for (const r of cur.r) {
		const img = new Image();
		img.className = 'psub-img'; img.alt = ''; img.decoding = 'sync';
		img.src = 'data:image/png;base64,' + r.png;
		const st = img.style;
		st.left = (r.x / PIC.w * 100) + '%'; st.top = (r.y / PIC.h * 100) + '%';
		st.width = (r.w / PIC.w * 100) + '%'; st.height = (r.h / PIC.h * 100) + '%';
		psubBox.appendChild(img);
	}
}
// psubBox over the picture as it shows inside the element (letterboxed: object-fit contain)
function picPlace() {
	const ar = assBox.getBoundingClientRect(), er = el.getBoundingClientRect();
	const vw = el.videoWidth, vh = el.videoHeight;
	let w = er.width, h = er.height;
	if (vw > 0 && vh > 0 && w > 0 && h > 0) { const k = Math.min(w / vw, h / vh); w = vw * k; h = vh * k; }
	const l = er.left - ar.left + (er.width - w) / 2, tp = er.top - ar.top + (er.height - h) / 2;
	const lay = [l, tp, w, h].map(x => x.toFixed(1)).join();
	if (lay === PIC.lay) return;
	PIC.lay = lay;
	Object.assign(psubBox.style, { left: l.toFixed(1) + 'px', top: tp.toFixed(1) + 'px', width: w.toFixed(1) + 'px', height: h.toFixed(1) + 'px' });
}

// the viewer's own look: size (a share of the picture's height) and how high the lines sit
export const CUE_SIZES = [['s', 'Small', .8], ['m', 'Medium', 1], ['l', 'Large', 1.25], ['xl', 'Extra large', 1.5]];
export const cueStyle = { size: lsGet('bpCueSize') || 'm', pos: lsGet('bpCuePos') || 'bottom' };
function placeCues() {
	const off = B.remux ? B.remux.base : 0;
	for (const t of el.textTracks) {
		if (t.mode !== 'showing' || !t.cues) continue;
		for (const c of t.cues) {
			c.snapToLines = true; c.line = cueStyle.pos === 'raised' ? -4 : -2;
			// a Direct Stream's clock starts at its base: the file's times, moved back by it
			if (c.bpS == null) { c.bpS = c.startTime; c.bpE = c.endTime; }
			if (c.bpOff !== off) { c.bpOff = off; c.startTime = c.bpS - off; c.endTime = c.bpE - off; }
		}
	}
}
function shiftSubs() { placeCues(); if (ASS.jas) ASS.jas.timeOffset = B.remux ? B.remux.base : 0; }
export function sizeCues() {
	const f = (CUE_SIZES.find(x => x[0] === cueStyle.size) || CUE_SIZES[1])[2];
	el.style.setProperty('--cue', Math.max(12, el.clientHeight * .045 * f).toFixed(1) + 'px');
}
if ('ResizeObserver' in window) new ResizeObserver(sizeCues).observe(el);
export function setCueStyle(k, v) { cueStyle[k] = v; lsSet(k === 'size' ? 'bpCueSize' : 'bpCuePos', v); sizeCues(); placeCues(); notify(); }

export function updateMediaSession() {
	if (!('mediaSession' in navigator)) return;
	const v = view();
	if (!v.has || store.conn === 'down') { try { navigator.mediaSession.metadata = null; navigator.mediaSession.playbackState = 'paused'; } catch (e) {} return; }
	try {
		navigator.mediaSession.metadata = new MediaMetadata({ title: v.title, artist: v.artist, album: v.album, artwork: v.cover ? [{ src: '/api/cover?p=' + encodeURIComponent(v.cover), sizes: '512x512' }] : [] });
		navigator.mediaSession.playbackState = v.paused ? 'paused' : 'playing';
		// a headset or a car coming back can press Play with nobody there: not while the connection is out, nor just after
		const live = () => store.conn === 'ok' && Date.now() - C.okAt > 3000;
		const h = (a, f) => { try { navigator.mediaSession.setActionHandler(a, f); } catch (e) {} };
		// the page's own file: its own element, never the handheld (a system Pause, or a Play after it, must not
		// start the handheld because the page's view moved on); the handheld's only when this page plays nothing
		const mine = () => B.meta && (B.active || B.parked != null);
		// (a finished list: a headset's Play doesn't start it again by itself)
		h('play', () => { if (!live() || view().finished) return; if (mine()) { if (B.parked != null) playParked(); else if (el.paused) el.play().catch(() => {}); } else if (view().paused && store.target === 'handheld') ctl.toggle(); });
		h('pause', () => { if (mine()) el.pause(); else if (!view().paused && store.target === 'handheld') ctl.toggle(); });
		h('previoustrack', () => { if (live()) ctl.prev(); }); h('nexttrack', () => { if (live()) ctl.next(); });
		h('seekbackward', () => ctl.seekRel(-5)); h('seekforward', () => ctl.seekRel(10));
		h('seekto', d => ctl.seek(d.seekTime));
	} catch (e) {}
}

// Shuffle is the handheld's setting (as Loop): the page, the handheld and another tab show the same, playing or not
export const shuffleOn = () => !!(store.snap && store.snap.shuffleMode);
/* the normalised "now playing" view, for whichever target is active */
export function view() {
	const s = store.snap;
	if (store.target === 'browser' && B.meta) {
		const m = B.meta;
		return { has: true, here: true, title: m.title, artist: m.artist, album: m.album, kind: m.kind, paused: el.paused, parked: B.parked, loading: false, pos: bPos, duration: bDur(), chapters: m.chapters, chapter: chapterAt(m.chapters, bPos()),
			source: B.source && Object.assign({}, B.source, { index: B.index, count: B.queue.length }), loop: store.snap && store.snap.loop || B.loop, shuffled: shuffleOn(), path: m.p, cover: m.kind === 'audio' ? m.p : '' };
	}
	if (s && s.path) {
		const pos = () => s.paused ? s.position : s.position + (Date.now() - store.snapAt) / 1000 * (s.speed || 1);
		return { has: true, here: false, title: titleFor(s.path, s.title), artist: s.artist, album: s.album, kind: s.kind, paused: s.paused, loading: s.loading, pos, duration: s.duration, chapters: s.chapters || [], chapter: s.chapter, source: s.source, loop: s.loop, shuffled: shuffleOn(), path: s.path, cover: s.kind === 'audio' ? s.path : '', tracks: s.tracks };
	}
	// a list that ended by itself: its last item, paused at its end (Play: the list again; Up next: any row of it)
	const f = finishedList();
	if (f) {
		const p = f.queue[f.index], m = finMeta(p), kind = kindOfName(p) === 'video' || (m && m.vcodec) ? 'video' : 'audio', dur = f.dur || (m && m.duration) || 0;
		return { has: true, finished: true, here: false, title: titleFor(p, m && m.title), artist: m && m.artist || '', album: m && m.album || '', kind, paused: true, loading: false, pos: () => dur, duration: dur, chapters: [], chapter: -1,
			source: Object.assign(finSource(f), { index: f.index, count: f.queue.length }), loop: store.snap && store.snap.loop || 'no', shuffled: shuffleOn(), path: p, cover: kind === 'audio' ? p : '' };
	}
	return { has: false, here: store.target === 'browser', pos: () => 0, duration: 0, chapters: [], chapter: -1, loop: store.snap && store.snap.loop || 'no', shuffled: shuffleOn() };
}

/* ---- the list that ended by itself (the session's `ended`, docs/design/session.md §13): shown, not played, till
   something plays or a Stop puts it away ---- */
export function finishedList() {
	const ss = S.s, f = ss && ss.index < 0 && ss.ended;
	return f && f.index >= 0 && f.queue && f.queue[f.index] ? f : null;
}
// where it came from (a folder the handheld played has no title in the session: its name)
export function finSource(f) {
	const kind = f.source.kind === 'playlist' ? 'playlist' : 'folder', p = f.queue[f.index];
	return { kind, title: f.source.title || (kind === 'folder' ? baseName(dirName(p)) || 'This card' : ''), id: f.source.id || '' };
}
// the last item's tags (its title, the cover's kind), probed once
const FIN = { p: '', m: null };
function finMeta(p) {
	if (FIN.p !== p) { FIN.p = p; FIN.m = null; probe1(p).then(m => { if (FIN.p === p && m) { FIN.m = m; notify(); } }); }
	return FIN.m;
}
// the finished list again from row i, where picks go: this browser, else the selected output (the handheld, another tab)
export async function playFinished(i) {
	const f = finishedList(); if (!f) return false;
	const src = finSource(f);
	if (store.target === 'browser') return browserPlay(f.queue.map(p => ({ p })), i, src, 0);
	try {
		const r = await intent('Open', Object.assign({ q: f.queue, index: i, t: '0', skind: src.kind, stitle: src.title, sid: src.id }, S.s.pref && S.s.pref.kind === 'handheld' ? { to: 'handheld' } : {}));
		if (r && r.rejected) toast("Couldn't play it: try again");
		return !!r && !r.rejected;
	} catch (e) { toast(e.status === 0 ? "Couldn't reach your handheld" : errMsg(e)); return false; }
}
// put the finished list away (the session's Stop): nothing playing
export function clearFinished() {
	if (!finishedList()) return;
	intent('Stop').then(r => { if (r && r.rejected && r.reason === 'stale') return intent('Stop'); }).catch(() => {});
}
function chapterAt(ch, t) { let c = -1; (ch || []).forEach((x, i) => { if (x.start <= t + 0.2) c = i; }); return c; }

/* handheld commands: fire and forget, the SSE brings the new state */
const cmd = (c, extra) => post('/api/cmd', Object.assign({ c }, extra)).catch(errToast);
export const hereNow = () => store.target === 'browser' && B.meta;
export const ctl = {
	// (a finished list: Play is the list again from its first, with Shuffle on from a random one; Previous its last
	// again; Next has nothing)
	toggle() { const f = view().finished && finishedList(); if (f) { playFinished(shuffleOn() ? Math.floor(Math.random() * f.queue.length) : 0); return; } if (hereNow()) { if (B.parked != null) playParked(); else el.paused ? el.play().catch(() => {}) : el.pause(); } else cmd('toggle'); },
	next() { if (view().finished) return; markTrack(1); hereNow() ? (B.parked == null && browserNext(1)) : cmd('next'); },
	prev() { const f = view().finished && finishedList(); if (f) { playFinished(f.index); return; } markTrack(-1); hereNow() ? (B.parked == null && browserNext(-1)) : cmd('prev'); },
	seek(t) { if (view().finished) return; if (hereNow()) { if (B.parked != null) { B.parked = t; notify(); } else seekTo(t); } else { cmd('seek', { t: t.toFixed(1) }); if (store.snap) { store.snap.position = t; store.snapAt = Date.now(); notify(); } } },
	seekRel(d) { if (view().finished) return; if (hereNow()) ctl.seek(Math.max(0, Math.min(bDur(), bPos() + d))); else cmd('seekrel', { d }); },
	chapterTo(i) { if (view().finished) return; if (hereNow()) ctl.seek(B.meta.chapters[i].start); else cmd('chapterto', { i }); },
	chapterStep(dir) { const v = view(); if (!v.chapters.length) return; const i = Math.max(0, Math.min(v.chapters.length - 1, v.chapter + dir)); ctl.chapterTo(i); },
	// one Loop for both: the handheld's setting (its mode key, the page's, either output), which the session follows
	// (songs and videos alike), so a page playing it ends as the handheld would. Off → all → one;
	// Shuffle is its own setting (with Loop all, a shuffled list plays on for ever, a new round each time)
	loop() {
		const order = ['no', 'all', 'one'], cur = store.snap ? store.snap.loop : B.loop;
		const v = order[(order.indexOf(cur) + 1) % 3];
		B.loop = v;
		if (store.snap) { store.snap.loop = v; notify(); }
		cmd('loop', { v });
	},
	// Shuffle on or off: the handheld's setting (it lasts, playing or not), the session's for what plays: the list
	// keeps its order where it shows, Next and an item's end follow a hidden random one (docs/design/session.md §14)
	shuffle() {
		const on = !shuffleOn();
		if (store.snap) { store.snap.shuffleMode = on; notify(); }
		intent('SetShuffle', { on: on ? 1 : 0 }).then(r => { if (r && !r.rejected) toast(on ? 'Shuffle on' : 'Shuffle off'); }).catch(errToast);
	},
};

/* ---- the Session (docs/design/session.md): where it plays (out), where picks go (pref, this page's "target") and the
   audio/subtitle picks are the handheld's one value, changed by intents (/api/intent) and followed here: when out
   becomes this tab it loads the session's file at its place with its tracks; when out leaves, its element stops ---- */
export const S = { boot: 0, v: -1, s: null, at: 0, me: false, quiet: false, load: -1 };
const outMe = ss => !!ss && ss.out && ss.out.kind === 'browser' && ss.out.tab === TAB_ID;
// this tab plays the session's item / is where picks go
export const isOutput = () => outMe(S.s) && S.s.index >= 0;
export const isPref = () => !!S.s && !!S.s.pref && S.s.pref.kind === 'browser' && S.s.pref.tab === TAB_ID;
// where it has got to: the session's pos as published, run on since it came while it plays on a loaded output
export function sessPos(ss, at) {
	const p = ss.pos + (ss.playing && ss.loaded && !ss.blocked ? (Date.now() - at) / 1000 * (ss.speed || 1) : 0);
	return Math.max(0, ss.dur > 0 ? Math.min(ss.dur, p) : p);
}
// seq: grows per tab, across reloads (the time), above what the handheld numbered this tab's old words
let seqN = 0;
export async function intent(type, o) {
	if ((type === 'SetOutput' || type === 'Open') && o && o.to === 'browser') S.selfAt = Date.now();
	const body = Object.assign({ type, tab: TAB_ID, seq: seqN = Math.max(seqN + 1, Date.now() * 1000), basis: Math.max(0, S.v) }, o);
	let r = await post('/api/intent', body);
	// a choice made over a state that moved on meanwhile (a file the page itself just opened): again, over the new one
	if (r && r.rejected && r.reason === 'stale' && type === 'SetOutput') {
		if (r.session) onSession(r.session);
		body.seq = seqN = Math.max(seqN + 1, Date.now() * 1000); body.basis = r.v;
		r = await post('/api/intent', body);
	}
	if (r && r.session) onSession(r.session);
	return r;
}
// a snapshot's session (the state's, or an intent's answer): only a newer one (or a new boot: the handheld restarted)
export function onSession(ss) {
	if (!ss || !ss.out) return;
	if (ss.boot === S.boot && ss.v < S.v) return;
	const fresh = ss.boot !== S.boot || ss.v > S.v, endedWas = S.s && JSON.stringify(S.s.ended || null);
	const listWas = S.s && JSON.stringify([S.s.queue, S.s.source]);
	Object.assign(S, { boot: ss.boot, v: ss.v, s: ss, at: Date.now() });
	if (!fresh) return;
	if (endedWas !== JSON.stringify(ss.ended || null)) notify();	// (the finished view comes or goes)
	// the list in another order (Shuffle on or off, from anywhere), the same item playing on: this page's queue follows
	if (listWas !== JSON.stringify([ss.queue, ss.source])) {
		if (outMe(ss) && ss.index >= 0 && B.active && B.meta && B.meta.p === ss.file && ss.qindex >= 0) { B.queue = ss.queue.map(q => ({ p: q })); B.index = ss.qindex; }
		notify();
	}
	// the switch says where it plays while something does (the handheld's pick of a network file plays there, the
	// browser still the choice for the next pick: the switch shows the handheld), else where the next pick goes
	const playsHh = ss.index >= 0 && ss.out.kind === 'handheld';
	const target = NO_HERE || playsHh ? 'handheld' : isPref() ? 'browser' : 'handheld';
	if (store.target !== target) set({ target });
	const was = S.me, me = isOutput();
	S.me = me;
	// (become the output: the handheld told this tab's volume, its keys go on from it)
	if (me && !was) post('/api/browser-volume', { v: Math.round(B.vol) }).catch(() => {});
	if (me && (!was || S.load !== ss.load)) {
		S.load = ss.load;
		// the file playing here already (this tab became the output of what it plays, a shuffle): nothing to load
		if (B.active && B.meta && B.meta.p === ss.file && el.getAttribute('src') && B.parked == null) {
			if (ss.queue.length && ss.qindex >= 0) { B.queue = ss.queue.map(q => ({ p: q })); B.index = ss.qindex; }
			if (!ss.loaded) intent('Loaded', { load: ss.load, dur: bDur(), audio: B.meta.kind === 'audio' ? 1 : 0 }).catch(() => {});
			notify();
		}
		else renderLoad(ss, S.at);
	} else if (!me && was && B.parked == null && (B.active || B.meta)) {
		// (the list ended here: the finished view keeps its last item, its tags known already)
		if (ss.ended && B.meta && ss.ended.queue[ss.ended.index] === B.meta.p && B.meta.media) Object.assign(FIN, { p: B.meta.p, m: B.meta.media });
		dropBrowser();
		if (!S.quiet && !ss.ended) toast(ss.index < 0 ? 'Stopped on your handheld' : ss.out.kind === 'handheld' ? 'Playing on your handheld now' : 'Playing in another browser');
	}
	if (!me) S.load = -1;
	// a Play / Pause or a seek made elsewhere (the handheld's A, its bar, a remote): this element follows
	else if (B.active && B.meta && B.meta.p === ss.file && B.parked == null) {
		if (ss.playV !== S.playV) { if (ss.playing && el.paused) el.play().catch(refused); else if (!ss.playing && !el.paused) el.pause(); }
		// (Loop one: the session's Ended put it back to its start, still playing; the element stopped at its end)
		else if (ss.seekN !== S.seekN && ss.playing && el.ended) { seekTo(0); el.play().catch(refused); }
		// (paused from elsewhere, or it plays: nothing to tap for)
		if (B.blocked && (!ss.playing || !el.paused)) { B.blocked = false; notify(); }
		if (ss.seekN !== S.seekN) { const t = sessPos(ss, S.at); if (Math.abs(bPos() - t) > 1) seekTo(t); }
		// a track picked elsewhere (the handheld's Audio & Subtitles) while this file plays here: this element follows
		const pk = JSON.stringify([ss.audio, ss.sub]);
		if (S.picks != null && pk !== S.picks && B.meta.media) followPicks(B.meta.media, ss);
		S.picks = pk;
	} else S.picks = null;
	S.playV = ss.playV; S.seekN = ss.seekN;
}
// the session's item here: its file, at its place, with its tracks, playing if it plays. One this browser can't have
// (not served, a format it can't play) is the session's CantPlay: the handheld plays it, this tab stays where picks go
async function renderLoad(ss, at) {
	const p = ss.file, tok = ++B.tok;
	// (the session skips it: the next one plays here)
	const cant = () => { skippedToast(p); intent('CantPlay', { load: ss.load, reason: 'format' }).catch(() => {}); };
	if (!p) return cant();
	const m = await probe1(p);
	if (tok !== B.tok || !isOutput() || S.s.load !== ss.load) return;	// moved on meanwhile
	if (m && !probeOK(m)) return cant();
	const sn = store.snap, src = sn && sn.source && sn.source.kind ? { kind: sn.source.kind, title: sn.source.title, id: sn.source.id } : null;
	const items = ss.queue.length && ss.qindex >= 0 ? ss.queue.map(q => ({ p: q })) : [{ p }];
	startItem(items, ss.qindex >= 0 ? ss.qindex : 0, src, m, sessPos(ss, at), tracksFor(m, ss), ss.playing);
	intent('Loaded', { load: ss.load, dur: m && m.duration || 0, audio: B.meta && B.meta.kind === 'audio' ? 1 : 0 }).catch(() => {});
}
// the session's picks on this file's tracks (as the handheld's: by place when both list as many, else label, else
// language); one this browser can't play: its default, said so, the pick kept for the handheld
function tracksFor(m, ss) {
	const out = { a: null, s: null };
	if (!m) return out;
	const audio = m.audio || [], subs = m.subs || [];
	if (ss.audio && ss.audio.set && audio.length) {
		const k = matchTrack(ss.audio, audio.map(x => ({ lang: x.lang || '', label: x.label, external: false })));
		if (k >= 0) {
			if ('audioTracks' in el || k === directAudio(m) || !streamWhy(m, k)) out.a = k;
			else toast(audio[k].label.split(' · ')[0] + ' ' + streamWhy(m, k).toLowerCase() + ': the default track here', null, 6000);
		}
	}
	if (ss.sub && ss.sub.set) {
		const k = matchTrack(ss.sub, subs.map(x => ({ lang: x.lang || '', label: x.label, external: !!x.external })));
		if (k === MATCH_OFF) out.s = -1;
		else if (k >= 0) out.s = subs[k].i;	// (one the page can't show, kind 'image': subsForFile says so)
	}
	return out;
}
// the session's picks on the file playing here, when they differ from what it plays (tracksFor says why one can't)
function followPicks(m, ss) {
	const w = tracksFor(m, ss);
	if (w.a != null && w.a !== playingAudio() && !(B.switching && B.switching.a === w.a)) chooseAudio(w.a, true);
	if (w.s != null && w.s !== SUBS.chosen) chooseSub(w.s, true);
}
// this page's track as a pick (its place in its kind's list, as the handheld lists them in the same order)
function audioPick(m, i) { const l = m && m.audio || [], x = l[i]; return x ? { ord: i, of: l.length, lang: x.lang || '', label: x.label, external: 0 } : null; }
function subPick(i) {
	const l = SUBS.list, k = l.findIndex(x => x.i === i);
	return i < 0 ? { off: 1, of: l.length } : k >= 0 ? { ord: k, of: l.length, lang: l[k].lang || '', label: l[k].label, external: l[k].external ? 1 : 0 } : null;
}
// a pick made here, for the session (what the handheld plays when it takes over); only for the session's own file
function sendPick(type, pick) {
	if (!pick || !S.s || !B.meta || B.meta.p !== S.s.file) return Promise.resolve();
	return intent(type, pick).catch(() => {});
}

/* ---- where it plays: the switch, Continue here, Play on handheld ---- */
export async function setTarget(t) {
	if (t === 'browser') {
		if (NO_HERE) { toast(NO_HERE_WHY + ': it plays on the handheld'); return; }
		if (store.snap && store.snap.path && store.snap.path.startsWith('bpnet://') && store.target === 'handheld') { toast('Network files play on the handheld only'); return; }
		if (isOutput() || !userTap()) return;
		await continueHere();
	} else if (store.target !== 'handheld' || isOutput()) await handoffToHandheld();
}
// this tab the output (and where picks go): what plays, here from where it is, with its tracks (onSession loads it).
// Nothing playing: only where the next pick goes. tapped: the caller saw the tap
export async function continueHere(tapped) {
	if (NO_HERE) { toast(NO_HERE_WHY + ': it plays on the handheld'); return; }
	if (!tapped && !userTap()) return;
	let r;
	try { r = await intent('SetOutput', { to: 'browser' }); }
	catch (e) { toast(e.status === 0 ? "Couldn't reach your handheld" : errMsg(e)); return; }
	if (!r || r.rejected) { toast("Couldn't switch: try again"); return; }
	const ss = r.session;
	if (ss && ss.index >= 0 && outMe(ss)) toast('Continuing here from ' + fmtTime(sessPos(ss, S.at)));
}
// the handheld the output (and where picks go), at this page's place with this page's tracks: o.a, o.s (this page's
// audio index / subtitle i to play there; else the ones it plays now)
export async function handoffToHandheld(o) {
	o = o || {};
	const mine = isOutput() && B.meta && B.meta.p === S.s.file && B.parked == null;
	const pos = bPos(), was = !el.paused, m = B.meta && B.meta.media;
	if (mine) {
		await sendPick('SetAudio', audioPick(m, o.a != null ? o.a : B.switching ? B.switching.a : playingAudio()));
		await sendPick('SetSub', subPick(o.s != null ? o.s : SUBS.chosen));
	}
	B.leaving = true;	// (its pause is the hand-off's, not a Pause for the session)
	el.pause();	// one sound: quiet here before the handheld starts
	let r;
	S.quiet = true;
	try { r = await intent('SetOutput', Object.assign({ to: 'handheld' }, mine ? { at: pos.toFixed(2) } : {})); }
	catch (e) { S.quiet = false; B.leaving = false; if (was && store.conn === 'ok') el.play().catch(() => {}); toast(e.status === 0 ? "Couldn't start it on your handheld" : errMsg(e)); return false; }
	S.quiet = false;
	if (!r || r.rejected) { B.leaving = false; if (was) el.play().catch(() => {}); toast("Couldn't start it on your handheld"); return false; }
	if (B.active) dropBrowser();
	if (mine) toast('Continuing on the handheld from ' + fmtTime(pos));
	return true;
}

// a pick for the handheld from a tab that is where picks go ("Play on handheld"): the handheld is chosen with it
// (to=handheld); else it plays on the selected output, wherever that is
const hhChoice = () => isPref() || isOutput() ? { to: 'handheld', tab: TAB_ID } : {};
// collects playable [{p}] from selected paths (a folder gives its own media, in order)
async function collectMedia(paths) {
	const out = [];
	for (const p of paths) {
		const isFolder = store.folderSet && store.folderSet.has(p);
		if (isFolder) { try { const r = await get('/api/ls', { p }); r.items.filter(i => !i.dir && (i.kind === 'audio' || i.kind === 'video')).sort((a, b) => cmpName(a.name, b.name)).forEach(i => out.push({ p: joinPath(p, i.name) })); } catch (e) {} }
		else if (['audio', 'video'].includes(kindOfName(p))) out.push({ p });
	}
	return out;
}
// one file with the rest of its folder after it, as the handheld plays a file (a folder's songs and videos, by name)
async function withFolder(p) {
	const d = dirName(p);
	let items;
	try { const r = await get('/api/ls', { p: d }); items = r.items.filter(i => !i.dir && (i.kind === 'audio' || i.kind === 'video')).sort((a, b) => cmpName(a.name, b.name)).map(i => ({ p: joinPath(d, i.name) })); } catch (e) { items = []; }
	let idx = items.findIndex(i => i.p === p);
	if (idx < 0) { items = [{ p }]; idx = 0; }
	return { items, idx, src: { kind: 'folder', title: baseName(d) || 'This card' } };
}
// "Play" for files/folders, on a target; the target changes only once it plays there
// stay: a row's tap plays and the page stays (the small player shows it) — for a song; a video opens the player,
// where it's watched (sv: stay for this file?)
// ('always': a video too, Home's Now playing card showing it)
const sv = (stay, p) => stay === 'always' || (stay && kindOfName(p || '') !== 'video');
// stay: a row's tap plays and the page stays (the small player shows it); true when it plays
export async function playPaths(paths, folderSet, target, stay) {
	if (!userTap()) return;
	store.folderSet = folderSet;
	target = target || store.target;
	if (target === 'browser') {
		let items, src, idx = 0;
		if (paths.length === 1 && !folderSet.has(paths[0])) ({ items, src, idx } = await withFolder(paths[0]));
		else {
			items = await collectMedia(paths);
			if (!items.length) { toast('Nothing to play there'); return; }
			src = { kind: 'playlist', title: paths.length === 1 ? baseName(paths[0]) : 'Selection' };
		}
		if (!await browserPlay(items, idx, src, 0)) return false;
		if (!sv(stay, items[idx] && items[idx].p)) go('#/playing');
		return true;
	} else {
		const was = B.active && !el.paused; B.leaving = true; el.pause();
		try { await post('/api/play', Object.assign(paths.length === 1 ? { p: paths[0] } : { p: paths }, hhChoice())); }
		catch (e) { B.leaving = false; if (was && store.conn === 'ok') el.play().catch(() => {}); toast(e.status === 0 ? "Couldn't start it on your handheld" : errMsg(e)); return false; }
		if (B.active) dropBrowser();
		if (!sv(stay, paths.length === 1 && !folderSet.has(paths[0]) ? paths[0] : '')) go('#/playing');
		return true;
	}
}
export async function playPlaylist(id, name, items, index, shuffle, target, tapped, stay) {
	if (!tapped && !userTap()) return;
	target = target || store.target;
	if (target === 'browser') {
		let list = items.filter(i => !i.missing && i.p).map(i => ({ p: i.p, title: i.title }));
		// (a Shuffle play: Shuffle on (it lasts), from a random item; the list keeps its order)
		if (shuffle && !shuffleOn()) { if (store.snap) store.snap.shuffleMode = true; try { await intent('SetShuffle', { on: 1 }); } catch (e) {} }
		const idx = shuffle ? Math.floor(Math.random() * list.length) : Math.max(0, list.findIndex(i => i.p === items[index || 0].p));
		if (await browserPlay(list, idx, { kind: 'playlist', title: name, id }, 0)) { markUsed(id); if (!sv(stay, list[idx] && list[idx].p)) go('#/playing'); return true; }
		return false;
	} else {
		const was = B.active && !el.paused; B.leaving = true; el.pause();
		try { await post('/api/play', Object.assign({ playlist: id, index: index || 0, shuffle: shuffle ? 1 : 0 }, hhChoice())); }
		catch (e) { B.leaving = false; if (was && store.conn === 'ok') el.play().catch(() => {}); toast(e.status === 0 ? "Couldn't start it on your handheld" : errMsg(e)); return false; }
		if (B.active) dropBrowser();
		if (!sv(stay, !shuffle && items[index || 0] ? items[index || 0].p : '')) go('#/playing');
		return true;
	}
}
// one file from where it was left (Recent): the handheld finds its own place (/api/play), this browser starts at pos
export async function playAt(p, pos, target, stay) {
	target = target || store.target;
	if (target === 'browser') {
		// (with its folder after it, as on the handheld: the next song plays when it ends)
		const { items, idx, src } = await withFolder(p);
		if (!await browserPlay(items, idx, src, pos > 0 ? pos : 0)) return false;
		if (!sv(stay, p)) go('#/playing');
		return true;
	}
	const was = B.active && !el.paused; B.leaving = true; el.pause();
	try { await post('/api/play', Object.assign({ p }, hhChoice())); }
	catch (e) { B.leaving = false; if (was && store.conn === 'ok') el.play().catch(() => {}); toast(e.status === 0 ? "Couldn't start it on your handheld" : errMsg(e)); return false; }
	if (B.active) dropBrowser();
	if (!sv(stay, p)) go('#/playing');
	return true;
}
// a row's tap: it plays at once where the last choice was. One this browser can't play asks first: play it on the
// handheld, or not (Cancel, the default: nothing moves without a yes)
export async function playHere(play, p, playable) {
	const where = await whereToPlay(p, playable);
	if (where) await play(where);
}
// where a file picked here plays: the chosen output; one this browser can't play asks (the handheld, or nothing)
export async function whereToPlay(p, playable) {
	if (store.target !== 'browser') return store.target;
	const m = playable === 'no' ? { playable: 'no' } : await probe1(p);
	if (!m || probeOK(m)) return 'browser';
	const yes = await ask("This browser can't play it", baseName(p) + ' plays on your handheld only. Play it there?',
		{ buttons: [{ label: 'Play on handheld', value: true }, { label: 'Cancel', value: null, cls: 'primary', focus: true }] });
	return yes ? 'handheld' : null;
}
// a list from one of its items, on the handheld (Up next's row of a file this browser can't play, said yes to)
export async function playListOnHandheld(paths, index, src) {
	src = src || {};
	try {
		const r = await intent('Open', { q: paths, index, t: '0', skind: src.kind || 'folder', stitle: src.title || '', sid: src.id || '', to: 'handheld' });
		if (r && r.rejected) toast("Couldn't play it: try again");
		return !!r && !r.rejected;
	} catch (e) { toast(e.status === 0 ? "Couldn't reach your handheld" : errMsg(e)); return false; }
}

