#include "game/mystery.h"

#include <map>

#include "game/text.h"

namespace feast {

VoteTally tallyVotes(const std::vector<Vote>& votes, const std::string& impostorId) {
  // Key nullopt = Skip, so Skip is counted like any candidate.
  std::map<std::optional<std::string>, int> counts;
  for (const Vote& v : votes) ++counts[v.targetId];

  int best = 0;
  int candidatesAtBest = 0;
  std::optional<std::string> leader;
  for (const auto& [target, count] : counts) {
    if (count > best) {
      best = count;
      candidatesAtBest = 1;
      leader = target;
    } else if (count == best) {
      ++candidatesAtBest;
    }
  }

  VoteTally tally;
  // No votes, a tie for the most, or Skip on top: nobody accused.
  if (best == 0 || candidatesAtBest > 1 || !leader.has_value()) return tally;
  tally.accusedId = leader;
  tally.caught = (*leader == impostorId);
  return tally;
}

bool isCorrectGuess(const std::string& guess, const std::string& mainWord) {
  return equalsIgnoreCase(trimWhitespace(guess), trimWhitespace(mainWord));
}

std::vector<PointsDelta> mysteryPoints(const std::vector<std::string>& roundPlayerIds,
                                       const std::string& impostorId, bool caught,
                                       bool guessCorrect) {
  std::vector<PointsDelta> points;
  for (const std::string& id : roundPlayerIds) {
    int delta = 0;
    if (id == impostorId) {
      if (caught) {
        delta = guessCorrect ? kCaughtGuessPoints : 0;
      } else {
        delta = kEscapePoints + (guessCorrect ? kEscapeGuessBonus : 0);
      }
    } else if (caught) {
      delta = kCaughtTeamPoints;
    }
    points.push_back({id, delta});
  }
  return points;
}

}  // namespace feast
