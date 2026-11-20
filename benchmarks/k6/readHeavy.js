// @file readHeavy.js
// k6 workload for scripts/runBenchmark.sh: 90 % GET / 10 % PUT over KEYS keys through the router.
// Each virtual user is its own client (X-Forwarded-For). Latency is recorded per X-Cache result.
// Environment: BASE_URL, KEYS (1000), VUS (32), DURATION (60s), OUT_DIR (where the summary goes).

import http from 'k6/http';
import { Counter, Trend } from 'k6/metrics';

const baseUrl = __ENV.BASE_URL || 'http://router:8080';
const keyCount = parseInt(__ENV.KEYS || '1000', 10);

export const options = {
    vus: parseInt(__ENV.VUS || '32', 10),
    duration: __ENV.DURATION || '60s',
    summaryTrendStats: ['avg', 'min', 'med', 'max', 'p(90)', 'p(95)', 'p(99)'],
    setupTimeout: '120s',
};

const hitLatency = new Trend('latency_get_hit', true);
const missLatency = new Trend('latency_get_miss', true);
const staleLatency = new Trend('latency_get_stale', true);
const putLatency = new Trend('latency_put', true);
const counters = {
    hit: new Counter('count_get_hit'),
    miss: new Counter('count_get_miss'),
    stale: new Counter('count_get_stale'),
    put: new Counter('count_put'),
    s2xx: new Counter('status_2xx'),
    s404: new Counter('status_404'),
    s429: new Counter('status_429'),
    s5xx: new Counter('status_5xx'),
    other: new Counter('status_other'),
};

export function setup() {
    for (let i = 0; i < keyCount; i++) {
        http.put(`${baseUrl}/key/bench-${i}`, `seed-${i}`, {
            headers: { 'X-Forwarded-For': `10.61.${i % 200}.1` },
        });
    }
}

export default function () {
    const key = `bench-${Math.floor(Math.random() * keyCount)}`;
    const params = { headers: { 'X-Forwarded-For': `10.60.${__VU}.1` } };
    let res;
    if (Math.random() < 0.9) {
        res = http.get(`${baseUrl}/key/${key}`, params);
        const cache = res.headers['X-Cache'];
        const trend = cache === 'HIT' ? hitLatency : cache === 'STALE' ? staleLatency : missLatency;
        trend.add(res.timings.duration);
        (cache === 'HIT' ? counters.hit : cache === 'STALE' ? counters.stale : counters.miss).add(1);
    } else {
        res = http.put(`${baseUrl}/key/${key}`, `value-${__ITER}`, params);
        putLatency.add(res.timings.duration);
        counters.put.add(1);
    }
    const s = res.status;
    const statusCounter = s === 404 ? counters.s404 : s === 429 ? counters.s429
        : s >= 200 && s < 300 ? counters.s2xx : s >= 500 ? counters.s5xx : counters.other;
    statusCounter.add(1);
}

export function handleSummary(data) {
    const out = {};
    out[`${__ENV.OUT_DIR || '/tmp'}/k6-summary.json`] = JSON.stringify(data, null, 2);
    return out;
}
