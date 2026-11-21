// @file panels.js
// Detail panels: one card per edge (cache, breaker, queue, validators, rate limiter, recent breaker
// changes), the router card (ring and how requests spread over the edges), the origin card (write-api
// and storage, including writes per fsync) and the merged, filterable event log.
'use strict';

import { EDGES } from './api.js';
import { clock, count, setPill } from './format.js';

const HISTORY = 60;
const panels = {};
const history = {};
let eventFilter = 'all';
let lastLive = null;

/** Draws values (0..max) as a polyline in an inline SVG sparkline. */
function drawSpark(svg, values, max) {
    const width = 120;
    const height = 32;
    const top = max > 0 ? max : 1;
    const step = values.length > 1 ? width / (values.length - 1) : width;
    const points = values.map((value, index) => {
        const y = height - 3 - (Math.min(value, top) / top) * (height - 6);
        return `${index === 0 ? 'M' : 'L'}${(index * step).toFixed(1)},${y.toFixed(1)}`;
    });
    svg.querySelector('path').setAttribute('d', points.join(' '));
}

/** Fills a <dl class="metrics"> with [label, value, explanation] rows. */
function fillMetrics(list, rows) {
    list.replaceChildren();
    for (const [label, value, explanation] of rows) {
        const wrapper = document.createElement('div');
        const term = document.createElement('dt');
        const description = document.createElement('dd');
        term.textContent = label;
        if (explanation) {
            term.title = explanation;
        }
        description.textContent = value;
        wrapper.append(term, description);
        list.appendChild(wrapper);
    }
}

/** Creates the three edge cards from the template. */
export function initPanels() {
    const template = document.getElementById('edgePanelTemplate');
    const container = document.getElementById('edgePanels');
    for (const edge of EDGES) {
        const panel = template.content.firstElementChild.cloneNode(true);
        panel.querySelector('.edgeName').textContent = edge.id;
        container.appendChild(panel);
        panels[edge.id] = panel;
        history[edge.id] = { hitRate: [], pending: [] };
    }
    for (const chip of document.querySelectorAll('#eventFilters .chip')) {
        chip.addEventListener('click', () => {
            eventFilter = chip.dataset.type;
            for (const other of document.querySelectorAll('#eventFilters .chip')) {
                other.setAttribute('aria-pressed', String(other === chip));
            }
            if (lastLive) {
                renderEvents(lastLive);
            }
        });
    }
}

/** Updates the edge cards. */
export function renderEdgePanels(live) {
    for (const edge of EDGES) {
        const panel = panels[edge.id];
        const stats = live.edges[edge.id];
        const pill = panel.querySelector('.pill');
        if (!stats) {
            setPill(pill, 'down', 'unreachable');
        } else {
            const breaker = stats.circuitBreaker;
            setPill(pill, breaker.state === 'CLOSED' ? 'healthy' : 'degraded',
                breaker.state === 'CLOSED' ? 'healthy' : `degraded · ${breaker.state}`);
            const track = history[edge.id];
            track.hitRate.push(stats.cache.hitRate);
            track.pending.push(stats.writeQueue.pending);
            track.hitRate.splice(0, Math.max(0, track.hitRate.length - HISTORY));
            track.pending.splice(0, Math.max(0, track.pending.length - HISTORY));
            drawSpark(panel.querySelector('.hitSpark'), track.hitRate, 100);
            drawSpark(panel.querySelector('.queueSpark'), track.pending, Math.max(...track.pending, 1));

            panel.querySelector('.hitRate').textContent = `${stats.cache.hitRate.toFixed(1)} %`;
            panel.querySelector('.entries').textContent = `${count(stats.cache.size)} / ${count(stats.cache.capacity)}`;
            panel.querySelector('.breaker').textContent = breaker.state === 'CLOSED'
                ? `CLOSED · ${breaker.consecutiveFailures}/${breaker.failureThreshold} failures`
                : breaker.state;
            panel.querySelector('.pending').textContent = count(stats.writeQueue.pending);
            panel.querySelector('.stale').textContent = count(stats.cache.staleServes);
            panel.querySelector('.validators').textContent =
                `${count(stats.cache.revalidated)} / ${count(stats.cache.notModified)}`;
            panel.querySelector('.purges').textContent = count(stats.cache.purges);
            panel.querySelector('.tokens').textContent =
                `${Math.floor(stats.rateLimiter.callerTokensRemaining)} / ${count(stats.rateLimiter.capacity)}`;
            panel.querySelector('.limited').textContent = count(stats.rateLimiter.rejected);

            const list = panel.querySelector('.transitions');
            list.replaceChildren();
            const recent = stats.circuitBreaker.recentTransitions.slice(-4).reverse();
            for (const transition of recent) {
                const item = document.createElement('li');
                item.textContent = `${clock(transition.atMs)}  ${transition.from} → ${transition.to}`;
                list.appendChild(item);
            }
            if (recent.length === 0) {
                const item = document.createElement('li');
                item.textContent = 'none since start';
                list.appendChild(item);
            }
        }
    }
}

