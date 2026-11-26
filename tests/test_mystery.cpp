#include <set>

#include "game/mystery.h"
#include "game/room.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace feast;
using namespace testhelp;

// ================================================================ pure rules

namespace {
Vote vote(const std::string& voter, const char* target) {
  return Vote{voter, target ? std::optional<std::string>(target) : std::nullopt};
}
}  // namespace

TEST(tally_no_votes_nobody_accused) {
  VoteTally t = tallyVotes({}, "imp");
  CHECK(!t.accusedId.has_value());
  CHECK(!t.caught);
}

TEST(tally_unique_top_impostor_is_caught) {
  VoteTally t = tallyVotes({vote("a", "imp"), vote("b", "imp"), vote("imp", "a")}, "imp");
  CHECK(t.accusedId == std::optional<std::string>("imp"));
  CHECK(t.caught);
}

TEST(tally_unique_top_innocent_impostor_escapes) {
  VoteTally t = tallyVotes({vote("a", "b"), vote("imp", "b"), vote("b", "a")}, "imp");
  CHECK(t.accusedId == std::optional<std::string>("b"));
  CHECK(!t.caught);
}

TEST(tally_skip_most_votes_nobody_accused) {
  VoteTally t = tallyVotes({vote("a", nullptr), vote("b", nullptr), vote("c", "imp")}, "imp");
  CHECK(!t.accusedId.has_value());
  CHECK(!t.caught);
}

TEST(tally_skip_ties_for_most_nobody_accused) {
  VoteTally t = tallyVotes({vote("a", nullptr), vote("b", "imp")}, "imp");
  CHECK(!t.accusedId.has_value());
  CHECK(!t.caught);
}

TEST(tally_two_players_tie_nobody_accused) {
  VoteTally t = tallyVotes(
      {vote("a", "imp"), vote("b", "imp"), vote("c", "a"), vote("imp", "a")}, "imp");
  CHECK(!t.accusedId.has_value());
  CHECK(!t.caught);
}

TEST(tally_skip_counted_but_player_can_still_win) {
  VoteTally t = tallyVotes({vote("a", "imp"), vote("b", "imp"), vote("c", nullptr)}, "imp");
  CHECK(t.caught);
}

TEST(guess_matching_trims_and_ignores_case) {
  CHECK(isCorrectGuess("  pumpkin PIE ", "Pumpkin Pie"));
  CHECK(!isCorrectGuess("pumpkinpie", "Pumpkin Pie"));
  CHECK(!isCorrectGuess("Apple Pie", "Pumpkin Pie"));
}

namespace {
int pointsFor(const std::vector<PointsDelta>& points, const std::string& id) {
  for (const PointsDelta& d : points) {
    if (d.playerId == id) return d.delta;
  }
  return -1;
}
const std::vector<std::string> kRound = {"a", "b", "imp", "c"};
}  // namespace

TEST(points_caught_no_guess) {
  auto p = mysteryPoints(kRound, "imp", true, false);
  CHECK_EQ(pointsFor(p, "a"), 100);
  CHECK_EQ(pointsFor(p, "c"), 100);
  CHECK_EQ(pointsFor(p, "imp"), 0);
  CHECK_EQ(p.size(), std::size_t(4));
}

TEST(points_caught_then_correct_guess) {
  auto p = mysteryPoints(kRound, "imp", true, true);
  CHECK_EQ(pointsFor(p, "a"), 100);
  CHECK_EQ(pointsFor(p, "imp"), 150);
}

TEST(points_escaped) {
  auto p = mysteryPoints(kRound, "imp", false, false);
  CHECK_EQ(pointsFor(p, "a"), 0);
  CHECK_EQ(pointsFor(p, "imp"), 200);
}

TEST(points_escaped_then_correct_guess) {
  auto p = mysteryPoints(kRound, "imp", false, true);
  CHECK_EQ(pointsFor(p, "a"), 0);
  CHECK_EQ(pointsFor(p, "imp"), 250);
}

// ================================================================ in a Room

namespace {

// Starts a match and reads the dealt words back out of the outbox.
struct MysteryGame : Setup {
  std::string impostor;
  std::string mainWord;
  std::string impostorWord;
  std::vector<Outgoing> startEvents;

  explicit MysteryGame(int count) : Setup(count) {
    room.startMatch(host(), T0);
    startEvents = room.takeOutbox();
    readWords(startEvents);
  }

