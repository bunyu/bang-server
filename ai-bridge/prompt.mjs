// Builds the prompt for one bot turn from what the server sent (only what that bot may know).

const LANGUAGES = { en: 'English', fr: 'French', vi: 'Vietnamese' };

const LANGUAGE_STYLE = {
    en: 'Write natural spoken English.',
    fr: 'Write natural spoken French with all accents (é, è, à, ç...).',
    vi: 'Write natural spoken Vietnamese with full diacritics (dấu), never without accents.',
};

const PERSONALITIES = [
    'hot-headed and loud, quick to accuse',
    'sly and quiet, says little but means it',
    'talkative and joking, teases everyone',
    'old and grumpy, complains about everything',
    'nervous and suspicious of everybody',
    'calm and polite, but cold when it counts',
];

const GOALS = {
    sheriff: 'You are the Sheriff (everyone knows). Win by killing all Outlaws and the Renegade. Deputies are your hidden allies.',
    deputy: 'You are a Deputy (secret). Protect the Sheriff; win when all Outlaws and the Renegade are dead. Do not reveal your role openly unless it helps the Sheriff.',
    outlaw: 'You are an Outlaw (secret). Win by killing the Sheriff. Other Outlaws are your hidden allies. Hide your role early: do not attack the Sheriff while he is strong unless the moment is right.',
    renegade: 'You are the Renegade (secret). Win only by being the last one alive, the Sheriff last. Pretend to help the Sheriff while the Outlaws are many, turn on him at the end.',
};

const RULES = `Bang! rules in short: players sit in a circle; distance = seats between players (Mustang adds 1, Scope removes 1);
a weapon's range limits BANG! targets. Each turn: draw 2 cards, play cards, discard down to your hp. BANG! = 1 damage unless
the target plays Missed! (or a Barrel draw saves him). Beer heals 1. Jail skips a turn, Dynamite may explode for 3 damage.
Roles are secret except the Sheriff. Players reveal themselves by whom they attack or help.`;

export function personalityOf(userId) {
    return PERSONALITIES[Math.abs(userId) % PERSONALITIES.length];
}

/** A log entry {"game_log": {format_str, format_args}} as one readable line. */
export function logLine(entry, nameOf) {
    const log = entry?.game_log ?? entry;
    if (!log?.format_str) return null;
    const args = (log.format_args ?? []).map(a =>
        a.player !== undefined ? nameOf(a.player)
        : a.card ? (a.card.name ?? 'a card')
        : a.integer !== undefined ? String(a.integer)
        : a.string ?? '').filter(Boolean);
    return `${log.format_str.replace(/^LOG_/, '').toLowerCase()}: ${args.join(', ')}`;
}

/**
 * @param {{ names: {user_id: number, name: string}[], game: any }} request
 * @returns {string}
 */
export function buildPrompt({ names, game }) {
    const userNames = new Map((names ?? []).map(n => [n.user_id, n.name]));
    const playerName = p => `${userNames.get(p.user_id) ?? 'Player ' + p.id} (${p.character || 'unknown character'})`;
    const byId = new Map(game.players.map(p => [p.id, p]));
    const nameOf = id => byId.has(id) ? playerName(byId.get(id)) : `player ${id}`;
    const self = game.self;
    const language = LANGUAGES[game.language] ?? 'English';

    const table = game.players.map(p => {
        const role = p.id === self.id ? `${p.role} (you)` : p.role ?? 'role unknown';
        return `- id ${p.id}: ${playerName(p)}, ${role}, ${p.alive ? `${p.hp}/${p.max_hp} hp` : 'DEAD'}, ` +
            `${p.hand_count} cards in hand, in play: ${p.table.join(', ') || 'nothing'}`;
    }).join('\n');

    const log = (game.log ?? []).map(e => logLine(e, nameOf)).filter(Boolean).slice(-30).join('\n');
    const said = (game.said ?? []).map(s => `- "${s}"`).join('\n') || '- (nothing yet)';

    return `You are a bot playing the card game Bang! at a table of human and bot players, as ${playerName(byId.get(self.id) ?? { user_id: self.user_id, id: self.id, character: self.character })}.
Your personality: ${personalityOf(self.user_id)}.
${GOALS[self.role] ?? GOALS.renegade}

${RULES}

It is round ${game.round}, your turn starts. Your hp: ${self.hp}/${self.max_hp}. Your hand: ${self.hand.join(', ') || 'empty'}. In play: ${self.table.join(', ') || 'nothing'}.

Players (seat order):
${table}

What happened recently (oldest first):
${log || '(nothing yet)'}

What you said earlier:
${said}

Answer with JSON only:
- suspects: for players whose role you do not know, your best guess {player_id, role} (sheriff, deputy, outlaw or renegade); skip players you cannot guess.
- plan: one of lie_low, attack_sheriff, protect_sheriff, hunt_outlaws, survive.
- target_id: the id of the player you most want to attack this turn (an enemy for your role), or omit.
- say: optional, one short line (max 100 characters) you say out loud at the table, in ${language}, in character.
  ${LANGUAGE_STYLE[game.language] ?? LANGUAGE_STYLE.en}
  Talk like a player: tease, accuse, bluff, react to what just happened. Do not repeat what you said before.
  Never state your secret role unless it is a deliberate lie. Say nothing about 30% of the time.
- emote: optional, one of happy, angry, trophy, thumbsup, laugh, scared, beer, handshake, skull; emote_target: optional player id.`;
}

/** Gemini response schema (OpenAPI subset). */
export const RESPONSE_SCHEMA = {
    type: 'OBJECT',
    properties: {
        suspects: {
            type: 'ARRAY',
            items: {
                type: 'OBJECT',
                properties: {
                    player_id: { type: 'INTEGER' },
                    role: { type: 'STRING', enum: ['sheriff', 'deputy', 'outlaw', 'renegade'] },
                },
                required: ['player_id', 'role'],
            },
        },
        plan: { type: 'STRING', enum: ['lie_low', 'attack_sheriff', 'protect_sheriff', 'hunt_outlaws', 'survive'] },
        target_id: { type: 'INTEGER' },
        say: { type: 'STRING' },
        emote: { type: 'STRING', enum: ['happy', 'angry', 'trophy', 'thumbsup', 'laugh', 'scared', 'beer', 'handshake', 'skull'] },
        emote_target: { type: 'INTEGER' },
    },
    required: ['plan'],
};
