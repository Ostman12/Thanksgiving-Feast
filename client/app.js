'use strict';
// Player page. The server is the source of truth: every incoming message
// updates `state`, then render() shows the screen for the current phase.
// Taps only send intents (and lock their buttons until the server answers).

const { el, fill } = Feast;
const $ = (id) => document.getElementById(id);

// ------------------------------------------------------------------ storage

const SAVED = 'feast.player';  // {code, token, playerId}: for REJOIN
const NAME = 'feast.name';     // last name typed, to prefill the form

function loadSaved() {
  try {
    return JSON.parse(localStorage.getItem(SAVED));
  } catch (e) {
    return null;
  }
}

// ------------------------------------------------------------------ state

const state = {
  connected: false,
  me: null,           // my playerId once JOINED
  rejoining: false,   // sent REJOIN, waiting for JOINED or ERROR
  joining: false,     // sent CREATE_ROOM / JOIN_ROOM
  room: null,         // ROOM_STATE payload
  phase: null,        // PHASE payload (round, deadline)
  // Mystery
  word: null,         // YOUR_WORD payload
  wordVisible: false,
  submitted: [],      // SUBMITTED playerIds for the current phase
  voteResult: null,
  roundResult: null,
  // Would You Rather
  prompt: null,
  wyrResult: null,
  choice: null,       // my pick, 'A' or 'B', once tapped
  // Taps waiting for the server (buttons stay locked meanwhile)
  pending: null,      // 'vote' | 'guess' | 'choice' | null
  matchOver: null,
  endArmed: false,    // first tap on "End match" arms it, second sends
  stealArmed: false,  // same two-tap lock-in for the impostor's steal
};

function resetMystery() {
  state.submitted = [];
  state.voteResult = null;
  state.roundResult = null;
  state.pending = null;
}

function resetWyr() {
  state.submitted = [];
  state.wyrResult = null;
  state.choice = null;
  state.pending = null;
}

// ------------------------------------------------------------------ socket

const socket = Feast.connect({
  onOpen() {
    const saved = loadSaved();
    if (saved && saved.token) {
      state.rejoining = true;
      socket.send('REJOIN', { code: saved.code, token: saved.token });
    }
    render();
  },
  onMessage: handle,
  onStatus(ok) {
    state.connected = ok;
    if (!ok) state.pending = null;  // anything in flight is lost; let them retry
    render();
  },
});

function handle({ type, payload }) {
  switch (type) {
    case 'JOINED':
      state.me = payload.playerId;
      state.joining = false;
      state.rejoining = false;
      localStorage.setItem(SAVED, JSON.stringify(payload));
      break;
    case 'ERROR':
      if (state.rejoining) {
        // Saved seat is gone (room deleted, server restarted): start over.
        localStorage.removeItem(SAVED);
        state.rejoining = false;
        state.me = null;
        state.room = null;
        Feast.toast(`Couldn't rejoin: ${payload.reason}`);
      } else {
        state.joining = false;
        state.pending = null;
        Feast.toast(payload.reason);
      }
      break;
    case 'ROOM_STATE':
      state.room = payload;
      break;
    case 'PHASE':
      state.phase = payload;
      state.endArmed = false;
      state.stealArmed = false;
      state.pending = null;  // a new phase: nothing of mine is in flight any more
      if (payload.phase === 'mystery_discuss') resetMystery();
      if (payload.phase === 'lobby') {
        state.word = null;
        state.matchOver = null;
      }
      break;
    case 'MATCH_STARTED':
      state.matchOver = null;
      resetMystery();
      resetWyr();
      break;
    case 'YOUR_WORD':
      state.word = payload;
      state.wordVisible = false;  // always arrives blurred
      break;
    case 'SUBMITTED':
      state.submitted = payload.playerIds;
      if (payload.playerIds.includes(state.me)) state.pending = null;  // mine landed
      break;
    case 'VOTE_RESULT':
      state.voteResult = payload;
      break;
    case 'ROUND_RESULT':
      state.roundResult = payload;
      state.pending = null;
      break;
    case 'WYR_PROMPT':
      resetWyr();
      state.prompt = payload;
      break;
    case 'WYR_RESULT':
      state.wyrResult = payload;
      break;
    case 'MATCH_OVER':
      state.matchOver = payload;
      break;
    default:
      return;
  }
  render();
}

