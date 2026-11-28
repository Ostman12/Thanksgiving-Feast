'use strict';
// Shared by the player page (app.js) and the display page (display.js):
// the WebSocket with automatic reconnect, and small DOM helpers.

const Feast = (() => {
  // CLAUDE.md: build the URL from location (wss on https, ws on http).
  function wsUrl() {
    return `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`;
  }

  // Keeps one WebSocket open forever. Phones drop sockets when the screen
  // locks, so: reconnect on close (0.5 s, doubling up to 5 s), and when the
  // page becomes visible again after a while, start a fresh socket even if the
  // old one still looks open (it is often silently dead).
  //   onOpen()      socket is open: send REJOIN / WATCH_ROOM here
  //   onMessage(m)  every parsed {type, payload}
  //   onStatus(ok)  true when open, false when not
  function connect({ onOpen, onMessage, onStatus }) {
    let ws = null;
    let delay = 500;
    let timer = null;
    let hiddenAt = null;

    function open() {
      clearTimeout(timer);
      timer = null;
      if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) return;
      const sock = new WebSocket(wsUrl());
      ws = sock;
      sock.onopen = () => {
        if (sock !== ws) return;
        delay = 500;
        onStatus(true);
        onOpen();
      };
      sock.onmessage = (event) => {
        if (sock !== ws) return;
        let message;
        try {
          message = JSON.parse(event.data);
        } catch (e) {
          return;
        }
        onMessage(message);
      };
      sock.onclose = () => {
        if (sock !== ws) return;  // An old socket we already replaced.
        onStatus(false);
        if (!timer) {
          timer = setTimeout(open, delay);
          delay = Math.min(delay * 2, 5000);
        }
      };
    }

    function reconnectNow() {
      const old = ws;
      ws = null;
      if (old) {
        try { old.close(); } catch (e) { /* already closed */ }
      }
      onStatus(false);
      delay = 500;
      open();
    }

    document.addEventListener('visibilitychange', () => {
      if (document.visibilityState === 'hidden') {
        hiddenAt = Date.now();
        return;
      }
      const away = hiddenAt === null ? 0 : Date.now() - hiddenAt;
      hiddenAt = null;
      if (away > 3000) reconnectNow(); else open();
      requestWakeLock();
    });
    window.addEventListener('online', open);
    window.addEventListener('pageshow', (e) => { if (e.persisted) reconnectNow(); });

    open();
    return {
      send(type, payload = {}) {
        if (!ws || ws.readyState !== WebSocket.OPEN) return false;
        ws.send(JSON.stringify({ type, payload }));
        return true;
      },
    };
  }

  // Keep the screen on while the page is open, so phones don't lock and drop
  // the socket. Must be asked for after a tap; failures are fine to ignore.
  let wakeLock = null;
  async function requestWakeLock() {
    if (!('wakeLock' in navigator) || document.visibilityState !== 'visible') return;
    if (wakeLock && !wakeLock.released) return;
    try {
      wakeLock = await navigator.wakeLock.request('screen');
    } catch (e) {
      wakeLock = null;
    }
  }

  // el('p', {class: 'x', onclick: fn}, 'text', child, ...). Text is always
  // added as text, never parsed as HTML, so player names can't inject markup.
  function el(tag, attrs, ...children) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attrs || {})) {
      if (value === false || value === null || value === undefined) continue;
      if (key === 'class') node.className = value;
      else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
      else if (value === true) node.setAttribute(key, '');
      else node.setAttribute(key, value);
    }
    for (const child of children.flat()) {
      if (child === null || child === undefined || child === false) continue;
      node.append(typeof child === 'string' || typeof child === 'number' ? String(child) : child);
    }
    return node;
  }

  // Replace the contents of the element with id `id`.
  function fill(id, ...children) {
    document.getElementById(id).replaceChildren(...children.flat().filter((c) => c !== null && c !== false && c !== undefined));
  }

  // Show the <section data-screen="name"> and hide the rest.
  function showScreen(name) {
    for (const section of document.querySelectorAll('[data-screen]')) {
      section.hidden = section.dataset.screen !== name;
    }
  }

  function secondsLeft(deadline) {
    if (deadline === null || deadline === undefined) return null;
    return Math.max(0, Math.ceil((deadline - Date.now()) / 1000));
  }

  let toastTimer = null;
  function toast(text) {
    const node = document.getElementById('toast');
    node.textContent = text;
    node.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { node.hidden = true; }, 3500);
  }

  function signed(n) {
    return n > 0 ? `+${n}` : String(n);
  }

  // Avatars (rules.md): the server sends names; this is the one table that
  // turns them into emoji and colors. Keep in sync with src/game/avatar.cpp.
  const AVATAR_EMOJI = {
    turkey: '🦃', pie: '🥧', leaf: '🍂', corn: '🌽', maple: '🍁', bear: '🐻', fox: '🦊', dog: '🐶',
    cat: '🐱', frog: '🐸', panda: '🐼', penguin: '🐧', owl: '🦉', cowboy: '🤠', cool: '😎', alien: '👽',
  };
  const AVATAR_COLOR = {
    orange: '#e2662a', cranberry: '#a11d3a', gold: '#f0b23e', green: '#4f8a3c',
    blue: '#3a6ea5', purple: '#7b4fa0', pink: '#d45d8c', brown: '#6b4428',
  };
  const AVATAR_ACCESSORY = {
    none: '', tophat: '🎩', crown: '👑', bow: '🎀', cap: '🧢', gradcap: '🎓', flower: '🌸',
  };

  // A round avatar. size: '' (inline with text), 'lg', or 'xl'. Only values
  // from the tables above go into the page, never raw text from the server.
  function avatar(a, size = '') {
    const av = a || {};
    const emoji = AVATAR_EMOJI[av.emoji] || AVATAR_EMOJI.turkey;
    const color = AVATAR_COLOR[av.color] || AVATAR_COLOR.orange;
    const acc = AVATAR_ACCESSORY[av.accessory] || '';
    return el('span', { class: `avatar ${size}`, style: `background:${color}`, 'aria-hidden': 'true' },
      el('span', { class: 'avatar-emoji' }, emoji),
      acc ? el('span', { class: 'avatar-acc' }, acc) : null);
  }

  // Avatar + name, for lists.
  function who(player, size = '') {
    if (!player) return el('span', { class: 'who' }, '?');
    return el('span', { class: 'who' }, avatar(player.avatar, size), player.name);
  }

  // ---- Podium ceremony (design.md). Groups MATCH_OVER's leaderboard by score
  // with competition ranking (1, 1, 3): [{place, score, entries}], best first.
  function rankGroups(leaderboard) {
    const groups = [];
    leaderboard.forEach((entry, i) => {
      const last = groups[groups.length - 1];
      if (last && last.score === entry.score) last.entries.push(entry);
      else groups.push({ place: i + 1, score: entry.score, entries: [entry] });
    });
    return groups;
  }

  function ordinal(n) {
    const s = ['th', 'st', 'nd', 'rd'];
    const v = n % 100;
    return n + (s[(v - 20) % 10] || s[v] || s[0]);
  }

  // The whole ceremony as one element. Every step is a CSS animation with a
  // delay (style.css .ceremony), so all screens play the same show.
  //   m:        MATCH_OVER payload
  //   playerOf: id -> player from ROOM_STATE (for avatars)
  //   me:       my player id on a phone, null on the display
  function ceremony(m, playerOf, me = null) {
    const groups = rankGroups(m.leaderboard);
    const avatarOf = (id) => {
      const p = playerOf(id);
      return p ? p.avatar : null;
    };
    const medal = ['🥇', '🥈', '🥉'];

    // Pedestals in podium order: 2nd on the left, 1st in the middle, 3rd on the right.
    const pedestal = (groupIndex) => {
      const g = groups[groupIndex];
      if (!g) return null;
      return el('div', { class: `pedestal p${groupIndex + 1}` },
        el('div', { class: 'stand' },
          groupIndex === 0 ? el('div', { class: 'crown' }, '👑') : null,
          g.entries.map((e) => el('div', { class: 'stander' },
            avatar(avatarOf(e.playerId), 'xl'),
            el('div', { class: 'stander-name' }, e.name)))),
        el('div', { class: 'block' },
          el('div', { class: 'place' }, medal[groupIndex]),
          el('div', { class: 'pts' }, `${g.score}`)));
    };

    const champs = groups[0] ? groups[0].entries.map((e) => e.name) : [];
    const championLine = champs.length > 1
      ? `🏆 ${champs.join(' & ')} are co-champions!`
      : `🏆 ${champs[0] || '?'} is the champion!`;

    let mine = null;
    if (me) {
      const g = groups.find((x) => x.entries.some((e) => e.playerId === me));
      if (g) {
        const icon = g.place <= 3 ? ` ${medal[groups.indexOf(g)] || ''}` : '';
        mine = g.place === 1 ? (champs.length > 1 ? "You're a co-champion! 🎉" : "You're the champion! 🎉")
                             : `You finished ${ordinal(g.place)}!${icon}`;
      }
    }

    const rest = groups.slice(3).flatMap((g) => g.entries.map((e) =>
      el('li', { class: e.playerId === me ? 'me' : '' },
        el('span', { class: 'rest-place' }, ordinal(g.place)),
        avatar(avatarOf(e.playerId)), `${e.name} — ${e.score}`)));

    return el('div', { class: 'ceremony' },
      el('p', { class: 'drumroll center' }, '🥁 And the final results are…'),
      el('div', { class: 'podium' }, pedestal(1), pedestal(0), pedestal(2)),
      el('div', { class: 'confetti', 'aria-hidden': 'true' },
        ['🎉', '🎊', '✨', '🦃', '🎉', '🎊', '🍂', '✨', '🎉', '🥧', '🎊', '✨'].map((c) => el('span', {}, c))),
      el('p', { class: 'champion-line center' }, championLine),
      mine ? el('p', { class: 'my-place center' }, mine) : null,
      rest.length ? el('ol', { class: 'rest' }, rest) : null);
  }

  return {
    connect, requestWakeLock, el, fill, showScreen, secondsLeft, toast, signed, avatar, who,
    ceremony, AVATAR_EMOJI, AVATAR_COLOR, AVATAR_ACCESSORY,
  };
})();
