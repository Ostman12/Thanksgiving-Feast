#pragma once
// Mystery rules as pure functions (no Room, no randomness), from docs/rules.md.

#include <optional>
#include <string>
#include <vector>

#include "game/events.h"

namespace feast {

// rules.md, Mystery scoring.
constexpr int kStealPoints = 300;        // impostor: right guess during discussion
constexpr int kEscapePoints = 200;       // impostor escapes the vote
constexpr int kHailMaryPoints = 100;     // impostor: caught, then final guess right
constexpr int kCaughtTeamPoints = 100;   // each non-impostor when caught (or outed by a wrong steal)
constexpr std::size_t kMaxGuessLength = 30;

struct VoteTally {
  std::optional<std::string> accusedId;  // The one player with the most votes, if any.
  bool caught = false;                   // accusedId is the impostor.
};

// Skip counts like a candidate. Nobody is accused if Skip has or ties for the
// most votes, two or more players tie for the most, or nobody voted.
VoteTally tallyVotes(const std::vector<Vote>& votes, const std::string& impostorId);

// Matches after trimming and ignoring case.
bool isCorrectGuess(const std::string& guess, const std::string& mainWord);

// Points for a finished (not void) round. One entry per round player, in the
// order given, including 0 deltas.
//   earlyGuess:   the impostor guessed during discussion (a steal attempt)
//   caught:       the vote caught the impostor (ignored for a steal attempt)
//   guessCorrect: the impostor's guess (steal or final) was right
std::vector<PointsDelta> mysteryPoints(const std::vector<std::string>& roundPlayerIds,
                                       const std::string& impostorId, bool earlyGuess,
                                       bool caught, bool guessCorrect);

}  // namespace feast
