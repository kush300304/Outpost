// @file guidedDemo.js
// The guided demo: the spec's demo sequence as nine steps, each with one button, a plain-language
// explanation, what to expect, and a check against the real response or the live cluster state. A step
// passes only when the cluster actually did what the step says.
'use strict';

import { EDGES, describeResult, sendKeyRequest, setOutage } from './api.js';
import { cacheTag } from './format.js';
import { showRequest } from './flowDiagram.js';
import { live, refresh, waitFor } from './live.js';

const QUEUED_VALUE = 'written during the outage';
const context = { key: '', etag: '' };
const rows = [];
let busy = false;

const degraded = (state) => EDGES.every((edge) => state.edges[edge.id] && state.edges[edge.id].circuitBreaker.state !== 'CLOSED');
const recovered = (state) => EDGES.every((edge) => {
    const stats = state.edges[edge.id];
    return stats && stats.circuitBreaker.state === 'CLOSED' && stats.writeQueue.pending === 0;
});

/** Sends a request for the demo key, shows its path on the diagram and returns the result. */
async function send(request) {
    const result = await sendKeyRequest({ key: context.key, ...request });
    showRequest(result);
    return result;
}

/** A step outcome from a request result and the condition it had to meet. */
function outcome(result, passed) {
    const reading = describeResult(result);
    return { passed, tag: reading.tag, label: reading.label, note: passed ? reading.explain : `Got ${reading.label}. ${reading.explain}` };
}

const STEPS = [
    {
        title: 'Write a value',
        explain: 'The router hashes the key to one edge. That edge writes through write-api to storage, which makes the write durable before answering, and caches the value.',
        expect: 'WRITTEN (200); the path lights up all the way to storage.',
        run: async () => {
            const result = await send({ method: 'PUT', value: 'first value' });
            return outcome(result, result.status === 200 && result.writeMode === 'DIRECT');
        },
    },
    {
        title: 'Purge it from every edge',
        explain: 'Invalidation: the router tells all three edges to drop the key, so the next read has to go to the origin.',
        expect: 'PURGED; the edge that cached it reports "evicted".',
        run: async () => {
            const result = await send({ method: 'PURGE' });
            return outcome(result, result.status === 200 && Boolean(result.data && result.data.complete));
        },
    },
    {
        title: 'Cold read',
        explain: 'The edge that owns the key has no copy, so it fetches the value from storage and caches it.',
        expect: 'MISS (200).',
        run: async () => {
            const result = await send({ method: 'GET' });
            return outcome(result, result.status === 200 && result.cache === 'MISS');
        },
    },
    {
        title: 'Warm read',
        explain: 'Same key, so the same edge (consistent hashing), now answered from memory. The response carries ETag, Age and Cache-Control headers.',
        expect: 'HIT, in a millisecond or two.',
        run: async () => {
            const result = await send({ method: 'GET' });
            context.etag = result.headers.ETag;
            return outcome(result, result.status === 200 && result.cache === 'HIT');
        },
    },
    {
        title: 'Conditional read',
        explain: 'A client that already holds this version sends its ETag in If-None-Match. The edge confirms it is current without sending the value again.',
        expect: '304 Not Modified, no body.',
        run: async () => {
            const result = await send({ method: 'GET', ifNoneMatch: context.etag || '"none"' });
            return outcome(result, result.status === 304);
        },
    },
    {
        title: 'Break the origin (simulated)',
        explain: 'write-api starts answering 503. Each edge\'s health checks fail, and after 3 failures its circuit breaker opens: the edge stops calling the origin.',
        expect: 'All three edges turn "degraded" (breaker OPEN) within a few seconds.',
        run: async () => {
            const answer = await setOutage(true);
            await refresh();
            const opened = answer.ok && await waitFor(degraded, 20000);
            const note = !answer.ok
                ? (answer.status === 404 ? 'The admin endpoint is disabled: start the cluster with ADMIN_ENABLED=1.' : 'write-api did not answer.')
                : (opened ? 'Every breaker is OPEN: the edges now protect the origin and serve from cache.' : 'Not every breaker opened within 20 s.');
            return { passed: opened, tag: opened ? 'STALE' : 'ERROR', label: opened ? 'breakers OPEN' : 'not open', note };
        },
    },
    {
        title: 'Read during the outage',
        explain: 'The edge cannot reach the origin, so it serves its cached copy, marked STALE, instead of an error.',
        expect: 'STALE (200).',
        run: async () => {
            const result = await send({ method: 'GET' });
            return outcome(result, result.status === 200 && result.cache === 'STALE');
        },
    },
    {
        title: 'Write during the outage',
        explain: 'The edge accepts the write into its local write-ahead log and answers 202. It will deliver it later, in order.',
        expect: 'QUEUED (202); that edge\'s "Queued writes" goes up.',
        run: async () => {
            const result = await send({ method: 'PUT', value: QUEUED_VALUE });
            return outcome(result, result.status === 202 && result.writeMode === 'QUEUED');
        },
    },
    {
        title: 'Recover',
        explain: 'write-api is back. The breakers close, each edge drains its queue in order, and a no-cache read asks storage to confirm the edge\'s copy.',
        expect: 'Breakers CLOSED, queues at 0, then REVALIDATED with the queued value: storage holds exactly that write.',
        run: async () => {
            const answer = await setOutage(false);
            await refresh();
            const healed = answer.ok && await waitFor(recovered, 30000);
            let result = null;
            if (healed) {
                result = await send({ method: 'GET', noCache: true });
            }
            const confirmed = Boolean(result) && result.cache === 'REVALIDATED'
                && Boolean(result.data) && result.data.value === QUEUED_VALUE;
            let step = { passed: false, tag: 'ERROR', label: 'not recovered', note: answer.ok ? 'Breakers did not all close with empty queues within 30 s.' : 'write-api did not answer.' };
            if (result) {
                step = outcome(result, confirmed);
                step.note = confirmed
                    ? 'Breakers closed, the queue drained, and storage confirmed the edge holds the write made during the outage.'
                    : step.note;
            }
            return step;
        },
    },
];

