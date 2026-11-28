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
  // Around the Table
  attSubject: null,   // ATT_SUBJECT payload: whose turn
  attPreview: null,   // ATT_PREVIEW payload: only when I'm the subject
  attQuestion: null,  // ATT_QUESTION payload: the chosen question
  attResult: null,    // ATT_RESULT payload
  attMine: null,      // what I tapped: 'A'..'D', or 'pass'
  // Awards
  award: null,        // AWARD payload on screen
  // Taps waiting for the server (buttons stay locked meanwhile)
  pending: null,      // 'vote' | 'guess' | 'choice' | 'att' | null
  matchOver: null,
  endArmed: false,    // first tap on "End match" arms it, second sends
  stealArmed: false,  // same two-tap lock-in for the impostor's steal
  editingAvatar: false,  // the avatar editor screen is open (lobby only, client only)
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
      if (payload.phase !== 'lobby') state.editingAvatar = false;  // the match started
      if (payload.phase === 'mystery_discuss') resetMystery();
      if (payload.phase === 'lobby') {
        state.word = null;
        state.matchOver = null;
      }
      break;
    case 'MATCH_STARTED':
      state.matchOver = null;
      state.award = null;
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
    case 'ATT_SUBJECT':
      // A new turn: forget the last one.
      state.attSubject = payload;
      state.attPreview = null;
      state.attQuestion = null;
      state.attResult = null;
      state.attMine = null;
      state.submitted = [];
      break;
    case 'ATT_PREVIEW':
      state.attPreview = payload;
      state.pending = null;
      break;
    case 'ATT_QUESTION':
      state.attQuestion = payload;
      state.submitted = [];
      break;
    case 'ATT_RESULT':
      state.attResult = payload;
      break;
    case 'AWARD':
      state.award = payload;
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

function playerOf(id) {
  return players().find((x) => x.id === id);
}

// Avatars + names, e.g. for award winners.
function whoList(ids, size = '') {
  return ids.map((id) => Feast.who(playerOf(id), size));
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
  if (screen === 'lobby' && state.editingAvatar) screen = 'avatar';
  Feast.showScreen(screen);

  renderRoundBar(screen);
  renderWordCard(screen);
  renderHostBar(screen);

  switch (screen) {
    case 'join': renderJoin(); break;
    case 'lobby': renderLobby(); break;
    case 'avatar': renderAvatarEditor(); break;
    case 'mystery_vote': renderVote(); break;
    case 'mystery_guess': renderGuess(); break;
    case 'mystery_reveal': renderMysteryReveal(); break;
    case 'wyr_choose': renderChoose(); break;
    case 'wyr_reveal': renderWyrReveal(); break;
    case 'att_choose': renderAttChoose(); break;
    case 'att_pick': renderAttPick(); break;
    case 'att_reveal': renderAttReveal(); break;
    case 'awards': renderAward(); break;
    case 'match_over': renderMatchOver(); break;
    default: break;
  }
  tickCountdown();
}

function renderRoundBar(screen) {
  const inGame = state.phase && !['join', 'connecting', 'lobby', 'avatar', 'match_over'].includes(screen);
  $('round-bar').hidden = !inGame;
  if (!inGame) return;
  const me = players().find((p) => p.id === state.me);
  const pts = me ? ` · ${me.score} pts` : '';
  let label;
  if (screen === 'awards') {
    label = `🏆 Awards · ${state.phase.round} of ${state.award ? state.award.total : '?'}`;
  } else if (state.phase.game === 'att') {
    label = `Around the Table · ${state.phase.round}/${state.attSubject ? state.attSubject.total : '?'}`;
  } else {
    label = `${state.phase.game === 'wyr' ? 'Would You Rather' : 'Mystery'} · ${state.phase.round}/3`;
  }
  $('round-label').textContent = label + pts;
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
  att_choose: 'Use this question',
  att_pick: 'Reveal now',
  match_over: 'Back to lobby',
};

function nextLabel(screen) {
  const round = state.phase ? state.phase.round : 0;
  if (screen === 'mystery_reveal') {
    if (state.roundResult && state.roundResult.void) return 'Replay this round';
    return round < 3 ? 'Next round' : 'Start Would You Rather';
  }
  if (screen === 'wyr_reveal') return round < 3 ? 'Next prompt' : 'Next game →';
  if (screen === 'att_reveal') {
    const total = state.attSubject ? state.attSubject.total : 0;
    return round < total ? 'Next person' : 'To the awards 🏆';
  }
  if (screen === 'awards') {
    return state.award && state.award.index < state.award.total ? 'Next award' : 'Final scores';
  }
  return NEXT_LABELS[screen];
}

