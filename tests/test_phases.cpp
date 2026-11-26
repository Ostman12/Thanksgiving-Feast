#include <vector>

#include "game/room.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace feast;

using namespace testhelp;

// ---------------------------------------------------------------- START_MATCH

TEST(start_needs_host) {
  Setup s(4);
  CHECK_EQ(s.room.startMatch(s.id(1), T0), std::string("not the host"));
  CHECK_EQ(s.room.phase(), Phase::Lobby);
}

TEST(start_needs_4_players) {
  Setup s(3);
  CHECK_EQ(s.room.startMatch(s.host(), T0), std::string("not enough players"));
  CHECK_EQ(s.room.phase(), Phase::Lobby);
}

TEST(start_needs_3_players_in_test_mode) {
  Setup two(2, true);
  CHECK_EQ(two.room.startMatch(two.host(), T0), std::string("not enough players"));
  Setup three(3, true);
  CHECK_EQ(three.room.startMatch(three.host(), T0), std::string(""));
}

TEST(start_counts_only_connected_players) {
  Setup s(4);
  s.room.disconnect(s.id(3), T0);
  CHECK_EQ(s.room.startMatch(s.host(), T0), std::string("not enough players"));
}

TEST(start_enters_mystery_discuss_round_1) {
  Setup s(4);
  CHECK_EQ(s.room.startMatch(s.host(), T0), std::string(""));
  CHECK_EQ(s.room.phase(), Phase::MysteryDiscuss);
  CHECK_EQ(s.room.game(), Game::Mystery);
  CHECK_EQ(s.room.round(), 1);
  CHECK(s.room.deadlineMs() == std::optional<std::int64_t>(T0 + 100'000));

  // Room-wide: exactly MATCH_STARTED, ROOM_STATE, PHASE, in that order.
  // Everything else is private (each player's YOUR_WORD).
  std::vector<Outgoing> roomWide;
  for (const Outgoing& o : s.room.takeOutbox()) {
    if (o.toPlayerId.empty()) {
      roomWide.push_back(o);
    } else {
      CHECK(std::holds_alternative<YourWordEvent>(o.event));
    }
  }
  CHECK_EQ(roomWide.size(), std::size_t(3));
  if (roomWide.size() != 3) return;
  CHECK(std::holds_alternative<MatchStartedEvent>(roomWide[0].event));
  CHECK(std::holds_alternative<RoomStateEvent>(roomWide[1].event));
  CHECK(std::holds_alternative<PhaseEvent>(roomWide[2].event));
  if (!std::holds_alternative<MatchStartedEvent>(roomWide[0].event) ||
      !std::holds_alternative<PhaseEvent>(roomWide[2].event)) {
    return;
  }

  MatchStartedEvent started = std::get<MatchStartedEvent>(roomWide[0].event);
  CHECK_EQ(started.mysteryRounds, 3);
  CHECK_EQ(started.wyrRounds, 3);
  PhaseEvent phase = std::get<PhaseEvent>(roomWide[2].event);
  CHECK_EQ(phase.phase, Phase::MysteryDiscuss);
  CHECK_EQ(phase.round, 1);
  CHECK(phase.deadlineMs == std::optional<std::int64_t>(T0 + 100'000));
}

TEST(start_twice_rejected) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  CHECK_EQ(s.room.startMatch(s.host(), T0), std::string("match already started"));
}

TEST(cannot_join_after_match_started) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  CHECK_EQ(s.room.addPlayer("Late").error, std::string("match already started"));
}

// ---------------------------------------------------------------- deadlines

TEST(deadlines_advance_timed_phases) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);

  s.room.tick(T0 + 99'999);
  CHECK_EQ(s.room.phase(), Phase::MysteryDiscuss);  // not yet

  std::int64_t t = T0 + 100'000;
  s.room.tick(t);
  CHECK_EQ(s.room.phase(), Phase::MysteryVote);
  CHECK(s.room.deadlineMs() == std::optional<std::int64_t>(t + 45'000));

  t += 45'000;
  s.room.tick(t);
  CHECK_EQ(s.room.phase(), Phase::MysteryGuess);
  CHECK(s.room.deadlineMs() == std::optional<std::int64_t>(t + 30'000));

  t += 30'000;
  s.room.tick(t);
  CHECK_EQ(s.room.phase(), Phase::MysteryReveal);
  CHECK(!s.room.deadlineMs().has_value());
}

TEST(reveal_waits_for_host) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  nextUntil(s, Phase::MysteryReveal);
  s.room.tick(T0 + 10'000'000);
  CHECK_EQ(s.room.phase(), Phase::MysteryReveal);
}

TEST(late_tick_advances_only_one_phase) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  s.room.tick(T0 + 10'000'000);  // e.g. server stalled
  CHECK_EQ(s.room.phase(), Phase::MysteryVote);
}

TEST(wyr_choose_has_30s_deadline) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  nextUntil(s, Phase::WyrChoose);
  CHECK(s.room.deadlineMs() == std::optional<std::int64_t>(T0 + 30'000));
}

// ---------------------------------------------------------------- NEXT

TEST(next_needs_host) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  CHECK_EQ(s.room.next(s.id(1), T0), std::string("not the host"));
  CHECK_EQ(s.room.phase(), Phase::MysteryDiscuss);
}

TEST(next_rejected_in_lobby) {
  Setup s(4);
  CHECK_EQ(s.room.next(s.host(), T0), std::string("wrong phase"));
}

TEST(full_match_order) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);

  struct Step { Phase phase; Game game; int round; };
  const std::vector<Step> expected = {
      {Phase::MysteryDiscuss, Game::Mystery, 1}, {Phase::MysteryVote, Game::Mystery, 1},
      {Phase::MysteryGuess, Game::Mystery, 1},   {Phase::MysteryReveal, Game::Mystery, 1},
      {Phase::MysteryDiscuss, Game::Mystery, 2}, {Phase::MysteryVote, Game::Mystery, 2},
      {Phase::MysteryGuess, Game::Mystery, 2},   {Phase::MysteryReveal, Game::Mystery, 2},
      {Phase::MysteryDiscuss, Game::Mystery, 3}, {Phase::MysteryVote, Game::Mystery, 3},
      {Phase::MysteryGuess, Game::Mystery, 3},   {Phase::MysteryReveal, Game::Mystery, 3},
      {Phase::WyrChoose, Game::Wyr, 1},          {Phase::WyrReveal, Game::Wyr, 1},
      {Phase::WyrChoose, Game::Wyr, 2},          {Phase::WyrReveal, Game::Wyr, 2},
      {Phase::WyrChoose, Game::Wyr, 3},          {Phase::WyrReveal, Game::Wyr, 3},
      {Phase::MatchOver, Game::None, 0},         {Phase::Lobby, Game::None, 0},
  };
  for (std::size_t i = 0; i < expected.size(); ++i) {
    CHECK_EQ(s.room.phase(), expected[i].phase);
    CHECK_EQ(s.room.game(), expected[i].game);
    CHECK_EQ(s.room.round(), expected[i].round);
    if (i + 1 < expected.size()) CHECK_EQ(s.room.next(s.host(), T0), std::string(""));
  }
}

TEST(match_over_sends_leaderboard_ties_are_co_champions) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  s.room.takeOutbox();
  s.room.endMatch(s.host());  // Before anyone has scored.

  std::vector<MatchOverEvent> overs = eventsOf<MatchOverEvent>(s.room.takeOutbox());
  CHECK_EQ(overs.size(), std::size_t(1));
  if (overs.empty()) return;
  // Everyone has 0, so all four tie.
  CHECK_EQ(overs[0].winnerIds.size(), std::size_t(4));
  CHECK_EQ(overs[0].leaderboard.size(), std::size_t(4));
}

