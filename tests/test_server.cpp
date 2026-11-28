// RoomManager tests: raw JSON in, JSON out, connections as plain numbers.

#include <cctype>
#include <map>

#include <nlohmann/json.hpp>

#include "server/room_manager.h"
#include "test_framework.h"

using namespace feast;
using nlohmann::json;

namespace {

const std::int64_t T0 = 1'000'000;

std::string msg(const std::string& type, json payload = json::object()) {
  return json{{"type", type}, {"payload", payload}}.dump();
}

// Every message one connection received, parsed.
std::vector<json> to(const Actions& a, ConnId conn) {
  std::vector<json> got;
  for (const OutgoingText& s : a.sends) {
    if (s.conn == conn) got.push_back(json::parse(s.text));
  }
  return got;
}

std::vector<json> ofType(const std::vector<json>& msgs, const std::string& type) {
  std::vector<json> got;
  for (const json& m : msgs) {
    if (m["type"] == type) got.push_back(m);
  }
  return got;
}

std::string errorReason(const Actions& a, ConnId conn) {
  std::vector<json> errors = ofType(to(a, conn), "ERROR");
  return errors.empty() ? "" : errors[0]["payload"]["reason"].get<std::string>();
}

// A RoomManager with players on connections 1..n (1 is host) in one room.
struct Lobby {
  RoomManager m{false, 7};
  std::string code;
  std::map<ConnId, std::string> playerIds;
  std::map<ConnId, std::string> tokens;

  explicit Lobby(int players) {
    Actions a = m.handleMessage(1, msg("CREATE_ROOM", {{"name", "P1"}}), T0);
    json joined = ofType(to(a, 1), "JOINED").at(0)["payload"];
    code = joined["code"];
    remember(1, joined);
    for (ConnId c = 2; c <= static_cast<ConnId>(players); ++c) {
      Actions j = m.handleMessage(
          c, msg("JOIN_ROOM", {{"code", code}, {"name", "P" + std::to_string(c)}}), T0);
      remember(c, ofType(to(j, c), "JOINED").at(0)["payload"]);
    }
  }
  void remember(ConnId c, const json& joined) {
    playerIds[c] = joined["playerId"];
    tokens[c] = joined["token"];
  }
};

}  // namespace

// ---------------------------------------------------------------- validation

TEST(server_rejects_malformed_messages) {
  RoomManager m(false, 1);
  const char* bad[] = {
      "not json",
      "[1,2]",
      R"({"type":"CREATE_ROOM"})",                         // no payload
      R"({"type":"CREATE_ROOM","payload":"x"})",           // payload not an object
      R"({"type":7,"payload":{}})",                        // type not a string
      R"({"type":"DANCE","payload":{}})",                  // unknown type
      R"({"type":"CREATE_ROOM","payload":{}})",            // missing name
      R"({"type":"CREATE_ROOM","payload":{"name":42}})",   // wrong field type
  };
  for (const char* text : bad) {
    Actions a = m.handleMessage(1, text, T0);
    CHECK_EQ(errorReason(a, 1), std::string("invalid message"));
    CHECK_EQ(a.sends.size(), std::size_t(1));
  }
  CHECK_EQ(m.roomCount(), std::size_t(0));
}

TEST(server_actions_need_a_room) {
  RoomManager m(false, 1);
  CHECK_EQ(errorReason(m.handleMessage(1, msg("START_MATCH"), T0), 1), std::string("not in a room"));
  CHECK_EQ(errorReason(m.handleMessage(1, msg("NEXT"), T0), 1), std::string("not in a room"));
}

// ---------------------------------------------------------------- joining

