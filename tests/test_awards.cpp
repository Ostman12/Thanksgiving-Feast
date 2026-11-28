// End-of-match awards (rules.md Awards).

#include <algorithm>
#include <map>

#include "game/awards.h"
#include "game/room.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace feast;
using namespace testhelp;

// ================================================================ pure rules

namespace {
const std::vector<std::string> kPlayers = {"a", "b", "c", "d"};

const Award* find(const std::vector<Award>& awards, const std::string& id) {
  for (const Award& a : awards) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

std::vector<std::string> winnersOf(const std::vector<Award>& awards, const std::string& id) {
  const Award* a = find(awards, id);
  return a ? a->playerIds : std::vector<std::string>{};
}
}  // namespace

TEST(awards_none_when_nothing_happened) {
  CHECK(computeAwards(MatchStats{}, kPlayers, true).empty());
}

TEST(awards_most_counts_and_ties_share) {
  MatchStats s;
  s.impostorVotes = {{"a", 2}, {"b", 2}, {"c", 1}};
  s.impostorWins = {{"d", 1}};
  s.majorityPicks = {{"c", 3}};
  s.loneWolfPicks = {{"b", 1}, {"d", 1}};
  std::vector<Award> awards = computeAwards(s, kPlayers, true);
  CHECK_EQ(winnersOf(awards, "detective"), (std::vector<std::string>{"a", "b"}));
  CHECK_EQ(winnersOf(awards, "master_of_disguise"), std::vector<std::string>{"d"});
  CHECK_EQ(winnersOf(awards, "crowd_pleaser"), std::vector<std::string>{"c"});
  CHECK_EQ(winnersOf(awards, "lone_wolf"), (std::vector<std::string>{"b", "d"}));
}

TEST(awards_follow_the_rules_table_order) {
  MatchStats s;
  s.loneWolfPicks = {{"a", 1}};
  s.impostorVotes = {{"b", 1}};
  s.correctPredictions = {{"c", 1}};
  std::vector<Award> awards = computeAwards(s, kPlayers, true);
  CHECK_EQ(awards.size(), std::size_t(3));
  if (awards.size() != 3) return;
  CHECK_EQ(awards[0].id, std::string("detective"));
  CHECK_EQ(awards[1].id, std::string("lone_wolf"));
  CHECK_EQ(awards[2].id, std::string("mind_reader"));
}

TEST(awards_closest_friend_needs_2_predictions) {
  MatchStats s;
  s.predictionsMade = {{"a", 1}, {"b", 4}, {"c", 2}};
  s.correctPredictions = {{"a", 1}, {"b", 3}, {"c", 1}};
  // a: 1/1 but only 1 made. b: 3/4 = 75%. c: 1/2 = 50%.
  CHECK_EQ(winnersOf(computeAwards(s, kPlayers, true), "closest_friend"),
           std::vector<std::string>{"b"});
  CHECK_EQ(winnersOf(computeAwards(s, kPlayers, true), "mind_reader"),
           std::vector<std::string>{"b"});
}

TEST(awards_plot_twist_and_open_book) {
  MatchStats s;
  s.subjectPredictions = {{"a", 3}, {"b", 3}, {"c", 2}};
  s.subjectCorrect = {{"a", 0}, {"b", 3}, {"c", 1}};
  std::vector<Award> awards = computeAwards(s, kPlayers, true);
  CHECK_EQ(winnersOf(awards, "plot_twist"), std::vector<std::string>{"a"});  // 0%
  CHECK_EQ(winnersOf(awards, "open_book"), std::vector<std::string>{"b"});   // 100%
}

TEST(awards_plot_twist_open_book_left_out_when_all_equal) {
  MatchStats s;
  s.subjectPredictions = {{"a", 2}, {"b", 4}};
  s.subjectCorrect = {{"a", 1}, {"b", 2}};  // both 50%
  std::vector<Award> awards = computeAwards(s, kPlayers, true);
  CHECK(find(awards, "plot_twist") == nullptr);
  CHECK(find(awards, "open_book") == nullptr);
}

TEST(awards_without_around_the_table) {
  MatchStats s;
  s.impostorWins = {{"a", 1}};
  s.correctPredictions = {{"b", 5}};
  std::vector<Award> awards = computeAwards(s, kPlayers, false);
  CHECK_EQ(awards.size(), std::size_t(1));
  CHECK(find(awards, "mind_reader") == nullptr);
}

// ================================================================ in a Room

namespace {

// Plays a whole 4-player match to the awards: every impostor escapes (nobody
// votes), Would You Rather has a clear majority, Around the Table is skipped
// through. Returns at the first award.
struct AwardsGame : Setup {
  std::map<std::string, int> beforeAwards;
  explicit AwardsGame() : Setup(4) {
    room.startMatch(host(), T0);
    nextUntil(*this, Phase::WyrChoose);
    for (int round = 1; round <= 3; ++round) {
      room.submitChoice(id(0), "A", T0);
      room.submitChoice(id(1), "A", T0);
      room.submitChoice(id(2), "A", T0);
      room.submitChoice(id(3), "B", T0);  // lone wolf every time
      room.next(room.hostId(), T0);
    }
    for (int i = 0; i < 20 && room.phase() != Phase::Awards; ++i) {
      if (room.phase() == Phase::AttReveal) {
        for (const Player& p : room.players()) beforeAwards[p.id] = p.score;
      }
      room.next(room.hostId(), T0);
    }
    if (beforeAwards.empty()) {
      for (const Player& p : room.players()) beforeAwards[p.id] = p.score;
    }
  }
  int gained(const std::string& pid) const { return room.findPlayer(pid)->score - beforeAwards.at(pid); }
};

}  // namespace

TEST(awards_revealed_one_at_a_time_worth_75) {
  AwardsGame g;
  CHECK_EQ(g.room.phase(), Phase::Awards);
  CHECK_EQ(g.room.round(), 1);
  CHECK(!g.room.deadlineMs().has_value());  // host taps Next

  std::vector<AwardEvent> first = eventsOf<AwardEvent>(g.room.takeOutbox());
  CHECK_EQ(first.size(), std::size_t(1));  // only the first one so far
  if (first.empty()) return;
  CHECK_EQ(first[0].index, 1);
  CHECK_EQ(first[0].points, 75);
  for (const std::string& pid : first[0].playerIds) CHECK_EQ(g.gained(pid), 75);

  // Reveal the rest, one per Next, then the leaderboard lists them all.
  int total = first[0].total;
  std::vector<std::string> ids = {first[0].id};
  for (int i = 2; i <= total; ++i) {
    g.room.next(g.room.hostId(), T0);
    std::vector<AwardEvent> a = eventsOf<AwardEvent>(g.room.takeOutbox());
    CHECK_EQ(a.size(), std::size_t(1));
    if (!a.empty()) {
      CHECK_EQ(a[0].index, i);
      ids.push_back(a[0].id);
    }
  }
  g.room.next(g.room.hostId(), T0);
  CHECK_EQ(g.room.phase(), Phase::MatchOver);
  std::vector<MatchOverEvent> over = eventsOf<MatchOverEvent>(g.room.takeOutbox());
  CHECK(!over.empty() && over[0].awards.size() == ids.size());

  // This match: impostors escaped (Master of Disguise), 3 picked A (Crowd
  // Pleaser), player 3 always alone (Lone Wolf).
  CHECK(std::find(ids.begin(), ids.end(), "master_of_disguise") != ids.end());
  CHECK(std::find(ids.begin(), ids.end(), "crowd_pleaser") != ids.end());
  CHECK(std::find(ids.begin(), ids.end(), "lone_wolf") != ids.end());
  CHECK_EQ(g.gained(g.id(3)) % 75, 0);
  CHECK(g.gained(g.id(3)) >= 75);  // at least Lone Wolf
}

TEST(awards_end_match_skips_the_rest) {
  AwardsGame g;
  g.room.takeOutbox();
  CHECK_EQ(g.room.endMatch(g.room.hostId()), std::string(""));
  std::vector<MatchOverEvent> over = eventsOf<MatchOverEvent>(g.room.takeOutbox());
  CHECK(!over.empty() && over[0].awards.size() == 1);  // only the one already shown
}

TEST(awards_rejoin_gets_current_award) {
  AwardsGame g;
  g.room.disconnect(g.id(2), T0);
  g.room.takeOutbox();
  g.room.rejoin(g.players[2].token, T0);
  CHECK_EQ(eventsOf<AwardEvent>(g.room.takeOutbox()).size(), std::size_t(1));
  bool displayHasAward = false;
  for (const Event& e : g.room.displaySnapshot()) {
    if (std::holds_alternative<AwardEvent>(e)) displayHasAward = true;
  }
  CHECK(displayHasAward);
}
