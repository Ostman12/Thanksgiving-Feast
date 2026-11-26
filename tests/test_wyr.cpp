#include <algorithm>
#include <map>
#include <set>

#include "game/room.h"
#include "game/wyr.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace feast;
using namespace testhelp;

// ================================================================ pure rules

namespace {
WyrChoice pick(const std::string& id, char choice, char prediction) {
  return WyrChoice{id, choice, prediction};
}

int deltaFor(const std::vector<PointsDelta>& points, const std::string& id) {
  for (const PointsDelta& d : points) {
    if (d.playerId == id) return d.delta;
  }
  return -1;
}
}  // namespace

TEST(wyr_tally_majority) {
  WyrTally t = tallyChoices({pick("a", 'A', 'A'), pick("b", 'A', 'B'), pick("c", 'B', 'A')});
  CHECK_EQ(t.countA, 2);
  CHECK_EQ(t.countB, 1);
  CHECK(t.majority == std::optional<char>('A'));
}

TEST(wyr_tally_even_split_has_no_majority) {
  WyrTally t = tallyChoices({pick("a", 'A', 'A'), pick("b", 'B', 'B')});
  CHECK(!t.majority.has_value());
  CHECK(!tallyChoices({}).majority.has_value());  // nobody chose
}

TEST(wyr_prediction_points) {
  // 3 A vs 1 B. Majority A.
  std::vector<WyrChoice> c = {pick("a", 'A', 'A'), pick("b", 'A', 'B'), pick("c", 'A', 'A'),
                              pick("d", 'B', 'B')};
  auto p = wyrPoints({"a", "b", "c", "d"}, c);
  CHECK_EQ(deltaFor(p, "a"), 100);
  CHECK_EQ(deltaFor(p, "b"), 0);
  CHECK_EQ(deltaFor(p, "c"), 100);
  CHECK_EQ(deltaFor(p, "d"), 50);  // lone wolf, wrong prediction
}

TEST(wyr_lone_wolf_and_correct_prediction_stack) {
  std::vector<WyrChoice> c = {pick("a", 'A', 'A'), pick("b", 'A', 'A'), pick("d", 'B', 'A')};
  auto p = wyrPoints({"a", "b", "d"}, c);
  CHECK_EQ(deltaFor(p, "d"), 150);
}

TEST(wyr_lone_wolf_needs_3_choosers) {
  // 2 choosers: each is alone on their side, but nobody is a lone wolf.
  std::vector<WyrChoice> c = {pick("a", 'A', 'A'), pick("b", 'B', 'A')};
  auto p = wyrPoints({"a", "b"}, c);
  CHECK_EQ(deltaFor(p, "a"), 0);  // even split: no prediction points either
  CHECK_EQ(deltaFor(p, "b"), 0);
}

TEST(wyr_even_split_no_prediction_points) {
  std::vector<WyrChoice> c = {pick("a", 'A', 'A'), pick("b", 'A', 'B'), pick("c", 'B', 'A'),
                              pick("d", 'B', 'B')};
  auto p = wyrPoints({"a", "b", "c", "d"}, c);
  for (const char* id : {"a", "b", "c", "d"}) CHECK_EQ(deltaFor(p, id), 0);
}

TEST(wyr_non_submitter_scores_zero) {
  std::vector<WyrChoice> c = {pick("a", 'A', 'A'), pick("b", 'A', 'A'), pick("c", 'B', 'A')};
  auto p = wyrPoints({"a", "b", "c", "x"}, c);
  CHECK_EQ(p.size(), std::size_t(4));
  CHECK_EQ(deltaFor(p, "x"), 0);
}

// ================================================================ in a Room

namespace {

// A match fast-forwarded to the first Would You Rather prompt.
struct WyrGame : Setup {
  std::vector<Outgoing> startEvents;
  explicit WyrGame(int count) : Setup(count) {
    room.startMatch(host(), T0);
    room.takeOutbox();
    nextUntil(*this, Phase::WyrChoose);
    startEvents = room.takeOutbox();
  }
  std::map<std::string, int> scores() const {
    std::map<std::string, int> s;
    for (const Player& p : room.players()) s[p.id] = p.score;
    return s;
  }
};

}  // namespace

TEST(wyr_prompt_sent_to_everyone_at_choose_start) {
  WyrGame g(4);
  CHECK_EQ(g.room.game(), Game::Wyr);
  CHECK_EQ(g.room.round(), 1);
  bool found = false;
  for (const Outgoing& o : g.startEvents) {
    if (const WyrPromptEvent* p = std::get_if<WyrPromptEvent>(&o.event)) {
      CHECK(o.toPlayerId.empty());
      CHECK(!p->optionA.empty());
      CHECK(!p->optionB.empty());
      found = true;
    }
  }
  CHECK(found);
}

