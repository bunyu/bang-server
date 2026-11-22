// Canned advice without Gemini (GEMINI_FAKE=1): for tests and for running without a key.

const LINES = {
    en: ['Who wants some lead?', 'I have my eye on you.', 'Easy, partner.'],
    fr: ['Qui veut du plomb ?', 'Je te surveille.', 'Doucement, cow-boy.'],
    vi: ['Ai muốn ăn đạn nào?', 'Tôi để mắt tới anh đấy.', 'Bình tĩnh nào, cao bồi.'],
};

/** Attacks the weakest other living player, says a line in the request's language. */
export function fakeAdvice(game) {
    const others = game.players.filter(p => p.alive && p.id !== game.self.id);
    const target = others.sort((a, b) => a.hp - b.hp)[0];
    const lines = LINES[game.language] ?? LINES.en;
    return {
        plan: game.self.role === 'outlaw' ? 'lie_low' : 'survive',
        target_id: target?.id,
        say: lines[game.round % lines.length],
        emote: 'thumbsup',
        suspects: [],
    };
}
