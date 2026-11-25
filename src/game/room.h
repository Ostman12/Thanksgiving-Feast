#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace feast {

// Phases from docs/protocol.md. Only Lobby is used so far.
enum class Phase {
  Lobby,
  MysteryDiscuss,
  MysteryVote,
  MysteryGuess,
  MysteryReveal,
  WyrChoose,
  WyrReveal,
  Paused,
  MatchOver,
};

// The protocol string for a phase, e.g. "mystery_vote".
const char* phaseName(Phase phase);

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

// One room's game state. No sockets: the network layer calls these methods
// and turns the results into protocol messages.
class Room {
 public:
  // `seed` drives the random tokens. Tests pass a fixed seed.
  Room(std::string code, bool testMode, std::uint32_t seed);

  // CREATE_ROOM / JOIN_ROOM. The first player to join becomes host.
  JoinResult addPlayer(const std::string& rawName);

  // REJOIN. Marks the player connected again; seat and score are kept.
  JoinResult rejoin(const std::string& token);

  // Socket closed. Unknown ids are ignored.
  void disconnect(const std::string& playerId);

  const std::string& code() const { return code_; }
  const std::string& hostId() const { return hostId_; }
  Phase phase() const { return phase_; }
  bool testMode() const { return testMode_; }
  const std::vector<Player>& players() const { return players_; }  // Join order.
  const Player* findPlayer(const std::string& playerId) const;
  std::size_t connectedCount() const;

 private:
  Player* findPlayerMutable(const std::string& playerId);
  void reassignHostIfNeeded();
  std::string makeToken();

  std::string code_;
  bool testMode_;
  std::mt19937 rng_;
  Phase phase_ = Phase::Lobby;
  std::vector<Player> players_;
  std::string hostId_;
  int nextPlayerNumber_ = 1;
};

}  // namespace feast
