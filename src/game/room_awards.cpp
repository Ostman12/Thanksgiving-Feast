// End-of-match awards (rules.md Awards): revealed one at a time, Mario Party
// style, +75 to each winner as each one is shown. What counts for each award
// is in awards.cpp; this file runs the reveal.

#include "game/room.h"

namespace feast {

// After the last game. No award with a winner: straight to the leaderboard.
void Room::startAwards(std::int64_t nowMs) {
  awards_ = computeAwards(stats_, playerIds(), withAroundTheTable_);
  awardIndex_ = 0;
  givenAwards_.clear();
  game_ = Game::None;
  if (awards_.empty()) {
    finishMatch();
    return;
  }
  revealAward(nowMs);
}

// Shows awards_[awardIndex_]: adds the points, then AWARD, ROOM_STATE, PHASE.
void Room::revealAward(std::int64_t nowMs) {
  const Award& award = awards_[awardIndex_];
  for (const std::string& id : award.playerIds) {
    if (Player* p = findPlayerMutable(id)) p->score += kAwardPoints;
  }
  givenAwards_.push_back({award.id, award.title, award.playerIds});

  round_ = static_cast<int>(awardIndex_) + 1;  // PHASE.round = the award on screen
  emit(AwardEvent{round_, static_cast<int>(awards_.size()), award.id, award.title,
                  award.description, award.playerIds, kAwardPoints});
  enterPhase(Phase::Awards, nowMs);
}

}  // namespace feast