// ------------------------------------------------------------------ helpers

function players() {
  return state.room ? state.room.players : [];
}

function nameOf(id) {
  const p = players().find((x) => x.id === id);
  return p ? p.name : '?';
}

function isHost() {
  return state.room && state.room.hostId === state.me;
}

function iSubmitted() {
  return state.submitted.includes(state.me);
}

function myDelta(points) {
  const mine = (points || []).find((p) => p.playerId === state.me);
  return mine ? mine.delta : 0;
}

// The phase to show. ROOM_STATE arrives before PHASE on a change, so the
// room's phase decides the screen and PHASE only adds round and deadline.
function currentPhase() {
  return state.room ? state.room.phase : null;
}

function currentDeadline() {
  return state.phase && state.phase.phase === currentPhase() ? state.phase.deadline : null;
}

function progress() {
  return `${state.submitted.length} of ${players().length}`;
}

// ------------------------------------------------------------------ render

function render() {
  $('banner').hidden = state.connected || !state.me;

  let screen;
  if (!state.me) screen = state.rejoining || (!state.connected && loadSaved()) ? 'connecting' : 'join';
  else if (!state.room) screen = 'connecting';
  else screen = currentPhase();
  Feast.showScreen(screen);

  renderRoundBar(screen);
  renderWordCard(screen);
  renderHostBar(screen);

  switch (screen) {
    case 'join': renderJoin(); break;
    case 'lobby': renderLobby(); break;
    case 'mystery_vote': renderVote(); break;
    case 'mystery_guess': renderGuess(); break;
    case 'mystery_reveal': renderMysteryReveal(); break;
    case 'wyr_choose': renderChoose(); break;
    case 'wyr_reveal': renderWyrReveal(); break;
    case 'match_over': renderMatchOver(); break;
    default: break;
  }
  tickCountdown();
}

function renderRoundBar(screen) {
  const inGame = state.phase && !['join', 'connecting', 'lobby', 'match_over'].includes(screen);
  $('round-bar').hidden = !inGame;
  if (!inGame) return;
  const game = state.phase.game === 'wyr' ? 'Would You Rather' : 'Mystery';
  const me = players().find((p) => p.id === state.me);
  $('round-label').textContent =
      `${game} · ${state.phase.round} of 3` + (me ? ` · ${me.score} pts` : '');
}

function tickCountdown() {
  const left = Feast.secondsLeft(currentDeadline());
  $('countdown').textContent = left === null ? '' : `⏱ ${left}s`;
  $('countdown').classList.toggle('urgent', left !== null && left <= 10);
}
setInterval(tickCountdown, 250);

function renderWordCard(screen) {
  const show = state.word && ['mystery_discuss', 'mystery_vote', 'mystery_guess'].includes(screen);
  $('word-card').hidden = !show;
  if (!show) return;
  // While hidden, every phone shows the same placeholder: a blurred real word
  // would still leak its length, and "You are the IMPOSTOR" is longer than
  // "Your word", which a neighbor could spot.
  const shown = state.wordVisible;
  $('word-role').textContent = !shown ? '🔒 Tap to see your word'
      : state.word.isImpostor ? '🕵️ You are the IMPOSTOR' : 'Your word';
  $('word-text').textContent = shown ? state.word.word : '••••••';
  $('word-btn').classList.toggle('blurred', !shown);
  $('word-btn').classList.toggle('impostor', shown && state.word.isImpostor);

  // Steal: impostor, discussion, card open. Two taps to lock in, like End match.
  const canSteal = shown && state.word.isImpostor && screen === 'mystery_discuss';
  $('steal-box').hidden = !canSteal;
  const sending = state.pending === 'steal';
  $('steal-btn').disabled = sending;
  $('steal-btn').textContent = sending ? 'Sending…'
      : state.stealArmed ? 'Tap again to lock it in' : 'Steal the round';
  $('steal-btn').classList.toggle('armed', state.stealArmed && !sending);
}

const NEXT_LABELS = {
  mystery_discuss: 'End discussion → vote',
  mystery_vote: 'End voting now',
  mystery_guess: 'Skip the guess',
  wyr_choose: 'Reveal now',
  match_over: 'Back to lobby',
};