  void readWords(const std::vector<Outgoing>& out) {
    for (const Outgoing& o : out) {
      if (const YourWordEvent* w = std::get_if<YourWordEvent>(&o.event)) {
        if (w->isImpostor) {
          impostor = o.toPlayerId;
          impostorWord = w->word;
        } else {
          mainWord = w->word;
        }
      }
    }
  }

  // Players other than the impostor, in join order.
  std::vector<std::string> innocents() const {
    std::vector<std::string> ids;
    for (const JoinResult& p : players) {
      if (p.playerId != impostor) ids.push_back(p.playerId);
    }
    return ids;
  }

  const JoinResult& player(const std::string& id) const {
    for (const JoinResult& p : players) {
      if (p.playerId == id) return p;
    }
    return players[0];
  }

  void toVote() { room.next(room.hostId(), T0); }  // discuss -> vote
};

int scoreOf(const Room& room, const std::string& id) { return room.findPlayer(id)->score; }

}  // namespace

TEST(each_player_privately_gets_a_word_one_impostor) {
  MysteryGame g(5);
  std::vector<Outgoing> words;
  for (const Outgoing& o : g.startEvents) {
    if (std::holds_alternative<YourWordEvent>(o.event)) words.push_back(o);
  }
  CHECK_EQ(words.size(), std::size_t(5));

  std::set<std::string> recipients;
  int impostors = 0;
  for (const Outgoing& o : words) {
    CHECK(!o.toPlayerId.empty());  // never room-wide
    recipients.insert(o.toPlayerId);
    const YourWordEvent& w = std::get<YourWordEvent>(o.event);
    if (w.isImpostor) {
      ++impostors;
      CHECK_EQ(w.word, g.impostorWord);
    } else {
      CHECK_EQ(w.word, g.mainWord);
    }
  }
  CHECK_EQ(recipients.size(), std::size_t(5));
  CHECK_EQ(impostors, 1);
  CHECK(g.mainWord != g.impostorWord);
}

TEST(room_wide_events_at_start_reveal_nothing_secret) {
  MysteryGame g(4);
  for (const Outgoing& o : g.startEvents) {
    if (!o.toPlayerId.empty()) continue;
    CHECK(!std::holds_alternative<YourWordEvent>(o.event));
    CHECK(!std::holds_alternative<RoundResultEvent>(o.event));
  }
}

TEST(no_player_is_impostor_twice_in_3_rounds) {
  MysteryGame g(4);
  std::set<std::string> impostors = {g.impostor};
  std::set<std::string> mainWords = {g.mainWord};
  for (int round = 2; round <= 3; ++round) {
    nextUntil(g, Phase::MysteryReveal);
    g.room.next(g.host(), T0);
    g.readWords(g.room.takeOutbox());
    impostors.insert(g.impostor);
    mainWords.insert(g.mainWord);
  }
  CHECK_EQ(impostors.size(), std::size_t(3));
  CHECK_EQ(mainWords.size(), std::size_t(3));  // unused word pair each round
}

// ---------------------------------------------------------------- voting

TEST(vote_only_in_vote_phase) {
  MysteryGame g(4);
  CHECK_EQ(g.room.castVote(g.id(1), std::nullopt, T0), std::string("wrong phase"));
}

TEST(vote_validation) {
  MysteryGame g(4);
  g.toVote();
  CHECK_EQ(g.room.castVote(g.id(1), g.id(1), T0), std::string("cannot vote for yourself"));
  CHECK_EQ(g.room.castVote(g.id(1), std::string("p999"), T0), std::string("invalid target"));
  CHECK_EQ(g.room.castVote("p999", std::nullopt, T0), std::string("not in the round"));
  CHECK_EQ(g.room.castVote(g.id(1), std::nullopt, T0), std::string(""));
  CHECK_EQ(g.room.castVote(g.id(1), g.id(2), T0), std::string("already submitted"));
}

TEST(vote_broadcasts_submitted_ids_only) {
  MysteryGame g(4);
  g.toVote();
  g.room.takeOutbox();
  g.room.castVote(g.id(1), g.id(2), T0);
  std::vector<Outgoing> out = g.room.takeOutbox();
  CHECK_EQ(out.size(), std::size_t(1));
  std::vector<SubmittedEvent> sub = eventsOf<SubmittedEvent>(out);
  CHECK_EQ(sub.size(), std::size_t(1));
  if (sub.empty()) return;
  CHECK_EQ(sub[0].playerIds, std::vector<std::string>{g.id(1)});
  CHECK(out[0].toPlayerId.empty());
  CHECK_EQ(g.room.phase(), Phase::MysteryVote);  // others still due
}

