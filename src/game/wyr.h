#pragma once
// Would You Rather rules as pure functions, from docs/rules.md.

#include <optional>
#include <string>
#include <vector>

#include "game/events.h"

namespace feast {

constexpr int kPredictionPoints = 100;  // prediction matches the majority
constexpr int kLoneWolfPoints = 50;     // only one to pick your option
constexpr int kLoneWolfMinChoosers = 3;

struct WyrTally {
  int countA = 0;
  int countB = 0;
  std::optional<char> majority;  // nullopt on an even split (including 0-0)
};

WyrTally tallyChoices(const std::vector<WyrChoice>& choices);

// One entry per id in playerIds, in that order, including 0 deltas. Only
// players with a choice in `choices` can score.
std::vector<PointsDelta> wyrPoints(const std::vector<std::string>& playerIds,
                                   const std::vector<WyrChoice>& choices);

}  // namespace feast
