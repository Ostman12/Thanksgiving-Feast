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
    case Phase::AttChoose: return "att_choose";
    case Phase::AttPick: return "att_pick";
    case Phase::AttReveal: return "att_reveal";
    case Phase::Awards: return "awards";
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
    case Game::Att: return "att";
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
    case Phase::AttChoose: return kAttChooseMs;
    case Phase::AttPick: return kAttPickMs;
    default: return std::nullopt;
  }
}

Phase firstPhaseOf(Game game) {
  switch (game) {
    case Game::Mystery: return Phase::MysteryDiscuss;
    case Game::Wyr: return Phase::WyrChoose;
    case Game::Att: return Phase::AttChoose;
    case Game::None: break;
  }
  return Phase::Lobby;
}

}  // namespace

Room::Room(std::string code, bool testMode, std::uint32_t seed, bool withAroundTheTable)
    : code_(std::move(code)),
      testMode_(testMode),
      withAroundTheTable_(withAroundTheTable),
      rng_(seed) {}

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
  player->disconnectedAtMs.reset();
  reassignHostIfNeeded(nowMs);
  result.playerId = player->id;
  result.token = player->token;

  if (phase_ == Phase::Paused && connectedCount() >= minConnectedToPlay()) {
    beginRound(nowMs);  // Resume: start the waiting round.
  } else {
    emitRoomState();
  }
  // The rejoining player missed earlier messages: send what they need to
  // rebuild their screen, to them only, then the current PHASE.
  mysteryCatchUp(result.playerId);
  wyrCatchUp(result.playerId);
  attCatchUp(result.playerId);
  if (phase_ == Phase::Awards && awardIndex_ < awards_.size()) {
    // The award on screen. The points were already added when it was revealed.
    const Award& a = awards_[awardIndex_];
    emit(AwardEvent{static_cast<int>(awardIndex_) + 1, static_cast<int>(awards_.size()), a.id,
                    a.title, a.description, a.playerIds, kAwardPoints},
         result.playerId);
  }
  if (phase_ == Phase::MatchOver && matchOver_) emit(*matchOver_, result.playerId);
  emitPhase(result.playerId);
  return result;
}

