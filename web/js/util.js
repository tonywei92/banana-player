/* ================= helpers ================= */
export const fmtTime = s => { s = Math.max(0, Math.floor(s || 0)); const h = Math.floor(s / 3600), m = Math.floor(s % 3600 / 60), x = s % 60; return (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(x).padStart(2, '0'); };
export function fmtSize(b) { if (b == null) return ''; const u = ['B', 'KB', 'MB', 'GB', 'TB']; let i = 0; while (b >= 1000 && i < 4) { b /= 1000; i++; } return (i > 1 ? b.toFixed(1) : Math.round(b)) + ' ' + u[i]; }
export function fmtDate(t) { return t ? new Date(t * 1000).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' }) : ''; }
export const baseName = p => p.split('/').pop();
export const dirName = p => p.split('/').slice(0, -1).join('/');
export const joinPath = (d, n) => d ? d + '/' + n : n;
export const stem = n => n.replace(/\.[^.]+$/, '');
export const plural = (n, a, b) => n.toLocaleString() + ' ' + (n === 1 ? a : b);
export const cmpName = (a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' });
// what a file is called everywhere: its tag title, else its name without the extension and a leading track number
export function titleFor(p, tag) {
	const name = baseName(p || '');
	if (tag && tag !== name && tag !== stem(name)) return tag;
	const s = stem(name), t = s.replace(/^\d{1,3}(\s*[-._]\s*|\s+)(?=\S)/, '');
	return t || s;
}
export const chapterTitle = (c, i) => c && c.title && !/^\(?unnamed\)?$/i.test(c.title) ? c.title : 'Chapter ' + (i + 1);
export const itemsText = (n, more) => more ? (n || 9999).toLocaleString() + '+ items' : plural(n || 0, 'item', 'items');
const EXT = { mp3: 'audio', wav: 'audio', flac: 'audio', ogg: 'audio', opus: 'audio', m4a: 'audio', aac: 'audio', ape: 'audio', wma: 'audio', mp4: 'video', mkv: 'video', webm: 'video', avi: 'video', mov: 'video', m4v: 'video', wmv: 'video', mpg: 'video', jpg: 'photo', jpeg: 'photo', png: 'photo', gif: 'photo', webp: 'photo', bmp: 'photo', srt: 'sub', ass: 'sub', ssa: 'sub', vtt: 'sub', sub: 'sub', idx: 'sub', lrc: 'sub', txt: 'text', epub: 'epub', pdf: 'pdf', cbz: 'comic', cbr: 'comic' };
export const kindOfName = n => EXT[(n.split('.').pop() || '').toLowerCase()] || 'other';
export const isIOS = /iP(hone|ad|od)/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
export const touchOnly = matchMedia('(hover: none)').matches;
export const throttled = (fn, ms) => { let t = 0, timer = 0, last; return (...a) => { last = a; const wait = ms - (Date.now() - t); clearTimeout(timer); if (wait <= 0) { t = Date.now(); fn(...last); } else timer = setTimeout(() => { t = Date.now(); fn(...last); }, wait); }; };
// a tap or a key, right now: what may start playback (never a reconnect, a timer or a headset coming back)
export const userTap = () => !navigator.userActivation || navigator.userActivation.isActive;

