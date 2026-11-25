#pragma once
// Events the Room queues for the network layer to send. Each one maps to one
// server -> client message in docs/protocol.md. The network layer only turns
// them into JSON; deciding who gets what is game logic and lives in Room.

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace feast {

// Phases from docs/protocol.md.
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

enum class Game { None, Mystery, Wyr };

// Protocol strings, e.g. "mystery_vote". gameName(Game::None) is "", which the
// network layer sends as null.
const char* phaseName(Phase phase);
const char* gameName(Game game);

// A player as everyone in the room may see them. Deliberately has no token.
struct PublicPlayer {
  std::string id;
  std::string name;
  int score = 0;
  bool connected = true;
};

// ROOM_STATE
struct RoomStateEvent {
  std::string code;
  std::string hostId;
  Phase phase = Phase::Lobby;
  bool testMode = false;
  std::vector<PublicPlayer> players;
};

// MATCH_STARTED
struct MatchStartedEvent {
  int mysteryRounds = 0;
  int wyrRounds = 0;
};

// PHASE. round is 1-based, 0 when there is no round (lobby, match_over).
struct PhaseEvent {
  Game game = Game::None;
  Phase phase = Phase::Lobby;
  int round = 0;
  std::optional<std::int64_t> deadlineMs;  // nullopt -> null
};

struct LeaderboardEntry {
  std::string playerId;
  std::string name;
  int score = 0;
};

// MATCH_OVER
struct MatchOverEvent {
  std::vector<std::string> winnerIds;
  std::vector<LeaderboardEntry> leaderboard;  // High to low.
};

using Event = std::variant<RoomStateEvent, MatchStartedEvent, PhaseEvent, MatchOverEvent>;

struct Outgoing {
  std::string toPlayerId;  // Empty means everyone in the room.
  Event event;
};

}  // namespace feast