function nextLabel(screen) {
  const round = state.phase ? state.phase.round : 0;
  if (screen === 'mystery_reveal') {
    if (state.roundResult && state.roundResult.void) return 'Replay this round';
    return round < 3 ? 'Next round' : 'Start Would You Rather';
  }
  if (screen === 'wyr_reveal') return round < 3 ? 'Next prompt' : 'Final scores';
  return NEXT_LABELS[screen];
}

function renderHostBar(screen) {
  const canNext = isHost() && !['join', 'connecting', 'lobby', 'paused'].includes(screen);
  const canEnd = isHost() && !['join', 'connecting', 'lobby', 'match_over'].includes(screen);
  $('host-bar').hidden = !canNext && !canEnd;
  $('next-btn').hidden = !canNext;
  $('next-btn').textContent = nextLabel(screen) || 'Next';
  $('end-btn').hidden = !canEnd;
  $('end-btn').textContent = state.endArmed ? 'Tap again to end the match' : 'End match';
  $('end-btn').classList.toggle('armed', state.endArmed);
}

function renderJoin() {
  $('join-btn').disabled = state.joining || !state.connected;
  $('create-btn').disabled = state.joining || !state.connected;
}

function renderLobby() {
  $('lobby-code').textContent = state.room.code;
  fill('lobby-players', players().map((p) =>
    el('li', { class: p.connected ? '' : 'away' },
      p.id === state.room.hostId ? '👑 ' : '', p.name,
      p.id === state.me ? ' (you)' : '',
      p.connected ? '' : ' · away')));

  const needed = state.room.testMode ? 2 : 4;
  const here = players().filter((p) => p.connected).length;
  if (isHost()) {
    fill('lobby-host',
      el('button', { class: 'btn primary', disabled: here < needed,
                     onclick: () => socket.send('START_MATCH') }, 'Start the game'),
      here < needed ? el('p', { class: 'muted center' }, `Need ${needed}+ players (${here} here)`) : null);
  } else {
    fill('lobby-host', el('p', { class: 'muted center' }, `Waiting for ${nameOf(state.room.hostId)} to start…`));
  }
}

function renderVote() {
  if (iSubmitted() || state.pending === 'vote') {
    fill('vote-body',
      el('p', { class: 'big center' }, state.pending ? 'Sending…' : 'Vote sent ✓'),
      el('p', { class: 'muted center' }, `${progress()} voted`));
    return;
  }
  const vote = (targetId) => {
    if (socket.send('CAST_VOTE', { targetId })) {
      state.pending = 'vote';
      render();
    }
  };
  fill('vote-body',
    el('p', {}, 'Who is the impostor?'),
    el('div', { class: 'choices' },
      players().filter((p) => p.id !== state.me).map((p) =>
        el('button', { class: 'btn', onclick: () => vote(p.id) },
          p.name, p.connected ? '' : ' (away)')),
      el('button', { class: 'btn subtle', onclick: () => vote(null) }, 'Skip')),
    el('p', { class: 'muted center' }, `${progress()} voted`));
}

// Summary of the vote result that is safe to show everyone.
function voteSummary(result) {
  if (!result) return null;
  if (result.caught) return el('p', { class: 'big center' }, `🚨 ${nameOf(result.accusedId)} was caught!`);
  if (result.accusedId) return el('p', { class: 'big center' }, `${nameOf(result.accusedId)} was voted out… but were they the impostor?`);
  return el('p', { class: 'big center' }, 'Nobody was voted out.');
}

function renderGuess() {
  fill('guess-result', voteSummary(state.voteResult));
  const impostor = state.word && state.word.isImpostor;
  const sending = state.pending === 'guess';
  $('guess-form').hidden = !impostor;
  $('guess-btn').disabled = sending;
  $('guess-btn').textContent = sending ? 'Sending…' : 'Send guess';
  fill('guess-wait', impostor ? null : el('p', { class: 'center' }, '🙏 Caught! The impostor gets one last guess at your word…'));
}

