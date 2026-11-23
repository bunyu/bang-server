// Calls the Gemini API (generateContent) and returns the parsed JSON answer.
import { readFileSync } from 'node:fs';
import { RESPONSE_SCHEMA } from './prompt.mjs';

const API = 'https://generativelanguage.googleapis.com/v1beta';

/** The key from GEMINI_API_KEY, else from the file GEMINI_API_KEY_FILE (first line). Never logged. */
export function loadApiKey(env = process.env) {
    if (env.GEMINI_API_KEY) return env.GEMINI_API_KEY.trim();
    if (env.GEMINI_API_KEY_FILE) {
        try { return readFileSync(env.GEMINI_API_KEY_FILE, 'utf8').split(/\r?\n/)[0].trim(); } catch { return ''; }
    }
    return '';
}

export class GeminiError extends Error {
    constructor(message, status) { super(message); this.status = status; }
}

// "minimal" thinking roughly halves the answer time; a model that rejects the setting gets requests without it
let thinkingLevel = process.env.GEMINI_THINKING ?? 'minimal';

/**
 * @param {string} prompt
 * @param {{ apiKey: string, model: string, timeoutMs?: number }} options
 */
export async function askGemini(prompt, options) {
    try {
        return await callGemini(prompt, options);
    } catch (e) {
        if (e.status === 400 && thinkingLevel) {
            console.log(`model rejected thinkingLevel=${thinkingLevel}, retrying without it from now on`);
            thinkingLevel = '';
            return callGemini(prompt, options);
        }
        throw e;
    }
}

async function callGemini(prompt, { apiKey, model, timeoutMs = 4700 }) {
    const response = await fetch(`${API}/models/${encodeURIComponent(model)}:generateContent`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'x-goog-api-key': apiKey },
        body: JSON.stringify({
            contents: [{ role: 'user', parts: [{ text: prompt }] }],
            generationConfig: {
                responseMimeType: 'application/json',
                responseSchema: RESPONSE_SCHEMA,
                temperature: 0.9,
                maxOutputTokens: 400,
                ...(thinkingLevel ? { thinkingConfig: { thinkingLevel } } : {}),
            },
        }),
        signal: AbortSignal.timeout(timeoutMs),
    });
    if (!response.ok) {
        // the body explains quota / model errors; it never contains the key
        const text = (await response.text()).slice(0, 300).replace(/\s+/g, ' ');
        throw new GeminiError(`HTTP ${response.status}: ${text}`, response.status);
    }
    const data = await response.json();
    const text = data?.candidates?.[0]?.content?.parts?.map(p => p.text ?? '').join('') ?? '';
    try {
        return JSON.parse(text);
    } catch {
        throw new GeminiError('answer is not JSON: ' + text.slice(0, 100));
    }
}

/** The models this key may call (for checking GEMINI_MODEL). */
export async function listModels(apiKey) {
    const response = await fetch(`${API}/models?pageSize=200`, { headers: { 'x-goog-api-key': apiKey } });
    if (!response.ok) throw new GeminiError(`HTTP ${response.status}`, response.status);
    const data = await response.json();
    return (data.models ?? [])
        .filter(m => (m.supportedGenerationMethods ?? []).includes('generateContent'))
        .map(m => m.name.replace(/^models\//, ''));
}
