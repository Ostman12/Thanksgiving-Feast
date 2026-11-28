// Avatars (rules.md, Players and rooms).

#include <nlohmann/json.hpp>

#include "game/avatar.h"
#include "game/room.h"
#include "server/room_manager.h"
#include "test_framework.h"
#include "test_helpers.h"

using namespace feast;
using namespace testhelp;
using nlohmann::json;

TEST(avatar_lists_match_the_rules) {
  CHECK_EQ(avatarEmojis().size(), std::size_t(16));
  CHECK_EQ(avatarColors().size(), std::size_t(8));
  CHECK_EQ(avatarAccessories().size(), std::size_t(7));
  CHECK_EQ(avatarAccessories().front(), std::string("none"));
}

TEST(avatar_validation) {
  CHECK(isValidAvatar(Avatar{"fox", "blue", "crown"}));
  CHECK(isValidAvatar(Avatar{"turkey", "orange", "none"}));
  CHECK(!isValidAvatar(Avatar{"dragon", "blue", "none"}));   // not on the emoji list
  CHECK(!isValidAvatar(Avatar{"fox", "#ff0000", "none"}));   // colors are names, not hex
  CHECK(!isValidAvatar(Avatar{"fox", "blue", ""}));          // "none", not empty
  CHECK(!isValidAvatar(Avatar{"🦊", "blue", "none"}));        // names, not raw emoji
}

TEST(random_avatars_are_valid) {
  std::mt19937 rng(5);
  for (int i = 0; i < 200; ++i) {
    Avatar a = randomAvatar(rng);
    CHECK(isValidAvatar(a));
    CHECK_EQ(a.accessory, std::string("none"));
  }
}

TEST(joining_gives_a_valid_avatar_in_room_state) {
  Room room("ABCD", false, 42);
  room.addPlayer("Ann");
  std::vector<RoomStateEvent> states = eventsOf<RoomStateEvent>(room.takeOutbox());
  CHECK(!states.empty());
  if (states.empty()) return;
  CHECK(isValidAvatar(states.back().players.at(0).avatar));
}

TEST(set_avatar_in_lobby) {
  Setup s(3);
  CHECK_EQ(s.room.setAvatar(s.id(1), Avatar{"owl", "purple", "tophat"}), std::string(""));
  std::vector<RoomStateEvent> states = eventsOf<RoomStateEvent>(s.room.takeOutbox());
  CHECK_EQ(states.size(), std::size_t(1));  // everyone sees the change
  if (states.empty()) return;
  const Avatar& a = states[0].players.at(1).avatar;
  CHECK_EQ(a.emoji, std::string("owl"));
  CHECK_EQ(a.color, std::string("purple"));
  CHECK_EQ(a.accessory, std::string("tophat"));
}

TEST(set_avatar_rejects_bad_names_and_changes_nothing) {
  Setup s(3);
  Avatar before = s.room.findPlayer(s.id(0))->avatar;
  CHECK_EQ(s.room.setAvatar(s.id(0), Avatar{"dragon", "blue", "none"}), std::string("invalid avatar"));
  CHECK_EQ(s.room.findPlayer(s.id(0))->avatar.emoji, before.emoji);
  CHECK(s.room.takeOutbox().empty());
  CHECK_EQ(s.room.setAvatar("p999", Avatar{}), std::string("not in a room"));
}

TEST(set_avatar_lobby_only) {
  Setup s(4);
  s.room.startMatch(s.host(), T0);
  CHECK_EQ(s.room.setAvatar(s.id(1), Avatar{"owl", "purple", "tophat"}), std::string("wrong phase"));
  s.room.endMatch(s.host());
  CHECK_EQ(s.room.setAvatar(s.id(1), Avatar{"owl", "purple", "tophat"}), std::string("wrong phase"));  // match_over
  s.room.next(s.host(), T0);  // back to the lobby
  CHECK_EQ(s.room.setAvatar(s.id(1), Avatar{"owl", "purple", "tophat"}), std::string(""));
}

TEST(avatar_kept_through_the_match_and_rejoin) {
  Setup s(4);
  s.room.setAvatar(s.id(2), Avatar{"frog", "green", "crown"});
  s.room.startMatch(s.host(), T0);
  s.room.disconnect(s.id(2), T0);
  s.room.rejoin(s.players[2].token, T0);
  const Avatar& a = s.room.findPlayer(s.id(2))->avatar;
  CHECK_EQ(a.emoji, std::string("frog"));
  CHECK_EQ(a.accessory, std::string("crown"));
}

// ---------------------------------------------------------------- over JSON

namespace {
std::string msg(const std::string& type, json payload = json::object()) {
  return json{{"type", type}, {"payload", payload}}.dump();
}
std::vector<json> sentTo(const Actions& a, ConnId conn) {
  std::vector<json> got;
  for (const OutgoingText& o : a.sends) {
    if (o.conn == conn) got.push_back(json::parse(o.text));
  }
  return got;
}
}  // namespace

TEST(server_set_avatar_over_json) {
  RoomManager m(false, 7);
  Actions created = m.handleMessage(1, msg("CREATE_ROOM", {{"name", "Ann"}}), T0);
  json state = sentTo(created, 1).at(1);
  CHECK_EQ(state["type"], "ROOM_STATE");
  json avatar = state["payload"]["players"][0]["avatar"];
  CHECK(avatar["emoji"].is_string() && avatar["color"].is_string());
  CHECK_EQ(avatar["accessory"], "none");

  Actions bad = m.handleMessage(1, msg("SET_AVATAR", {{"emoji", "fox"}, {"color", "blue"}}), T0);
  CHECK_EQ(sentTo(bad, 1).at(0)["payload"]["reason"], "invalid message");  // accessory missing
  Actions wrong = m.handleMessage(1, msg("SET_AVATAR", {{"emoji", "fox"}, {"color", "blue"}, {"accessory", "cape"}}), T0);
  CHECK_EQ(sentTo(wrong, 1).at(0)["payload"]["reason"], "invalid avatar");

  Actions ok = m.handleMessage(1, msg("SET_AVATAR", {{"emoji", "fox"}, {"color", "blue"}, {"accessory", "crown"}}), T0);
  std::vector<json> got = sentTo(ok, 1);
  CHECK_EQ(got.size(), std::size_t(1));
  if (got.empty()) return;
  CHECK_EQ(got[0]["type"], "ROOM_STATE");
  CHECK_EQ(got[0]["payload"]["players"][0]["avatar"],
           (json{{"emoji", "fox"}, {"color", "blue"}, {"accessory", "crown"}}));
}
