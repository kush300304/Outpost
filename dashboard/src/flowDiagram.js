// @file flowDiagram.js
// The "How a request flows" diagram: live state of every service on its node, and the path the last
// request took (client → router → edge, and on to write-api → storage when the origin was involved),
// with the cache result on the edge that served it.
'use strict';

import { EDGES, describeResult } from './api.js';
import { cacheTag, count, ms, setPill } from './format.js';

const flow = () => document.getElementById('flow');
const nodeOf = (id) => flow().querySelector(`.flowNode[data-node="${id}"]`);
const hopOf = (id) => flow().querySelector(`.hop[data-hop="${id}"]`);

/** Updates every node's state pill and one-line summary from the latest poll. */
export function renderFlow(live) {
    for (const edge of EDGES) {
        const node = nodeOf(edge.id);
        const stats = live.edges[edge.id];
        const pill = node.querySelector('.pill');
        const sub = node.querySelector('.nodeSub');
        if (!stats) {
            setPill(pill, 'down', 'unreachable');
            sub.textContent = 'not answering';
        } else {
            const breaker = stats.circuitBreaker.state;
            const labels = { CLOSED: 'healthy', OPEN: 'degraded', HALF_OPEN: 'probing origin' };
            setPill(pill, breaker === 'CLOSED' ? 'healthy' : 'degraded', labels[breaker] || breaker);
            sub.textContent = `hit rate ${stats.cache.hitRate.toFixed(0)} % · ${count(stats.writeQueue.pending)} queued`;
        }
    }

    const router = live.router;
    const routerPill = nodeOf('router').querySelector('.pill');
    if (!router) {
        setPill(routerPill, 'down', 'unreachable');
    } else {
        const complete = router.edgesInRing === router.edgesConfigured;
        setPill(routerPill, complete ? 'healthy' : 'degraded',
            complete ? 'healthy' : `${router.edgesInRing}/${router.edgesConfigured} edges`);
    }

    const apiPill = nodeOf('write-api').querySelector('.pill');
    const apiSub = nodeOf('write-api').querySelector('.nodeSub');
    if (live.origin && live.origin.simulatedOutage) {
        setPill(apiPill, 'down', 'outage (simulated)');
        apiSub.textContent = 'answering 503 on purpose';
    } else if (live.origin) {
        setPill(apiPill, 'healthy', 'healthy');
        apiSub.textContent = 'validates and forwards';
    } else if (live.originStatus === 404) {
        setPill(apiPill, 'unknown', 'admin off');
        apiSub.textContent = 'ADMIN_ENABLED is not 1';
    } else {
        setPill(apiPill, 'down', 'unreachable');
        apiSub.textContent = 'not answering';
    }

    const storagePill = nodeOf('storage').querySelector('.pill');
    const storageSub = nodeOf('storage').querySelector('.nodeSub');
    if (live.storage) {
        setPill(storagePill, 'healthy', 'healthy');
        const recent = live.storageWindow;
        const perFsync = recent && recent.fsyncs > 0 ? ` · ${(recent.appends / recent.fsyncs).toFixed(1)} writes/fsync` : '';
        storageSub.textContent = `${count(live.storage.totalKeys)} keys${perFsync}`;
    } else {
        setPill(storagePill, 'down', 'unreachable');
        storageSub.textContent = 'not answering';
    }
}

/** Lights up the path a request took and explains its result under the diagram. */
export function showRequest(result) {
    const reading = describeResult(result);
    for (const element of flow().querySelectorAll('.onPath, .failedHere')) {
        element.classList.remove('onPath', 'failedHere');
    }
    for (const chip of flow().querySelectorAll('.resultChip')) {
        chip.hidden = true;
        chip.replaceChildren();
    }
    // Re-adding the classes on the next frame restarts the dash animation for repeated requests.
    requestAnimationFrame(() => {
        const mark = (element, failed) => element && element.classList.add(failed ? 'failedHere' : 'onPath');
        mark(nodeOf('client'), false);
        mark(hopOf('toRouter'), false);
        mark(nodeOf('router'), reading.failedAt === 'router');
        const edges = result.method === 'PURGE'
            ? ((result.data && result.data.edges) || []).map((edge) => edge.nodeId)
            : [result.node].filter(Boolean);
        if (edges.length > 0) {
            mark(hopOf('toEdge'), false);
        }
        for (const id of edges) {
            const node = nodeOf(id);
            if (node) {
                mark(node, reading.failedAt === 'edge');
                const chip = node.querySelector('.resultChip');
                const purged = result.method === 'PURGE'
                    && (result.data.edges.find((edge) => edge.nodeId === id) || {}).purged;
                const text = result.method === 'PURGE'
                    ? (purged ? 'evicted' : 'not cached')
                    : `${reading.label} · ${ms(result.latencyMs)}`;
                chip.replaceChildren(cacheTag(reading.tag, text));
                chip.hidden = false;
            }
        }
        if (reading.reachedOrigin) {
            mark(hopOf('toOrigin'), false);
            mark(nodeOf('write-api'), false);
            mark(hopOf('toStorage'), false);
            mark(nodeOf('storage'), false);
        }
    });

    const caption = document.getElementById('flowCaption');
    const target = result.method === 'PURGE' ? `/purge/key/${result.key}` : `/key/${result.key}`;
    const verb = result.method === 'PURGE' ? 'POST' : result.method;
    const strong = document.createElement('strong');
    strong.textContent = `${verb} ${target}`;
    caption.replaceChildren(strong, ' ', cacheTag(reading.tag, reading.label), ` in ${ms(result.latencyMs)}. ${reading.explain}`);
}
