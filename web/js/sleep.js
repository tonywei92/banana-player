import { html } from '../vendor/htm-preact-standalone.module.js';
import { Modal } from './components.js';
import { store, useStore } from './store.js';
import { Icon } from './icons.js';
import { openModal, toast } from './toasts.js';
import { errMsg, post } from './api.js';

/* ================= the sleep timer: the handheld's one (src/ui_sleep.h), shown and changed here ================= */
// The state's "sleep": { kind: off|min|end, label ("24 min", "0:48", "Song"), warn (its last minute), at ("22:41"),
// minutes (the length chosen), snooze (minutes), word (song|video|chapter) }. Each change is one POST /api/sleep; the
// handheld answers in the state, so both sides follow at once.
const LENGTHS = [15, 30, 45, 60, 90];
const sleepOf = () => (store.snap && store.snap.sleep) || { kind: 'off', snooze: 10, word: 'song' };
function send(params) { return post('/api/sleep', params).catch(e => toast(errMsg(e))); }
export const sleepSet = min => send({ do: 'set', min });
export const sleepEnd = () => send({ do: 'end' });
export const sleepSnooze = () => send({ do: 'snooze' });
export const sleepOff = () => send({ do: 'off' });
// what is left, for the row and the key ("24 min left", "This song", "0:48")
function leftWords(sl) {
	if (sl.kind === 'off') return 'Off';
	if (sl.warn) return sl.label;
	return sl.kind === 'end' ? 'This ' + (sl.word || 'song') : sl.label + ' left';
}

// the sheet (a phone: from the foot; a desktop: a card over the bar): six chips, and while one runs two keys
export function openSleepSheet() {
	return openModal(close => html`<${SleepSheet} close=${close} />`);
}
function SleepSheet({ close }) {
	useStore();
	const sl = sleepOf(), on = sl.kind !== 'off';
	const pick = f => () => { close(); f(); };
	return html`<${Modal} cls="sheet sleep-sheet" label="Sleep timer" raw=${true} onClose=${close}>
		<div class="sheet-head"><span class="tile xs"><${Icon} n="clock" k=${2} /></span>
			<div class="grow"><div class="t">Sleep timer</div><div class="s">${on ? leftWords(sl) : 'Off'}</div></div></div>
		<div class="sleep-body"><div class="kicker">Pause the handheld after</div>
			<div class="sleep-chips">${LENGTHS.map(m => html`<button class=${'sleep-chip' + (sl.kind === 'min' && sl.minutes === m ? ' on' : '')} aria-pressed=${sl.kind === 'min' && sl.minutes === m} onClick=${pick(() => sleepSet(m))}>${m} min</button>`)}
				<button class=${'sleep-chip' + (sl.kind === 'end' ? ' on' : '')} aria-pressed=${sl.kind === 'end'} onClick=${pick(sleepEnd)}>This ${sl.word || 'song'}</button></div>
			${on && html`<div class="sleep-keys"><button class="btn primary" onClick=${pick(sleepSnooze)}><${Icon} n="plus" k=${2} />${sl.snooze} more min</button>
				<button class="btn" onClick=${pick(sleepOff)}><${Icon} n="cross" k=${2} />Timer off</button></div>
				${sl.at && html`<p class="dim sleep-at">Pauses at ${sl.at}.</p>`}`}</div></${Modal}>`;
}

// Now playing on a phone: a row under the keys ("Off", or what is left in the accent with an accent edge)
export function SleepRow() {
	const sl = sleepOf(), on = sl.kind !== 'off';
	return html`<button class=${'sleep-row box' + (on ? ' on' : '')} aria-haspopup="dialog" onClick=${() => openSleepSheet()}>
		<${Icon} n="clock" k=${2} /><span class="grow">Sleep timer</span><span class="sleep-left">${leftWords(sl)}</span></button>`;
}

// a desktop's width: a key at the right end of the player bar, reading what is left while one runs
export function SleepKey() {
	const sl = sleepOf(), on = sl.kind !== 'off';
	return html`<button class=${'ib opt sleep-key' + (on ? ' on' : '')} aria-label=${on ? 'Sleep timer: ' + leftWords(sl) : 'Sleep timer'} title="Sleep timer"
		aria-haspopup="dialog" onClick=${() => openSleepSheet()}><${Icon} n="clock" k=${2} />${on && html`<span>${sl.label}</span>`}</button>`;
}

// the last minute: a card pinned to the foot of every page (over the bar on a desktop). No sound, no red, no
// notification; whichever side answers, both cards go
export function SleepWarnCard() {
	const s = useStore(), sl = sleepOf();
	if (!sl.warn || s.conn === 'down') return null;
	const words = sl.kind === 'end' ? 'Sleeping after this ' + (sl.word || 'song') + ' · ' + sl.label : 'Sleeping in ' + sl.label;
	return html`<div class="sleep-warn box surface" role="alert"><${Icon} n="clock" k=${2} /><span class="grow">${words}</span>
		<button class="btn sm primary" onClick=${sleepSnooze}>${sl.snooze} more min</button>
		<button class="btn sm" onClick=${sleepOff}>Timer off</button></div>`;
}
