// Around the Table (rules.md Game 3).

#include <algorithm>
#include <map>
#include <set>

#include "game/att.h"
#include "game/content.h"
#include "game/room.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace feast;
using namespace testhelp;

// ================================================================ pure scoring

namespace {
AttPrediction pred(const std::string& id, std::optional<char> option) { return {id, option}; }

int deltaFor(const AttScore& s, const std::string& id) {
  for (const PointsDelta& d : s.points) {
    if (d.playerId == id) return d.delta;
  }
  return -1;
}
const std::vector<std::string> kIds = {"subj", "a", "b", "c"};
}  // namespace

TEST(att_correct_prediction_scores_100) {
  AttScore s = attPoints(kIds, "subj", 'B', {pred("a", 'B'), pred("b", 'A'), pred("c", 'B')});
  CHECK_EQ(deltaFor(s, "a"), 100);
  CHECK_EQ(deltaFor(s, "b"), 0);
  CHECK_EQ(deltaFor(s, "c"), 100);
  CHECK_EQ(deltaFor(s, "subj"), 0);  // the subject never scores
  CHECK(!s.perfect);
  CHECK_EQ(s.points.size(), std::size_t(4));
}

TEST(att_perfect_round_adds_150_each) {
  AttScore s = attPoints(kIds, "subj", 'C', {pred("a", 'C'), pred("b", 'C'), pred("c", 'C')});
  CHECK(s.perfect);
  for (const char* id : {"a", "b", "c"}) CHECK_EQ(deltaFor(s, id), 250);
  CHECK_EQ(deltaFor(s, "subj"), 0);
}

TEST(att_perfect_ignores_passes_but_needs_2_predictions) {
  AttScore two = attPoints(kIds, "subj", 'A', {pred("a", 'A'), pred("b", 'A'), pred("c", std::nullopt)});
  CHECK(two.perfect);  // a pass doesn't spoil it
  CHECK_EQ(deltaFor(two, "c"), 0);
  AttScore one = attPoints(kIds, "subj", 'A', {pred("a", 'A'), pred("b", std::nullopt)});
  CHECK(!one.perfect);  // a single prediction can't be "everyone"
  CHECK_EQ(deltaFor(one, "a"), 100);
}

TEST(att_no_answer_nobody_scores) {
  AttScore s = attPoints(kIds, "subj", std::nullopt, {pred("a", 'A'), pred("b", 'A')});
  CHECK(!s.perfect);
  for (const std::string& id : kIds) CHECK_EQ(deltaFor(s, id), 0);
}

TEST(att_question_bank_is_well_formed) {
  const std::vector<AttQuestion>& qs = attQuestions();
  CHECK_EQ(qs.size(), std::size_t(35));
  for (const AttQuestion& q : qs) {
    CHECK(std::string(q.question).size() > 0);
    for (const char* o : q.options) CHECK(std::string(o).size() > 0);
  }
}

// ================================================================ in a Room

namespace {

// A match fast-forwarded to the first Around the Table question choice.
struct AttGame : Setup {
  std::vector<Outgoing> events;
  std::string subject;
  explicit AttGame(int count, bool testMode = false) : Setup(count, testMode) {
    room.startMatch(host(), T0);
    room.takeOutbox();
    nextUntil(*this, Phase::AttChoose);
    read(room.takeOutbox());
  }
  // Remembers the latest subject from these events.
  void read(const std::vector<Outgoing>& out) {
    events = out;
    for (const AttSubjectEvent& e : eventsOf<AttSubjectEvent>(out)) subject = e.subjectId;
  }
  std::vector<std::string> others() const {
    std::vector<std::string> ids;
    for (const JoinResult& p : players) {
      if (p.playerId != subject) ids.push_back(p.playerId);
    }
    return ids;
  }
  const JoinResult& player(const std::string& id) const {
    for (const JoinResult& p : players) {
      if (p.playerId == id) return p;
    }
    return players[0];
  }
  std::map<std::string, int> scores() const {
    std::map<std::string, int> s;
    for (const Player& p : room.players()) s[p.id] = p.score;
    return s;
  }
};

std::vector<Outgoing> roomWide(const std::vector<Outgoing>& out) {
  std::vector<Outgoing> r;
  for (const Outgoing& o : out) {
    if (o.toPlayerId.empty()) r.push_back(o);
  }
  return r;
}

}  // namespace

