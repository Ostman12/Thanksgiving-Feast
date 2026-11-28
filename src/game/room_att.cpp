// Room's Around the Table methods (rules.md Game 3). Scoring is a pure
// function in att.cpp; this file wires it to the room's state.

#include <algorithm>

#include "game/content.h"
#include "game/room.h"

namespace feast {

namespace {
bool contains(const std::vector<std::string>& ids, const std::string& id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}
}  // namespace

// ---------------------------------------------------------------- setup

// Someone who hasn't been the subject yet is connected right now.
bool Room::nextSubjectAvailable() const {
  for (const Player& p : players_) {
    if (p.connected && !contains(pastSubjects_, p.id)) return true;
  }
  return false;
}

// An unused question; the list starts over once every question was used.
std::size_t Room::pickAttQuestion() {
  const std::vector<AttQuestion>& questions = attQuestions();
  if (usedAttQuestions_.size() >= questions.size()) usedAttQuestions_.clear();
  std::vector<std::size_t> unused;
  for (std::size_t i = 0; i < questions.size(); ++i) {
    if (usedAttQuestions_.count(i) == 0) unused.push_back(i);
  }
  std::size_t index = unused[std::uniform_int_distribution<std::size_t>(0, unused.size() - 1)(rng_)];
  usedAttQuestions_.insert(index);
  return index;
}

// Steps 1-2: a random connected player who hasn't been the subject, and their
// first question, shown to them only. Returns false if nobody can be subject.
bool Room::setupAttRound() {
  std::vector<std::string> candidates;
  for (const Player& p : players_) {
    if (p.connected && !contains(pastSubjects_, p.id)) candidates.push_back(p.id);
  }
  if (candidates.empty()) return false;

  att_ = AttRoundState{};
  att_.subjectId =
      candidates[std::uniform_int_distribution<std::size_t>(0, candidates.size() - 1)(rng_)];
  pastSubjects_.push_back(att_.subjectId);
  att_.questionIndex = pickAttQuestion();
  att_.skipsLeft = kAttSkipsPerTurn;

  emit(attSubjectEvent());
  emit(attPreview(), att_.subjectId);  // Private: skipped questions never reach others.
  return true;
}

AttSubjectEvent Room::attSubjectEvent() const {
  return AttSubjectEvent{att_.subjectId, round_, attTotalRounds_};
}

AttPreviewEvent Room::attPreview() const {
  const AttQuestion& q = attQuestions()[att_.questionIndex];
  return AttPreviewEvent{q.question, {q.options[0], q.options[1], q.options[2], q.options[3]},
                         att_.skipsLeft};
}

// ---------------------------------------------------------------- choosing

std::string Room::attSkip(const std::string& playerId, std::int64_t nowMs) {
  if (phase_ != Phase::AttChoose) return "wrong phase";
  if (playerId != att_.subjectId) return "not the subject";
  if (att_.skipsLeft <= 0) return "no skips left";

  --att_.skipsLeft;
  att_.questionIndex = pickAttQuestion();
  emit(attPreview(), att_.subjectId);
  enterPhase(Phase::AttChoose, nowMs);  // A fresh 20 s to read the new question.
  return "";
}

std::string Room::attContinue(const std::string& playerId, std::int64_t nowMs) {
  if (phase_ != Phase::AttChoose) return "wrong phase";
  if (playerId != att_.subjectId) return "not the subject";
  advance(nowMs);
  return "";
}

// Step 3 begins: the chosen question goes to everyone.
void Room::startAttPick(std::int64_t nowMs) {
  const AttQuestion& q = attQuestions()[att_.questionIndex];
  att_.question = AttQuestionEvent{att_.subjectId, q.question,
                                   {q.options[0], q.options[1], q.options[2], q.options[3]}};
  emit(*att_.question);
  enterPhase(Phase::AttPick, nowMs);
}

// ---------------------------------------------------------------- picking

std::string Room::attPick(const std::string& playerId, std::optional<char> option,
                          std::int64_t nowMs) {
  if (phase_ != Phase::AttPick) return "wrong phase";
  if (findPlayer(playerId) == nullptr) return "not in a room";
  if (contains(attPickerIds(), playerId)) return "already submitted";
  if (option && (*option < 'A' || *option > 'D')) return "invalid option";
  if (!option && playerId == att_.subjectId) return "invalid option";  // the subject can't pass

  att_.picks.push_back({playerId, option});
  emit(SubmittedEvent{attPickerIds()});
  if (allAttPicked()) advance(nowMs);
  return "";
}

// rules.md: the phase ends early only once every connected player has picked
// or passed AND the subject has answered (even if the subject is away).
bool Room::allAttPicked() const {
  std::vector<std::string> picked = attPickerIds();
  if (!contains(picked, att_.subjectId)) return false;
  for (const Player& p : players_) {
    if (p.connected && !contains(picked, p.id)) return false;
  }
  return true;
}

std::vector<std::string> Room::attPickerIds() const {
  std::vector<std::string> ids;
  for (const AttPrediction& p : att_.picks) ids.push_back(p.playerId);
  return ids;
}

void Room::attOnDisconnect(std::int64_t nowMs) {
  if (phase_ == Phase::AttPick && allAttPicked()) advance(nowMs);
}

// ---------------------------------------------------------------- reveal

// Step 4: score, record stats for the awards, and reveal.
void Room::finishAttRound(std::int64_t nowMs) {
  AttResultEvent result;
  result.subjectId = att_.subjectId;
  for (const AttPrediction& p : att_.picks) {
    if (p.playerId == att_.subjectId) result.answer = p.option;
  }
  // Predictions in join order (the subject's answer is not a prediction).
  for (const Player& pl : players_) {
    for (const AttPrediction& p : att_.picks) {
      if (p.playerId == pl.id && p.playerId != att_.subjectId) result.predictions.push_back(p);
    }
  }

  AttScore score = attPoints(playerIds(), att_.subjectId, result.answer, result.predictions);
  result.perfect = score.perfect;
  result.points = score.points;
  for (const PointsDelta& d : result.points) {
    if (Player* p = findPlayerMutable(d.playerId)) p->score += d.delta;
  }

  // Awards bookkeeping: only rounds where the subject answered count.
  if (result.answer) {
    for (const AttPrediction& p : result.predictions) {
      if (!p.option) continue;  // a pass is not a prediction
      ++stats_.predictionsMade[p.playerId];
      ++stats_.subjectPredictions[att_.subjectId];
      if (*p.option == *result.answer) {
        ++stats_.correctPredictions[p.playerId];
        ++stats_.subjectCorrect[att_.subjectId];
      }
    }
  }

  att_.result = result;
  emit(std::move(result));
  enterPhase(Phase::AttReveal, nowMs);  // ROOM_STATE carries the new scores.
}

// ---------------------------------------------------------------- catch-up

// What a rejoining player needs to rebuild an Around the Table screen.
void Room::attCatchUp(const std::string& playerId) {
  if (phase_ != Phase::AttChoose && phase_ != Phase::AttPick && phase_ != Phase::AttReveal) return;
  emit(attSubjectEvent(), playerId);
  if (phase_ == Phase::AttChoose && playerId == att_.subjectId) emit(attPreview(), playerId);
  if (att_.question) emit(*att_.question, playerId);
  if (phase_ == Phase::AttPick) emit(SubmittedEvent{attPickerIds()}, playerId);
  if (phase_ == Phase::AttReveal && att_.result) emit(*att_.result, playerId);
}

// The same for a display: room-wide messages only (never the preview).
void Room::attSnapshot(std::vector<Event>& events) const {
  events.push_back(attSubjectEvent());
  if (att_.question) events.push_back(*att_.question);
  if (phase_ == Phase::AttPick) events.push_back(SubmittedEvent{attPickerIds()});
  if (phase_ == Phase::AttReveal && att_.result) events.push_back(*att_.result);
}

std::vector<std::string> Room::playerIds() const {
  std::vector<std::string> ids;
  for (const Player& p : players_) ids.push_back(p.id);
  return ids;
}

}  // namespace feast