TEST(wyr_three_different_prompts) {
  WyrGame g(4);
  std::set<std::string> seen;
  for (const WyrPromptEvent& p : eventsOf<WyrPromptEvent>(g.startEvents)) seen.insert(p.optionA);
  for (int round = 2; round <= 3; ++round) {
    g.room.next(g.host(), T0);  // choose -> reveal
    g.room.next(g.host(), T0);  // reveal -> next choose
    for (const WyrPromptEvent& p : eventsOf<WyrPromptEvent>(g.room.takeOutbox())) {
      seen.insert(p.optionA);
    }
  }
  CHECK_EQ(seen.size(), std::size_t(3));
}

TEST(wyr_submit_validation) {
  WyrGame g(4);
  CHECK_EQ(g.room.submitChoice(g.id(1), "C", "A", T0), std::string("invalid choice"));
  CHECK_EQ(g.room.submitChoice(g.id(1), "a", "A", T0), std::string("invalid choice"));
  CHECK_EQ(g.room.submitChoice(g.id(1), "A", "", T0), std::string("invalid choice"));
  CHECK_EQ(g.room.submitChoice(g.id(1), "AB", "A", T0), std::string("invalid choice"));
  CHECK_EQ(g.room.submitChoice("p999", "A", "A", T0), std::string("not in a room"));
  CHECK_EQ(g.room.submitChoice(g.id(1), "A", "B", T0), std::string(""));
  CHECK_EQ(g.room.submitChoice(g.id(1), "B", "B", T0), std::string("already submitted"));
}

TEST(wyr_submit_only_in_choose_phase) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  CHECK_EQ(s.room.submitChoice(s.id(1), "A", "A", T0), std::string("wrong phase"));
  WyrGame g(4);
  g.room.next(g.host(), T0);  // reveal
  CHECK_EQ(g.room.submitChoice(g.id(1), "A", "A", T0), std::string("wrong phase"));
}

TEST(wyr_submit_broadcasts_ids_only) {
  WyrGame g(4);
  g.room.submitChoice(g.id(2), "B", "A", T0);
  std::vector<Outgoing> out = g.room.takeOutbox();
  CHECK_EQ(out.size(), std::size_t(1));
  std::vector<SubmittedEvent> sub = eventsOf<SubmittedEvent>(out);
  CHECK_EQ(sub.size(), std::size_t(1));
  if (sub.empty()) return;
  CHECK_EQ(sub[0].playerIds, std::vector<std::string>{g.id(2)});
  CHECK(out[0].toPlayerId.empty());
  CHECK(eventsOf<WyrResultEvent>(out).empty());  // no choices before the reveal
}

TEST(wyr_ends_when_all_connected_submitted) {
  WyrGame g(4);
  for (int i = 0; i < 3; ++i) g.room.submitChoice(g.id(i), "A", "A", T0);
  CHECK_EQ(g.room.phase(), Phase::WyrChoose);
  g.room.submitChoice(g.id(3), "B", "A", T0);
  CHECK_EQ(g.room.phase(), Phase::WyrReveal);
  CHECK(!g.room.deadlineMs().has_value());
}

