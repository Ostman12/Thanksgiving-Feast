#pragma once
#include <cstdint>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "game/events.h"
#include "game/mystery.h"
#include "game/wyr.h"

namespace feast {

struct Player {
  std::string id;
  std::string name;
  std::string token;  // Secret. Only ever sent to this player (JOINED).
  int score = 0;
  bool connected = true;
  std::optional<std::int64_t> disconnectedAtMs;  // Set while disconnected.
};

struct JoinResult {
  std::string error;  // Empty on success, otherwise the ERROR reason.
  std::string playerId;
  std::string token;
  bool ok() const { return error.empty(); }
};

constexpr std::size_t kMaxPlayers = 10;
constexpr std::size_t kMinPlayersToStart = 4;
constexpr std::size_t kMinPlayersToStartTestMode = 3;
constexpr std::size_t kMinConnectedToPlay = 3;  // Fewer at a round start pauses.
constexpr int kMysteryRounds = 3;
constexpr int kWyrRounds = 3;

constexpr std::int64_t kDiscussMs = 100'000;
constexpr std::int64_t kVoteMs = 45'000;
constexpr std::int64_t kGuessMs = 30'000;
constexpr std::int64_t kChooseMs = 30'000;
// rules.md: how long a disconnected impostor has to come back before the
// round is void, how long the vote keeps waiting for anyone who left, and how
// long a disconnected host keeps host.
constexpr std::int64_t kReconnectGraceMs = 20'000;

// One Mystery round, from setup to reveal.
struct MysteryRoundState {
  std::vector<std::string> playerIds;  // Dealt a word at setup ("in the round").
  std::string impostorId;
  std::string mainWord;
  std::string impostorWord;
  std::vector<Vote> votes;  // In the order cast.
  std::optional<VoteResultEvent> voteResult;
  std::optional<std::string> guess;
  std::optional<RoundResultEvent> roundResult;
};

// One Would You Rather prompt, from choose to reveal.
struct WyrRoundState {
  WyrPromptEvent prompt;
  std::vector<WyrChoice> choices;  // In the order submitted.
  std::optional<WyrResultEvent> result;
};

// One room's game state. No sockets: the network layer calls these methods,
// sends what takeOutbox() returns, and calls tick() regularly so deadlines fire.
// Times are Unix epoch milliseconds passed in by the caller, so tests control
// the clock.
//
// Methods that return std::string return "" on success, otherwise the ERROR
// reason (and change nothing).
class Room {
 public:
  // `seed` drives the random tokens. Tests pass a fixed seed.
  Room(std::string code, bool testMode, std::uint32_t seed);

  // CREATE_ROOM / JOIN_ROOM. The first player to join becomes host.
  JoinResult addPlayer(const std::string& rawName);

  // REJOIN. Marks the player connected again; seat and score are kept.
  // May resume a paused match.
  JoinResult rejoin(const std::string& token, std::int64_t nowMs);

  // Socket closed. Unknown ids are ignored. May end the current phase
  // (e.g. the impostor leaving voids the round).
  void disconnect(const std::string& playerId, std::int64_t nowMs);

  std::string startMatch(const std::string& playerId, std::int64_t nowMs);  // START_MATCH
  std::string next(const std::string& playerId, std::int64_t nowMs);        // NEXT
  std::string endMatch(const std::string& playerId);                        // END_MATCH

  // CAST_VOTE. targetId nullopt = Skip.
  std::string castVote(const std::string& playerId, const std::optional<std::string>& targetId,
                       std::int64_t nowMs);
  // GUESS_WORD
  std::string guessWord(const std::string& playerId, const std::string& rawWord,
                        std::int64_t nowMs);
  // SUBMIT_CHOICE. choice and prediction must each be exactly "A" or "B".
  std::string submitChoice(const std::string& playerId, const std::string& choice,
                           const std::string& prediction, std::int64_t nowMs);

  // Ends the current phase if its deadline has passed.
  void tick(std::int64_t nowMs);

  // Hands over the queued events, oldest first, and clears the queue.
  std::vector<Outgoing> takeOutbox();

  // WATCH_ROOM catch-up for a display: ROOM_STATE, the room-wide messages
  // needed to rebuild the current screen, then PHASE. Never anything private.
  std::vector<Event> displaySnapshot() const;

  const std::string& code() const { return code_; }
  const std::string& hostId() const { return hostId_; }
  Phase phase() const { return phase_; }
  Game game() const { return game_; }
  int round() const { return round_; }  // While paused: the round waiting to start.
  std::optional<std::int64_t> deadlineMs() const { return deadlineMs_; }
  bool testMode() const { return testMode_; }
  const std::vector<Player>& players() const { return players_; }  // Join order.
  const Player* findPlayer(const std::string& playerId) const;
  std::size_t connectedCount() const;

 private:
  Player* findPlayerMutable(const std::string& playerId);
  bool reassignHostIfNeeded(std::int64_t nowMs);
  std::string makeToken();

  // Phase engine (room.cpp).
  void advance(std::int64_t nowMs);
  void startRound(Game game, int round, std::int64_t nowMs);
  void beginRound(std::int64_t nowMs);
  void enterPhase(Phase phase, std::int64_t nowMs);
  void finishMatch();
  void returnToLobby();

  // Mystery (room_mystery.cpp).
  void setupMysteryRound();
  void finishVote(std::int64_t nowMs);
  void finishMysteryRound(bool voided, std::int64_t nowMs);
  void mysteryOnDisconnect(const std::string& playerId, std::int64_t nowMs);
  bool mysteryTick(std::int64_t nowMs);
  void mysteryCatchUp(const std::string& playerId);
  bool inMysteryRound(const std::string& playerId) const;
  bool allDueVoted(std::int64_t nowMs) const;
  std::vector<std::string> voterIds() const;

  // Would You Rather (room_wyr.cpp).
  void setupWyrRound();
  void finishWyrRound(std::int64_t nowMs);
  void wyrOnDisconnect(std::int64_t nowMs);
  void wyrCatchUp(const std::string& playerId);
  bool allConnectedChose() const;
  std::vector<std::string> chooserIds() const;

  void emit(Event event, std::string toPlayerId = "");
  RoomStateEvent roomStateEvent() const;
  void emitRoomState();
  void emitPhase(std::string toPlayerId = "");

  std::string code_;
  bool testMode_;
  std::mt19937 rng_;
  std::vector<Player> players_;
  std::string hostId_;
  int nextPlayerNumber_ = 1;

  Phase phase_ = Phase::Lobby;
  Game game_ = Game::None;
  int round_ = 0;
  std::optional<std::int64_t> deadlineMs_;

  MysteryRoundState mystery_;
  std::vector<std::string> pastImpostors_;  // This match.
  std::set<std::size_t> usedWordPairs_;     // This room; cleared when all are used.
  WyrRoundState wyr_;
  std::set<std::size_t> usedPrompts_;       // This room; cleared when all are used.
  std::optional<MatchOverEvent> matchOver_;  // Kept for rejoin catch-up.

  std::vector<Outgoing> outbox_;
};

}  // namespace feast
