// Room's Mystery methods. The rules themselves (tally, scoring) are pure
// functions in mystery.cpp; this file wires them to the room's state.

#include <algorithm>

#include "game/content.h"
#include "game/room.h"
#include "game/text.h"

namespace feast {

namespace {
bool contains(const std::vector<std::string>& ids, const std::string& id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}
}  // namespace

// rules.md Mystery step 1-2: pick an unused word pair and an impostor, then
// deal every player their word privately.
void Room::setupMysteryRound() {
  mystery_ = MysteryRoundState{};

  const std::vector<WordPair>& pairs = wordPairs();
  if (usedWordPairs_.size() >= pairs.size()) usedWordPairs_.clear();
  std::vector<std::size_t> unused;
  for (std::size_t i = 0; i < pairs.size(); ++i) {
    if (usedWordPairs_.count(i) == 0) unused.push_back(i);
  }
  std::size_t pairIndex = unused[std::uniform_int_distribution<std::size_t>(0, unused.size() - 1)(rng_)];
  usedWordPairs_.insert(pairIndex);
  mystery_.mainWord = pairs[pairIndex].mainWord;
  mystery_.impostorWord = pairs[pairIndex].impostorWord;

  // A random connected player who hasn't been impostor this match. If all of
  // them have, anyone connected. (startRound guarantees 3+ are connected.)
  std::vector<std::string> candidates;
  for (const Player& p : players_) {
    if (p.connected && !contains(pastImpostors_, p.id)) candidates.push_back(p.id);
  }
  if (candidates.empty()) {
    for (const Player& p : players_) {
      if (p.connected) candidates.push_back(p.id);
    }
  }
  mystery_.impostorId =
      candidates[std::uniform_int_distribution<std::size_t>(0, candidates.size() - 1)(rng_)];
  pastImpostors_.push_back(mystery_.impostorId);

  // Everyone in the room is dealt in, connected or not, so a player who
  // reconnects mid-round gets their word back and can still vote.
  for (const Player& p : players_) {
    mystery_.playerIds.push_back(p.id);
    bool isImpostor = (p.id == mystery_.impostorId);
    emit(YourWordEvent{isImpostor ? mystery_.impostorWord : mystery_.mainWord, isImpostor}, p.id);
  }
}

std::string Room::castVote(const std::string& playerId, const std::optional<std::string>& targetId,
                           std::int64_t nowMs) {
  if (phase_ != Phase::MysteryVote) return "wrong phase";
  if (!inMysteryRound(playerId)) return "not in the round";
  if (contains(voterIds(), playerId)) return "already submitted";
  if (targetId) {
    if (*targetId == playerId) return "cannot vote for yourself";
    if (!inMysteryRound(*targetId)) return "invalid target";
  }

  mystery_.votes.push_back({playerId, targetId});
  emit(SubmittedEvent{voterIds()});
  if (allDueVoted(nowMs)) advance(nowMs);
  return "";
}

// GUESS_WORD. Two moments (rules.md Mystery):
//   discuss: a steal. Right wins the round, wrong outs the impostor. Either way
//            the round ends now.
//   guess:   the final guess (hail mary) after being caught.
std::string Room::guessWord(const std::string& playerId, const std::string& rawWord,
                            std::int64_t nowMs) {
  if (phase_ != Phase::MysteryDiscuss && phase_ != Phase::MysteryGuess) return "wrong phase";
  if (playerId != mystery_.impostorId) return "not the impostor";
  if (mystery_.guess) return "already submitted";
  std::string word = trimWhitespace(rawWord);
  std::size_t length = utf8Length(word);
  if (length < 1 || length > kMaxGuessLength) return "invalid guess";

  mystery_.guess = word;
  // No SUBMITTED here: only the impostor can guess, so it would name them to
  // the whole room. The guess ends the round or phase right away instead.
  if (phase_ == Phase::MysteryDiscuss) {
    mystery_.earlyGuess = true;
    finishMysteryRound(false, nowMs);
  } else {
    advance(nowMs);
  }
  return "";
}

// End of the vote phase: tally and tell everyone. Caught: the impostor gets a
// final guess. Escaped: straight to the reveal.
void Room::finishVote(std::int64_t nowMs) {
  const Player* impostor = findPlayer(mystery_.impostorId);
  if (impostor == nullptr || !impostor->connected) {
    // The vote ended (deadline or host Next) while the impostor was still
    // away, inside their grace time. No vote result without them: void.
    finishMysteryRound(true, nowMs);
    return;
  }
  VoteTally tally = tallyVotes(mystery_.votes, mystery_.impostorId);
  VoteResultEvent result{mystery_.votes, tally.accusedId, tally.caught};
  mystery_.voteResult = result;
  emit(std::move(result));
  if (tally.caught) {
    enterPhase(Phase::MysteryGuess, nowMs);
  } else {
    finishMysteryRound(false, nowMs);
  }
}

// Enter the reveal. A void round (impostor left before the vote result)
// scores nothing.
void Room::finishMysteryRound(bool voided, std::int64_t nowMs) {
  RoundResultEvent result;
  result.impostorId = mystery_.impostorId;
  result.mainWord = mystery_.mainWord;
  result.impostorWord = mystery_.impostorWord;
  result.voided = voided;

  if (!voided) {
    bool caught = mystery_.voteResult && mystery_.voteResult->caught;
    result.guess = mystery_.guess;
    result.earlyGuess = mystery_.earlyGuess;
    if (mystery_.guess) result.guessCorrect = isCorrectGuess(*mystery_.guess, mystery_.mainWord);
    result.points = mysteryPoints(mystery_.playerIds, mystery_.impostorId, mystery_.earlyGuess,
                                  caught, result.guessCorrect.value_or(false));
    for (const PointsDelta& d : result.points) {
      if (Player* p = findPlayerMutable(d.playerId)) p->score += d.delta;
    }
  }

  mystery_.roundResult = result;
  emit(std::move(result));
  enterPhase(Phase::MysteryReveal, nowMs);  // ROOM_STATE carries the new scores.
}

// rules.md Disconnects, Mystery part. During discuss and vote nothing happens
// right away: the player has kReconnectGraceMs to come back (see mysteryTick).
void Room::mysteryOnDisconnect(const std::string& playerId, std::int64_t nowMs) {
  if (phase_ == Phase::MysteryGuess && playerId == mystery_.impostorId) {
    advance(nowMs);  // Counts as no guess.
  }
}

// Called from tick(), before deadlines. Returns true if it changed the phase.
bool Room::mysteryTick(std::int64_t nowMs) {
  if (phase_ != Phase::MysteryDiscuss && phase_ != Phase::MysteryVote) return false;

  // Impostor gone longer than the grace time: void the round.
  const Player* impostor = findPlayer(mystery_.impostorId);
  if (impostor != nullptr && !impostor->connected && impostor->disconnectedAtMs &&
      nowMs - *impostor->disconnectedAtMs >= kReconnectGraceMs) {
    finishMysteryRound(true, nowMs);
    return true;
  }
  // Someone's grace ran out and they were the only vote still missing.
  if (phase_ == Phase::MysteryVote && allDueVoted(nowMs)) {
    advance(nowMs);
    return true;
  }
  return false;
}

// What a rejoining player needs to rebuild a Mystery screen. Sent to them only.
void Room::mysteryCatchUp(const std::string& playerId) {
  bool roundRunning = phase_ == Phase::MysteryDiscuss || phase_ == Phase::MysteryVote ||
                      phase_ == Phase::MysteryGuess;
  if (roundRunning && inMysteryRound(playerId)) {
    bool isImpostor = (playerId == mystery_.impostorId);
    emit(YourWordEvent{isImpostor ? mystery_.impostorWord : mystery_.mainWord, isImpostor},
         playerId);
  }
  if (phase_ == Phase::MysteryVote) emit(SubmittedEvent{voterIds()}, playerId);
  if ((phase_ == Phase::MysteryGuess || phase_ == Phase::MysteryReveal) && mystery_.voteResult) {
    emit(*mystery_.voteResult, playerId);
  }
  if (phase_ == Phase::MysteryReveal && mystery_.roundResult) {
    emit(*mystery_.roundResult, playerId);
  }
}

bool Room::inMysteryRound(const std::string& playerId) const {
  return contains(mystery_.playerIds, playerId);
}

// rules.md: the vote ends when every round player who is still due has voted.
// Due = connected, or disconnected less than kReconnectGraceMs ago. Waiting on
// every recent leaver, not just the impostor, keeps the wait from naming them.
bool Room::allDueVoted(std::int64_t nowMs) const {
  std::vector<std::string> voted = voterIds();
  for (const std::string& id : mystery_.playerIds) {
    const Player* p = findPlayer(id);
    if (p == nullptr || contains(voted, id)) continue;
    bool inGrace = p->disconnectedAtMs && nowMs - *p->disconnectedAtMs < kReconnectGraceMs;
    if (p->connected || inGrace) return false;
  }
  return true;
}

std::vector<std::string> Room::voterIds() const {
  std::vector<std::string> ids;
  for (const Vote& v : mystery_.votes) ids.push_back(v.voterId);
  return ids;
}

}  // namespace feast
