// @file controls.js
// The tools: the request tester (GET / PUT / purge, optionally conditional or no-cache), the simulated
// outage switch and the mini load generator. Every request goes through the router like a real client.
'use strict';

import { describeResult, fetchJson, lastEtagFor, sendKeyRequest, setOutage } from './api.js';
import { cacheTag, ms } from './format.js';
import { showRequest } from './flowDiagram.js';
import { refresh } from './live.js';

let outageOn = false;

/** Shows one request's result in the tester: status, cache result, headers and body. */
function showResponse(result) {
    const reading = describeResult(result);
    const summary = document.getElementById('reqSummary');
    const status = result.status === 0 ? 'no response' : `HTTP ${result.status}`;
    summary.replaceChildren(cacheTag(reading.tag, reading.label), `${status} · ${result.node || 'no edge'} · ${ms(result.latencyMs)}`);
    const list = document.getElementById('reqHeaders');
    list.replaceChildren();
    for (const [name, value] of Object.entries(result.headers)) {
        if (value) {
            const term = document.createElement('dt');
            const description = document.createElement('dd');
            term.textContent = name;
            description.textContent = value;
            list.append(term, description);
        }
    }
    document.getElementById('reqBody').textContent = result.data ? JSON.stringify(result.data, null, 2) : '(no body)';
    document.getElementById('reqResponse').hidden = false;
}

/** Updates the outage switch from the admin endpoint's answer (or its absence). */
export function renderOutage(live) {
    const toggle = document.getElementById('outageToggle');
    const label = toggle.querySelector('.switchLabel');
    const status = document.getElementById('outageStatus');
    if (live.origin) {
        outageOn = live.origin.simulatedOutage;
        toggle.disabled = false;
        toggle.setAttribute('aria-pressed', String(outageOn));
        label.textContent = `Simulated outage: ${outageOn ? 'ON' : 'off'}`;
        status.textContent = outageOn
            ? 'write-api is answering 503. Watch the breakers open, reads turn STALE and writes queue.'
            : 'The origin is serving normally.';
    } else {
        toggle.disabled = true;
        status.textContent = live.originStatus === 404
            ? 'The admin endpoint is disabled (ADMIN_ENABLED is not 1).'
            : 'write-api is unreachable.';
    }
}

/** Wires the request tester, the outage switch and the load generator. */
export function initControls() {
    const methodInputs = document.querySelectorAll('#requestForm input[name="method"]');
    const syncValueField = () => {
        const method = document.querySelector('#requestForm input[name="method"]:checked').value;
        document.getElementById('reqValueField').hidden = method !== 'PUT';
    };
    for (const input of methodInputs) {
        input.addEventListener('change', syncValueField);
    }
    syncValueField();

    document.getElementById('requestForm').addEventListener('submit', async (event) => {
        event.preventDefault();
        const key = document.getElementById('reqKey').value;
        const method = document.querySelector('#requestForm input[name="method"]:checked').value;
        const button = event.submitter || document.querySelector('#requestForm button[type="submit"]');
        button.disabled = true;
        const result = await sendKeyRequest({
            key,
            method,
            value: document.getElementById('reqValue').value,
            ifNoneMatch: document.getElementById('reqConditional').checked ? lastEtagFor(key) : '',
            noCache: document.getElementById('reqNoCache').checked,
        });
        button.disabled = false;
        showResponse(result);
        showRequest(result);
    });

    document.getElementById('outageToggle').addEventListener('click', async () => {
        const toggle = document.getElementById('outageToggle');
        toggle.disabled = true;
        await setOutage(!outageOn);
        await refresh();
    });

    document.getElementById('loadForm').addEventListener('submit', async (event) => {
        event.preventDefault();
        const total = Math.max(1, parseInt(document.getElementById('loadCount').value, 10) || 1);
        const concurrency = Math.max(1, parseInt(document.getElementById('loadConcurrency').value, 10) || 1);
        const range = Math.max(1, parseInt(document.getElementById('loadRange').value, 10) || 1);
        const button = document.getElementById('loadStart');
        const progress = document.getElementById('loadProgress');
        const counts = { HIT: 0, MISS: 0, REVALIDATED: 0, STALE: 0, LIMITED: 0, ERROR: 0 };
        const latencies = [];
        let issued = 0;
        button.disabled = true;

        const worker = async () => {
            while (issued < total) {
                issued += 1;
                const started = performance.now();
                const response = await fetchJson(`/router/key/load-${Math.floor(Math.random() * range)}`, {}, 5000);
                latencies.push(performance.now() - started);
                const cache = response.headers ? response.headers.get('X-Cache') : null;
                if (response.status === 429) {
                    counts.LIMITED += 1;
                } else if (cache in counts) {
                    counts[cache] += 1;
                } else {
                    counts.ERROR += 1;
                }
                if (latencies.length % 25 === 0 || latencies.length === total) {
                    progress.textContent = `${latencies.length} / ${total} done`;
                }
            }
        };
        await Promise.all(Array.from({ length: concurrency }, worker));

        latencies.sort((a, b) => a - b);
        const percentile = (p) => latencies[Math.min(latencies.length - 1, Math.ceil((p / 100) * latencies.length) - 1)];
        const mix = document.getElementById('loadMix');
        mix.replaceChildren();
        const labels = { HIT: 'HIT', MISS: 'MISS', REVALIDATED: 'REVALIDATED', STALE: 'STALE', LIMITED: '429', ERROR: 'error' };
        for (const [tag, value] of Object.entries(counts)) {
            if (value > 0) {
                const segment = document.createElement('span');
                segment.dataset.cache = tag;
                segment.style.width = `${(value / total) * 100}%`;
                segment.title = `${labels[tag]}: ${value}`;
                mix.appendChild(segment);
            }
        }
        mix.setAttribute('aria-label', Object.entries(counts).map(([tag, value]) => `${labels[tag]} ${value}`).join(', '));
        const numbers = document.getElementById('loadNumbers');
        numbers.replaceChildren();
        const rows = [
            ...Object.entries(counts).filter(([, value]) => value > 0).map(([tag, value]) => [labels[tag], String(value)]),
            ['p50', ms(percentile(50))], ['p95', ms(percentile(95))], ['p99', ms(percentile(99))],
        ];
        for (const [label, value] of rows) {
            const wrapper = document.createElement('div');
            const term = document.createElement('dt');
            const description = document.createElement('dd');
            term.textContent = label;
            description.textContent = value;
            wrapper.append(term, description);
            numbers.appendChild(wrapper);
        }
        document.getElementById('loadResult').hidden = false;
        progress.textContent = `${total} requests, ${concurrency} at a time.`;
        button.disabled = false;
    });
}