TEST(wyr_deadline_ends_choose_phase) {
  WyrGame g(4);
  g.room.submitChoice(g.id(1), "A", "A", T0);
  g.room.tick(T0 + 29'999);
  CHECK_EQ(g.room.phase(), Phase::WyrChoose);
  g.room.tick(T0 + 30'000);
  CHECK_EQ(g.room.phase(), Phase::WyrReveal);
}

TEST(wyr_does_not_wait_for_disconnected_player) {
  WyrGame g(4);
  for (int i = 0; i < 3; ++i) g.room.submitChoice(g.id(i), "A", "A", T0);
  g.room.disconnect(g.id(3), T0);
  CHECK_EQ(g.room.phase(), Phase::WyrReveal);
}

TEST(wyr_result_and_scores) {
  WyrGame g(4);
  std::map<std::string, int> before = g.scores();
  g.room.submitChoice(g.id(0), "A", "A", T0);  // +100
  g.room.submitChoice(g.id(1), "A", "B", T0);  // 0
  g.room.submitChoice(g.id(2), "A", "A", T0);  // +100
  g.room.submitChoice(g.id(3), "B", "A", T0);  // +100 prediction, +50 lone wolf
  std::vector<Outgoing> out = g.room.takeOutbox();

  std::vector<WyrResultEvent> results = eventsOf<WyrResultEvent>(out);
  CHECK_EQ(results.size(), std::size_t(1));
  if (results.empty()) return;
  const WyrResultEvent& r = results[0];
  CHECK_EQ(r.countA, 3);
  CHECK_EQ(r.countB, 1);
  CHECK(r.majority == std::optional<char>('A'));
  CHECK_EQ(r.choices.size(), std::size_t(4));
  CHECK_EQ(r.choices[0].playerId, g.id(0));  // join order
  CHECK_EQ(r.choices[3].choice, 'B');

  std::map<std::string, int> after = g.scores();
  CHECK_EQ(after[g.id(0)] - before[g.id(0)], 100);
  CHECK_EQ(after[g.id(1)] - before[g.id(1)], 0);
  CHECK_EQ(after[g.id(2)] - before[g.id(2)], 100);
  CHECK_EQ(after[g.id(3)] - before[g.id(3)], 150);
}

TEST(wyr_disconnected_choice_still_counts_and_scores) {
  WyrGame g(4);
  std::map<std::string, int> before = g.scores();
  g.room.submitChoice(g.id(3), "B", "A", T0);
  g.room.disconnect(g.id(3), T0);
  for (int i = 0; i < 3; ++i) g.room.submitChoice(g.id(i), "A", "A", T0);
  CHECK_EQ(g.room.phase(), Phase::WyrReveal);
  CHECK_EQ(g.scores()[g.id(3)] - before[g.id(3)], 150);
}

TEST(wyr_rejoin_during_choose_gets_prompt_and_submitted) {
  WyrGame g(4);
  g.room.submitChoice(g.id(1), "A", "A", T0);
  g.room.disconnect(g.id(2), T0);
  g.room.takeOutbox();
  g.room.rejoin(g.players[2].token, T0);
  std::vector<Outgoing> out = g.room.takeOutbox();
  int prompts = 0;
  int submitted = 0;
  for (const Outgoing& o : out) {
    if (std::holds_alternative<WyrPromptEvent>(o.event)) {
      CHECK_EQ(o.toPlayerId, g.id(2));
      ++prompts;
    }
    if (const SubmittedEvent* s = std::get_if<SubmittedEvent>(&o.event)) {
      CHECK_EQ(o.toPlayerId, g.id(2));
      CHECK_EQ(s->playerIds, std::vector<std::string>{g.id(1)});
      ++submitted;
    }
  }
  CHECK_EQ(prompts, 1);
  CHECK_EQ(submitted, 1);
  CHECK_EQ(g.room.submitChoice(g.id(2), "B", "B", T0), std::string(""));
}

TEST(wyr_rejoin_during_reveal_gets_result) {
  WyrGame g(4);
  g.room.next(g.host(), T0);  // reveal
  g.room.disconnect(g.id(2), T0);
  g.room.takeOutbox();
  g.room.rejoin(g.players[2].token, T0);
  std::vector<Outgoing> out = g.room.takeOutbox();
  CHECK_EQ(eventsOf<WyrPromptEvent>(out).size(), std::size_t(1));
  CHECK_EQ(eventsOf<WyrResultEvent>(out).size(), std::size_t(1));
}

TEST(full_match_winner_is_top_score) {
  WyrGame g(4);
  // Player 3 is a correct-predicting lone wolf every prompt: +150 x 3.
  for (int round = 1; round <= 3; ++round) {
    for (int i = 0; i < 3; ++i) g.room.submitChoice(g.id(i), "A", "B", T0);
    g.room.submitChoice(g.id(3), "B", "A", T0);
    g.room.next(g.host(), T0);  // reveal -> next prompt or match over
  }
  CHECK_EQ(g.room.phase(), Phase::MatchOver);
  std::vector<MatchOverEvent> overs = eventsOf<MatchOverEvent>(g.room.takeOutbox());
  CHECK_EQ(overs.size(), std::size_t(1));
  if (overs.empty()) return;
  const MatchOverEvent& over = overs[0];
  // Leaderboard is sorted high to low and winners are everyone at the top.
  for (std::size_t i = 1; i < over.leaderboard.size(); ++i) {
    CHECK(over.leaderboard[i - 1].score >= over.leaderboard[i].score);
  }
  for (const LeaderboardEntry& e : over.leaderboard) {
    bool isWinner = std::find(over.winnerIds.begin(), over.winnerIds.end(), e.playerId) !=
                    over.winnerIds.end();
    CHECK_EQ(isWinner, e.score == over.leaderboard.front().score);
  }
  CHECK(g.room.findPlayer(g.id(3))->score >= 450);
}