TEST(can_start_again_after_returning_to_lobby) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  nextUntil(s, Phase::MatchOver);
  s.room.next(s.host(), T0);
  CHECK_EQ(s.room.phase(), Phase::Lobby);
  CHECK_EQ(s.room.startMatch(s.host(), T0), std::string(""));
  CHECK_EQ(s.room.round(), 1);
}

// ---------------------------------------------------------------- END_MATCH

TEST(end_match_goes_to_match_over) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  s.room.takeOutbox();
  CHECK_EQ(s.room.endMatch(s.host()), std::string(""));
  CHECK_EQ(s.room.phase(), Phase::MatchOver);
  CHECK(!s.room.deadlineMs().has_value());
  CHECK_EQ(eventsOf<MatchOverEvent>(s.room.takeOutbox()).size(), std::size_t(1));
}

TEST(end_match_needs_host_and_running_match) {
  Setup s(4);
  CHECK_EQ(s.room.endMatch(s.host()), std::string("wrong phase"));  // lobby
  s.room.startMatch(s.host(), T0);
  CHECK_EQ(s.room.endMatch(s.id(1)), std::string("not the host"));
  s.room.endMatch(s.host());
  CHECK_EQ(s.room.endMatch(s.host()), std::string("wrong phase"));  // match_over
}

// ---------------------------------------------------------------- pause

