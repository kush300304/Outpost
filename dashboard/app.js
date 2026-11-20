// @file app.js
// Outpost dashboard logic. Polls every edge's /debug/stats, the router and the origin admin endpoint
// once per second through the nginx proxy, renders topology, per-edge panels and the merged event
// log, and drives the request tester, the simulated-outage toggle and the mini load generator.
// Every number shown comes from a live response or is measured in this browser.
'use strict';

const EDGES = [
    { id: 'edge-1', base: '/edge1' },
    { id: 'edge-2', base: '/edge2' },
    { id: 'edge-3', base: '/edge3' },
];
const POLL_MS = 1000;
const HISTORY = 60;
const history = {};
const panels = {};
let outageOn = false;

/** Fetches url as JSON with a timeout; resolves to {ok, status, data} and never rejects. */
async function fetchJson(url, options = {}, timeoutMs = 900) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    let result = { ok: false, status: 0, data: null };
    try {
        const response = await fetch(url, { ...options, signal: controller.signal, cache: 'no-store' });
        const text = await response.text();
        let data = null;
        try {
            data = JSON.parse(text);
        } catch (parseError) {
            data = { raw: text };
        }
        result = { ok: response.ok, status: response.status, data, headers: response.headers };
    } catch (networkError) {
        result = { ok: false, status: 0, data: null };
    }
    clearTimeout(timer);
    return result;
}

/** Sets a status pill: the state is shown as colour (data-state) and as text. */
function setPill(pill, state, text) {
    pill.dataset.state = state;
    pill.textContent = text || state;
}

/** Draws values (0..max) as a polyline in an inline SVG sparkline. */
function drawSpark(svg, values, max) {
    const width = 120;
    const height = 24;
    const top = max > 0 ? max : 1;
    const step = values.length > 1 ? width / (values.length - 1) : width;
    const points = values.map((value, index) => {
        const y = height - 2 - (Math.min(value, top) / top) * (height - 4);
        return `${index === 0 ? 'M' : 'L'}${(index * step).toFixed(1)},${y.toFixed(1)}`;
    });
    svg.querySelector('path').setAttribute('d', points.join(' '));
}

/** Formats an epoch-ms timestamp as local wall-clock time. */
function clock(atMs) {
    return new Date(atMs).toLocaleTimeString([], { hour12: false });
}

function renderOrigin(response) {
    const node = document.querySelector('.node[data-node="origin"]');
    const toggle = document.getElementById('outageToggle');
    const status = document.getElementById('outageStatus');
    const admin = response.ok ? response.data : null;
    if (admin) {
        outageOn = admin.simulatedOutage;
        let state = 'healthy';
        let label = 'healthy';
        if (admin.simulatedOutage) {
            state = 'down';
            label = 'down (simulated)';
        } else if (!admin.storageHealthy) {
            state = 'down';
            label = 'down (storage unreachable)';
        }
        setPill(node.querySelector('.pill'), state, label);
        node.querySelector('.detail').textContent = admin.storageHealthy ? 'storage answering' : 'storage not answering';
        toggle.disabled = false;
        toggle.setAttribute('aria-pressed', String(outageOn));
        toggle.textContent = outageOn ? 'Stop simulated outage' : 'Start simulated outage';
        status.textContent = outageOn ? 'Simulated outage is ON: write-api answers 503.' : 'Simulated outage is off.';
    } else {
        setPill(node.querySelector('.pill'), 'down', response.status === 404 ? 'unknown (admin off)' : 'down (unreachable)');
        node.querySelector('.detail').textContent = '';
        toggle.disabled = response.status === 404;
        status.textContent = response.status === 404
            ? 'The admin endpoint is disabled (ADMIN_ENABLED is not 1).'
            : 'write-api is unreachable.';
    }
}

