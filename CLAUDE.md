# Feast Mode

Thanksgiving party game for 4-10 players (Mystery, then Would You Rather Extreme). C++ WebSocket server (authoritative)
plus a plain HTML/JS client served by the same server.
Protocol: docs/protocol.md. Rules: docs/rules.md. Design decisions: docs/design.md.

Decided: Crow + standalone Asio + nlohmann/json (via CMake FetchContent); one
mutex + one timer thread; phones are players at `/`, a laptop display at
`/display` sends WATCH_ROOM; host stays a phone player. See docs/design.md.
Game night: Saturday Nov 28, 2026. Stability beats features.

## Hard rules
- The server is authoritative. The client only displays state and sends
  intents.
- Never put private information (secret words, impostor identity, votes or
  choices before their result) in a message sent to the whole room.
- Validate every client message against the current phase and the sender's
  permissions. Reject invalid messages with ERROR. Once-only actions stay
  once-only.
- Follow docs/protocol.md exactly. Ask before adding or renaming message
  types.
- Shared game state is touched by one thread only, or guarded by a mutex.
- Game logic lives in classes separate from networking, so it can be unit
  tested without sockets.

## Client
- Players use phones: iOS Safari and Chrome. Design mobile-first (portrait,
  ~375px wide), large tap targets, no hover-only UI, no tiny text.
- Build the WebSocket URL from `location` (wss on https, ws on http).
- Reconnect automatically and send REJOIN when the socket drops (phones drop
  sockets when the screen locks).
- Plain HTML/CSS/JS, no build step, no frameworks.

## Workflow
- Work in small tasks. Show the diff before moving on.
- Never edit a test to make it pass. If a test looks wrong, say so.
- Ask before adding a dependency.