TEST(server_create_room_replies_joined_then_room_state) {
  RoomManager m(false, 1);
  Actions a = m.handleMessage(1, msg("CREATE_ROOM", {{"name", " Ann "}}), T0);
  std::vector<json> got = to(a, 1);
  CHECK_EQ(got.size(), std::size_t(2));
  if (got.size() != 2) return;
  CHECK_EQ(got[0]["type"], "JOINED");
  CHECK_EQ(got[0]["payload"]["code"].get<std::string>().size(), std::size_t(4));
  CHECK(got[0]["payload"]["token"].is_string());
  CHECK_EQ(got[1]["type"], "ROOM_STATE");
  CHECK_EQ(got[1]["payload"]["players"][0]["name"], "Ann");
  CHECK_EQ(got[1]["payload"]["hostId"], got[0]["payload"]["playerId"]);
  CHECK_EQ(got[1]["payload"]["phase"], "lobby");
  CHECK_EQ(m.roomCount(), std::size_t(1));
}

TEST(server_bad_name_creates_no_room) {
  RoomManager m(false, 1);
  CHECK_EQ(errorReason(m.handleMessage(1, msg("CREATE_ROOM", {{"name", "  "}}), T0), 1),
           std::string("invalid name"));
  CHECK_EQ(m.roomCount(), std::size_t(0));
}

TEST(server_join_room) {
  Lobby l(1);
  std::string lower = l.code;
  for (char& c : lower) c = static_cast<char>(std::tolower(c));
  Actions a = l.m.handleMessage(2, msg("JOIN_ROOM", {{"code", lower}, {"name", "Bob"}}), T0);
  CHECK_EQ(ofType(to(a, 2), "JOINED").size(), std::size_t(1));  // code is case-insensitive
  CHECK(ofType(to(a, 1), "JOINED").empty());                     // JOINED is private
  CHECK_EQ(ofType(to(a, 1), "ROOM_STATE").size(), std::size_t(1));
  CHECK_EQ(ofType(to(a, 2), "ROOM_STATE").size(), std::size_t(1));

  CHECK_EQ(errorReason(l.m.handleMessage(3, msg("JOIN_ROOM", {{"code", "ZZZZ"}, {"name", "C"}}), T0), 3),
           std::string("room not found"));
  CHECK_EQ(errorReason(l.m.handleMessage(3, msg("JOIN_ROOM", {{"code", l.code}, {"name", "bob"}}), T0), 3),
           std::string("name taken"));
  CHECK_EQ(errorReason(l.m.handleMessage(2, msg("CREATE_ROOM", {{"name", "X"}}), T0), 2),
           std::string("already in a room"));
}

TEST(server_room_state_never_contains_tokens) {
  Lobby l(4);
  Actions a = l.m.handleMessage(1, msg("START_MATCH"), T0);
  for (const OutgoingText& s : a.sends) {
    for (const auto& [conn, token] : l.tokens) CHECK(s.text.find(token) == std::string::npos);
  }
}

// ---------------------------------------------------------------- secrecy

TEST(server_words_go_only_to_their_player) {
  Lobby l(4);
  Actions a = l.m.handleMessage(1, msg("START_MATCH"), T0);
  int impostors = 0;
  for (ConnId c = 1; c <= 4; ++c) {
    std::vector<json> words = ofType(to(a, c), "YOUR_WORD");
    CHECK_EQ(words.size(), std::size_t(1));
    if (!words.empty() && words[0]["payload"]["isImpostor"] == true) ++impostors;
  }
  CHECK_EQ(impostors, 1);
}

