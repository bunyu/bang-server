// Checks the AI's answer against the request: keeps only valid fields, never trusts the model.

export const ROLES = ['sheriff', 'deputy', 'outlaw', 'renegade'];
export const PLANS = ['lie_low', 'attack_sheriff', 'protect_sheriff', 'hunt_outlaws', 'survive'];
export const EMOTES = ['happy', 'angry', 'sheriff', 'deputy', 'outlaw', 'renegade', 'trophy',
    'thumbsup', 'laugh', 'scared', 'beer', 'handshake', 'skull'];
export const MAX_SAY = 120;

/**
 * @param {any} raw the model's parsed JSON
 * @param {{ self: { id: number }, players: { id: number, alive: boolean }[] }} game the request's game part
 * @returns the advice for the server: { suspects: {id: role}, plan, target_id, say, emote, emote_target }
 */
export function validateAdvice(raw, game) {
    const advice = {};
    if (!raw || typeof raw !== 'object') return advice;
    const others = new Set(game.players.filter(p => p.id !== game.self.id).map(p => p.id));
    const alive = new Set(game.players.filter(p => p.alive && p.id !== game.self.id).map(p => p.id));

    const suspects = {};
    for (const entry of Array.isArray(raw.suspects) ? raw.suspects : []) {
        if (entry && others.has(entry.player_id) && ROLES.includes(entry.role)) {
            suspects[entry.player_id] = entry.role;
        }
    }
    if (Object.keys(suspects).length) advice.suspects = suspects;

    if (PLANS.includes(raw.plan)) advice.plan = raw.plan;
    if (Number.isInteger(raw.target_id) && alive.has(raw.target_id)) advice.target_id = raw.target_id;

    if (typeof raw.say === 'string') {
        // one line, no emote syntax, at most MAX_SAY characters (not bytes: Vietnamese)
        const say = [...raw.say.replace(/\s+/g, ' ').trim().replace(/^\[\[/, '')].slice(0, MAX_SAY).join('').trim();
        if (say) advice.say = say;
    }
    if (EMOTES.includes(raw.emote)) {
        advice.emote = raw.emote;
        if (Number.isInteger(raw.emote_target) && others.has(raw.emote_target)) advice.emote_target = raw.emote_target;
    }
    return advice;
}
