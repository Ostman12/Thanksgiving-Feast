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
  room.disconnect(ann.playerId);
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
    room.disconnect(r.playerId);
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

TEST(host_passes_to_earliest_connected_player) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  JoinResult bob = room.addPlayer("Bob");
  JoinResult cat = room.addPlayer("Cat");
  room.disconnect(bob.playerId);
  CHECK_EQ(room.hostId(), ann.playerId);  // non-host leaving changes nothing
  room.disconnect(ann.playerId);
  CHECK_EQ(room.hostId(), cat.playerId);  // Bob is disconnected, so Cat
}

TEST(original_host_rejoining_does_not_take_host_back) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  JoinResult bob = room.addPlayer("Bob");
  room.disconnect(ann.playerId);
  CHECK_EQ(room.hostId(), bob.playerId);
  CHECK(room.rejoin(ann.token).ok());
  CHECK_EQ(room.hostId(), bob.playerId);
}

TEST(everyone_gone_then_first_back_becomes_host) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  JoinResult bob = room.addPlayer("Bob");
  room.disconnect(ann.playerId);
  room.disconnect(bob.playerId);
  CHECK_EQ(room.connectedCount(), std::size_t(0));
  CHECK(room.rejoin(bob.token).ok());
  CHECK_EQ(room.hostId(), bob.playerId);
}

TEST(rejoin_restores_seat_and_score) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  room.addPlayer("Bob");
  room.disconnect(ann.playerId);
  CHECK(!room.findPlayer(ann.playerId)->connected);
  CHECK_EQ(room.connectedCount(), std::size_t(1));

  JoinResult back = room.rejoin(ann.token);
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
  CHECK_EQ(room.rejoin("nope").error, std::string("invalid rejoin token"));
  CHECK_EQ(room.rejoin("").error, std::string("invalid rejoin token"));
}

TEST(disconnect_unknown_id_is_ignored) {
  Room room = makeRoom();
  JoinResult ann = room.addPlayer("Ann");
  room.disconnect("p999");
  CHECK_EQ(room.hostId(), ann.playerId);
  CHECK_EQ(room.connectedCount(), std::size_t(1));
}