TEST(server_display_never_sees_secrets_during_a_round) {
  Lobby l(4);
  CHECK_EQ(to(l.m.handleMessage(9, msg("WATCH_ROOM", {{"code", l.code}}), T0), 9).size(),
           std::size_t(2));  // ROOM_STATE + PHASE

  std::vector<OutgoingText> all;
  auto run = [&](ConnId c, const std::string& text) {
    Actions a = l.m.handleMessage(c, text, T0);
    all.insert(all.end(), a.sends.begin(), a.sends.end());
    return a;
  };
  Actions start = run(1, msg("START_MATCH"));
  std::vector<std::string> words;
  for (ConnId c = 1; c <= 4; ++c) words.push_back(ofType(to(start, c), "YOUR_WORD").at(0)["payload"]["word"]);

  run(1, msg("NEXT"));                                                       // -> vote
  for (ConnId c = 1; c <= 4; ++c) run(c, msg("CAST_VOTE", {{"targetId", nullptr}}));  // -> escaped -> reveal

  // Everything the display got before ROUND_RESULT (the reveal, which is
  // allowed to show words and the impostor): no word, token or YOUR_WORD.
  for (const OutgoingText& s : all) {
    if (s.conn != 9) continue;
    if (s.text.find("ROUND_RESULT") != std::string::npos) break;
    CHECK(s.text.find("YOUR_WORD") == std::string::npos);
    CHECK(s.text.find("token") == std::string::npos);
    for (const std::string& w : words) CHECK(s.text.find("\"" + w + "\"") == std::string::npos);
    CHECK(s.text.find("impostorId") == std::string::npos);
  }
}

// ---------------------------------------------------------------- actions

TEST(server_cast_vote_field_checks) {
  Lobby l(4);
  l.m.handleMessage(1, msg("START_MATCH"), T0);
  l.m.handleMessage(1, msg("NEXT"), T0);
  CHECK_EQ(errorReason(l.m.handleMessage(2, msg("CAST_VOTE"), T0), 2), std::string("invalid message"));
  CHECK_EQ(errorReason(l.m.handleMessage(2, msg("CAST_VOTE", {{"targetId", 3}}), T0), 2),
           std::string("invalid message"));
  Actions ok = l.m.handleMessage(2, msg("CAST_VOTE", {{"targetId", nullptr}}), T0);
  CHECK_EQ(errorReason(ok, 2), std::string(""));
  std::vector<json> sub = ofType(to(ok, 3), "SUBMITTED");  // everyone sees ids only
  CHECK_EQ(sub.size(), std::size_t(1));
  if (!sub.empty()) CHECK_EQ(sub[0]["payload"]["playerIds"], json::array({l.playerIds[2]}));
  CHECK_EQ(errorReason(l.m.handleMessage(2, msg("CAST_VOTE", {{"targetId", nullptr}}), T0), 2),
           std::string("already submitted"));
}

TEST(server_game_errors_go_to_sender_only) {
  Lobby l(4);
  Actions a = l.m.handleMessage(2, msg("START_MATCH"), T0);
  CHECK_EQ(errorReason(a, 2), std::string("not the host"));
  CHECK_EQ(a.sends.size(), std::size_t(1));
}