TEST(too_few_connected_mid_round_does_not_pause) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  // Two non-impostors leave (the impostor leaving would void the round).
  std::vector<std::string> innocents = nonImpostors(s.room.takeOutbox());
  s.room.disconnect(innocents[1], T0);
  s.room.disconnect(innocents[2], T0);
  CHECK_EQ(s.room.connectedCount(), std::size_t(2));
  CHECK_EQ(s.room.phase(), Phase::MysteryDiscuss);
  s.room.next(s.room.hostId(), T0);  // Host may have been one who left.
  CHECK_EQ(s.room.phase(), Phase::MysteryVote);
}

TEST(too_few_connected_at_round_start_pauses) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  nextUntil(s, Phase::MysteryReveal);
  s.room.disconnect(s.id(2), T0);
  s.room.disconnect(s.id(3), T0);
  s.room.next(s.host(), T0);

  CHECK_EQ(s.room.phase(), Phase::Paused);
  CHECK_EQ(s.room.game(), Game::Mystery);
  CHECK_EQ(s.room.round(), 2);  // the round waiting to start
  CHECK(!s.room.deadlineMs().has_value());
  CHECK_EQ(s.room.next(s.host(), T0), std::string("wrong phase"));
  s.room.tick(T0 + 10'000'000);
  CHECK_EQ(s.room.phase(), Phase::Paused);
}

TEST(rejoin_resumes_paused_match) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  nextUntil(s, Phase::MysteryReveal);
  s.room.disconnect(s.id(2), T0);
  s.room.disconnect(s.id(3), T0);
  s.room.next(s.host(), T0);
  CHECK_EQ(s.room.phase(), Phase::Paused);

  std::int64_t later = T0 + 60'000;
  CHECK(s.room.rejoin(s.players[2].token, later).ok());
  CHECK_EQ(s.room.phase(), Phase::MysteryDiscuss);
  CHECK_EQ(s.room.round(), 2);
  CHECK(s.room.deadlineMs() == std::optional<std::int64_t>(later + 100'000));
}

TEST(pause_before_wyr_resumes_into_wyr) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  for (int round = 1; round <= 3; ++round) {
    s.room.next(s.host(), T0);  // leave discuss
    nextUntil(s, Phase::MysteryReveal);
  }
  s.room.disconnect(s.id(2), T0);
  s.room.disconnect(s.id(3), T0);
  s.room.next(s.host(), T0);
  CHECK_EQ(s.room.phase(), Phase::Paused);
  CHECK_EQ(s.room.game(), Game::Wyr);
  CHECK_EQ(s.room.round(), 1);

  s.room.rejoin(s.players[3].token, T0);
  CHECK_EQ(s.room.phase(), Phase::WyrChoose);
}

TEST(host_can_end_match_while_paused) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  nextUntil(s, Phase::MysteryReveal);
  s.room.disconnect(s.id(2), T0);
  s.room.disconnect(s.id(3), T0);
  s.room.next(s.host(), T0);
  CHECK_EQ(s.room.endMatch(s.host()), std::string(""));
  CHECK_EQ(s.room.phase(), Phase::MatchOver);
}

// ---------------------------------------------------------------- events

TEST(joins_and_disconnects_broadcast_room_state) {
  Room room("ABCD", false, 42);
  JoinResult ann = room.addPlayer("Ann");
  room.disconnect(ann.playerId, T0);
  std::vector<Outgoing> out = room.takeOutbox();
  CHECK_EQ(out.size(), std::size_t(2));
  CHECK_EQ(eventsOf<RoomStateEvent>(out).size(), std::size_t(2));
  for (const Outgoing& o : out) CHECK(o.toPlayerId.empty());
  CHECK(room.takeOutbox().empty());  // takeOutbox clears the queue
}

TEST(rejoin_sends_current_phase_to_that_player) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  s.room.disconnect(s.id(1), T0);
  s.room.takeOutbox();

  s.room.rejoin(s.players[1].token, T0 + 5'000);
  std::vector<Outgoing> out = s.room.takeOutbox();
  CHECK(!out.empty());
  if (out.empty()) return;
  const Outgoing& last = out.back();
  CHECK_EQ(last.toPlayerId, s.id(1));
  const PhaseEvent* phase = std::get_if<PhaseEvent>(&last.event);
  CHECK(phase != nullptr);
  if (phase == nullptr) return;
  CHECK_EQ(phase->phase, Phase::MysteryDiscuss);
  CHECK(phase->deadlineMs == std::optional<std::int64_t>(T0 + 100'000));  // unchanged
}

TEST(new_host_can_run_match_after_host_leaves) {
  Setup s(5);
  s.room.startMatch(s.host(), T0);
  s.room.disconnect(s.host(), T0);
  CHECK_EQ(s.room.hostId(), s.id(1));
  CHECK_EQ(s.room.next(s.id(1), T0), std::string(""));
  CHECK_EQ(s.room.next(s.id(0), T0), std::string("not the host"));
}
