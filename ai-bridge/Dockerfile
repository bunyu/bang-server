# AI helper for bang-server (Gemini advice and table talk for bots).
#   docker build -t bang-ai-bridge ai-bridge
#   docker run -e BANG_SERVER_URL=wss://... -e AI_BRIDGE_TOKEN=... -e GEMINI_API_KEY=... bang-ai-bridge
FROM node:22-alpine
WORKDIR /app
COPY *.mjs package.json ./
USER node
CMD ["node", "index.mjs"]
