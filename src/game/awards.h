#pragma once
// End-of-match awards (rules.md Awards), as pure functions: Room records what
// happened during the match into MatchStats, and computeAwards turns that into
// the list of awards to reveal.

#include <map>
#include <string>
#include <vector>

namespace feast {

constexpr int kAwardPoints = 75;

// Counters for one match, keyed by player id. Room adds to these as rounds end.
struct MatchStats {
  // Mystery
  std::map<std::string, int> impostorVotes;  // votes for the actual impostor (not by them)
  std::map<std::string, int> impostorWins;   // rounds won as the impostor (escaped or stole)
  // Would You Rather
  std::map<std::string, int> majorityPicks;
  std::map<std::string, int> loneWolfPicks;
  // Around the Table, as a predictor
  std::map<std::string, int> correctPredictions;
  std::map<std::string, int> predictionsMade;  // passes don't count
  // Around the Table, as the subject: predictions about them, and how many were right.
  std::map<std::string, int> subjectPredictions;
  std::map<std::string, int> subjectCorrect;
};

struct Award {
  std::string id;           // e.g. "mind_reader"
  std::string title;        // e.g. "🔮 Mind Reader"
  std::string description;  // e.g. "Most correct predictions"
  std::vector<std::string> playerIds;  // Winners, in join order. Ties share.
};

// Awards with at least one winner, in the rules.md table's order. playerIds
// (join order) decides the order of tied winners. Around the Table awards are
// only considered when withAroundTheTable is true.
std::vector<Award> computeAwards(const MatchStats& stats,
                                 const std::vector<std::string>& playerIds,
                                 bool withAroundTheTable);

}  // namespace feast