void Room::disconnect(const std::string& playerId, std::int64_t nowMs) {
  Player* p = findPlayerMutable(playerId);
  if (p == nullptr || !p->connected) return;
  p->connected = false;
  p->disconnectedAtMs = nowMs;
  reassignHostIfNeeded(nowMs);
  emitRoomState();
  mysteryOnDisconnect(playerId, nowMs);
  wyrOnDisconnect(nowMs);
  attOnDisconnect(nowMs);
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

// rules.md: the creator (first to join) is host whenever connected. A host who
// disconnects keeps it for kReconnectGraceMs; after that it passes to the
// connected player who joined earliest. If nobody is connected, it stays put.
// Returns true if the host changed.
bool Room::reassignHostIfNeeded(std::int64_t nowMs) {
  const std::string before = hostId_;
  if (!players_.empty() && players_.front().connected) {
    hostId_ = players_.front().id;
    return hostId_ != before;
  }
  const Player* host = findPlayer(hostId_);
  if (host != nullptr && (host->connected ||
                          (host->disconnectedAtMs &&
                           nowMs - *host->disconnectedAtMs < kReconnectGraceMs))) {
    return false;
  }
  for (const Player& p : players_) {  // players_ is in join order
    if (p.connected) {
      hostId_ = p.id;
      break;
    }
  }
  return hostId_ != before;
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
  pastSubjects_.clear();
  stats_ = MatchStats{};
  awards_.clear();
  givenAwards_.clear();
  awardIndex_ = 0;
  matchOver_.reset();
  // rules.md Game 3: one round per player in the room when the match starts.
  attTotalRounds_ = withAroundTheTable_ ? static_cast<int>(players_.size()) : 0;
  emit(MatchStartedEvent{kMysteryRounds, kWyrRounds, attTotalRounds_});
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
  // rules.md Awards: ending early skips any awards not yet revealed.
  finishMatch();
  return "";
}

void Room::tick(std::int64_t nowMs) {
  if (reassignHostIfNeeded(nowMs)) emitRoomState();  // A host's 20 s ran out.
  if (mysteryTick(nowMs)) return;  // Grace timers (rules.md Disconnects).
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
      if (mystery_.roundResult && mystery_.roundResult->voided) {
        startRound(Game::Mystery, round_, nowMs);  // Void: play this round again.
      } else if (round_ < kMysteryRounds) {
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
      } else if (withAroundTheTable_ && nextSubjectAvailable()) {
        startRound(Game::Att, 1, nowMs);
      } else {
        startAwards(nowMs);
      }
      break;
    case Phase::AttChoose:
      startAttPick(nowMs);
      break;
    case Phase::AttPick:
      finishAttRound(nowMs);
      break;
    case Phase::AttReveal:
      if (round_ < attTotalRounds_ && nextSubjectAvailable()) {
        startRound(Game::Att, round_ + 1, nowMs);
      } else {
        startAwards(nowMs);
      }
      break;
    case Phase::Awards:
      ++awardIndex_;
      if (awardIndex_ < awards_.size()) {
        revealAward(nowMs);
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

// rules.md: if fewer than 3 players (2 in test mode) are connected when a round would start,
// the match pauses until enough reconnect (see rejoin) or the host ends it.
void Room::startRound(Game game, int round, std::int64_t nowMs) {
  game_ = game;
  round_ = round;
  if (connectedCount() < minConnectedToPlay()) {
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
  if (game_ == Game::Att && !setupAttRound()) {
    startAwards(nowMs);  // Nobody left who can be the subject.
    return;
  }
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
  over.awards = givenAwards_;

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

RoomStateEvent Room::roomStateEvent() const {
  RoomStateEvent state;
  state.code = code_;
  state.hostId = hostId_;
  state.phase = phase_;
  state.testMode = testMode_;
  for (const Player& p : players_) {
    state.players.push_back({p.id, p.name, p.score, p.connected});
  }
  return state;
}

void Room::emitRoomState() { emit(roomStateEvent()); }

std::vector<Event> Room::displaySnapshot() const {
  std::vector<Event> events;
  events.push_back(roomStateEvent());
  switch (phase_) {
    case Phase::MysteryVote:
      events.push_back(SubmittedEvent{voterIds()});
      break;
    case Phase::MysteryGuess:
      if (mystery_.voteResult) events.push_back(*mystery_.voteResult);
      break;
    case Phase::MysteryReveal:
      if (mystery_.voteResult) events.push_back(*mystery_.voteResult);
      if (mystery_.roundResult) events.push_back(*mystery_.roundResult);
      break;
    case Phase::WyrChoose:
      events.push_back(wyr_.prompt);
      events.push_back(SubmittedEvent{chooserIds()});
      break;
    case Phase::WyrReveal:
      events.push_back(wyr_.prompt);
      if (wyr_.result) events.push_back(*wyr_.result);
      break;
    case Phase::AttChoose:
    case Phase::AttPick:
    case Phase::AttReveal:
      attSnapshot(events);
      break;
    case Phase::Awards:
      if (awardIndex_ < awards_.size()) {
        const Award& a = awards_[awardIndex_];
        events.push_back(AwardEvent{static_cast<int>(awardIndex_) + 1,
                                    static_cast<int>(awards_.size()), a.id, a.title,
                                    a.description, a.playerIds, kAwardPoints});
      }
      break;
    case Phase::MatchOver:
      if (matchOver_) events.push_back(*matchOver_);
      break;
    default:
      break;
  }
  events.push_back(PhaseEvent{game_, phase_, round_, deadlineMs_});
  return events;
}

void Room::emitPhase(std::string toPlayerId) {
  emit(PhaseEvent{game_, phase_, round_, deadlineMs_}, std::move(toPlayerId));
}

}  // namespace feast
