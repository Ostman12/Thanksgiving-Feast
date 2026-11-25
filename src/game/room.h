#pragma once
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "game/events.h"

namespace feast {

struct Player {
  std::string id;
  std::string name;
  std::string token;  // Secret. Only ever sent to this player (JOINED).
  int score = 0;
  bool connected = true;
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

  // Socket closed. Unknown ids are ignored.
  void disconnect(const std::string& playerId);

  std::string startMatch(const std::string& playerId, std::int64_t nowMs);  // START_MATCH
  std::string next(const std::string& playerId, std::int64_t nowMs);        // NEXT
  std::string endMatch(const std::string& playerId);                        // END_MATCH

  // Ends the current phase if its deadline has passed.
  void tick(std::int64_t nowMs);

  // Hands over the queued events, oldest first, and clears the queue.
  std::vector<Outgoing> takeOutbox();

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
  void reassignHostIfNeeded();
  std::string makeToken();

  void advance(std::int64_t nowMs);
  void startRound(Game game, int round, std::int64_t nowMs);
  void enterPhase(Phase phase, std::int64_t nowMs);
  void finishMatch();
  void returnToLobby();

  void emit(Event event, std::string toPlayerId = "");
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

  std::vector<Outgoing> outbox_;
};

}  // namespace feast
