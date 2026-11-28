#include "game/att.h"

namespace feast {

AttScore attPoints(const std::vector<std::string>& playerIds, const std::string& subjectId,
                   std::optional<char> answer, const std::vector<AttPrediction>& predictions) {
  AttScore score;

  int made = 0;
  int right = 0;
  for (const AttPrediction& p : predictions) {
    if (p.playerId == subjectId || !p.option) continue;  // the subject and passes don't count
    ++made;
    if (answer && *p.option == *answer) ++right;
  }
  score.perfect = answer.has_value() && made >= kAttPerfectMinPredictors && right == made;

  for (const std::string& id : playerIds) {
    int delta = 0;
    for (const AttPrediction& p : predictions) {
      if (p.playerId != id || id == subjectId || !p.option || !answer) continue;
      if (*p.option == *answer) delta = kAttCorrectPoints + (score.perfect ? kAttPerfectBonus : 0);
    }
    score.points.push_back({id, delta});
  }
  return score;
}

}  // namespace feast
