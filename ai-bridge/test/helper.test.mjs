import { test } from 'node:test';
import assert from 'node:assert/strict';
import { validateAdvice, MAX_SAY } from '../validate.mjs';
import { buildPrompt, logLine } from '../prompt.mjs';

const game = {
    language: 'vi', round: 2,
    self: { id: 1, user_id: -1, role: 'outlaw', character: 'SLAB_THE_KILLER', hp: 4, max_hp: 4, hand: ['BANG', 'BEER'], table: [] },
    players: [
        { id: 1, user_id: -1, character: 'SLAB_THE_KILLER', hp: 4, max_hp: 4, alive: true, hand_count: 2, table: [], role: 'outlaw' },
        { id: 2, user_id: 1, character: 'WILLY_THE_KID', hp: 5, max_hp: 5, alive: true, hand_count: 3, table: ['BARREL'], role: 'sheriff' },
        { id: 3, user_id: -2, character: 'ROSE_DOOLAN', hp: 2, max_hp: 4, alive: true, hand_count: 4, table: [] },
        { id: 4, user_id: -3, character: 'JOURDONNAIS', hp: 0, max_hp: 4, alive: false, hand_count: 0, table: [] },
    ],
    log: [{ game_log: { format_str: 'LOG_PLAYED_CARD_ON', format_args: [{ card: { name: 'BANG' } }, { player: 2 }, { player: 3 }] } }],
    said: ['Hello'],
};

test('valid advice is kept', () => {
    const advice = validateAdvice({ suspects: [{ player_id: 3, role: 'deputy' }], plan: 'lie_low', target_id: 3,
        say: 'Chào cả bàn', emote: 'angry', emote_target: 2 }, game);
    assert.deepEqual(advice, { suspects: { 3: 'deputy' }, plan: 'lie_low', target_id: 3, say: 'Chào cả bàn', emote: 'angry', emote_target: 2 });
});

test('invalid fields are dropped', () => {
    const advice = validateAdvice({ suspects: [{ player_id: 999, role: 'deputy' }, { player_id: 3, role: 'king' }, { player_id: 1, role: 'outlaw' }],
        plan: 'dance', target_id: 4, emote: 'kiss', emote_target: 2 }, game);
    assert.deepEqual(advice, {});
});

test('target must be another living player', () => {
    assert.equal(validateAdvice({ target_id: 1 }, game).target_id, undefined);
    assert.equal(validateAdvice({ target_id: 4 }, game).target_id, undefined);
    assert.equal(validateAdvice({ target_id: '3' }, game).target_id, undefined);
});

test('say is cut to MAX_SAY characters, one line, no emote syntax', () => {
    const advice = validateAdvice({ say: 'ố'.repeat(500) }, game);
    assert.equal([...advice.say].length, MAX_SAY);
    assert.equal(validateAdvice({ say: 'a\nb' }, game).say, 'a b');
    assert.ok(!validateAdvice({ say: '[[emote:angry]]' }, game).say.startsWith('[['));
    assert.equal(validateAdvice({ say: '   ' }, game).say, undefined);
});

test('garbage answers give empty advice', () => {
    assert.deepEqual(validateAdvice(null, game), {});
    assert.deepEqual(validateAdvice('text', game), {});
    assert.deepEqual(validateAdvice({ suspects: 'x', plan: 3 }, game), {});
});

test('prompt has language, own role, every player, and no hidden hands', () => {
    const prompt = buildPrompt({ names: [{ user_id: 1, name: 'Phuc' }, { user_id: -2, name: 'BOT Rosa' }], game });
    assert.match(prompt, /in Vietnamese/);
    assert.match(prompt, /You are an Outlaw/);
    for (const c of ['WILLY_THE_KID', 'ROSE_DOOLAN', 'JOURDONNAIS']) assert.ok(prompt.includes(c), c);
    assert.match(prompt, /Phuc \(WILLY_THE_KID\), sheriff/);
    assert.match(prompt, /id 3: BOT Rosa \(ROSE_DOOLAN\), role unknown/);
    assert.match(prompt, /Your hand: BANG, BEER/);
    assert.match(prompt, /played_card_on: BANG, Phuc \(WILLY_THE_KID\), BOT Rosa \(ROSE_DOOLAN\)/);
});

test('log lines are readable', () => {
    assert.equal(logLine({ game_log: { format_str: 'LOG_TAKEN_DAMAGE', format_args: [{ player: 2 }, { integer: 1 }] } }, id => 'P' + id),
        'taken_damage: P2, 1');
    assert.equal(logLine({}, String), null);
});