TEST(vote_ends_when_all_connected_voted) {
  MysteryGame g(4);
  g.toVote();
  for (const JoinResult& p : g.players) g.room.castVote(p.playerId, std::nullopt, T0);
  CHECK_EQ(g.room.phase(), Phase::MysteryGuess);
  CHECK(g.room.deadlineMs() == std::optional<std::int64_t>(T0 + 30'000));
}

TEST(vote_does_not_wait_for_disconnected_player) {
  MysteryGame g(5);
  std::vector<std::string> innocents = g.innocents();
  g.room.disconnect(innocents[0], T0);
  g.toVote();
  for (const JoinResult& p : g.players) {
    if (p.playerId != innocents[0]) g.room.castVote(p.playerId, std::nullopt, T0);
  }
  CHECK_EQ(g.room.phase(), Phase::MysteryGuess);
}

TEST(last_non_voter_disconnecting_ends_vote) {
  MysteryGame g(4);
  std::vector<std::string> innocents = g.innocents();
  g.toVote();
  for (const JoinResult& p : g.players) {
    if (p.playerId != innocents[0]) g.room.castVote(p.playerId, std::nullopt, T0);
  }
  CHECK_EQ(g.room.phase(), Phase::MysteryVote);
  g.room.disconnect(innocents[0], T0);
  CHECK_EQ(g.room.phase(), Phase::MysteryGuess);
}

TEST(vote_result_is_sent_to_everyone_with_all_votes) {
  MysteryGame g(4);
  g.toVote();
  g.room.takeOutbox();
  for (const std::string& id : g.innocents()) g.room.castVote(id, g.impostor, T0);
  g.room.castVote(g.impostor, g.innocents()[0], T0);

  std::vector<Outgoing> out = g.room.takeOutbox();
  std::vector<VoteResultEvent> results = eventsOf<VoteResultEvent>(out);
  CHECK_EQ(results.size(), std::size_t(1));
  if (results.empty()) return;
  CHECK_EQ(results[0].votes.size(), std::size_t(4));
  CHECK(results[0].accusedId == std::optional<std::string>(g.impostor));
  CHECK(results[0].caught);
  for (const Outgoing& o : out) {
    if (std::holds_alternative<VoteResultEvent>(o.event)) CHECK(o.toPlayerId.empty());
  }
}

// ---------------------------------------------------------------- guessing

TEST(guess_validation) {
  MysteryGame g(4);
  CHECK_EQ(g.room.guessWord(g.impostor, "x", T0), std::string("wrong phase"));
  nextUntil(g, Phase::MysteryGuess);
  CHECK_EQ(g.room.guessWord(g.innocents()[0], "x", T0), std::string("not the impostor"));
  CHECK_EQ(g.room.guessWord(g.impostor, "   ", T0), std::string("invalid guess"));
  CHECK_EQ(g.room.guessWord(g.impostor, std::string(31, 'x'), T0), std::string("invalid guess"));
  CHECK_EQ(g.room.phase(), Phase::MysteryGuess);
  CHECK_EQ(g.room.guessWord(g.impostor, std::string(30, 'x'), T0), std::string(""));
  CHECK_EQ(g.room.guessWord(g.impostor, "again", T0), std::string("wrong phase"));  // once only
}

TEST(guess_ends_phase_without_naming_impostor_first) {
  MysteryGame g(4);
  nextUntil(g, Phase::MysteryGuess);
  g.room.takeOutbox();
  g.room.guessWord(g.impostor, g.mainWord, T0);
  CHECK_EQ(g.room.phase(), Phase::MysteryReveal);
  // No SUBMITTED for a guess: it would tell the room who the impostor is.
  CHECK(eventsOf<SubmittedEvent>(g.room.takeOutbox()).empty());
}

// ---------------------------------------------------------------- scoring

TEST(caught_then_correct_guess_scores) {
  MysteryGame g(4);
  g.toVote();
  for (const std::string& id : g.innocents()) g.room.castVote(id, g.impostor, T0);
  g.room.castVote(g.impostor, std::nullopt, T0);
  g.room.takeOutbox();
  g.room.guessWord(g.impostor, "  " + g.mainWord + " ", T0);

  for (const std::string& id : g.innocents()) CHECK_EQ(scoreOf(g.room, id), 100);
  CHECK_EQ(scoreOf(g.room, g.impostor), 150);

  std::vector<Outgoing> out = g.room.takeOutbox();
  std::vector<RoundResultEvent> results = eventsOf<RoundResultEvent>(out);
  CHECK_EQ(results.size(), std::size_t(1));
  if (results.empty()) return;
  CHECK_EQ(results[0].impostorId, g.impostor);
  CHECK_EQ(results[0].mainWord, g.mainWord);
  CHECK_EQ(results[0].impostorWord, g.impostorWord);
  CHECK(results[0].guessCorrect == std::optional<bool>(true));
  CHECK(!results[0].voided);
  // ROOM_STATE after the result carries the new scores.
  std::vector<RoomStateEvent> states = eventsOf<RoomStateEvent>(out);
  CHECK(!states.empty());
  if (!states.empty()) {
    for (const PublicPlayer& p : states.back().players) {
      CHECK_EQ(p.score, p.id == g.impostor ? 150 : 100);
    }
  }
}

TEST(caught_wrong_guess_scores) {
  MysteryGame g(4);
  g.toVote();
  for (const std::string& id : g.innocents()) g.room.castVote(id, g.impostor, T0);
  g.room.castVote(g.impostor, std::nullopt, T0);
  g.room.guessWord(g.impostor, "definitely wrong", T0);
  for (const std::string& id : g.innocents()) CHECK_EQ(scoreOf(g.room, id), 100);
  CHECK_EQ(scoreOf(g.room, g.impostor), 0);
}

TEST(skip_wins_impostor_escapes_and_guesses) {
  MysteryGame g(4);
  g.toVote();
  for (const JoinResult& p : g.players) g.room.castVote(p.playerId, std::nullopt, T0);
  g.room.guessWord(g.impostor, g.mainWord, T0);
  for (const std::string& id : g.innocents()) CHECK_EQ(scoreOf(g.room, id), 0);
  CHECK_EQ(scoreOf(g.room, g.impostor), 250);
}

TEST(innocent_accused_impostor_escapes) {
  MysteryGame g(4);
  std::vector<std::string> innocents = g.innocents();
  g.toVote();
  g.room.castVote(g.impostor, innocents[0], T0);
  g.room.castVote(innocents[1], innocents[0], T0);
  g.room.castVote(innocents[2], innocents[0], T0);
  g.room.castVote(innocents[0], g.impostor, T0);
  g.room.next(g.host(), T0);  // guess phase ends without a guess
  CHECK_EQ(scoreOf(g.room, g.impostor), 200);
  CHECK_EQ(scoreOf(g.room, innocents[1]), 0);
}

TEST(no_votes_by_deadline_impostor_escapes) {
  MysteryGame g(4);
  std::int64_t t = T0 + 100'000;
  g.room.tick(t);  // discuss over
  t += 45'000;
  g.room.tick(t);  // vote over, nobody voted
  CHECK_EQ(g.room.phase(), Phase::MysteryGuess);
  t += 30'000;
  g.room.tick(t);  // guess over, no guess
  CHECK_EQ(g.room.phase(), Phase::MysteryReveal);
  CHECK_EQ(scoreOf(g.room, g.impostor), 200);
}

TEST(disconnected_innocent_still_earns_points) {
  MysteryGame g(5);
  std::vector<std::string> innocents = g.innocents();
  g.toVote();
  g.room.castVote(innocents[0], g.impostor, T0);
  g.room.disconnect(innocents[0], T0);
  for (std::size_t i = 1; i < innocents.size(); ++i) g.room.castVote(innocents[i], g.impostor, T0);
  g.room.castVote(g.impostor, std::nullopt, T0);
  g.room.next(g.room.hostId(), T0);  // innocents[0] may have been the host
  CHECK_EQ(scoreOf(g.room, innocents[0]), 100);
}

TEST(scores_carry_across_rounds_and_reset_in_lobby) {
  MysteryGame g(4);
  g.toVote();
  for (const JoinResult& p : g.players) g.room.castVote(p.playerId, std::nullopt, T0);
  g.room.next(g.host(), T0);  // reveal: impostor +200
  std::string firstImpostor = g.impostor;
  CHECK_EQ(scoreOf(g.room, firstImpostor), 200);

  g.room.next(g.host(), T0);  // round 2
  CHECK_EQ(scoreOf(g.room, firstImpostor), 200);

  g.room.takeOutbox();
  nextUntil(g, Phase::MatchOver);
  std::vector<MatchOverEvent> overs = eventsOf<MatchOverEvent>(g.room.takeOutbox());
  CHECK_EQ(overs.size(), std::size_t(1));
  if (!overs.empty()) {
    CHECK(overs[0].winnerIds.size() >= std::size_t(1));
    CHECK(overs[0].leaderboard.front().score >= 200);
  }

  g.room.next(g.host(), T0);  // back to lobby
  for (const Player& p : g.room.players()) CHECK_EQ(p.score, 0);
}

// ---------------------------------------------------------------- disconnects

TEST(impostor_leaves_during_discuss_voids_round) {
  MysteryGame g(4);
  g.room.disconnect(g.impostor, T0);
  CHECK_EQ(g.room.phase(), Phase::MysteryReveal);
  std::vector<RoundResultEvent> results = eventsOf<RoundResultEvent>(g.room.takeOutbox());
  CHECK_EQ(results.size(), std::size_t(1));
  if (results.empty()) return;
  CHECK(results[0].voided);
  CHECK(results[0].points.empty());
  CHECK_EQ(results[0].mainWord, g.mainWord);  // words still revealed
  for (const Player& p : g.room.players()) CHECK_EQ(p.score, 0);
}

TEST(impostor_leaves_during_vote_voids_round) {
  MysteryGame g(4);
  g.toVote();
  for (const std::string& id : g.innocents()) g.room.castVote(id, g.impostor, T0);
  g.room.disconnect(g.impostor, T0);
  CHECK_EQ(g.room.phase(), Phase::MysteryReveal);
  for (const Player& p : g.room.players()) CHECK_EQ(p.score, 0);
}

TEST(impostor_leaves_during_guess_counts_as_no_guess) {
  MysteryGame g(4);
  g.toVote();
  for (const JoinResult& p : g.players) g.room.castVote(p.playerId, std::nullopt, T0);
  CHECK_EQ(g.room.phase(), Phase::MysteryGuess);
  g.room.disconnect(g.impostor, T0);
  CHECK_EQ(g.room.phase(), Phase::MysteryReveal);
  CHECK_EQ(scoreOf(g.room, g.impostor), 200);  // vote result still applies
}

// ---------------------------------------------------------------- rejoin

TEST(rejoin_mid_round_resends_word_privately) {
  MysteryGame g(4);
  std::string who = g.innocents()[0];
  g.room.disconnect(who, T0);
  g.room.takeOutbox();
  g.room.rejoin(g.player(who).token, T0);
  std::vector<Outgoing> out = g.room.takeOutbox();
  bool gotWord = false;
  for (const Outgoing& o : out) {
    if (const YourWordEvent* w = std::get_if<YourWordEvent>(&o.event)) {
      CHECK_EQ(o.toPlayerId, who);
      CHECK_EQ(w->word, g.mainWord);
      CHECK(!w->isImpostor);
      gotWord = true;
    }
  }
  CHECK(gotWord);
}

TEST(rejoin_during_vote_gets_submitted_and_can_vote) {
  MysteryGame g(4);
  std::string who = g.innocents()[0];
  g.toVote();
  g.room.castVote(g.impostor, std::nullopt, T0);
  g.room.disconnect(who, T0);
  g.room.takeOutbox();
  g.room.rejoin(g.player(who).token, T0);
  std::vector<Outgoing> out = g.room.takeOutbox();
  bool gotSubmitted = false;
  for (const Outgoing& o : out) {
    if (const SubmittedEvent* s = std::get_if<SubmittedEvent>(&o.event)) {
      CHECK_EQ(o.toPlayerId, who);
      CHECK_EQ(s->playerIds, std::vector<std::string>{g.impostor});
      gotSubmitted = true;
    }
  }
  CHECK(gotSubmitted);
  CHECK_EQ(g.room.castVote(who, std::nullopt, T0), std::string(""));
}

TEST(rejoin_during_reveal_gets_results) {
  MysteryGame g(4);
  std::string who = g.innocents()[0];
  nextUntil(g, Phase::MysteryReveal);
  g.room.disconnect(who, T0);
  g.room.takeOutbox();
  g.room.rejoin(g.player(who).token, T0);
  std::vector<Outgoing> out = g.room.takeOutbox();
  CHECK_EQ(eventsOf<VoteResultEvent>(out).size(), std::size_t(1));
  CHECK_EQ(eventsOf<RoundResultEvent>(out).size(), std::size_t(1));
  CHECK(eventsOf<YourWordEvent>(out).empty());
}
