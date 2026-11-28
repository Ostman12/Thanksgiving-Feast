#include "game/wyr.h"

namespace feast {

WyrTally tallyChoices(const std::vector<WyrChoice>& choices) {
  WyrTally tally;
  for (const WyrChoice& c : choices) {
    if (c.choice == 'A') {
      ++tally.countA;
    } else {
      ++tally.countB;
    }
  }
  if (tally.countA > tally.countB) tally.majority = 'A';
  if (tally.countB > tally.countA) tally.majority = 'B';
  return tally;
}

std::vector<PointsDelta> wyrPoints(const std::vector<std::string>& playerIds,
                                   const std::vector<WyrChoice>& choices) {
  WyrTally tally = tallyChoices(choices);
  int total = tally.countA + tally.countB;

  std::vector<PointsDelta> points;
  for (const std::string& id : playerIds) {
    int delta = 0;
    for (const WyrChoice& c : choices) {
      if (c.playerId != id) continue;
      if (tally.majority && c.choice == *tally.majority) delta += kMajorityPoints;
      int sameChoice = (c.choice == 'A') ? tally.countA : tally.countB;
      if (sameChoice == 1 && total >= kLoneWolfMinChoosers) delta += kLoneWolfPoints;
    }
    points.push_back({id, delta});
  }
  return points;
}

}  // namespace feast