function renderHostBar(screen) {
  const canNext = isHost() && !['join', 'connecting', 'lobby', 'avatar', 'paused'].includes(screen);
  const canEnd = isHost() && !['join', 'connecting', 'lobby', 'avatar', 'match_over'].includes(screen);
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
  const me = playerOf(state.me);
  fill('lobby-me',
    el('div', { class: 'center' }, Feast.avatar(me && me.avatar, 'xl')),
    el('button', { class: 'btn', id: 'customize-btn',
                   onclick: () => { state.editingAvatar = true; render(); } }, '🎨 Customize avatar'));
  fill('lobby-players', players().map((p) =>
    el('li', { class: p.connected ? '' : 'away' },
      Feast.avatar(p.avatar), p.id === state.room.hostId ? '👑 ' : '', p.name,
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

// Lobby only: tap an emoji, a color or an accessory; each tap is sent
// straight away and comes back in ROOM_STATE.
function renderAvatarEditor() {
  const me = playerOf(state.me);
  if (!me) return;
  const current = me.avatar || { emoji: 'turkey', color: 'orange', accessory: 'none' };
  const set = (part, value) => {
    socket.send('SET_AVATAR', { ...current, [part]: value });
  };
  const row = (part, table, label, show) => el('div', {},
    el('p', { class: 'step' }, label),
    el('div', { class: `pick-grid ${part}` }, Object.keys(table).map((name) =>
      el('button', {
        class: `pick ${current[part] === name ? 'selected' : ''}`,
        'data-avatar': `${part}:${name}`,
        'aria-label': name,
        onclick: () => set(part, name),
      }, show(name)))));
  fill('avatar-editor',
    el('div', { class: 'center' }, Feast.avatar(current, 'xl')),
    row('emoji', Feast.AVATAR_EMOJI, 'Pick your avatar', (n) => Feast.AVATAR_EMOJI[n]),
    row('color', Feast.AVATAR_COLOR, 'Color', (n) => el('span', { class: 'swatch', style: `background:${Feast.AVATAR_COLOR[n]}` })),
    row('accessory', Feast.AVATAR_ACCESSORY, 'Accessory', (n) => Feast.AVATAR_ACCESSORY[n] || '✖'));
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
          Feast.avatar(p.avatar), p.name, p.connected ? '' : ' (away)')),
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
        m.winnerIds.includes(e.playerId) ? '🏆 ' : '',
        Feast.avatar(playerOf(e.playerId) && playerOf(e.playerId).avatar), `${e.name} — ${e.score}`))),
    me ? el('p', { class: 'center' }, `You scored ${me.score}.`) : null,
    (m.awards || []).length ? el('h3', { class: 'center' }, 'Awards') : null,
    el('ul', { class: 'plain awards-list' }, (m.awards || []).map((a) =>
      el('li', { class: a.playerIds.includes(state.me) ? 'me' : '' },
        `${a.title}: ${a.playerIds.map(nameOf).join(' & ')}`))));
}

// ------------------------------------------------------------------ Around the Table

const LETTERS = ['A', 'B', 'C', 'D'];

function iAmSubject() {
  return state.attSubject && state.attSubject.subjectId === state.me;
}

function subjectName() {
  return state.attSubject ? nameOf(state.attSubject.subjectId) : '?';
}

function optionText(q, letter) {
  return q ? q.options[LETTERS.indexOf(letter)] : '';
}

// Step 2: the subject privately picks a question; everyone else waits.
function renderAttChoose() {
  if (!iAmSubject()) {
    fill('att-choose-body',
      el('p', { class: 'center' }, Feast.avatar(playerOf(state.attSubject && state.attSubject.subjectId)
          ? playerOf(state.attSubject.subjectId).avatar : null, 'xl')),
      el('h2', { class: 'center' }, `🎤 ${subjectName()}'s turn!`),
      el('p', { class: 'big center' }, `${subjectName()} is picking a question…`),
      el('p', { class: 'muted center' }, 'Get ready to guess what they would say.'));
    return;
  }
  const q = state.attPreview;
  const busy = state.pending === 'att';
  const send = (type) => {
    if (socket.send(type)) {
      state.pending = 'att';
      render();
    }
  };
  fill('att-choose-body',
    el('h2', { class: 'center' }, '🎤 Your turn!'),
    el('p', { class: 'center' }, 'Everyone will guess your answer. Happy with this question?'),
    q ? el('div', { class: 'att-card' },
      el('p', { class: 'att-question' }, q.question),
      el('ul', { class: 'att-options' }, LETTERS.map((l, i) => el('li', {}, el('b', {}, `${l}. `), q.options[i]))))
      : el('p', { class: 'center' }, 'Loading…'),
    el('button', { class: 'btn primary', disabled: busy || !q, onclick: () => send('ATT_CONTINUE') },
      'Use this question'),
    el('button', { class: 'btn', disabled: busy || !q || q.skipsLeft <= 0, onclick: () => send('ATT_SKIP') },
      q && q.skipsLeft > 0 ? `Skip (${q.skipsLeft} left)` : 'No skips left'));
}

