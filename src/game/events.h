#pragma once
// Events the Room queues for the network layer to send. Each one maps to one
// server -> client message in docs/protocol.md. The network layer only turns
// them into JSON; deciding who gets what is game logic and lives in Room.

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "game/avatar.h"

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
  AttChoose,
  AttPick,
  AttReveal,
  Awards,
  Paused,
  MatchOver,
};

// Att = Around the Table (rules.md Game 3).
enum class Game { None, Mystery, Wyr, Att };

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
  Avatar avatar;
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
  int attRounds = 0;  // 0 when Around the Table is turned off.
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

// One end-of-match award (rules.md Awards), as listed in MATCH_OVER.
struct AwardSummary {
  std::string id;
  std::string title;
  std::vector<std::string> playerIds;
};

// MATCH_OVER
struct MatchOverEvent {
  std::vector<std::string> winnerIds;
  std::vector<LeaderboardEntry> leaderboard;  // High to low.
  std::vector<AwardSummary> awards;           // Every award given this match.
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

// One player's Would You Rather pick. 'A' or 'B'.
struct WyrChoice {
  std::string playerId;
  char choice = 'A';
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

// ATT_SUBJECT: whose turn it is, at the start of att_choose.
struct AttSubjectEvent {
  std::string subjectId;
  int round = 0;
  int total = 0;
};

// ATT_PREVIEW. Always sent to the subject only: questions they skip must never
// reach anyone else.
struct AttPreviewEvent {
  std::string question;
  std::vector<std::string> options;  // 4, for A to D
  int skipsLeft = 0;
};

// ATT_QUESTION: the question the subject chose, to everyone.
struct AttQuestionEvent {
  std::string subjectId;
  std::string question;
  std::vector<std::string> options;
};

// One player's prediction. option nullopt = Pass.
struct AttPrediction {
  std::string playerId;
  std::optional<char> option;
};

// ATT_RESULT
struct AttResultEvent {
  std::string subjectId;
  std::optional<char> answer;  // nullopt if the subject didn't answer
  std::vector<AttPrediction> predictions;  // Join order; players who didn't pick left out.
  bool perfect = false;
  std::vector<PointsDelta> points;
};

// AWARD: one award being revealed (rules.md Awards).
struct AwardEvent {
  int index = 0;  // 1-based
  int total = 0;
  std::string id;
  std::string title;
  std::string description;
  std::vector<std::string> playerIds;
  int points = 0;
};

using Event = std::variant<RoomStateEvent, MatchStartedEvent, PhaseEvent, MatchOverEvent,
                           YourWordEvent, SubmittedEvent, VoteResultEvent, RoundResultEvent,
                           WyrPromptEvent, WyrResultEvent, AttSubjectEvent, AttPreviewEvent,
                           AttQuestionEvent, AttResultEvent, AwardEvent>;

struct Outgoing {
  std::string toPlayerId;  // Empty means everyone in the room.
  Event event;
};

}  // namespace feast
