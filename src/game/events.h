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

struct PointsDelta {
  std::string playerId;
  int delta = 0;
};

struct Vote {
  std::string voterId;
  std::optional<std::string> targetId;  // nullopt = Skip
};

// YOUR_WORD. Always sent to one player only.
struct YourWordEvent {
  std::string word;
  bool isImpostor = false;
};

// SUBMITTED. Ids only, never what anyone chose.
struct SubmittedEvent {
  std::vector<std::string> playerIds;
};

// VOTE_RESULT
struct VoteResultEvent {
  std::vector<Vote> votes;
  std::optional<std::string> accusedId;  // nullopt when nobody is accused
  bool caught = false;
};

// ROUND_RESULT
struct RoundResultEvent {
  std::string impostorId;
  std::string mainWord;
  std::string impostorWord;
  std::optional<std::string> guess;
  std::optional<bool> guessCorrect;  // nullopt when there was no guess
  bool voided = false;               // "void" in JSON
  bool earlyGuess = false;           // the guess was a steal during discussion
  std::vector<PointsDelta> points;
};

// One player's Would You Rather submission. 'A' or 'B'.
struct WyrChoice {
  std::string playerId;
  char choice = 'A';
  char prediction = 'A';
};

// WYR_PROMPT
struct WyrPromptEvent {
  std::string optionA;
  std::string optionB;
};

// WYR_RESULT
struct WyrResultEvent {
  std::vector<WyrChoice> choices;  // Join order.
  int countA = 0;
  int countB = 0;
  std::optional<char> majority;  // nullopt on an even split
  std::vector<PointsDelta> points;
};

using Event = std::variant<RoomStateEvent, MatchStartedEvent, PhaseEvent, MatchOverEvent,
                           YourWordEvent, SubmittedEvent, VoteResultEvent, RoundResultEvent,
                           WyrPromptEvent, WyrResultEvent>;

struct Outgoing {
  std::string toPlayerId;  // Empty means everyone in the room.
  Event event;
};

}  // namespace feast
