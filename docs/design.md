# Feast Mode Design Decisions

Decisions made before game night (Nov 28, 2026), and why. Rules are in rules.md and messages
in protocol.md. This file explains how the pieces are built.

## Server stack

- **Crow** (C++ web framework) with **standalone Asio**, plus **nlohmann/json** for
  messages. All three are fetched by CMake (`FetchContent`), so there is nothing to install
  by hand.
- One process, one port: Crow serves the client files over HTTP and the WebSocket at `/ws`.
- No TLS in the server. The tunnel provides HTTPS; the server only sees plain HTTP and `ws://`.

**Why Crow:** it serves static files and WebSockets on one port out of the box, and its
route-plus-callback code is short enough to explain line by line.

**Rejected:**
- **Boost.Beast:** low level. You write the accept loop, HTTP upgrade, per-connection
  lifetimes (`shared_from_this`), a write queue (one write in flight per socket), strands,
  and static file serving yourself. That is ~250 lines before the first game message, and
  its bugs show up as crashes when phones drop sockets.
- **IXWebSocket:** no static file serving, so it would need a second server or port.
- **uWebSockets:** awkward to build with MSVC.

## Threading

- **One mutex** guards all rooms and game state. Every WebSocket handler locks it, calls into
  the game classes, and sends the resulting messages.
- **One timer thread** wakes about every 250 ms, takes the same lock, and ends phases whose
  deadline has passed.
- Game logic stays in `src/game` with no networking (CLAUDE.md), so it stays unit-testable.

## Screens: phones plus a laptop display

- **Phones** (players) open `/`. **The laptop** opens `/display` and sends `WATCH_ROOM`
  (protocol.md, Display). The display is watch-only and never gets secrets.
- **The host stays a phone player.** The room creator taps Start and Next on their phone,
  as rules.md says. The laptop is not the host.
- Open `/display` on the laptop **through the tunnel URL**, because the display shows its
  own address as the join URL.

| Laptop (big screen) | Phones |
|---|---|
| Join URL (medium) and room code (huge) | Join form, then "You're in!" |
| Player list as people join | — |
| Round number, big countdown | Small countdown |
| "5 of 7 voted" progress | Vote buttons, secret word (blurred), guess box, A/B + prediction |
| Would You Rather prompt, large | A/B buttons |
| Reveals: votes, impostor, words, points | "You earned +100" |
| Leaderboard, champion 🏆 | Your score |

## Client: smooth without reloads

Plain HTML/CSS/JS, no frameworks, no build step (CLAUDE.md). The goal is to feel like
Kahoot, done like this:

1. **The page loads once.** The WebSocket stays open and the server pushes every change. No
   page reloads, no polling.
2. **One page, many screens.** Every screen is a hidden section in the HTML. One `state`
   object is updated by incoming messages, and one `render()` function shows the screen for
   `state.phase` and fills it in.
3. **Taps react instantly.** Buttons lock and show "Vote sent ✓" right away. The server's
   messages are still the source of truth.
4. **Silent reconnect.** When the socket drops, reconnect and send `REJOIN` (phones) or
   `WATCH_ROOM` (display), then re-render. No reload.
5. **Countdowns come from the server's `deadline` timestamp**, so they are correct after a
   phone wakes up.

## Visual style

- Harvest-night theme set once with CSS variables: a dark cranberry-to-chocolate sky,
  content on a cream "plate" with gold trim, orange primary buttons, cranberry headings.
- Chunky Kahoot-style buttons: a darker bottom edge that presses down on tap.
- Falling turkeys: a fixed, non-tappable layer (`.sky`) of 12 emoji behind the content,
  animated with CSS `transform` only (cheap on phones). Hidden under
  `prefers-reduced-motion`.
- Emoji for icons (🦃 🥧 🕵️ ✅). No image files. An optional inline SVG logo comes last.
- System fonts only. No web fonts, which would be one more download that can fail.
- Big type and tap targets (buttons at least 48px tall), portrait at ~375px wide.
- Secret word: CSS `filter: blur()`, toggled by tapping. This is client-only.
- Short CSS fades and slides for screen changes and reveals, turned off under
  `prefers-reduced-motion`.
- **Out:** canvas, WebGL, sprites, animation libraries, JavaScript-driven animation, sound,
  QR codes (would need a dependency).
- **Order:** build the client plain and working first, then style it once the game runs end
  to end.
