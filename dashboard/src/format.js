// @file format.js
// Number and time formatting shared by the dashboard modules.
'use strict';

/** Local wall-clock time (24 h) of an epoch-ms timestamp. */
export function clock(atMs) {
    return new Date(atMs).toLocaleTimeString([], { hour12: false });
}

/** A count with thousands separators, or '–' when unknown. */
export function count(value) {
    return Number.isFinite(value) ? Math.round(value).toLocaleString() : '–';
}

/** A milliseconds duration with one decimal. */
export function ms(value) {
    return `${value.toFixed(1)} ms`;
}

/** Sets a status pill: the state is shown as colour (data-state) and as text. */
export function setPill(pill, state, text) {
    pill.dataset.state = state;
    pill.textContent = text;
}

/** Creates a coloured cache-result tag such as HIT or STALE. */
export function cacheTag(tag, text) {
    const element = document.createElement('span');
    element.className = 'cacheTag';
    element.dataset.cache = tag;
    element.textContent = text;
    return element;
}
