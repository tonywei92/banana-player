import { html } from '../vendor/htm-preact-standalone.module.js';
import { errToast, get, post } from './api.js';
import { ask, toast } from './toasts.js';
import { Icon } from './icons.js';
import { entriesFromDrop, entriesFromInput, startUpload } from './uploads.js';

/* ================= Upload on the Videos, Music and Books tabs =================
   The files go into that section's folder (Videos, Music, Books at the top of a card). The card is chosen first, only
   the cards that are there (SD1, SD2): the one that already has the section's folder is the default, else the main
   card. A card in a phone's file picker must be opened from the tap itself, so the card is asked before the picker and
   the folder is made (POST /api/library/folder) once files are chosen. `cards` is /api/library's answer. */
const NAMES = { videos: 'Videos', music: 'Music', books: 'Books' };

// the card Upload goes to by default: the first one that has the folder, else the first (the main card)
export function defaultCard(cards, kind) {
	return cards.find(c => c[kind]) || cards[0];
}

// what a section takes (the kinds its index reads)
const KINDS = { videos: /\.(mp4|mkv|webm|avi|mov|m4v|wmv|mpg|mpeg|ts|m2ts|flv|3gp)$/i, music: /\.(mp3|wav|flac|ogg|opus|m4a|aac|ape|wma|alac)$/i, books: /\.(epub|pdf|cbz|cbr)$/i };
const KIND_HELP = { videos: 'video files', music: 'music files', books: 'EPUB, PDF, CBZ or CBR files' };
export const sectionOf = page => KINDS[page] ? page : null;

// a drop on the Videos, Music or Books tab: the same card question and folder as Upload
export async function dropToSection(kind, dt) {
	const all = await entriesFromDrop(dt);
	const ents = all.filter(e => KINDS[kind].test(e.file.name));
	if (!ents.length) return toast(all.length ? 'Only ' + KIND_HELP[kind] + ' go into ' + NAMES[kind] : 'Nothing to upload');
	let cards;
	try { cards = (await get('/api/library', null, { quiet: true })).cards; } catch (err) { return errToast(err); }
	if (ents.length < all.length) toast('Skipped ' + (all.length - ents.length) + ' file(s) that are not ' + KIND_HELP[kind]);
	const card = await chooseCard(kind, cards);
	if (!card) return;
	try { const r = await post('/api/library/folder', { kind, card: card.card }); startUpload(ents, r.p); } catch (err) { errToast(err); }
}

async function chooseCard(kind, cards) {
	if (!cards || !cards.length) { toast('No card to upload to'); return null; }
	let card = defaultCard(cards, kind);
	if (cards.length > 1) {
		const pick = await ask('Upload to ' + NAMES[kind], 'Choose the card. The ' + NAMES[kind] + ' folder is made there if it is missing.',
			{ buttons: [{ label: 'Cancel', value: null }, ...cards.map(c => ({ label: c.name + (c[kind] ? '' : ' (new folder)'), value: c, cls: c === card ? 'primary' : '', focus: c === card }))] });
		if (!pick) return null;
		card = pick;
	}
	return card;
}

export async function uploadToSection(kind, cards) {
	const card = await chooseCard(kind, cards);
	if (!card) return;
	const inp = document.createElement('input');
	inp.type = 'file';
	inp.multiple = true;
	inp.onchange = async e => {
		const ents = entriesFromInput(e.target.files);
		if (!ents.length) return;
		try {
			const r = await post('/api/library/folder', { kind, card: card.card });
			startUpload(ents, r.p);
		} catch (err) { errToast(err); }
	};
	inp.click();
}

// the tab's Upload button (in its header row, before the app menu)
export function SectionUpload({ kind, cards }) {
	return html`<button class="btn" title=${'Upload to ' + NAMES[kind]} onClick=${() => uploadToSection(kind, cards)}><${Icon} n="up" k=${2} />Upload</button>`;
}
