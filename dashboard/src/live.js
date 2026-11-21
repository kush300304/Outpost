// @file live.js
// Live state of the cluster. Polls every edge's /debug/stats, the router, the origin admin endpoint and
// storage's stats once per second, keeps the latest answers in `live`, and notifies listeners. waitFor()
// lets the guided demo wait until the cluster reaches a state it should reach.
'use strict';

import { EDGES, fetchJson } from './api.js';

const POLL_MS = 1000;
const STORAGE_WINDOW = 10;

/** Latest answers; a member is null while its service does not answer. */
export const live = {
    edges: {},
    router: null,
    routerStats: null,
    origin: null,
    originStatus: 0,
    storage: null,
    storageWindow: null,
    updatedAt: 0,
};

const listeners = new Set();
const storageSamples = [];
let timer = null;

/** Calls fn(live) after every poll. Returns a function that removes the listener. */
export function onUpdate(fn) {
    listeners.add(fn);
    return () => listeners.delete(fn);
}

/** Polls every service once, updates `live` and notifies listeners. */
export async function refresh() {
    const responses = await Promise.all([
        ...EDGES.map((edge) => fetchJson(`${edge.base}/debug/stats`)),
        fetchJson('/router/health'),
        fetchJson('/router/debug/stats'),
        fetchJson('/origin-admin/outage'),
        fetchJson('/storage/debug/stats'),
    ]);
    EDGES.forEach((edge, index) => {
        live.edges[edge.id] = responses[index].ok ? responses[index].data : null;
    });
    const [router, routerStats, origin, storage] = responses.slice(EDGES.length);
    live.router = router.ok ? router.data : null;
    live.routerStats = routerStats.ok ? routerStats.data : null;
    live.origin = origin.ok ? origin.data : null;
    live.originStatus = origin.status;
    live.storage = storage.ok ? storage.data : null;

    // Writes per fsync over the last STORAGE_WINDOW polls, from storage's cumulative WAL counters.
    if (live.storage && live.storage.wal) {
        storageSamples.push({ appends: live.storage.wal.appends, fsyncs: live.storage.wal.fsyncs });
        storageSamples.splice(0, Math.max(0, storageSamples.length - STORAGE_WINDOW));
        const first = storageSamples[0];
        const last = storageSamples[storageSamples.length - 1];
        const restarted = last.appends < first.appends;
        live.storageWindow = restarted ? null : {
            appends: last.appends - first.appends,
            fsyncs: last.fsyncs - first.fsyncs,
            seconds: storageSamples.length - 1,
        };
        if (restarted) {
            storageSamples.splice(0, storageSamples.length - 1);
        }
    } else {
        live.storageWindow = null;
    }
    live.updatedAt = Date.now();
    for (const listener of [...listeners]) {
        listener(live);
    }
}

/** Starts the once-per-second poll loop. */
export function startPolling() {
    const loop = async () => {
        const started = performance.now();
        await refresh();
        timer = setTimeout(loop, Math.max(0, POLL_MS - (performance.now() - started)));
    };
    if (timer === null) {
        loop();
    }
}

/**
 * Resolves true once predicate(live) holds after a poll, or false after timeoutMs.
 * @param {(state: object) => boolean} predicate
 */
export function waitFor(predicate, timeoutMs) {
    return new Promise((resolve) => {
        let settled = false;
        let remove = () => {};
        const finish = (value) => {
            if (!settled) {
                settled = true;
                remove();
                clearTimeout(deadline);
                resolve(value);
            }
        };
        const deadline = setTimeout(() => finish(false), timeoutMs);
        remove = onUpdate((state) => {
            if (predicate(state)) {
                finish(true);
            }
        });
        if (live.updatedAt > 0 && predicate(live)) {
            finish(true);
        }
    });
}
