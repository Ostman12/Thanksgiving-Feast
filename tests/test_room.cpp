#include <set>

#include "game/room.h"
#include "test_framework.h"

using namespace feast;

namespace {
Room makeRoom() { return Room("ABCD", false, 42); }
}  // namespace

TEST(new_room_is_in_lobby) {
  Room room = makeRoom();
  CHECK_EQ(room.phase(), Phase::Lobby);
  CHECK_EQ(std::string(phaseName(room.phase())), std::string("lobby"));
  CHECK(room.players().empty());
}

TEST(first_player_becomes_host) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  JoinResult bob = room.addPlayer("Bob");
  CHECK(ann.ok());
  CHECK(bob.ok());
  CHECK_EQ(room.hostId(), ann.playerId);
}

TEST(name_is_trimmed) {
  Room room = makeRoom();
  JoinResult r = room.addPlayer("  Ann  ");
  CHECK(r.ok());
  CHECK_EQ(room.findPlayer(r.playerId)->name, std::string("Ann"));
}

TEST(invalid_names_rejected) {
  Room room = makeRoom();
  CHECK_EQ(room.addPlayer("").error, std::string("invalid name"));
  CHECK_EQ(room.addPlayer("   ").error, std::string("invalid name"));
  CHECK_EQ(room.addPlayer("abcdefghijklmnopq").error, std::string("invalid name"));
  CHECK(room.players().empty());
}

TEST(duplicate_name_rejected_case_insensitive) {
  Room room = makeRoom();
  CHECK(room.addPlayer("Ann").ok());
  CHECK_EQ(room.addPlayer("ann").error, std::string("name taken"));
  CHECK_EQ(room.addPlayer(" ANN ").error, std::string("name taken"));
  CHECK_EQ(room.players().size(), std::size_t(1));
}

TEST(disconnected_player_keeps_name) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  room.disconnect(ann.playerId, 0);
  CHECK_EQ(room.addPlayer("Ann").error, std::string("name taken"));
}

TEST(room_holds_at_most_10_players) {
  Room room = makeRoom();
  for (int i = 0; i < 10; ++i) {
    CHECK(room.addPlayer("P" + std::to_string(i)).ok());
  }
  CHECK_EQ(room.addPlayer("Extra").error, std::string("room is full"));
  CHECK_EQ(room.players().size(), std::size_t(10));
}

TEST(disconnected_players_still_count_toward_limit) {
  Room room = makeRoom();
  for (int i = 0; i < 10; ++i) {
    JoinResult r = room.addPlayer("P" + std::to_string(i));
    room.disconnect(r.playerId, 0);
  }
  CHECK_EQ(room.addPlayer("Extra").error, std::string("room is full"));
}

TEST(ids_and_tokens_are_unique) {
  Room room = makeRoom();
  std::set<std::string> ids;
  std::set<std::string> tokens;
  for (int i = 0; i < 10; ++i) {
    JoinResult r = room.addPlayer("P" + std::to_string(i));
    ids.insert(r.playerId);
    tokens.insert(r.token);
    CHECK(!r.token.empty());
    CHECK(r.token != r.playerId);
  }
  CHECK_EQ(ids.size(), std::size_t(10));
  CHECK_EQ(tokens.size(), std::size_t(10));
}

// rules.md: a disconnected host keeps host for 20 s, then it passes to the
// connected player who joined earliest.
TEST(host_passes_to_earliest_connected_player_after_20s) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  JoinResult bob = room.addPlayer("Bob");
  JoinResult cat = room.addPlayer("Cat");
  room.disconnect(bob.playerId, 0);
  CHECK_EQ(room.hostId(), ann.playerId);  // non-host leaving changes nothing
  room.disconnect(ann.playerId, 0);
  CHECK_EQ(room.hostId(), ann.playerId);  // still host during the 20 s
  room.tick(19'999);
  CHECK_EQ(room.hostId(), ann.playerId);
  room.takeOutbox();
  room.tick(20'000);
  CHECK_EQ(room.hostId(), cat.playerId);  // Bob is disconnected, so Cat
  bool sentState = false;  // everyone is told who the new host is
  for (const Outgoing& o : room.takeOutbox()) {
    if (const RoomStateEvent* s = std::get_if<RoomStateEvent>(&o.event)) sentState = s->hostId == cat.playerId;
  }
  CHECK(sentState);
}

TEST(host_back_within_20s_never_loses_host) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  room.addPlayer("Bob");
  room.disconnect(ann.playerId, 0);
  room.tick(10'000);
  CHECK(room.rejoin(ann.token, 15'000).ok());
  room.tick(30'000);
  CHECK_EQ(room.hostId(), ann.playerId);
}

TEST(creator_takes_host_back_on_rejoin) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  JoinResult bob = room.addPlayer("Bob");
  room.disconnect(ann.playerId, 0);
  room.tick(20'000);
  CHECK_EQ(room.hostId(), bob.playerId);
  CHECK(room.rejoin(ann.token, 60'000).ok());
  CHECK_EQ(room.hostId(), ann.playerId);
}

TEST(stand_in_host_also_gets_20s) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  JoinResult bob = room.addPlayer("Bob");
  JoinResult cat = room.addPlayer("Cat");
  room.disconnect(ann.playerId, 0);
  room.tick(20'000);
  CHECK_EQ(room.hostId(), bob.playerId);
  room.disconnect(bob.playerId, 30'000);
  room.tick(49'999);
  CHECK_EQ(room.hostId(), bob.playerId);
  room.tick(50'000);
  CHECK_EQ(room.hostId(), cat.playerId);
}

TEST(everyone_gone_then_first_back_becomes_host) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  JoinResult bob = room.addPlayer("Bob");
  room.disconnect(ann.playerId, 0);
  room.disconnect(bob.playerId, 0);
  CHECK_EQ(room.connectedCount(), std::size_t(0));
  room.tick(60'000);
  CHECK_EQ(room.hostId(), ann.playerId);  // nobody to pass it to: stays put
  CHECK(room.rejoin(bob.token, 60'000).ok());
  CHECK_EQ(room.hostId(), bob.playerId);  // Ann's 20 s are long gone
}

TEST(rejoin_restores_seat_and_score) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  room.addPlayer("Bob");
  room.disconnect(ann.playerId, 0);
  CHECK(!room.findPlayer(ann.playerId)->connected);
  CHECK_EQ(room.connectedCount(), std::size_t(1));

  JoinResult back = room.rejoin(ann.token, 0);
  CHECK(back.ok());
  CHECK_EQ(back.playerId, ann.playerId);
  CHECK_EQ(back.token, ann.token);
  CHECK(room.findPlayer(ann.playerId)->connected);
  CHECK_EQ(room.findPlayer(ann.playerId)->score, 0);
  CHECK_EQ(room.players().size(), std::size_t(2));
}

TEST(rejoin_with_bad_token_fails) {
  Room room = makeRoom();
  room.addPlayer("Ann");
  CHECK_EQ(room.rejoin("nope", 0).error, std::string("invalid rejoin token"));
  CHECK_EQ(room.rejoin("", 0).error, std::string("invalid rejoin token"));
}

TEST(disconnect_unknown_id_is_ignored) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  room.disconnect("p999", 0);
  CHECK_EQ(room.hostId(), ann.playerId);
  CHECK_EQ(room.connectedCount(), std::size_t(1));
}
