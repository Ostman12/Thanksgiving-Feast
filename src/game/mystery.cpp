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
                                       const std::string& impostorId, bool earlyGuess,
                                       bool caught, bool guessCorrect) {
  // A wrong steal outs the impostor: it scores exactly like being caught with
  // no final guess. A right steal is its own outcome.
  bool stole = earlyGuess && guessCorrect;
  bool impostorCaught = earlyGuess ? !guessCorrect : caught;

  int impostorDelta = 0;
  if (stole) {
    impostorDelta = kStealPoints;
  } else if (!impostorCaught) {
    impostorDelta = kEscapePoints;
  } else if (!earlyGuess && guessCorrect) {
    impostorDelta = kHailMaryPoints;
  }
  int othersDelta = impostorCaught ? kCaughtTeamPoints : 0;

  std::vector<PointsDelta> points;
  for (const std::string& id : roundPlayerIds) {
    points.push_back({id, id == impostorId ? impostorDelta : othersDelta});
  }
  return points;
}

}  // namespace feast
