#include "game/room.h"

#include <algorithm>
#include <utility>

#include "game/text.h"

namespace feast {

const char* phaseName(Phase phase) {
  switch (phase) {
    case Phase::Lobby: return "lobby";
    case Phase::MysteryDiscuss: return "mystery_discuss";
    case Phase::MysteryVote: return "mystery_vote";
    case Phase::MysteryGuess: return "mystery_guess";
    case Phase::MysteryReveal: return "mystery_reveal";
    case Phase::WyrChoose: return "wyr_choose";
    case Phase::WyrReveal: return "wyr_reveal";
    case Phase::Paused: return "paused";
    case Phase::MatchOver: return "match_over";
  }
  return "unknown";
}

const char* gameName(Game game) {
  switch (game) {
    case Game::None: return "";
    case Game::Mystery: return "mystery";
    case Game::Wyr: return "wyr";
  }
  return "";
}

namespace {

// How long a phase lasts, or nullopt if it waits for the host.
std::optional<std::int64_t> phaseDuration(Phase phase) {
  switch (phase) {
    case Phase::MysteryDiscuss: return kDiscussMs;
    case Phase::MysteryVote: return kVoteMs;
    case Phase::MysteryGuess: return kGuessMs;
    case Phase::WyrChoose: return kChooseMs;
    default: return std::nullopt;
  }
}

Phase firstPhaseOf(Game game) {
  return game == Game::Mystery ? Phase::MysteryDiscuss : Phase::WyrChoose;
}

}  // namespace

Room::Room(std::string code, bool testMode, std::uint32_t seed)
    : code_(std::move(code)), testMode_(testMode), rng_(seed) {}

// ---------------------------------------------------------------- players

JoinResult Room::addPlayer(const std::string& rawName) {
  JoinResult result;
  std::string name = trimWhitespace(rawName);

  if (!isValidName(name)) {
    result.error = "invalid name";
    return result;
  }
  if (phase_ != Phase::Lobby) {
    result.error = "match already started";
    return result;
  }
  // Disconnected players keep their seat, so they count toward the limit.
  if (players_.size() >= kMaxPlayers) {
    result.error = "room is full";
    return result;
  }
  for (const Player& p : players_) {
    if (equalsIgnoreCase(p.name, name)) {
      result.error = "name taken";
      return result;
    }
  }

  Player player;
  player.id = "p" + std::to_string(nextPlayerNumber_++);
  player.name = name;
  player.token = makeToken();
  players_.push_back(player);

  if (hostId_.empty()) hostId_ = player.id;
  emitRoomState();

  result.playerId = player.id;
  result.token = player.token;
  return result;
}

JoinResult Room::rejoin(const std::string& token, std::int64_t nowMs) {
  JoinResult result;
  Player* player = nullptr;
  for (Player& p : players_) {
    if (!token.empty() && p.token == token) player = &p;
  }
  if (player == nullptr) {
    result.error = "invalid rejoin token";
    return result;
  }

  player->connected = true;
  reassignHostIfNeeded();
  result.playerId = player->id;
  result.token = player->token;

  if (phase_ == Phase::Paused && connectedCount() >= kMinConnectedToPlay) {
    beginRound(nowMs);  // Resume: start the waiting round.
  } else {
    emitRoomState();
  }
  // The rejoining player missed earlier messages: send what they need to
  // rebuild their screen, to them only, then the current PHASE.
  mysteryCatchUp(result.playerId);
  wyrCatchUp(result.playerId);
  if (phase_ == Phase::MatchOver && matchOver_) emit(*matchOver_, result.playerId);
  emitPhase(result.playerId);
  return result;
}

void Room::disconnect(const std::string& playerId, std::int64_t nowMs) {
  Player* p = findPlayerMutable(playerId);
  if (p == nullptr) return;
  p->connected = false;
  reassignHostIfNeeded();
  emitRoomState();
  mysteryOnDisconnect(playerId, nowMs);
  wyrOnDisconnect(nowMs);
}

const Player* Room::findPlayer(const std::string& playerId) const {
  for (const Player& p : players_) {
    if (p.id == playerId) return &p;
  }
  return nullptr;
}

std::size_t Room::connectedCount() const {
  std::size_t count = 0;
  for (const Player& p : players_) {
    if (p.connected) ++count;
  }
  return count;
}

Player* Room::findPlayerMutable(const std::string& playerId) {
  for (Player& p : players_) {
    if (p.id == playerId) return &p;
  }
  return nullptr;
}

// rules.md: if the host disconnects, host passes to the connected player who
// joined earliest. If nobody is connected, the host stays as is until someone
// comes back (then that player becomes host).
void Room::reassignHostIfNeeded() {
  const Player* host = findPlayer(hostId_);
  if (host != nullptr && host->connected) return;
  for (const Player& p : players_) {  // players_ is in join order
    if (p.connected) {
      hostId_ = p.id;
      return;
    }
  }
}

std::string Room::makeToken() {
  static const char kHex[] = "0123456789abcdef";
  std::uniform_int_distribution<int> pick(0, 15);
  std::string token;
  for (int i = 0; i < 32; ++i) token += kHex[pick(rng_)];
  return token;
}

// ---------------------------------------------------------------- host actions

std::string Room::startMatch(const std::string& playerId, std::int64_t nowMs) {
  if (phase_ != Phase::Lobby) return "match already started";
  if (playerId != hostId_) return "not the host";
  std::size_t needed = testMode_ ? kMinPlayersToStartTestMode : kMinPlayersToStart;
  if (connectedCount() < needed) return "not enough players";

  pastImpostors_.clear();
  matchOver_.reset();
  emit(MatchStartedEvent{kMysteryRounds, kWyrRounds});
  startRound(Game::Mystery, 1, nowMs);
  return "";
}

std::string Room::next(const std::string& playerId, std::int64_t nowMs) {
  if (playerId != hostId_) return "not the host";
  if (phase_ == Phase::Lobby || phase_ == Phase::Paused) return "wrong phase";
  advance(nowMs);
  return "";
}

std::string Room::endMatch(const std::string& playerId) {
  if (playerId != hostId_) return "not the host";
  if (phase_ == Phase::Lobby || phase_ == Phase::MatchOver) return "wrong phase";
  finishMatch();
  return "";
}

void Room::tick(std::int64_t nowMs) {
  if (deadlineMs_ && nowMs >= *deadlineMs_) advance(nowMs);
}

// ---------------------------------------------------------------- phase engine

// Ends the current phase and moves to the next one. Called by the deadline
// (tick), the host (next), or when everyone due an action has acted.
void Room::advance(std::int64_t nowMs) {
  switch (phase_) {
    case Phase::MysteryDiscuss:
      enterPhase(Phase::MysteryVote, nowMs);
      break;
    case Phase::MysteryVote:
      finishVote(nowMs);
      break;
    case Phase::MysteryGuess:
      finishMysteryRound(false, nowMs);
      break;
    case Phase::MysteryReveal:
      if (round_ < kMysteryRounds) {
        startRound(Game::Mystery, round_ + 1, nowMs);
      } else {
        startRound(Game::Wyr, 1, nowMs);
      }
      break;
    case Phase::WyrChoose:
      finishWyrRound(nowMs);
      break;
    case Phase::WyrReveal:
      if (round_ < kWyrRounds) {
        startRound(Game::Wyr, round_ + 1, nowMs);
      } else {
        finishMatch();
      }
      break;
    case Phase::MatchOver:
      returnToLobby();
      break;
    case Phase::Lobby:
    case Phase::Paused:
      break;  // Nothing to end; callers reject these.
  }
}

// rules.md: if fewer than 3 players are connected when a round would start,
// the match pauses until enough reconnect (see rejoin) or the host ends it.
void Room::startRound(Game game, int round, std::int64_t nowMs) {
  game_ = game;
  round_ = round;
  if (connectedCount() < kMinConnectedToPlay) {
    phase_ = Phase::Paused;
    deadlineMs_.reset();
    emitRoomState();
    emitPhase();
    return;
  }
  beginRound(nowMs);
}

// Sets up the content for game_/round_ (words, prompt) and enters its first
// phase. Used by startRound and when a paused match resumes.
void Room::beginRound(std::int64_t nowMs) {
  if (game_ == Game::Mystery) setupMysteryRound();
  if (game_ == Game::Wyr) setupWyrRound();
  enterPhase(firstPhaseOf(game_), nowMs);
}

void Room::enterPhase(Phase phase, std::int64_t nowMs) {
  phase_ = phase;
  std::optional<std::int64_t> duration = phaseDuration(phase);
  if (duration) {
    deadlineMs_ = nowMs + *duration;
  } else {
    deadlineMs_.reset();
  }
  emitRoomState();
  emitPhase();
}

void Room::finishMatch() {
  phase_ = Phase::MatchOver;
  game_ = Game::None;
  round_ = 0;
  deadlineMs_.reset();

  MatchOverEvent over;
  for (const Player& p : players_) over.leaderboard.push_back({p.id, p.name, p.score});
  // stable_sort keeps join order among equal scores.
  std::stable_sort(over.leaderboard.begin(), over.leaderboard.end(),
                   [](const LeaderboardEntry& a, const LeaderboardEntry& b) {
                     return a.score > b.score;
                   });
  for (const LeaderboardEntry& e : over.leaderboard) {
    if (e.score == over.leaderboard.front().score) over.winnerIds.push_back(e.playerId);
  }

  matchOver_ = over;
  emit(std::move(over));
  emitRoomState();
  emitPhase();
}

void Room::returnToLobby() {
  for (Player& p : players_) p.score = 0;
  matchOver_.reset();
  phase_ = Phase::Lobby;
  game_ = Game::None;
  round_ = 0;
  deadlineMs_.reset();
  emitRoomState();
  emitPhase();
}

// ---------------------------------------------------------------- outbox

std::vector<Outgoing> Room::takeOutbox() {
  std::vector<Outgoing> out;
  out.swap(outbox_);
  return out;
}

void Room::emit(Event event, std::string toPlayerId) {
  outbox_.push_back({std::move(toPlayerId), std::move(event)});
}

void Room::emitRoomState() {
  RoomStateEvent state;
  state.code = code_;
  state.hostId = hostId_;
  state.phase = phase_;
  state.testMode = testMode_;
  for (const Player& p : players_) {
    state.players.push_back({p.id, p.name, p.score, p.connected});
  }
  emit(std::move(state));
}

void Room::emitPhase(std::string toPlayerId) {
  emit(PhaseEvent{game_, phase_, round_, deadlineMs_}, std::move(toPlayerId));
}

}  // namespace feast