TEST(server_json_shapes) {
  Lobby l(4);
  Actions a = l.m.handleMessage(1, msg("START_MATCH"), T0);
  json started = ofType(to(a, 2), "MATCH_STARTED").at(0)["payload"];
  CHECK_EQ(started["games"], json::array({"mystery", "wyr"}));
  CHECK_EQ(started["rounds"]["mystery"], 3);
  json phase = ofType(to(a, 2), "PHASE").at(0)["payload"];
  CHECK_EQ(phase["game"], "mystery");
  CHECK_EQ(phase["phase"], "mystery_discuss");
  CHECK_EQ(phase["round"], 1);
  CHECK_EQ(phase["deadline"], T0 + 100'000);

  Actions end = l.m.handleMessage(1, msg("END_MATCH"), T0);
  json over = ofType(to(end, 2), "MATCH_OVER").at(0)["payload"];
  CHECK(over["winnerIds"].is_array());
  CHECK_EQ(over["leaderboard"].size(), std::size_t(4));
  Actions lobby = l.m.handleMessage(1, msg("NEXT"), T0);
  json lobbyPhase = ofType(to(lobby, 2), "PHASE").at(0)["payload"];
  CHECK(lobbyPhase["game"].is_null());
  CHECK(lobbyPhase["deadline"].is_null());
}

TEST(server_wyr_messages_over_json) {
  Lobby l(4);
  l.m.handleMessage(1, msg("START_MATCH"), T0);
  // Host presses Next through the 3 Mystery rounds until the first prompt.
  Actions a;
  for (int i = 0; i < 20 && ofType(to(a, 3), "WYR_PROMPT").empty(); ++i) {
    a = l.m.handleMessage(1, msg("NEXT"), T0);
  }
  CHECK_EQ(ofType(to(a, 3), "WYR_PROMPT").size(), std::size_t(1));
  CHECK_EQ(errorReason(l.m.handleMessage(2, msg("SUBMIT_CHOICE"), T0), 2),
           std::string("invalid message"));  // choice missing
  Actions last;
  for (ConnId c = 1; c <= 4; ++c) {
    last = l.m.handleMessage(c, msg("SUBMIT_CHOICE", {{"choice", c == 4 ? "B" : "A"}}), T0);
  }
  std::vector<json> results = ofType(to(last, 2), "WYR_RESULT");
  CHECK_EQ(results.size(), std::size_t(1));
  if (results.empty()) return;
  CHECK_EQ(results[0]["payload"]["majority"], "A");
  CHECK_EQ(results[0]["payload"]["countA"], 3);
  CHECK_EQ(results[0]["payload"]["choices"][3]["choice"], "B");
}

// ---------------------------------------------------------------- displays

TEST(server_display_rules) {
  Lobby l(3);
  CHECK_EQ(errorReason(l.m.handleMessage(9, msg("WATCH_ROOM", {{"code", "ZZZZ"}}), T0), 9),
           std::string("room not found"));
  Actions w = l.m.handleMessage(9, msg("WATCH_ROOM", {{"code", l.code}}), T0);
  std::vector<json> got = to(w, 9);
  CHECK_EQ(got.size(), std::size_t(2));
  if (got.size() == 2) {
    CHECK_EQ(got[0]["type"], "ROOM_STATE");
    CHECK_EQ(got[1]["type"], "PHASE");
  }
  CHECK_EQ(errorReason(l.m.handleMessage(9, msg("NEXT"), T0), 9), std::string("not a player"));
  CHECK_EQ(errorReason(l.m.handleMessage(9, msg("CREATE_ROOM", {{"name", "X"}}), T0), 9),
           std::string("not a player"));
  CHECK_EQ(errorReason(l.m.handleMessage(9, msg("WATCH_ROOM", {{"code", l.code}}), T0), 9),
           std::string("already in a room"));

  // 3 players + a display is still not enough to start in normal mode.
  CHECK_EQ(errorReason(l.m.handleMessage(1, msg("START_MATCH"), T0), 1),
           std::string("not enough players"));
  // Displays get room-wide messages.
  Actions join = l.m.handleMessage(4, msg("JOIN_ROOM", {{"code", l.code}, {"name", "D"}}), T0);
  CHECK_EQ(ofType(to(join, 9), "ROOM_STATE").size(), std::size_t(1));
  CHECK(ofType(to(join, 9), "JOINED").empty());
}

TEST(server_display_catch_up_mid_vote) {
  Lobby l(4);
  l.m.handleMessage(1, msg("START_MATCH"), T0);
  l.m.handleMessage(1, msg("NEXT"), T0);
  l.m.handleMessage(2, msg("CAST_VOTE", {{"targetId", nullptr}}), T0);
  std::vector<json> got = to(l.m.handleMessage(9, msg("WATCH_ROOM", {{"code", l.code}}), T0), 9);
  CHECK_EQ(got.size(), std::size_t(3));  // ROOM_STATE, SUBMITTED, PHASE
  if (got.size() == 3) CHECK_EQ(got[1]["type"], "SUBMITTED");
}

// ---------------------------------------------------------------- rejoin & close

TEST(server_close_marks_player_disconnected) {
  Lobby l(3);
  Actions a = l.m.handleClose(2, T0);
  std::vector<json> states = ofType(to(a, 1), "ROOM_STATE");
  CHECK_EQ(states.size(), std::size_t(1));
  if (!states.empty()) CHECK_EQ(states[0]["payload"]["players"][1]["connected"], false);
  CHECK(to(a, 2).empty());                       // closed connection gets nothing
  CHECK(l.m.handleClose(2, T0).sends.empty());   // second close is a no-op
}

TEST(server_rejoin_on_new_connection) {
  Lobby l(4);
  l.m.handleMessage(1, msg("START_MATCH"), T0);
  l.m.handleClose(3, T0);
  Actions a = l.m.handleMessage(30, msg("REJOIN", {{"code", l.code}, {"token", l.tokens[3]}}), T0 + 1);
  std::vector<json> got = to(a, 30);
  CHECK(!got.empty());
  if (got.empty()) return;
  CHECK_EQ(got[0]["type"], "JOINED");
  CHECK_EQ(got[0]["payload"]["playerId"], l.playerIds[3]);
  CHECK_EQ(ofType(got, "YOUR_WORD").size(), std::size_t(1));
  CHECK_EQ(got.back()["type"], "PHASE");
  CHECK(a.closes.empty());  // old socket was already gone
}

TEST(server_rejoin_replaces_open_socket) {
  Lobby l(3);
  Actions a = l.m.handleMessage(20, msg("REJOIN", {{"code", l.code}, {"token", l.tokens[2]}}), T0);
  CHECK_EQ(a.closes, std::vector<ConnId>{2});
  CHECK(to(a, 2).empty());
  // The replaced socket's close arrives later and changes nothing.
  CHECK(l.m.handleClose(2, T0).sends.empty());
  Room* room = l.m.findRoom(l.code);
  CHECK(room != nullptr && room->findPlayer(l.playerIds[2])->connected);
  // Messages now go to the new connection.
  Actions j = l.m.handleMessage(4, msg("JOIN_ROOM", {{"code", l.code}, {"name", "New"}}), T0);
  CHECK_EQ(ofType(to(j, 20), "ROOM_STATE").size(), std::size_t(1));
}

TEST(server_rejoin_bad_token) {
  Lobby l(2);
  CHECK_EQ(errorReason(l.m.handleMessage(5, msg("REJOIN", {{"code", l.code}, {"token", "nope"}}), T0), 5),
           std::string("invalid rejoin token"));
}

// ---------------------------------------------------------------- tick

TEST(server_tick_sends_phase_on_deadline) {
  Lobby l(4);
  l.m.handleMessage(1, msg("START_MATCH"), T0);
  CHECK(l.m.tick(T0 + 99'999).sends.empty());
  Actions a = l.m.tick(T0 + 100'000);
  std::vector<json> phases = ofType(to(a, 2), "PHASE");
  CHECK_EQ(phases.size(), std::size_t(1));
  if (!phases.empty()) CHECK_EQ(phases[0]["payload"]["phase"], "mystery_vote");
}

TEST(server_empty_room_deleted_after_10_minutes) {
  Lobby l(2);
  l.m.handleMessage(9, msg("WATCH_ROOM", {{"code", l.code}}), T0);
  l.m.handleClose(1, T0);
  l.m.handleClose(2, T0);
  l.m.tick(T0);
  l.m.tick(T0 + 10 * 60 * 1000 - 1);
  CHECK_EQ(l.m.roomCount(), std::size_t(1));
  l.m.tick(T0 + 10 * 60 * 1000);
  CHECK_EQ(l.m.roomCount(), std::size_t(0));
  // The display is no longer in a room; watching again finds nothing.
  CHECK_EQ(errorReason(l.m.handleMessage(9, msg("WATCH_ROOM", {{"code", l.code}}), T0), 9),
           std::string("room not found"));
}

TEST(server_room_kept_if_someone_returns) {
  Lobby l(2);
  l.m.handleClose(1, T0);
  l.m.handleClose(2, T0);
  l.m.tick(T0);
  l.m.handleMessage(5, msg("REJOIN", {{"code", l.code}, {"token", l.tokens[1]}}), T0 + 60'000);
  l.m.tick(T0 + 11 * 60 * 1000);
  CHECK_EQ(l.m.roomCount(), std::size_t(1));
}
