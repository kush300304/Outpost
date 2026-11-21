// @file api.js
// The cluster as the dashboard sees it through its nginx proxy: endpoint paths, a JSON fetch that never
// rejects, one request function shared by the guided demo and the request tester, and the plain-language
// reading of a response (which cache result, how far the request travelled).
'use strict';

export const EDGES = [
    { id: 'edge-1', base: '/edge1' },
    { id: 'edge-2', base: '/edge2' },
    { id: 'edge-3', base: '/edge3' },
];

const lastEtags = new Map();

/** Fetches url and parses JSON with a timeout; resolves to {ok, status, data, headers}, never rejects. */
export async function fetchJson(url, options = {}, timeoutMs = 900) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    let result = { ok: false, status: 0, data: null, headers: null };
    try {
        const response = await fetch(url, { ...options, signal: controller.signal, cache: 'no-store' });
        const text = await response.text();
        let data = null;
        try {
            data = text ? JSON.parse(text) : null;
        } catch (parseError) {
            data = { raw: text };
        }
        result = { ok: response.ok, status: response.status, data, headers: response.headers };
    } catch (networkError) {
        result = { ok: false, status: 0, data: null, headers: null };
    }
    clearTimeout(timer);
    return result;
}

/** The ETag of the last response seen for key, or ''. */
export function lastEtagFor(key) {
    return lastEtags.get(key) || '';
}

/**
 * Sends GET, PUT or PURGE for key through the router and measures the latency in this browser.
 * @param {{key: string, method: string, value?: string, ifNoneMatch?: string, noCache?: boolean}} request
 */
export async function sendKeyRequest(request) {
    const encoded = encodeURIComponent(request.key);
    const headers = {};
    if (request.ifNoneMatch) {
        headers['If-None-Match'] = request.ifNoneMatch;
    }
    if (request.noCache) {
        headers['Cache-Control'] = 'no-cache';
    }
    let url = `/router/key/${encoded}`;
    const options = { method: request.method, headers };
    if (request.method === 'PUT') {
        headers['Content-Type'] = 'text/plain';
        options.body = request.value || '';
    } else if (request.method === 'PURGE') {
        url = `/router/purge/key/${encoded}`;
        options.method = 'POST';
    }
    const started = performance.now();
    const response = await fetchJson(url, options, 8000);
    const latencyMs = performance.now() - started;
    const header = (name) => (response.headers ? response.headers.get(name) : null) || '';
    const result = {
        key: request.key,
        method: request.method,
        status: response.status,
        latencyMs,
        data: response.data,
        node: header('X-Node'),
        cache: header('X-Cache'),
        writeMode: header('X-Write-Mode'),
        headers: {
            'X-Node': header('X-Node'),
            'X-Cache': header('X-Cache'),
            'X-Write-Mode': header('X-Write-Mode'),
            ETag: header('ETag'),
            Age: header('Age'),
            'Cache-Control': header('Cache-Control'),
            'Last-Modified': header('Last-Modified'),
            'Retry-After': header('Retry-After'),
        },
    };
    if (result.headers.ETag) {
        lastEtags.set(request.key, result.headers.ETag);
    }
    if (request.method === 'PURGE' && response.status === 200) {
        lastEtags.delete(request.key);
    }
    return result;
}

/** Turns the simulated origin outage on or off; resolves to the admin endpoint's answer. */
export function setOutage(enabled) {
    return fetchJson('/origin-admin/outage', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ enabled }),
    }, 3000);
}

/**
 * Reads a result in plain language.
 * @returns {{tag: string, label: string, explain: string, reachedOrigin: boolean, failedAt: string}}
 *   tag is the cache result shown as a coloured tag; failedAt is 'router', 'edge' or ''.
 */
export function describeResult(result) {
    const node = result.node || 'the edge';
    const reading = { tag: 'ERROR', label: '', explain: '', reachedOrigin: false, failedAt: '' };
    if (result.status === 0) {
        reading.label = 'no response';
        reading.explain = 'The dashboard could not reach the router.';
        reading.failedAt = 'router';
    } else if (result.method === 'PURGE') {
        const edges = (result.data && result.data.edges) || [];
        const evicted = edges.filter((edge) => edge.purged).map((edge) => edge.nodeId);
        const unreached = edges.filter((edge) => !edge.reached).map((edge) => edge.nodeId);
        reading.tag = result.status === 200 ? 'PURGED' : 'ERROR';
        reading.label = result.status === 200 ? 'PURGED' : `purge incomplete (${result.status})`;
        reading.explain = result.status === 200
            ? `The router asked all ${edges.length} edges to drop the key; `
              + (evicted.length ? `${evicted.join(', ')} had it cached and evicted it.` : 'none had it cached.')
            : `Not every edge confirmed the purge: ${unreached.join(', ') || 'unknown'} could not be reached.`;
    } else if (result.status === 429) {
        reading.tag = 'LIMITED';
        reading.label = '429 rate-limited';
        reading.explain = `${node} has no tokens left for this client; retry after ${result.headers['Retry-After'] || '1'} s.`;
        reading.failedAt = 'edge';
    } else if (result.status === 304) {
        reading.tag = 'NOT_MODIFIED';
        reading.label = '304 Not Modified';
        reading.explain = `Your copy is current: ${node} matched your If-None-Match to its ETag and sent no body.`;
    } else if (result.method === 'PUT' && result.writeMode === 'QUEUED') {
        reading.tag = 'QUEUED';
        reading.label = `QUEUED (${result.status})`;
        reading.explain = `The origin is unreachable, so ${node} saved the write in its local write-ahead log `
            + 'and will deliver it once the origin is back.';
    } else if (result.method === 'PUT' && result.status === 200) {
        reading.tag = 'DIRECT';
        reading.label = 'WRITTEN';
        reading.explain = `${node} wrote it through write-api to storage, which made it durable (fsync) `
            + 'before answering, and cached the new value.';
        reading.reachedOrigin = true;
    } else if (result.cache === 'HIT') {
        reading.tag = 'HIT';
        reading.label = 'HIT';
        reading.explain = `Served from ${node}'s memory; the origin was not contacted.`;
    } else if (result.cache === 'MISS') {
        reading.tag = 'MISS';
        reading.label = result.status === 404 ? 'MISS · 404' : 'MISS';
        reading.explain = result.status === 404
            ? `${node} asked the origin, and storage has no such key.`
            : `${node} had no copy, so it fetched the value from storage and cached it.`;
        reading.reachedOrigin = true;
    } else if (result.cache === 'REVALIDATED') {
        reading.tag = 'REVALIDATED';
        reading.label = 'REVALIDATED';
        reading.explain = `${node} asked storage whether its copy is still current (If-None-Match); storage `
            + 'answered 304, so the value was not sent again.';
        reading.reachedOrigin = true;
    } else if (result.cache === 'STALE') {
        reading.tag = 'STALE';
        reading.label = 'STALE';
        reading.explain = `The origin is unreachable, so ${node} served its older copy instead of failing.`;
    } else {
        reading.label = `HTTP ${result.status}`;
        reading.explain = result.data && result.data.message ? result.data.message : 'The request failed.';
        reading.failedAt = result.node ? 'edge' : 'router';
    }
    return reading;
}
