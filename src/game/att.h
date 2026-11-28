#pragma once
// Around the Table scoring as a pure function, from docs/rules.md (Game 3).

#include <optional>
#include <string>
#include <vector>

#include "game/events.h"

namespace feast {

constexpr int kAttCorrectPoints = 100;       // predicted the subject's answer
constexpr int kAttPerfectBonus = 150;        // more each, on a perfect round
constexpr int kAttPerfectMinPredictors = 2;  // a perfect round needs this many predictions
constexpr int kAttSkipsPerTurn = 2;

struct AttScore {
  std::vector<PointsDelta> points;  // One per id in playerIds, in order, 0s included.
  bool perfect = false;
};

// answer: the subject's real pick, nullopt if they didn't answer (nobody scores).
// predictions: everyone else's picks; option nullopt = Pass (scores 0).
// Perfect round: at least 2 actual predictions and every one of them right.
AttScore attPoints(const std::vector<std::string>& playerIds, const std::string& subjectId,
                   std::optional<char> answer, const std::vector<AttPrediction>& predictions);

}  // namespace feast