TEST(att_starts_after_wyr_with_a_subject_and_private_preview) {
  AttGame g(4);
  CHECK_EQ(g.room.game(), Game::Att);
  CHECK_EQ(g.room.round(), 1);
  CHECK(g.room.deadlineMs() == std::optional<std::int64_t>(T0 + 20'000));
  CHECK(!g.subject.empty());

  std::vector<AttSubjectEvent> subj = eventsOf<AttSubjectEvent>(roomWide(g.events));
  CHECK_EQ(subj.size(), std::size_t(1));
  if (!subj.empty()) CHECK_EQ(subj[0].total, 4);

  int previews = 0;
  for (const Outgoing& o : g.events) {
    if (const AttPreviewEvent* p = std::get_if<AttPreviewEvent>(&o.event)) {
      ++previews;
      CHECK_EQ(o.toPlayerId, g.subject);  // never room-wide
      CHECK_EQ(p->options.size(), std::size_t(4));
      CHECK_EQ(p->skipsLeft, 2);
    }
  }
  CHECK_EQ(previews, 1);
  CHECK(eventsOf<AttQuestionEvent>(g.events).empty());  // not shown to everyone yet
}

TEST(att_skip_rules) {
  AttGame g(4);
  CHECK_EQ(g.room.attSkip(g.others()[0], T0), std::string("not the subject"));
  CHECK_EQ(g.room.attSkip(g.subject, T0 + 5'000), std::string(""));
  std::vector<Outgoing> out = g.room.takeOutbox();
  std::vector<AttPreviewEvent> p1 = eventsOf<AttPreviewEvent>(out);
  CHECK(!p1.empty() && p1[0].skipsLeft == 1);
  for (const Outgoing& o : out) {
    if (std::holds_alternative<AttPreviewEvent>(o.event)) CHECK_EQ(o.toPlayerId, g.subject);
  }
  CHECK(g.room.deadlineMs() == std::optional<std::int64_t>(T0 + 25'000));  // fresh 20 s
  CHECK_EQ(g.room.attSkip(g.subject, T0 + 6'000), std::string(""));
  CHECK_EQ(g.room.attSkip(g.subject, T0 + 7'000), std::string("no skips left"));
  CHECK_EQ(g.room.phase(), Phase::AttChoose);
}

TEST(att_skipped_questions_never_reach_anyone_else) {
  AttGame g(4);
  std::set<std::string> skipped;
  for (const AttPreviewEvent& p : eventsOf<AttPreviewEvent>(g.events)) skipped.insert(p.question);
  g.room.attSkip(g.subject, T0);
  std::vector<Outgoing> out = g.room.takeOutbox();
  std::string kept;
  for (const AttPreviewEvent& p : eventsOf<AttPreviewEvent>(out)) kept = p.question;
  g.room.attContinue(g.subject, T0);
  out = g.room.takeOutbox();
  std::vector<AttQuestionEvent> shown = eventsOf<AttQuestionEvent>(out);
  CHECK_EQ(shown.size(), std::size_t(1));
  if (shown.empty()) return;
  CHECK_EQ(shown[0].question, kept);           // the one they continued with
  CHECK(skipped.count(shown[0].question) == 0);  // not the skipped one
  CHECK_EQ(shown[0].subjectId, g.subject);
  for (const Outgoing& o : roomWide(out)) CHECK(!std::holds_alternative<AttPreviewEvent>(o.event));
}

TEST(att_continue_starts_pick_phase) {
  AttGame g(4);
  CHECK_EQ(g.room.attContinue(g.others()[0], T0), std::string("not the subject"));
  CHECK_EQ(g.room.attContinue(g.subject, T0 + 3'000), std::string(""));
  CHECK_EQ(g.room.phase(), Phase::AttPick);
  CHECK(g.room.deadlineMs() == std::optional<std::int64_t>(T0 + 48'000));
  CHECK_EQ(g.room.attSkip(g.subject, T0), std::string("wrong phase"));  // too late to skip
}

TEST(att_choose_deadline_uses_question_on_screen) {
  AttGame g(4);
  std::string onScreen = eventsOf<AttPreviewEvent>(g.events).at(0).question;
  g.room.tick(T0 + 20'000);
  CHECK_EQ(g.room.phase(), Phase::AttPick);
  std::vector<AttQuestionEvent> shown = eventsOf<AttQuestionEvent>(g.room.takeOutbox());
  CHECK(!shown.empty() && shown[0].question == onScreen);
}

TEST(att_pick_validation) {
  AttGame g(4);
  CHECK_EQ(g.room.attPick(g.subject, 'A', T0), std::string("wrong phase"));  // still choosing
  g.room.attContinue(g.subject, T0);
  CHECK_EQ(g.room.attPick(g.subject, std::nullopt, T0), std::string("invalid option"));  // no pass
  CHECK_EQ(g.room.attPick(g.others()[0], 'E', T0), std::string("invalid option"));
  CHECK_EQ(g.room.attPick("p999", 'A', T0), std::string("not in a room"));
  CHECK_EQ(g.room.attPick(g.others()[0], std::nullopt, T0), std::string(""));  // Pass
  CHECK_EQ(g.room.attPick(g.others()[0], 'A', T0), std::string("already submitted"));
}

TEST(att_picks_stay_secret_until_reveal) {
  AttGame g(4);
  g.room.attContinue(g.subject, T0);
  g.room.takeOutbox();
  g.room.attPick(g.subject, 'C', T0);
  std::vector<Outgoing> out = g.room.takeOutbox();
  CHECK_EQ(out.size(), std::size_t(1));  // SUBMITTED only: ids, never the pick
  CHECK_EQ(eventsOf<SubmittedEvent>(out).size(), std::size_t(1));
  CHECK(eventsOf<AttResultEvent>(out).empty());
}

TEST(att_waits_for_subject_answer) {
  AttGame g(4);
  g.room.attContinue(g.subject, T0);
  for (const std::string& id : g.others()) g.room.attPick(id, 'A', T0);
  CHECK_EQ(g.room.phase(), Phase::AttPick);  // everyone else done, subject not yet
  g.room.attPick(g.subject, 'A', T0);
  CHECK_EQ(g.room.phase(), Phase::AttReveal);
}

TEST(att_round_scores_and_reveals) {
  AttGame g(4);
  std::map<std::string, int> before = g.scores();
  std::vector<std::string> o = g.others();
  g.room.attContinue(g.subject, T0);
  g.room.takeOutbox();
  g.room.attPick(g.subject, 'B', T0);
  g.room.attPick(o[0], 'B', T0);
  g.room.attPick(o[1], 'A', T0);
  g.room.attPick(o[2], std::nullopt, T0);  // pass
  std::vector<AttResultEvent> r = eventsOf<AttResultEvent>(g.room.takeOutbox());
  CHECK_EQ(r.size(), std::size_t(1));
  if (r.empty()) return;
  CHECK(r[0].answer == std::optional<char>('B'));
  CHECK(!r[0].perfect);
  CHECK_EQ(r[0].predictions.size(), std::size_t(3));  // subject's answer isn't a prediction
  std::map<std::string, int> after = g.scores();
  CHECK_EQ(after[o[0]] - before[o[0]], 100);
  CHECK_EQ(after[o[1]] - before[o[1]], 0);
  CHECK_EQ(after[o[2]] - before[o[2]], 0);
  CHECK_EQ(after[g.subject] - before[g.subject], 0);
}

TEST(att_perfect_round_in_room) {
  AttGame g(4);
  std::map<std::string, int> before = g.scores();
  g.room.attContinue(g.subject, T0);
  g.room.attPick(g.subject, 'D', T0);
  for (const std::string& id : g.others()) g.room.attPick(id, 'D', T0);
  std::vector<AttResultEvent> r = eventsOf<AttResultEvent>(g.room.takeOutbox());
  CHECK(!r.empty() && r[0].perfect);
  for (const std::string& id : g.others()) CHECK_EQ(g.scores()[id] - before[id], 250);
}

TEST(att_subject_away_no_answer_nobody_scores) {
  AttGame g(4);
  std::map<std::string, int> before = g.scores();
  g.room.attContinue(g.subject, T0);
  g.room.disconnect(g.subject, T0);
  for (const std::string& id : g.others()) g.room.attPick(id, 'A', T0);
  CHECK_EQ(g.room.phase(), Phase::AttPick);  // still waits for the subject
  g.room.takeOutbox();
  g.room.tick(T0 + 45'000);
  CHECK_EQ(g.room.phase(), Phase::AttReveal);
  std::vector<AttResultEvent> r = eventsOf<AttResultEvent>(g.room.takeOutbox());
  CHECK(!r.empty() && !r[0].answer.has_value());
  for (const std::string& id : g.others()) CHECK_EQ(g.scores()[id], before[id]);
}

TEST(att_everyone_is_subject_once) {
  AttGame g(4);
  std::set<std::string> subjects = {g.subject};
  for (int round = 2; round <= 4; ++round) {
    g.room.next(g.room.hostId(), T0);  // choose -> pick
    g.room.next(g.room.hostId(), T0);  // pick -> reveal
    g.room.next(g.room.hostId(), T0);  // reveal -> next subject
    CHECK_EQ(g.room.phase(), Phase::AttChoose);
    CHECK_EQ(g.room.round(), round);
    g.read(g.room.takeOutbox());
    subjects.insert(g.subject);
  }
  CHECK_EQ(subjects.size(), std::size_t(4));
  g.room.next(g.room.hostId(), T0);
  g.room.next(g.room.hostId(), T0);
  g.room.next(g.room.hostId(), T0);  // after the last reveal: awards or leaderboard
  CHECK(g.room.phase() == Phase::Awards || g.room.phase() == Phase::MatchOver);
}

TEST(att_away_player_gets_a_later_turn) {
  AttGame g(5);
  // Someone who hasn't been the subject goes away before the next pick.
  std::string away = g.others()[0];
  g.room.disconnect(away, T0);
  std::set<std::string> subjects = {g.subject};
  for (int round = 2; round <= 4; ++round) {
    for (int i = 0; i < 3; ++i) g.room.next(g.room.hostId(), T0);
    g.read(g.room.takeOutbox());
    subjects.insert(g.subject);
  }
  CHECK(subjects.count(away) == 0);  // passed over while away
  g.room.rejoin(g.player(away).token, T0);
  for (int i = 0; i < 3; ++i) g.room.next(g.room.hostId(), T0);
  g.read(g.room.takeOutbox());
  CHECK_EQ(g.room.round(), 5);
  CHECK_EQ(g.subject, away);  // the last turn is theirs
}

TEST(att_still_away_at_the_end_is_skipped) {
  AttGame g(5);
  std::string away = g.others()[0];
  g.room.disconnect(away, T0 + 1);
  for (int round = 2; round <= 4; ++round) {
    for (int i = 0; i < 3; ++i) g.room.next(g.room.hostId(), T0 + 30'000);
  }
  // Round 4 revealed; only the away player hasn't had a turn.
  for (int i = 0; i < 3; ++i) g.room.next(g.room.hostId(), T0 + 30'000);
  CHECK(g.room.phase() == Phase::Awards || g.room.phase() == Phase::MatchOver);
}

TEST(att_rejoin_catch_up) {
  AttGame g(4);
  std::string other = g.others()[0];
  g.room.disconnect(g.subject, T0);
  g.room.disconnect(other, T0);
  g.room.takeOutbox();
  g.room.rejoin(g.player(g.subject).token, T0 + 1'000);
  std::vector<Outgoing> mine = g.room.takeOutbox();
  CHECK_EQ(eventsOf<AttPreviewEvent>(mine).size(), std::size_t(1));  // subject gets the preview back
  g.room.rejoin(g.player(other).token, T0 + 1'000);
  std::vector<Outgoing> theirs = g.room.takeOutbox();
  CHECK(eventsOf<AttPreviewEvent>(theirs).empty());  // others never do
  CHECK_EQ(eventsOf<AttSubjectEvent>(theirs).size(), std::size_t(1));
}

TEST(att_display_snapshot_has_no_preview) {
  AttGame g(4);
  for (const Event& e : g.room.displaySnapshot()) CHECK(!std::holds_alternative<AttPreviewEvent>(e));
  g.room.attContinue(g.subject, T0);
  bool hasQuestion = false;
  for (const Event& e : g.room.displaySnapshot()) {
    if (std::holds_alternative<AttQuestionEvent>(e)) hasQuestion = true;
  }
  CHECK(hasQuestion);
}

TEST(att_flag_off_leaves_game_3_out) {
  Room room("ABCD", false, 42, /*withAroundTheTable=*/false);
  std::vector<JoinResult> ps;
  for (int i = 0; i < 4; ++i) ps.push_back(room.addPlayer("P" + std::to_string(i)));
  room.startMatch(ps[0].playerId, T0);
  std::vector<MatchStartedEvent> started = eventsOf<MatchStartedEvent>(room.takeOutbox());
  CHECK(!started.empty() && started[0].attRounds == 0);
  for (int i = 0; i < 60 && room.phase() != Phase::MatchOver; ++i) {
    room.next(room.hostId(), T0);
    CHECK(room.game() != Game::Att);
  }
  CHECK_EQ(room.phase(), Phase::MatchOver);
}
