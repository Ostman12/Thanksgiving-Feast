// Room's Would You Rather methods. Tally and scoring are pure functions in
// wyr.cpp; this file wires them to the room's state.

#include <algorithm>

#include "game/content.h"
#include "game/room.h"

namespace feast {

namespace {
bool isOption(const std::string& s) { return s == "A" || s == "B"; }
}  // namespace

// rules.md WYR step 1: pick an unused prompt and show it to everyone.
void Room::setupWyrRound() {
  wyr_ = WyrRoundState{};

  const std::vector<WyrPrompt>& prompts = wyrPrompts();
  if (usedPrompts_.size() >= prompts.size()) usedPrompts_.clear();
  std::vector<std::size_t> unused;
  for (std::size_t i = 0; i < prompts.size(); ++i) {
    if (usedPrompts_.count(i) == 0) unused.push_back(i);
  }
  std::size_t index = unused[std::uniform_int_distribution<std::size_t>(0, unused.size() - 1)(rng_)];
  usedPrompts_.insert(index);

  wyr_.prompt = WyrPromptEvent{prompts[index].optionA, prompts[index].optionB};
  emit(wyr_.prompt);
}

std::string Room::submitChoice(const std::string& playerId, const std::string& choice,
                               std::int64_t nowMs) {
  if (phase_ != Phase::WyrChoose) return "wrong phase";
  if (findPlayer(playerId) == nullptr) return "not in a room";
  std::vector<std::string> chose = chooserIds();
  if (std::find(chose.begin(), chose.end(), playerId) != chose.end()) return "already submitted";
  if (!isOption(choice)) return "invalid choice";

  wyr_.choices.push_back({playerId, choice[0]});
  emit(SubmittedEvent{chooserIds()});
  if (allConnectedChose()) advance(nowMs);
  return "";
}

// End of the choose phase: counts, points, and the reveal.
void Room::finishWyrRound(std::int64_t nowMs) {
  std::vector<std::string> playerIds;
  for (const Player& p : players_) playerIds.push_back(p.id);

  WyrTally tally = tallyChoices(wyr_.choices);
  WyrResultEvent result;
  for (const Player& p : players_) {  // choices in join order
    for (const WyrChoice& c : wyr_.choices) {
      if (c.playerId == p.id) result.choices.push_back(c);
    }
  }
  result.countA = tally.countA;
  result.countB = tally.countB;
  result.majority = tally.majority;
  result.points = wyrPoints(playerIds, wyr_.choices);
  for (const PointsDelta& d : result.points) {
    if (Player* p = findPlayerMutable(d.playerId)) p->score += d.delta;
  }

  wyr_.result = result;
  emit(std::move(result));
  enterPhase(Phase::WyrReveal, nowMs);  // ROOM_STATE carries the new scores.
}

// A disconnected player's submitted choice still counts; the phase just stops
// waiting for players who left without submitting.
void Room::wyrOnDisconnect(std::int64_t nowMs) {
  if (phase_ == Phase::WyrChoose && allConnectedChose()) advance(nowMs);
}

// What a rejoining player needs to rebuild a WYR screen. Sent to them only.
void Room::wyrCatchUp(const std::string& playerId) {
  if (phase_ != Phase::WyrChoose && phase_ != Phase::WyrReveal) return;
  emit(wyr_.prompt, playerId);
  if (phase_ == Phase::WyrChoose) emit(SubmittedEvent{chooserIds()}, playerId);
  if (phase_ == Phase::WyrReveal && wyr_.result) emit(*wyr_.result, playerId);
}

bool Room::allConnectedChose() const {
  std::vector<std::string> chose = chooserIds();
  for (const Player& p : players_) {
    if (p.connected && std::find(chose.begin(), chose.end(), p.id) == chose.end()) return false;
  }
  return true;
}

std::vector<std::string> Room::chooserIds() const {
  std::vector<std::string> ids;
  for (const WyrChoice& c : wyr_.choices) ids.push_back(c.playerId);
  return ids;
}

}  // namespace feast