async function poll() {
    const started = performance.now();
    const responses = await Promise.all([
        ...EDGES.map((edge) => fetchJson(`${edge.base}/debug/stats`)),
        fetchJson('/router/health'),
        fetchJson('/origin-admin/outage'),
    ]);
    EDGES.forEach((edge, index) => {
        const response = responses[index];
        const panel = panels[edge.id];
        const node = document.querySelector(`.node[data-node="${edge.id}"]`);
        const stats = response.ok ? response.data : null;
        if (stats) {
            const breaker = stats.circuitBreaker.state;
            const state = breaker === 'CLOSED' ? 'healthy' : 'degraded';
            const label = breaker === 'CLOSED' ? 'healthy' : `degraded (${breaker})`;
            setPill(panel.querySelector('.pill'), state, label);
            setPill(node.querySelector('.pill'), state, label);
            node.querySelector('.detail').textContent =
                `hit rate ${stats.cache.hitRate.toFixed(1)} % · queue ${stats.writeQueue.pending}`;

            const track = history[edge.id];
            track.hitRate.push(stats.cache.hitRate);
            track.pending.push(stats.writeQueue.pending);
            track.hitRate.splice(0, Math.max(0, track.hitRate.length - HISTORY));
            track.pending.splice(0, Math.max(0, track.pending.length - HISTORY));
            drawSpark(panel.querySelector('.hitSpark'), track.hitRate, 100);
            drawSpark(panel.querySelector('.queueSpark'), track.pending, Math.max(...track.pending, 1));

            panel.querySelector('.hitRate').textContent = `${stats.cache.hitRate.toFixed(1)} %`;
            panel.querySelector('.entries').textContent = `${stats.cache.size} / ${stats.cache.capacity}`;
            panel.querySelector('.breaker').textContent =
                `${breaker} (${stats.circuitBreaker.consecutiveFailures}/${stats.circuitBreaker.failureThreshold} failures)`;
            panel.querySelector('.tokens').textContent =
                `${Math.floor(stats.rateLimiter.callerTokensRemaining)} / ${stats.rateLimiter.capacity}`;
            panel.querySelector('.pending').textContent = String(stats.writeQueue.pending);
            panel.querySelector('.staleAnd429').textContent =
                `${stats.cache.staleServes} / ${stats.rateLimiter.rejected}`;

            const list = panel.querySelector('.transitions');
            list.replaceChildren();
            const recent = stats.circuitBreaker.recentTransitions.slice(-5).reverse();
            for (const transition of recent) {
                const item = document.createElement('li');
                item.textContent = `${clock(transition.atMs)} ${transition.from} → ${transition.to}`;
                list.appendChild(item);
            }
            if (recent.length === 0) {
                const item = document.createElement('li');
                item.className = 'muted';
                item.textContent = 'none yet';
                list.appendChild(item);
            }
        } else {
            setPill(panel.querySelector('.pill'), 'down', 'down (unreachable)');
            setPill(node.querySelector('.pill'), 'down', 'down (unreachable)');
            node.querySelector('.detail').textContent = '';
        }
    });
    {
        const response = responses[EDGES.length];
        const node = document.querySelector('.node[data-node="router"]');
        const health = response.ok ? response.data : null;
        if (health) {
            const complete = health.edgesInRing === health.edgesConfigured;
            setPill(node.querySelector('.pill'), complete ? 'healthy' : 'degraded',
                complete ? 'healthy' : 'degraded (edge missing)');
            node.querySelector('.detail').textContent =
                `${health.edgesInRing} of ${health.edgesConfigured} edges on the ring`;
        } else {
            setPill(node.querySelector('.pill'), 'down', 'down (unreachable)');
            node.querySelector('.detail').textContent = '';
        }
    }
    renderOrigin(responses[EDGES.length + 1]);
    {
        const seen = new Set();
        const events = [];
        EDGES.forEach((edge, index) => {
            const stats = responses[index].ok ? responses[index].data : null;
            for (const event of stats ? stats.events : []) {
                const id = `${edge.id}|${event.atMs}|${event.message}`;
                if (!seen.has(id)) {
                    seen.add(id);
                    events.push({ ...event, node: edge.id });
                }
            }
        });
        events.sort((a, b) => b.atMs - a.atMs);
        const body = document.getElementById('events');
        body.replaceChildren();
        for (const event of events.slice(0, 100)) {
            const row = document.createElement('tr');
            for (const text of [clock(event.atMs), event.node, event.type, event.message]) {
                const cell = document.createElement('td');
                cell.textContent = text;
                row.appendChild(cell);
            }
            body.appendChild(row);
        }
    }
    const reachable = responses.slice(0, EDGES.length).filter((response) => response.ok).length;
    document.getElementById('pollStatus').textContent =
        `Live · updated ${clock(Date.now())} · ${reachable}/${EDGES.length} edges answering`;
    setTimeout(poll, Math.max(0, POLL_MS - (performance.now() - started)));
}