/** Marks which step to run next and enables or disables the buttons. */
function syncRows() {
    const next = rows.findIndex((row) => row.dataset.status !== 'passed');
    rows.forEach((row, index) => {
        row.dataset.next = String(index === next && !busy);
        row.querySelector('button').disabled = busy;
    });
    document.getElementById('demoRunAll').disabled = busy;
    document.getElementById('demoReset').disabled = busy;
}

/** Runs one step and shows its outcome; resolves to whether it passed. */
async function runStep(index) {
    const row = rows[index];
    const result = row.querySelector('.demoResult');
    row.dataset.status = 'running';
    result.textContent = 'Running…';
    let step = null;
    try {
        step = await STEPS[index].run();
    } catch (error) {
        step = { passed: false, tag: 'ERROR', label: 'error', note: String(error) };
    }
    row.dataset.status = step.passed ? 'passed' : 'failed';
    result.replaceChildren(cacheTag(step.tag, step.label), ` ${step.note}`);
    return step.passed;
}

/** Runs the given steps in order, stopping at the first failure. */
async function runSteps(indexes) {
    busy = true;
    syncRows();
    let passed = true;
    for (const index of indexes) {
        if (passed) {
            passed = await runStep(index);
        }
    }
    busy = false;
    syncRows();
}

/** Picks a fresh demo key and clears every step (turning a simulated outage off). */
async function reset() {
    context.key = `demo-${Date.now().toString(36)}`;
    context.etag = '';
    document.getElementById('demoKey').textContent = `Demo key: ${context.key}`;
    for (const row of rows) {
        row.dataset.status = 'idle';
        row.querySelector('.demoResult').textContent = 'Not run yet.';
    }
    if (live.origin && live.origin.simulatedOutage) {
        await setOutage(false);
        await refresh();
    }
    syncRows();
}

/** Builds the step list and wires its buttons. */
export function initGuidedDemo() {
    const list = document.getElementById('demoSteps');
    STEPS.forEach((step, index) => {
        const row = document.createElement('li');
        row.className = 'demoStep';
        const body = document.createElement('div');
        body.className = 'demoBody';
        const title = document.createElement('h3');
        title.textContent = step.title;
        const explain = document.createElement('p');
        explain.textContent = step.explain;
        const expect = document.createElement('p');
        expect.className = 'demoExpect';
        const label = document.createElement('strong');
        label.textContent = 'Expect: ';
        expect.append(label, step.expect);
        const footer = document.createElement('div');
        footer.className = 'demoFooter';
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'small';
        button.textContent = `Run step ${index + 1}`;
        button.addEventListener('click', () => runSteps([index]));
        const result = document.createElement('span');
        result.className = 'demoResult';
        result.setAttribute('aria-live', 'polite');
        footer.append(button, result);
        body.append(title, explain, expect, footer);
        row.appendChild(body);
        list.appendChild(row);
        rows.push(row);
    });
    document.getElementById('demoRunAll').addEventListener('click', () => runSteps(STEPS.map((step, index) => index)));
    document.getElementById('demoReset').addEventListener('click', reset);
    reset();
}
