// @file conditional.js
// k6 workload for scripts/runBenchmark.sh (WORKLOAD=conditional): standard HTTP caching with validators.
// Over KEYS keys with VALUE_BYTES-byte values, through the router:
//   WRITE_SHARE (0.10) PUT: changes values, so some remembered ETags go out of date;
//   5 % GET with Cache-Control: no-cache: the edge revalidates its copy with the origin;
//   the rest GET: each virtual user remembers the ETag it last saw for a key and sends it in
//             If-None-Match, so an unchanged value comes back as 304 Not Modified without a body,
//             and a changed one as 200 with the new value.
// The 304 share depends on how often a key is rewritten between two visits by the same client.
// Each virtual user is its own client (X-Forwarded-For). Latency and body bytes are recorded per outcome.
// Environment: BASE_URL, KEYS (1000), VALUE_BYTES (4096), WRITE_SHARE (0.10), VUS (32), DURATION (60s),
// OUT_DIR.

import http from 'k6/http';
import { Counter, Trend } from 'k6/metrics';

const baseUrl = __ENV.BASE_URL || 'http://router:8080';
const keyCount = parseInt(__ENV.KEYS || '1000', 10);
const valueBytes = parseInt(__ENV.VALUE_BYTES || '4096', 10);
const writeShare = parseFloat(__ENV.WRITE_SHARE || '0.10');

export const options = {
    vus: parseInt(__ENV.VUS || '32', 10),
    duration: __ENV.DURATION || '60s',
    summaryTrendStats: ['avg', 'min', 'med', 'max', 'p(90)', 'p(95)', 'p(99)'],
    setupTimeout: '180s',
};

const latency = {
    notModified: new Trend('latency_get_304', true),
    changed: new Trend('latency_get_changed', true),
    unconditionalHit: new Trend('latency_get_hit', true),
    miss: new Trend('latency_get_miss', true),
    revalidated: new Trend('latency_get_revalidated', true),
    put: new Trend('latency_put', true),
};
const bodyBytes = {
    notModified: new Trend('body_bytes_get_304'),
    full: new Trend('body_bytes_get_200'),
};
const counters = {
    conditional: new Counter('count_get_conditional'),
    notModified: new Counter('count_get_304'),
    changed: new Counter('count_get_changed'),
    unconditionalHit: new Counter('count_get_hit'),
    miss: new Counter('count_get_miss'),
    revalidated: new Counter('count_get_revalidated'),
    noCache: new Counter('count_get_nocache'),
    put: new Counter('count_put'),
    s2xx: new Counter('status_2xx'),
    s304: new Counter('status_304'),
    s404: new Counter('status_404'),
    s429: new Counter('status_429'),
    s5xx: new Counter('status_5xx'),
    other: new Counter('status_other'),
};

// Module scope is per virtual user in k6: the ETags this client has seen.
const etags = {};

function valueFor(tag) {
    return `${tag}-`.padEnd(valueBytes, 'x');
}

function rememberEtag(key, res) {
    const etag = res.headers.Etag || res.headers.ETag;
    if (etag) {
        etags[key] = etag;
    }
}

export function setup() {
    for (let i = 0; i < keyCount; i++) {
        http.put(`${baseUrl}/key/bench-${i}`, valueFor(`seed-${i}`), {
            headers: { 'X-Forwarded-For': `10.61.${i % 200}.1` },
        });
    }
}

export default function () {
    const key = `bench-${Math.floor(Math.random() * keyCount)}`;
    const headers = { 'X-Forwarded-For': `10.60.${__VU}.1` };
    const roll = Math.random();
    let res;
    if (roll < writeShare) {
        res = http.put(`${baseUrl}/key/${key}`, valueFor(`v${__VU}-${__ITER}`), { headers });
        latency.put.add(res.timings.duration);
        counters.put.add(1);
        rememberEtag(key, res);
    } else if (roll < writeShare + 0.05) {
        headers['Cache-Control'] = 'no-cache';
        res = http.get(`${baseUrl}/key/${key}`, { headers });
        counters.noCache.add(1);
        const cache = res.headers['X-Cache'];
        (cache === 'REVALIDATED' ? latency.revalidated : latency.miss).add(res.timings.duration);
        (cache === 'REVALIDATED' ? counters.revalidated : counters.miss).add(1);
        bodyBytes.full.add(res.body ? res.body.length : 0);
        rememberEtag(key, res);
    } else {
        const conditional = key in etags;
        if (conditional) {
            headers['If-None-Match'] = etags[key];
            counters.conditional.add(1);
        }
        res = http.get(`${baseUrl}/key/${key}`, { headers });
        const cache = res.headers['X-Cache'];
        if (res.status === 304) {
            latency.notModified.add(res.timings.duration);
            counters.notModified.add(1);
            bodyBytes.notModified.add(res.body ? res.body.length : 0);
        } else {
            const trend = cache === 'MISS' ? latency.miss : conditional ? latency.changed : latency.unconditionalHit;
            const counter = cache === 'MISS' ? counters.miss : conditional ? counters.changed : counters.unconditionalHit;
            trend.add(res.timings.duration);
            counter.add(1);
            bodyBytes.full.add(res.body ? res.body.length : 0);
            rememberEtag(key, res);
        }
    }
    const s = res.status;
    const statusCounter = s === 304 ? counters.s304 : s === 404 ? counters.s404 : s === 429 ? counters.s429
        : s >= 200 && s < 300 ? counters.s2xx : s >= 500 ? counters.s5xx : counters.other;
    statusCounter.add(1);
}

export function handleSummary(data) {
    const out = {};
    out[`${__ENV.OUT_DIR || '/tmp'}/k6-summary.json`] = JSON.stringify(data, null, 2);
    return out;
}