/** Updates the router card: ring membership, totals and how requests spread over the edges. */
export function renderRouter(live) {
    const pill = document.getElementById('routerPill');
    const stats = live.routerStats;
    const bars = document.getElementById('routerBars');
    if (!stats || !live.router) {
        setPill(pill, 'down', 'unreachable');
        fillMetrics(document.getElementById('routerMetrics'), []);
        bars.replaceChildren();
    } else {
        const complete = live.router.edgesInRing === live.router.edgesConfigured;
        setPill(pill, complete ? 'healthy' : 'degraded', complete ? 'healthy' : 'edge missing');
        fillMetrics(document.getElementById('routerMetrics'), [
            ['Edges on the ring', `${live.router.edgesInRing} of ${live.router.edgesConfigured}`,
                'Edges the router currently sends keys to. An edge leaves after failed health checks; only its keys move.'],
            ['Ring points', count(stats.virtualNodes), 'Virtual nodes: each edge appears many times on the hash ring so keys spread evenly.'],
            ['Requests routed', count(stats.requests), '/key requests the router forwarded since it started.'],
            ['Failovers / purges', `${count(stats.failovers)} / ${count(stats.purges)}`,
                'Failovers: an edge refused a connection and the request was retried on the next owner. Purges: fan-outs to every edge.'],
        ]);
        const total = stats.edges.reduce((sum, edge) => sum + edge.routed, 0);
        bars.replaceChildren();
        for (const edge of stats.edges) {
            const share = total > 0 ? (edge.routed / total) * 100 : 0;
            const row = document.createElement('div');
            row.className = 'bar';
            const name = document.createElement('span');
            name.textContent = edge.id;
            const track = document.createElement('div');
            track.className = 'barTrack';
            const fill = document.createElement('div');
            fill.className = 'barFill';
            fill.style.width = `${share.toFixed(1)}%`;
            track.appendChild(fill);
            const value = document.createElement('span');
            value.className = 'barValue';
            value.textContent = `${share.toFixed(1)} % · ${count(edge.routed)}`;
            row.append(name, track, value);
            row.setAttribute('aria-label', `${edge.id}: ${share.toFixed(1)} percent, ${edge.routed} requests`);
            bars.appendChild(row);
        }
    }
}

/** Updates the origin card: simulated outage, storage reachability, keys, put outcomes, group commit. */
export function renderOrigin(live) {
    const pill = document.getElementById('originPill');
    const storage = live.storage;
    const origin = live.origin;
    if (origin && origin.simulatedOutage) {
        setPill(pill, 'down', 'outage (simulated)');
    } else if (origin && origin.storageHealthy) {
        setPill(pill, 'healthy', 'healthy');
    } else if (origin) {
        setPill(pill, 'down', 'storage unreachable');
    } else {
        setPill(pill, live.originStatus === 404 ? 'unknown' : 'down',
            live.originStatus === 404 ? 'admin off' : 'write-api unreachable');
    }
    const recent = live.storageWindow;
    const perFsync = recent && recent.fsyncs > 0
        ? `${(recent.appends / recent.fsyncs).toFixed(2)} (${count(recent.appends)} writes, last ${recent.seconds} s)`
        : 'no writes in the last 10 s';
    fillMetrics(document.getElementById('originMetrics'), storage ? [
        ['Keys stored', count(storage.totalKeys), 'Keys in storage, the source of truth.'],
        ['Durable', storage.durable ? 'yes: write-ahead log + fsync' : 'no (in memory)',
            'Every applied write is in the write-ahead log and flushed to disk before it is acknowledged.'],
        ['Writes per fsync', perFsync,
            'Group commit: writes that arrive together share one disk flush, so durability costs less under load.'],
        ['Total WAL appends / fsyncs', `${count(storage.wal.appends)} / ${count(storage.wal.fsyncs)}`,
            'Since storage started.'],
        ['Puts applied / duplicate / superseded',
            `${count(storage.puts.applied)} / ${count(storage.puts.duplicates)} / ${count(storage.puts.superseded)}`,
            'Last-writer-wins on (version, writer): a replayed write is a duplicate, an older one is superseded; neither changes data.'],
        ['Recovered at start-up', `${count(storage.walRecovery.validRecords)} records`,
            'Valid write-ahead log records replayed when storage last started.'],
    ] : [['Storage', 'not answering', '']]);
}

/** Updates the merged event log, newest first, with the selected type filter. */
export function renderEvents(live) {
    lastLive = live;
    const seen = new Set();
    const events = [];
    for (const edge of EDGES) {
        const stats = live.edges[edge.id];
        for (const event of stats ? stats.events : []) {
            const id = `${edge.id}|${event.atMs}|${event.message}`;
            if (!seen.has(id) && (eventFilter === 'all' || event.type === eventFilter)) {
                seen.add(id);
                events.push({ ...event, node: edge.id });
            }
        }
    }
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
    if (events.length === 0) {
        const row = document.createElement('tr');
        const cell = document.createElement('td');
        cell.colSpan = 4;
        cell.className = 'emptyRow';
        cell.textContent = eventFilter === 'all' ? 'No events yet.' : `No ${eventFilter} events yet.`;
        row.appendChild(cell);
        body.appendChild(row);
    }
}