function renderMysteryReveal() {
  const r = state.roundResult;
  if (!r) {
    fill('mreveal-body', el('p', { class: 'center' }, 'Revealing…'));
    return;
  }
  if (r.void) {
    fill('mreveal-body',
      el('h2', {}, '🚫 Round void'),
      el('p', {}, `The impostor (${nameOf(r.impostorId)}) left, so nobody scores. This round will be played again with new words.`),
      wordsLine(r));
    return;
  }
  const delta = myDelta(r.points);
  const votes = r.earlyGuess || !state.voteResult ? [] : state.voteResult.votes;
  fill('mreveal-body',
    el('h2', {}, `The impostor was ${nameOf(r.impostorId)} 🕵️`),
    r.earlyGuess ? stealLine(r) : voteSummary(state.voteResult),
    wordsLine(r),
    r.earlyGuess ? null
      : r.guess === null ? el('p', {}, 'No guess.')
      : el('p', {}, `Hail mary: “${r.guess}” `, r.guessCorrect ? '✅ correct!' : '❌ wrong'),
    el('p', { class: 'big center' }, delta ? `You earned ${Feast.signed(delta)} 🎉` : 'No points for you this round.'),
    votes.length ? el('h3', {}, 'Votes') : null,
    votes.length ? el('ul', { class: 'plain' }, votes.map((v) =>
      el('li', {}, `${nameOf(v.voterId)} → ${v.targetId ? nameOf(v.targetId) : 'Skip'}`))) : null,
    pointsList(r.points));
}

// The impostor guessed during discussion.
function stealLine(r) {
  return r.guessCorrect
    ? el('p', { class: 'big center' }, `🎯 ${nameOf(r.impostorId)} guessed “${r.guess}” and stole the round!`)
    : el('p', { class: 'big center' }, `😬 ${nameOf(r.impostorId)} guessed “${r.guess}”, wrong, and outed themselves!`);
}

function wordsLine(r) {
  return el('p', {}, 'Word: ', el('b', {}, r.mainWord), ' · Impostor had: ', el('b', {}, r.impostorWord));
}

function pointsList(points) {
  const scored = (points || []).filter((p) => p.delta !== 0);
  if (!scored.length) return null;
  return [el('h3', {}, 'Points'),
          el('ul', { class: 'plain' }, scored.map((p) => el('li', {}, `${nameOf(p.playerId)} ${Feast.signed(p.delta)}`)))];
}

function renderChoose() {
  const p = state.prompt || { optionA: 'A', optionB: 'B' };
  for (const button of document.querySelectorAll('[data-pick]')) {
    const option = button.dataset.option;
    button.textContent = `${option}: ${option === 'A' ? p.optionA : p.optionB}`;
    button.classList.toggle('selected', state.choice === option);
  }
  const done = iSubmitted() || state.pending === 'choice';
  $('choose-body').hidden = done;
  const picked = state.choice === 'A' ? p.optionA : state.choice === 'B' ? p.optionB : null;
  fill('choose-done', done
    ? [el('p', { class: 'big center' }, state.pending ? 'Sending…' : 'Sent ✓'),
       picked ? el('p', { class: 'center' }, `You'd rather: ${picked}`) : null,
       el('p', { class: 'muted center' }, `${progress()} answered`)]
    : el('p', { class: 'muted center' }, `${progress()} answered`));
}

function renderWyrReveal() {
  const r = state.wyrResult;
  const p = state.prompt;
  if (!r || !p) {
    fill('wreveal-body', el('p', { class: 'center' }, 'Revealing…'));
    return;
  }
  const delta = myDelta(r.points);
  const pick = (id) => r.choices.find((c) => c.playerId === id);
  const mine = pick(state.me);
  fill('wreveal-body',
    el('h2', {}, '📊 Results'),
    el('p', { class: r.majority === 'A' ? 'winner' : '' }, `A: ${p.optionA} — `, el('b', {}, String(r.countA))),
    el('p', { class: r.majority === 'B' ? 'winner' : '' }, `B: ${p.optionB} — `, el('b', {}, String(r.countB))),
    el('p', {}, r.majority ? `Most people picked ${r.majority}.` : 'Even split: no majority points.'),
    mine ? el('p', {}, `You picked ${mine.choice}.`) : el('p', {}, "You didn't answer."),
    el('p', { class: 'big center' }, delta ? `You earned ${Feast.signed(delta)} 🎉` : 'No points this time.'),
    el('ul', { class: 'plain' }, r.choices.map((c) =>
      el('li', {}, `${nameOf(c.playerId)}: ${c.choice}`))),
    pointsList(r.points));
}

