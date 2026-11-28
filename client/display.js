'use strict';
// Display page (laptop / TV). Watch-only: it sends WATCH_ROOM and nothing
// else, and only ever receives room-wide messages, so it holds no secrets.

const { el, fill } = Feast;
const $ = (id) => document.getElementById(id);

const SAVED_CODE = 'feast.display.code';

const state = {
  connected: false,
  watching: false,   // WATCH_ROOM accepted (first ROOM_STATE arrived)
  asking: false,     // sent WATCH_ROOM, waiting
  room: null,
  phase: null,
  submitted: [],
  voteResult: null,
  roundResult: null,
  prompt: null,
  wyrResult: null,
  attSubject: null,   // ATT_SUBJECT: whose turn (never the private preview)
  attQuestion: null,
  attResult: null,
  award: null,
  matchOver: null,
};

function watch(code) {
  if (socket.send('WATCH_ROOM', { code })) state.asking = true;
  render();
}

const socket = Feast.connect({
  onOpen() {
    state.watching = false;
    const code = localStorage.getItem(SAVED_CODE);
    if (code) watch(code);
    render();
  },
  onMessage: handle,
  onStatus(ok) {
    state.connected = ok;
    render();
  },
});

function handle({ type, payload }) {
  switch (type) {
    case 'ERROR':
      if (state.asking) {
        localStorage.removeItem(SAVED_CODE);
        state.asking = false;
        state.watching = false;
        state.room = null;
      }
      Feast.toast(payload.reason);
      break;
    case 'ROOM_STATE':
      state.room = payload;
      if (state.asking) {
        state.asking = false;
        state.watching = true;
        localStorage.setItem(SAVED_CODE, payload.code);
      }
      break;
    case 'PHASE':
      state.phase = payload;
      if (payload.phase === 'mystery_discuss' || payload.phase === 'lobby') {
        state.submitted = [];
        state.voteResult = null;
        state.roundResult = null;
      }
      if (payload.phase === 'lobby') state.matchOver = null;
      break;
    case 'MATCH_STARTED':
      state.matchOver = null;
      state.award = null;
      break;
    case 'SUBMITTED':
      state.submitted = payload.playerIds;
      break;
    case 'VOTE_RESULT':
      state.voteResult = payload;
      break;
    case 'ROUND_RESULT':
      state.roundResult = payload;
      break;
    case 'WYR_PROMPT':
      state.prompt = payload;
      state.submitted = [];
      state.wyrResult = null;
      break;
    case 'WYR_RESULT':
      state.wyrResult = payload;
      break;
    case 'ATT_SUBJECT':
      state.attSubject = payload;
      state.attQuestion = null;
      state.attResult = null;
      state.submitted = [];
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

function currentPhase() {
  return state.room ? state.room.phase : null;
}

function currentDeadline() {
  return state.phase && state.phase.phase === currentPhase() ? state.phase.deadline : null;
}

// ------------------------------------------------------------------ render

function render() {
  $('banner').hidden = state.connected || !state.watching;

  let screen;
  if (!state.watching) screen = state.asking ? 'connecting' : 'watch';
  else if (currentPhase() === 'lobby') screen = 'lobby';
  else if (currentPhase() === 'match_over') screen = 'match_over';
  else screen = 'game';
  Feast.showScreen(screen);

  const inGame = screen === 'game' && state.phase;
  $('round-bar').hidden = !inGame;
  if (inGame) {
    let label;
    if (currentPhase() === 'awards') {
      label = `🏆 Awards · ${state.phase.round} of ${state.award ? state.award.total : '?'}`;
    } else if (state.phase.game === 'att') {
      label = `Around the Table · Round ${state.phase.round} of ${state.attSubject ? state.attSubject.total : '?'}`;
    } else {
      const game = state.phase.game === 'wyr' ? 'Would You Rather Extreme' : 'Mystery';
      label = `${game} · Round ${state.phase.round} of 3`;
    }
    $('round-label').textContent = `${label} · Room ${state.room.code}`;
  }

  if (screen === 'watch') $('watch-btn').disabled = !state.connected;
  if (screen === 'lobby') renderLobby();
  if (screen === 'game') renderGame();
  if (screen === 'match_over') renderMatchOver();
  tickCountdown();
}

function tickCountdown() {
  const left = Feast.secondsLeft(currentDeadline());
  $('countdown').textContent = left === null ? '' : `${left}`;
  $('countdown').classList.toggle('urgent', left !== null && left <= 10);
}
setInterval(tickCountdown, 250);

function renderLobby() {
  // Open /display through the tunnel URL: this shows its own address.
  $('join-url').textContent = location.host;
  $('lobby-code').textContent = state.room.code;
  const needed = state.room.testMode ? 2 : 4;
  fill('lobby-players',
    players().map((p) => el('li', { class: p.connected ? '' : 'away' },
      p.id === state.room.hostId ? '👑 ' : '', p.name)),
    players().length < needed
      ? el('li', { class: 'muted' }, `Waiting for ${needed - players().length} more…`)
      : null);
}

function progressList(verb) {
  return [
    el('p', { class: 'big center' }, `${state.submitted.length} of ${players().length} ${verb}`),
    el('ul', { class: 'players grid' }, players().map((p) =>
      el('li', { class: state.submitted.includes(p.id) ? 'done' : (p.connected ? '' : 'away') },
        state.submitted.includes(p.id) ? '✅ ' : '⏳ ', p.name))),
  ];
}

function voteSummary(result) {
  if (!result) return null;
  if (result.caught) return el('p', { class: 'huge-text center' }, `🚨 ${nameOf(result.accusedId)} was caught!`);
  if (result.accusedId) return el('p', { class: 'huge-text center' }, `${nameOf(result.accusedId)} was voted out…`);
  return el('p', { class: 'huge-text center' }, 'Nobody was voted out.');
}

function pointsList(points) {
  const scored = (points || []).filter((p) => p.delta !== 0);
  if (!scored.length) return el('p', { class: 'center muted' }, 'No points this round.');
  return el('ul', { class: 'players grid' }, scored.map((p) =>
    el('li', { class: 'done' }, `${nameOf(p.playerId)} ${Feast.signed(p.delta)}`)));
}

function renderGame() {
  switch (currentPhase()) {
    case 'mystery_discuss':
      return fill('game-body',
        el('p', { class: 'huge-text center' }, '🗣️ Say one clue each'),
        el('p', { class: 'big center' }, 'Then discuss: who is the impostor?'));
    case 'mystery_vote':
      return fill('game-body', el('p', { class: 'huge-text center' }, '🕵️ Vote on your phones'), progressList('voted'));
    case 'mystery_guess':
      return fill('game-body', voteSummary(state.voteResult),
        el('p', { class: 'big center' }, '🙏 Hail mary: the impostor gets one last guess at the word…'));
    case 'mystery_reveal':
      return renderMysteryReveal();
    case 'wyr_choose': {
      const p = state.prompt || { optionA: '…', optionB: '…' };
      return fill('game-body',
        el('p', { class: 'big center' }, 'Would you rather…'),
        el('div', { class: 'ab big-ab' },
          el('div', { class: 'option-card' }, el('span', { class: 'tag' }, 'A'), p.optionA),
          el('div', { class: 'option-card' }, el('span', { class: 'tag' }, 'B'), p.optionB)),
        progressList('answered'));
    }
    case 'wyr_reveal':
      return renderWyrReveal();
    case 'att_choose':
      return fill('game-body',
        el('p', { class: 'huge-emoji center' }, '🎤'),
        el('p', { class: 'huge-text center' }, `${subjectName()}'s turn!`),
        el('p', { class: 'big center' }, `${subjectName()} is picking a question…`),
        el('p', { class: 'center muted' }, 'Everyone else: get ready to guess their answer.'));
    case 'att_pick':
      return renderAttPick();
    case 'att_reveal':
      return renderAttReveal();
    case 'awards':
      return renderAward();
    case 'paused':
      return fill('game-body', el('p', { class: 'huge-text center' }, '⏸️ Paused'),
        el('p', { class: 'big center' }, `Waiting for at least ${state.room.testMode ? 2 : 3} players to reconnect.`));
    default:
      return fill('game-body');
  }
}

function renderMysteryReveal() {
  const r = state.roundResult;
  if (!r) return fill('game-body', el('p', { class: 'big center' }, 'Revealing…'));
  if (r.void) {
    return fill('game-body',
      el('p', { class: 'huge-text center' }, '🚫 Round void'),
      el('p', { class: 'big center' }, `The impostor (${nameOf(r.impostorId)}) left. This round will be replayed.`),
      el('p', { class: 'big center' }, `Word: ${r.mainWord} · Impostor had: ${r.impostorWord}`));
  }
  const votes = r.earlyGuess || !state.voteResult ? [] : state.voteResult.votes;
  let guessLine;
  if (r.earlyGuess) {
    guessLine = r.guessCorrect
      ? `🎯 ${nameOf(r.impostorId)} guessed “${r.guess}” mid-discussion and STOLE the round!`
      : `😬 ${nameOf(r.impostorId)} guessed “${r.guess}”, wrong, and outed themselves!`;
  } else {
    guessLine = r.guess === null ? '' : `Hail mary: “${r.guess}” ${r.guessCorrect ? '✅' : '❌'}`;
  }
  fill('game-body',
    el('p', { class: 'huge-text center' }, `The impostor was ${nameOf(r.impostorId)} 🕵️`),
    el('p', { class: 'big center' }, 'Word: ', el('b', {}, r.mainWord), ' · Impostor had: ', el('b', {}, r.impostorWord)),
    guessLine ? el('p', { class: 'big center' }, guessLine) : null,
    el('ul', { class: 'players grid' }, votes.map((v) =>
      el('li', {}, `${nameOf(v.voterId)} → ${v.targetId ? nameOf(v.targetId) : 'Skip'}`))),
    pointsList(r.points));
}

function renderWyrReveal() {
  const r = state.wyrResult;
  const p = state.prompt;
  if (!r || !p) return fill('game-body', el('p', { class: 'big center' }, 'Revealing…'));
  const who = (option) => r.choices.filter((c) => c.choice === option).map((c) => nameOf(c.playerId)).join(', ');
  fill('game-body',
    el('div', { class: 'ab big-ab' },
      el('div', { class: `option-card ${r.majority === 'A' ? 'winner' : ''}` },
        el('span', { class: 'tag' }, `A · ${r.countA}`), p.optionA, el('small', {}, who('A'))),
      el('div', { class: `option-card ${r.majority === 'B' ? 'winner' : ''}` },
        el('span', { class: 'tag' }, `B · ${r.countB}`), p.optionB, el('small', {}, who('B')))),
    el('p', { class: 'big center' }, r.majority ? `Most picked ${r.majority}!` : 'Even split!'),
    pointsList(r.points));
}

function renderMatchOver() {
  const m = state.matchOver;
  if (!m) return fill('over-body', el('p', { class: 'big center' }, 'Final scores…'));
  fill('over-body',
    el('p', { class: 'huge-text center' }, '🏆 ', m.winnerIds.map(nameOf).join(' & '),
      m.winnerIds.length > 1 ? ' — co-champions!' : ' — champion!'),
    el('ol', { class: 'leaderboard big' }, m.leaderboard.map((e) =>
      el('li', { class: m.winnerIds.includes(e.playerId) ? 'me' : '' }, `${e.name} — ${e.score}`))),
    (m.awards || []).length ? el('ul', { class: 'players grid awards-list' }, m.awards.map((a) =>
      el('li', {}, el('b', {}, a.title), el('br'), a.playerIds.map(nameOf).join(' & ')))) : null);
}

// ------------------------------------------------------------------ Around the Table

const LETTERS = ['A', 'B', 'C', 'D'];

function subjectName() {
  return state.attSubject ? nameOf(state.attSubject.subjectId) : '?';
}

function renderAttPick() {
  const q = state.attQuestion;
  if (!q) return fill('game-body', el('p', { class: 'big center' }, 'Loading…'));
  fill('game-body',
    el('p', { class: 'big center' }, `🎤 ${nameOf(q.subjectId)}:`),
    el('p', { class: 'huge-text center' }, q.question),
    el('div', { class: 'att-grid' }, LETTERS.map((l, i) =>
      el('div', { class: 'option-card' }, el('span', { class: 'tag' }, l), q.options[i]))),
    progressList('locked in'));
}

function renderAttReveal() {
  const r = state.attResult;
  const q = state.attQuestion;
  if (!r) return fill('game-body', el('p', { class: 'big center' }, 'Revealing…'));
  const name = nameOf(r.subjectId);
  const who = (letter) => r.predictions.filter((p) => p.option === letter).map((p) => nameOf(p.playerId));
  fill('game-body',
    q ? el('p', { class: 'big center' }, q.question) : null,
    r.answer
      ? el('p', { class: 'huge-text center' }, `${name} picked ${r.answer}!`)
      : el('p', { class: 'huge-text center' }, `${name} didn't answer in time.`),
    r.perfect ? el('p', { class: 'big center perfect' }, '🎯 Perfect round! Everyone knew!') : null,
    q ? el('div', { class: 'att-grid' }, LETTERS.map((l, i) =>
      el('div', { class: `option-card ${l === r.answer ? 'winner' : ''}` },
        el('span', { class: 'tag' }, l), q.options[i],
        el('small', {}, who(l).join(', '))))) : null,
    pointsList(r.points));
}

// ------------------------------------------------------------------ awards

function renderAward() {
  const a = state.award;
  if (!a) return fill('game-body', el('p', { class: 'huge-text center' }, 'And the award goes to…'));
  fill('game-body',
    el('p', { class: 'big center muted' }, `Award ${a.index} of ${a.total}`),
    el('div', { class: 'award-card' },
      el('p', { class: 'award-title' }, a.title),
      el('p', { class: 'big center' }, a.description),
      el('p', { class: 'award-winners' }, a.playerIds.map(nameOf).join(' & ')),
      el('p', { class: 'big center' }, `+${a.points}${a.playerIds.length > 1 ? ' each' : ''}`)));
}

// ------------------------------------------------------------------ taps

$('code-input').addEventListener('input', (e) => {
  e.target.value = e.target.value.toUpperCase().replace(/[^A-Z]/g, '');
});
$('watch-btn').addEventListener('click', () => {
  const code = $('code-input').value.trim();
  if (code.length !== 4) {
    Feast.toast('Room codes are 4 letters');
    return;
  }
  Feast.requestWakeLock();
  watch(code);
});

render();