async function sendRequest(event) {
    event.preventDefault();
    const key = document.getElementById('reqKey').value;
    const method = document.getElementById('reqMethod').value;
    const value = document.getElementById('reqValue').value;
    const options = method === 'PUT' ? { method, body: value, headers: { 'Content-Type': 'text/plain' } } : { method };
    const started = performance.now();
    const response = await fetchJson(`/router/key/${encodeURIComponent(key)}`, options, 5000);
    const latency = performance.now() - started;
    const header = (name) => (response.headers ? response.headers.get(name) : null) || '–';
    const rows = [
        ['Status', response.status === 0 ? 'no response' : String(response.status)],
        ['Served by', header('X-Node')],
        ['X-Cache', header('X-Cache')],
        ['X-Write-Mode', header('X-Write-Mode')],
        ['Latency', `${latency.toFixed(1)} ms (browser-measured)`],
    ];
    const list = document.getElementById('reqResult');
    list.replaceChildren();
    for (const [term, description] of rows) {
        const dt = document.createElement('dt');
        const dd = document.createElement('dd');
        dt.textContent = term;
        dd.textContent = description;
        list.append(dt, dd);
    }
    document.getElementById('reqBody').textContent = response.data ? JSON.stringify(response.data) : '';
}

async function toggleOutage() {
    const toggle = document.getElementById('outageToggle');
    toggle.disabled = true;
    const response = await fetchJson('/origin-admin/outage', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ enabled: !outageOn }),
    }, 3000);
    renderOrigin(response);
}

async function runLoad(event) {
    event.preventDefault();
    const total = Math.max(1, parseInt(document.getElementById('loadCount').value, 10) || 1);
    const concurrency = Math.max(1, parseInt(document.getElementById('loadConcurrency').value, 10) || 1);
    const range = Math.max(1, parseInt(document.getElementById('loadRange').value, 10) || 1);
    const button = document.getElementById('loadStart');
    const progress = document.getElementById('loadProgress');
    const counts = { HIT: 0, MISS: 0, STALE: 0, 429: 0, other: 0 };
    const latencies = [];
    let issued = 0;
    button.disabled = true;

    const worker = async () => {
        while (issued < total) {
            issued += 1;
            const key = `load-${Math.floor(Math.random() * range)}`;
            const started = performance.now();
            const response = await fetchJson(`/router/key/${key}`, {}, 5000);
            latencies.push(performance.now() - started);
            const cache = response.headers ? response.headers.get('X-Cache') : null;
            if (response.status === 429) {
                counts[429] += 1;
            } else if (cache === 'HIT' || cache === 'MISS' || cache === 'STALE') {
                counts[cache] += 1;
            } else {
                counts.other += 1;
            }
            progress.textContent = `${latencies.length} / ${total} done`;
        }
    };
    await Promise.all(Array.from({ length: concurrency }, worker));

    latencies.sort((a, b) => a - b);
    const percentile = (p) => latencies[Math.min(latencies.length - 1, Math.ceil((p / 100) * latencies.length) - 1)];
    const rows = [
        ['HIT', counts.HIT], ['MISS', counts.MISS], ['STALE', counts.STALE], ['429', counts[429]],
        ['other', counts.other],
        ['p50', `${percentile(50).toFixed(1)} ms`], ['p95', `${percentile(95).toFixed(1)} ms`],
        ['p99', `${percentile(99).toFixed(1)} ms`],
    ];
    const list = document.getElementById('loadResult');
    list.replaceChildren();
    for (const [term, description] of rows) {
        const dt = document.createElement('dt');
        const dd = document.createElement('dd');
        dt.textContent = term;
        dd.textContent = String(description);
        list.append(dt, dd);
    }
    progress.textContent = `${total} requests at concurrency ${concurrency} (browser-measured, not a benchmark)`;
    button.disabled = false;
}

{
    const template = document.getElementById('edgePanelTemplate');
    const container = document.getElementById('edgePanels');
    for (const edge of EDGES) {
        const panel = template.content.firstElementChild.cloneNode(true);
        panel.querySelector('.edgeName').textContent = edge.id;
        container.appendChild(panel);
        panels[edge.id] = panel;
        history[edge.id] = { hitRate: [], pending: [] };
    }
}
document.getElementById('requestForm').addEventListener('submit', sendRequest);
document.getElementById('outageToggle').addEventListener('click', toggleOutage);
document.getElementById('loadForm').addEventListener('submit', runLoad);
poll();