function renderMatchOver() {
  const m = state.matchOver;
  if (!m) {
    fill('over-body', el('p', { class: 'center' }, 'Final scores…'));
    return;
  }
  const me = m.leaderboard.find((e) => e.playerId === state.me);
  fill('over-body',
    el('h2', { class: 'center' }, '🏆 Final scores'),
    el('p', { class: 'big center' },
      m.winnerIds.includes(state.me)
        ? (m.winnerIds.length > 1 ? "You're a co-champion! 🎉" : "You're the champion! 🎉")
        : `Champion: ${m.winnerIds.map(nameOf).join(' & ')}`),
    el('ol', { class: 'leaderboard' }, m.leaderboard.map((e) =>
      el('li', { class: e.playerId === state.me ? 'me' : '' },
        m.winnerIds.includes(e.playerId) ? '🏆 ' : '', `${e.name} — ${e.score}`))),
    me ? el('p', { class: 'center' }, `You scored ${me.score}.`) : null);
}

// ------------------------------------------------------------------ taps

$('code-input').addEventListener('input', (e) => {
  e.target.value = e.target.value.toUpperCase().replace(/[^A-Z]/g, '');
});
$('name-input').value = localStorage.getItem(NAME) || '';

function readName() {
  const name = $('name-input').value.trim();
  if (!name) {
    Feast.toast('Type your name first');
    $('name-input').focus();
    return null;
  }
  localStorage.setItem(NAME, name);
  return name;
}

$('join-btn').addEventListener('click', () => {
  const name = readName();
  if (!name) return;
  const code = $('code-input').value.trim();
  if (code.length !== 4) {
    Feast.toast('Room codes are 4 letters');
    $('code-input').focus();
    return;
  }
  Feast.requestWakeLock();
  if (socket.send('JOIN_ROOM', { code, name })) state.joining = true;
  render();
});

$('create-btn').addEventListener('click', () => {
  const name = readName();
  if (!name) return;
  Feast.requestWakeLock();
  if (socket.send('CREATE_ROOM', { name })) state.joining = true;
  render();
});

$('word-btn').addEventListener('click', () => {
  state.wordVisible = !state.wordVisible;  // client only: nothing is sent
  render();
});

function sendGuess() {
  const word = $('guess-input').value.trim();
  if (!word) return;
  if (socket.send('GUESS_WORD', { word })) {
    state.pending = 'guess';
    $('guess-input').value = '';
    render();
  }
}
$('guess-btn').addEventListener('click', sendGuess);

// Steal during discussion: first tap arms, second (within 3 s) sends.
let stealTimer = null;
$('steal-btn').addEventListener('click', () => {
  const word = $('steal-input').value.trim();
  if (!word) {
    Feast.toast('Type your guess first');
    return;
  }
  if (!state.stealArmed) {
    state.stealArmed = true;
    clearTimeout(stealTimer);
    stealTimer = setTimeout(() => { state.stealArmed = false; render(); }, 3000);
  } else if (socket.send('GUESS_WORD', { word })) {
    state.stealArmed = false;
    state.pending = 'steal';
    $('steal-input').value = '';
  }
  render();
});
$('guess-input').addEventListener('keydown', (e) => { if (e.key === 'Enter') sendGuess(); });

// One tap sends your pick, like a buzzer.
for (const button of document.querySelectorAll('[data-pick]')) {
  button.addEventListener('click', () => {
    if (state.pending || iSubmitted()) return;
    if (socket.send('SUBMIT_CHOICE', { choice: button.dataset.option })) {
      state.choice = button.dataset.option;
      state.pending = 'choice';
      render();
    }
  });
}

$('next-btn').addEventListener('click', () => socket.send('NEXT'));

let endTimer = null;
$('end-btn').addEventListener('click', () => {
  // No confirm() dialog (it freezes some phone browsers): tap twice instead.
  if (!state.endArmed) {
    state.endArmed = true;
    clearTimeout(endTimer);
    endTimer = setTimeout(() => { state.endArmed = false; render(); }, 3000);
  } else {
    state.endArmed = false;
    socket.send('END_MATCH');
  }
  render();
});

render();