// Step 3: the subject answers, everyone else predicts (or passes).
function renderAttPick() {
  const q = state.attQuestion;
  if (!q) {
    fill('att-pick-body', el('p', { class: 'center' }, 'Loading…'));
    return;
  }
  const done = iSubmitted() || state.pending === 'att';
  const pick = (option) => {
    if (state.pending || iSubmitted()) return;
    if (socket.send('ATT_PICK', { option })) {
      state.attMine = option || 'pass';
      state.pending = 'att';
      render();
    }
  };
  const header = [
    el('p', { class: 'att-who' }, iAmSubject() ? '🎤 About you' : `🎤 About ${subjectName()}`),
    el('p', { class: 'att-question' }, q.question),
  ];
  if (done) {
    const mine = state.attMine === 'pass' ? 'You passed.'
        : state.attMine ? `${state.attMine}. ${optionText(q, state.attMine)}` : '';
    fill('att-pick-body', header,
      el('p', { class: 'big center' }, state.pending ? 'Sending…' : 'Locked in ✓'),
      mine ? el('p', { class: 'center' }, mine) : null,
      el('p', { class: 'muted center' }, `${progress()} locked in`));
    return;
  }
  fill('att-pick-body', header,
    el('p', { class: 'step' }, iAmSubject() ? 'Pick your real answer:' : `What would ${subjectName()} pick?`),
    el('div', { class: 'choices' }, LETTERS.map((l, i) =>
      el('button', { class: 'btn option', 'data-att': l, onclick: () => pick(l) }, el('b', {}, `${l}. `), q.options[i]))),
    iAmSubject() ? null : el('button', { class: 'btn subtle', 'data-att': 'pass', onclick: () => pick(null) }, 'Pass'),
    el('p', { class: 'muted center' }, `${progress()} locked in`));
}

// Step 4: the reveal.
function renderAttReveal() {
  const r = state.attResult;
  const q = state.attQuestion;
  if (!r) {
    fill('att-reveal-body', el('p', { class: 'center' }, 'Revealing…'));
    return;
  }
  const name = nameOf(r.subjectId);
  const delta = myDelta(r.points);
  const right = r.predictions.filter((p) => p.option && p.option === r.answer).length;
  const made = r.predictions.filter((p) => p.option).length;
  let mine;
  if (r.subjectId === state.me) {
    mine = r.answer ? `${right} of ${made} knew your answer.` : "You didn't answer in time.";
  } else if (delta) {
    mine = `You called it! ${Feast.signed(delta)} 🎉`;
  } else {
    const p = r.predictions.find((x) => x.playerId === state.me);
    mine = !r.answer ? 'No points this round.' : p && p.option ? `${name} surprised you!` : 'You passed.';
  }
  fill('att-reveal-body',
    q ? el('p', { class: 'att-question' }, q.question) : null,
    r.answer
      ? el('div', { class: 'att-answer' }, el('span', { class: 'muted' }, `${name} picked`),
          el('b', {}, `${r.answer}. ${optionText(q, r.answer)}`))
      : el('p', { class: 'big center' }, `${name} didn't answer in time, so nobody scores.`),
    r.perfect ? el('p', { class: 'big center perfect' }, '🎯 Perfect round! Everyone got it!') : null,
    el('p', { class: 'big center' }, mine),
    el('ul', { class: 'plain' }, r.predictions.map((p) =>
      el('li', {}, `${nameOf(p.playerId)}: `,
        !p.option ? 'passed' : `${p.option} ${p.option === r.answer ? '✅' : '❌'}`))),
    pointsList(r.points),
    r.subjectId === state.me && r.answer ? el('p', { class: 'muted center' }, 'Tell everyone why you picked it!') : null);
}

// ------------------------------------------------------------------ awards

function renderAward() {
  const a = state.award;
  if (!a) {
    fill('award-body', el('p', { class: 'center' }, 'And the award goes to…'));
    return;
  }
  const won = a.playerIds.includes(state.me);
  fill('award-body',
    el('p', { class: 'muted center' }, `Award ${a.index} of ${a.total}`),
    el('div', { class: `award-card ${won ? 'won' : ''}` },
      el('p', { class: 'award-title' }, a.title),
      el('p', { class: 'center' }, a.description),
      el('p', { class: 'award-winners' }, whoList(a.playerIds, 'lg')),
      el('p', { class: 'center' }, `+${a.points}${a.playerIds.length > 1 ? ' each' : ''}`)),
    won ? el('p', { class: 'big center' }, `That's you! +${a.points} 🎉`) : null);
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

$('avatar-done').addEventListener('click', () => {
  state.editingAvatar = false;  // client only: the avatar was already saved on each tap
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
