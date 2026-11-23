# ai-bridge: AI advice and table talk for bang-server bots

A small Node program (Node 22+, no dependencies) that connects to bang-server as the "AI bridge". When a lobby has
the **AI bots** option on, the server sends one request per bot turn with what that bot may know (its role and hand,
the public table, recent log lines). The helper asks Google Gemini for advice and sends back:

- `suspects`: the role it guesses for players whose role the bot doesn't know
- `plan`: `lie_low`, `attack_sheriff`, `protect_sheriff`, `hunt_outlaws` or `survive`
- `target_id`: the player to attack first
- `say`: a chat line in the lobby's bot chat language (French, Vietnamese or English), max 120 characters
- `emote` / `emote_target`

The bot still makes every move itself; the advice only guides its choices. Every field is validated
(`validate.mjs`) and the server checks it again. If the helper is missing, slow (> 5 s) or fails, bots play by the rules; a bot that gets 3 failures in a row stops asking for that game.

## Environment

| Variable | Meaning |
|---|---|
| `BANG_SERVER_URL` | the game server, e.g. `ws://localhost:47654` or `wss://<railway address>` |
| `AI_BRIDGE_TOKEN` | must equal the game server's `AI_BRIDGE_TOKEN` (the server refuses the bridge without it) |
| `GEMINI_API_KEY` | the Gemini key, or `GEMINI_API_KEY_FILE` = a file holding it (first line) |
| `GEMINI_MODEL` | default `gemini-flash-lite-latest`; `node index.mjs --list-models` lists the models the key may use |
| `GEMINI_THINKING` | default `minimal` (faster answers); empty = the model default |
| `GEMINI_FAKE=1` | canned advice, no Gemini call (tests) |
| `AI_BRIDGE_VERBOSE=1` | log every answer |

## Run locally

```
# game server (WSL), with the token
wsl -d bangbuild -u root -- env AI_BRIDGE_TOKEN=dev-token /root/bang-build/bangserver 47654 -r -l info
# helper
AI_BRIDGE_TOKEN=dev-token GEMINI_API_KEY_FILE=C:/Users/<you>/.bang-ai/gemini.key node ai-bridge/index.mjs
```

Tests: `npm test` (in this folder). Every minute the helper logs how many requests it answered and the average time.

## Railway

A second service in the same project, image `ghcr.io/bunyu/bang-ai-bridge:<sha>` (built by `.github/workflows/ai-bridge.yml`),
with the variables above (`BANG_SERVER_URL` = the game server's public `wss://` address). The game server service needs
`AI_BRIDGE_TOKEN` too (same value, a long random string).

Free Gemini tier: limits are per Google Cloud project (see Google AI Studio); about one request per bot turn,
40-60 per 5-player game. When the quota is used up, Gemini answers HTTP 429 and the bots play by the rules.
