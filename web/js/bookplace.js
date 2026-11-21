import { TAB_ID, lsGet, lsSet } from './store.js';

/* ================= a book's reading place, shared with the handheld (docs/redesign/web-books.md §4) =================
   One place per book, kept by the handheld (doc-places.conf) with a rev bumped on every write. A turn here is saved
   1.5 s after the last one (at once as the page hides), naming the rev it read (base). The handheld's turns (or
   another browser's) come on the events stream as "book-place": the reader shows a note and takes the new rev as its
   base, so its own next turn writes cleanly (last turn wins). A place read on while the handheld couldn't be reached
   waits here ("Not sent yet"); when it answers and its place moved elsewhere meanwhile, the server says 409 and the
   reader asks which to keep (onConflict). The PDF and the comic readers and the text reader all use this one. */

const SAVE_MS = 1500, RETRY_MS = 10000;
const lastKey = p => 'bp-book-last:' + p;
// this browser's own last place in a book (for "Picked up from the handheld …, Back to 4%")
export function localPlace(p) { try { return JSON.parse(lsGet(lastKey(p)) || 'null'); } catch (e) { return null; } }
export const samePlace = (a, b) => !!a && !!b && a.t === b.t && Math.floor(a.pos) === Math.floor(b.pos);
// how far through the book, 0..1 (a text's share; a page of so many)
export const placeFrac = pl => !pl ? 0 : pl.t === 'pages' ? (pl.share > 0 ? (pl.pos + 1) / pl.share : 0) : pl.share;

function form(o) { const b = new URLSearchParams(); for (const [k, v] of Object.entries(o)) if (v != null) b.append(k, v); return b.toString(); }

export class BookPlace {
	// stored: the place /api/book gave (or null). hooks: {status(s), remote(place), conflict(mine, theirs)}
	constructor(p, stored, hooks) {
		this.p = p; this.hooks = hooks || {};
		this.base = stored ? stored.rev : 0;
		this.cur = stored;	// the place shown here
		this.unsent = null; this.sending = false; this.timer = 0; this.status = 'shared'; this.sent = null;
		this.onEvent = e => this.remote(e.detail);
		this.onHide = () => this.flush();
		this.onOnline = () => { if (this.unsent) this.send(); };
		addEventListener('bp-book-place', this.onEvent);
		addEventListener('pagehide', this.onHide);
		addEventListener('online', this.onOnline);
		this.onVis = () => { if (document.visibilityState === 'hidden') this.flush(); };
		document.addEventListener('visibilitychange', this.onVis);
	}
	close() {
		this.flush();
		removeEventListener('bp-book-place', this.onEvent); removeEventListener('pagehide', this.onHide); removeEventListener('online', this.onOnline);
		document.removeEventListener('visibilitychange', this.onVis);
		clearTimeout(this.timer);
	}
	setStatus(s) { if (this.status !== s) { this.status = s; this.hooks.status && this.hooks.status(s); } }
	// a turn here: {t, pos, share}
	turn(pl) {
		// (the place it opened at is the view's first report: nothing turned yet, nothing written)
		if (!this.opened) { this.opened = true; this.cur = pl; return; }
		if (samePlace(pl, this.cur) && Math.abs((pl.share || 0) - (this.cur.share || 0)) < 1e-6) return;
		this.cur = pl; this.unsent = pl;
		lsSet(lastKey(this.p), JSON.stringify(pl));
		this.setStatus('saving');
		clearTimeout(this.timer); this.timer = setTimeout(() => this.send(), SAVE_MS);
	}
	// at once, and still sent as the page goes (fetch with keepalive: sendBeacon can't carry X-BP)
	flush() { if (this.unsent) { clearTimeout(this.timer); this.send(false, true); } }
	async send(force, keepalive) {
		const pl = this.unsent;
		if (!pl || (this.sending && !keepalive)) return;
		this.sending = true;
		const body = form({ p: this.p, t: pl.t, pos: pl.pos, share: pl.share, base: this.base, force: force ? 1 : null, tab: TAB_ID });
		let r = null, j = null;
		try {
			r = await fetch('/api/book/place', { method: 'POST', credentials: 'same-origin', keepalive: !!keepalive, headers: { 'X-BP': '1', 'Content-Type': 'application/x-www-form-urlencoded' }, body });
			try { j = await r.json(); } catch (e) {}
		} catch (e) { r = null; }
		this.sending = false;
		if (!r || r.status === 0 || r.status >= 500) {
			// the handheld is asleep or off the network: the place waits here
			this.setStatus('unsent');
			clearTimeout(this.timer); this.timer = setTimeout(() => this.send(), RETRY_MS);
			return;
		}
		if (this.unsent === pl) this.unsent = null;
		if (r.status === 409 && j && j.place) {
			this.base = j.place.rev;
			this.setStatus('shared');
			this.hooks.conflict && this.hooks.conflict(pl, j.place);
			return;
		}
		if (r.ok && j && j.place) { this.base = Math.max(this.base, j.place.rev); this.sent = j.place; }
		this.setStatus(this.unsent ? 'saving' : 'shared');
		if (this.unsent) { clearTimeout(this.timer); this.timer = setTimeout(() => this.send(), SAVE_MS); }
	}
	// keep mine over the handheld's (the Two places sheet's choice)
	keepMine(pl) { this.unsent = pl; this.cur = pl; this.send(true); }
	// take theirs: the reader goes there
	takeTheirs(pl) { this.cur = pl; this.base = Math.max(this.base, pl.rev || 0); lsSet(lastKey(this.p), JSON.stringify(pl)); }
	// "book-place" from the stream: another device's write (our own comes back too: same place, skipped)
	remote(d) {
		if (!d || d.p !== this.p || !d.place) return;
		const pl = d.place;
		if (pl.rev <= this.base) return;
		this.base = pl.rev;
		if (this.sent && samePlace(pl, this.sent) && pl.rev === this.sent.rev) return;
		if (samePlace(pl, this.cur)) return;
		this.hooks.remote && this.hooks.remote(pl);
	}
}
