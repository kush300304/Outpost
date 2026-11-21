// @file main.js
// Entry point: builds the panels and the guided demo, wires the tools and the theme switch, and renders
// every live view after each poll. Every number shown comes from a live response or is measured in this
// browser.
'use strict';

import { EDGES } from './api.js';
import { initControls, renderOutage } from './controls.js';
import { clock } from './format.js';
import { renderFlow } from './flowDiagram.js';
import { initGuidedDemo } from './guidedDemo.js';
import { onUpdate, startPolling } from './live.js';
import { initPanels, renderEdgePanels, renderEvents, renderOrigin, renderRouter } from './panels.js';

const THEMES = ['auto', 'light', 'dark'];
const THEME_KEY = 'outpost.theme';

/** Applies a theme: 'auto' follows the system setting, 'light' and 'dark' override it. */
function applyTheme(theme) {
    if (theme === 'auto') {
        delete document.documentElement.dataset.theme;
    } else {
        document.documentElement.dataset.theme = theme;
    }
    const button = document.getElementById('themeToggle');
    button.textContent = `Theme: ${theme}`;
    button.setAttribute('aria-label', `Colour theme: ${theme === 'auto' ? 'automatic' : theme}. Click to change.`);
}

let theme = 'auto';
try {
    theme = THEMES.includes(localStorage.getItem(THEME_KEY)) ? localStorage.getItem(THEME_KEY) : 'auto';
} catch (storageError) {
    theme = 'auto';
}
applyTheme(theme);
document.getElementById('themeToggle').addEventListener('click', () => {
    theme = THEMES[(THEMES.indexOf(theme) + 1) % THEMES.length];
    applyTheme(theme);
    try {
        localStorage.setItem(THEME_KEY, theme);
    } catch (storageError) {
        // The choice still applies to this page; it is just not remembered.
    }
});

initPanels();
initControls();
initGuidedDemo();

onUpdate((live) => {
    renderFlow(live);
    renderEdgePanels(live);
    renderRouter(live);
    renderOrigin(live);
    renderOutage(live);
    renderEvents(live);
    const answering = EDGES.filter((edge) => live.edges[edge.id]).length;
    const status = document.getElementById('liveStatus');
    status.dataset.state = answering === EDGES.length ? 'healthy' : (answering > 0 ? 'degraded' : 'down');
    status.textContent = `Live · ${clock(live.updatedAt)} · ${answering}/${EDGES.length} edges answering`;
});
startPolling();
