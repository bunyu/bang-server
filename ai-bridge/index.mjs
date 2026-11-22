// AI helper for bang-server: connects to the game server as the "AI bridge", answers each advice request
// (one per bot turn) with Gemini's advice, checked by validateAdvice. See README.md.
//
// Environment:
//   BANG_SERVER_URL      ws(s) address of the game server (default ws://localhost:47654)
//   AI_BRIDGE_TOKEN      must equal the server's AI_BRIDGE_TOKEN
//   GEMINI_API_KEY       the key, or GEMINI_API_KEY_FILE: a file holding it
//   GEMINI_MODEL         model name (default gemini-flash-lite-latest)
//   GEMINI_FAKE=1        no Gemini: canned advice (tests, no key needed)
import { buildPrompt } from './prompt.mjs';
import { validateAdvice } from './validate.mjs';
import { askGemini, loadApiKey, listModels } from './gemini.mjs';
import { fakeAdvice } from './fake.mjs';

const SERVER = process.env.BANG_SERVER_URL || 'ws://localhost:47654';
const TOKEN = process.env.AI_BRIDGE_TOKEN || '';
const MODEL = process.env.GEMINI_MODEL || 'gemini-flash-lite-latest';
const FAKE = process.env.GEMINI_FAKE === '1';
const API_KEY = FAKE ? '' : loadApiKey();

if (!TOKEN) { console.error('AI_BRIDGE_TOKEN is not set'); process.exit(1); }
if (!FAKE && !API_KEY) { console.error('No Gemini key: set GEMINI_API_KEY or GEMINI_API_KEY_FILE (or GEMINI_FAKE=1)'); process.exit(1); }

const stats = { requests: 0, answered: 0, failed: 0, totalMs: 0 };
const log = (...args) => console.log(new Date().toISOString().slice(11, 19), ...args);

async function advise(request) {
    if (FAKE) return fakeAdvice(request.game);
    return askGemini(buildPrompt(request), { apiKey: API_KEY, model: MODEL });
}

function connect() {
    const ws = new WebSocket(SERVER);
    ws.onopen = () => {
        ws.send(JSON.stringify({ ai_bridge_hello: { token: TOKEN } }));
        log(`connected to ${SERVER} (${FAKE ? 'fake advice' : 'model ' + MODEL})`);
    };
    ws.onmessage = async event => {
        let message;
        try { message = JSON.parse(event.data); } catch { return; }
        if (message.ping) { ws.send(JSON.stringify({ pong: {} })); return; }
        const req = message.ai_advice_request;
        if (!req) return;
        const started = Date.now();
        ++stats.requests;
        let advice;
        try {
            advice = validateAdvice(await advise(req.request), req.request.game);
            ++stats.answered;
        } catch (e) {
            ++stats.failed;
            log('advice failed:', e.message);
            advice = { error: String(e.message).slice(0, 100) };
        }
        const ms = Date.now() - started;
        stats.totalMs += ms;
        if (ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ ai_advice: { request_id: req.request_id, advice } }));
        }
        if (process.env.AI_BRIDGE_VERBOSE) log(`#${req.request_id} ${ms} ms`, JSON.stringify(advice));
    };
    ws.onclose = event => {
        log(`disconnected (${event.code} ${event.reason || ''}), retrying in 5 s`);
        setTimeout(connect, 5000);
    };
    ws.onerror = () => { /* onclose follows */ };
}

setInterval(() => {
    if (stats.requests) {
        log(`requests ${stats.requests}, answered ${stats.answered}, failed ${stats.failed}, ` +
            `average ${Math.round(stats.totalMs / stats.requests)} ms`);
    }
}, 60000).unref();

if (!FAKE && process.argv.includes('--list-models')) {
    console.log((await listModels(API_KEY)).join('\n'));
    process.exit(0);
}
connect();
