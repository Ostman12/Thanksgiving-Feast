#include "server/room_manager.h"

#include <cctype>

#include "game/room_code.h"
#include "game/text.h"
#include "server/protocol.h"

namespace feast {

using nlohmann::json;

namespace {

// payload[key] exists and is a string.
bool readString(const json& payload, const char* key, std::string& out) {
  auto it = payload.find(key);
  if (it == payload.end() || !it->is_string()) return false;
  out = it->get<std::string>();
  return true;
}

std::string normalizeCode(const std::string& raw) {
  std::string code = trimWhitespace(raw);
  for (char& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return code;
}

bool isPlayerAction(const std::string& type) {
  return type == "START_MATCH" || type == "NEXT" || type == "END_MATCH" || type == "CAST_VOTE" ||
         type == "GUESS_WORD" || type == "SUBMIT_CHOICE" || type == "ATT_SKIP" ||
         type == "ATT_CONTINUE" || type == "ATT_PICK";
}

bool isJoinAction(const std::string& type) {
  return type == "CREATE_ROOM" || type == "JOIN_ROOM" || type == "REJOIN" || type == "WATCH_ROOM";
}

}  // namespace

RoomManager::RoomManager(bool testMode, std::uint32_t seed, bool withAroundTheTable)
    : testMode_(testMode), withAroundTheTable_(withAroundTheTable), rng_(seed) {}

Room* RoomManager::findRoom(const std::string& code) {
  auto it = rooms_.find(code);
  return it == rooms_.end() ? nullptr : it->second.get();
}

// ---------------------------------------------------------------- messages

Actions RoomManager::handleMessage(ConnId conn, const std::string& text, std::int64_t nowMs) {
  Actions out;
  json msg = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (msg.is_discarded() || !msg.is_object() || !msg.contains("type") ||
      !msg["type"].is_string() || !msg.contains("payload") || !msg["payload"].is_object()) {
    sendError(out, conn, "invalid message");
    return out;
  }
  const std::string type = msg["type"].get<std::string>();
  const json& payload = msg["payload"];

  if (!isJoinAction(type) && !isPlayerAction(type)) {
    sendError(out, conn, "invalid message");  // Unknown type.
    return out;
  }

  auto seatIt = seats_.find(conn);
  if (seatIt != seats_.end() && seatIt->second.playerId.empty() && type != "WATCH_ROOM") {
    sendError(out, conn, "not a player");  // Displays only ever send WATCH_ROOM.
    return out;
  }

  if (isJoinAction(type)) {
    if (seatIt != seats_.end()) {
      sendError(out, conn, "already in a room");
    } else if (type == "CREATE_ROOM") {
      createRoom(conn, payload, out);
    } else if (type == "JOIN_ROOM") {
      joinRoom(conn, payload, out);
    } else if (type == "REJOIN") {
      rejoin(conn, payload, nowMs, out);
    } else {
      watchRoom(conn, payload, out);
    }
    return out;
  }

  // A player action.
  if (seatIt == seats_.end()) {
    sendError(out, conn, "not in a room");
    return out;
  }
  const Seat seat = seatIt->second;
  Room* room = findRoom(seat.code);
  if (room == nullptr) {
    sendError(out, conn, "room not found");
    return out;
  }
  std::string error = playerAction(seat, type, payload, nowMs);
  if (!error.empty()) sendError(out, conn, error);
  flush(*room, out);
  return out;
}

// Calls the game method for one player action. Returns "" or the ERROR reason.
std::string RoomManager::playerAction(const Seat& seat, const std::string& type,
                                      const json& payload, std::int64_t nowMs) {
  Room& room = *findRoom(seat.code);
  const std::string& me = seat.playerId;

  if (type == "START_MATCH") return room.startMatch(me, nowMs);
  if (type == "NEXT") return room.next(me, nowMs);
  if (type == "END_MATCH") return room.endMatch(me);

  if (type == "CAST_VOTE") {
    // The key must be present; only an explicit null means Skip.
    auto it = payload.find("targetId");
    if (it == payload.end()) return "invalid message";
    if (it->is_null()) return room.castVote(me, std::nullopt, nowMs);
    if (!it->is_string()) return "invalid message";
    return room.castVote(me, it->get<std::string>(), nowMs);
  }
  if (type == "GUESS_WORD") {
    std::string word;
    if (!readString(payload, "word", word)) return "invalid message";
    return room.guessWord(me, word, nowMs);
  }
  if (type == "SUBMIT_CHOICE") {
    std::string choice;
    if (!readString(payload, "choice", choice)) return "invalid message";
    return room.submitChoice(me, choice, nowMs);
  }
  if (type == "ATT_SKIP") return room.attSkip(me, nowMs);
  if (type == "ATT_CONTINUE") return room.attContinue(me, nowMs);
  if (type == "ATT_PICK") {
    // The key must be present; null means Pass. A string must be one letter A-D.
    auto it = payload.find("option");
    if (it == payload.end()) return "invalid message";
    if (it->is_null()) return room.attPick(me, std::nullopt, nowMs);
    if (!it->is_string()) return "invalid message";
    std::string option = it->get<std::string>();
    if (option.size() != 1) return "invalid option";
    return room.attPick(me, option[0], nowMs);
  }
  return "invalid message";
}

void RoomManager::createRoom(ConnId conn, const json& payload, Actions& out) {
  std::string name;
  if (!readString(payload, "name", name)) return sendError(out, conn, "invalid message");
  std::string code = newRoomCode();
  if (code.empty()) return sendError(out, conn, "server full");

  auto room = std::make_unique<Room>(code, testMode_, static_cast<std::uint32_t>(rng_()),
                                     withAroundTheTable_);
  JoinResult joined = room->addPlayer(name);
  if (!joined.ok()) return sendError(out, conn, joined.error);  // Room is discarded.

  Room& ref = *room;
  rooms_[code] = std::move(room);
  seatPlayer(conn, ref, joined, out);
}

void RoomManager::joinRoom(ConnId conn, const json& payload, Actions& out) {
  std::string code;
  std::string name;
  if (!readString(payload, "code", code) || !readString(payload, "name", name)) {
    return sendError(out, conn, "invalid message");
  }
  Room* room = findRoom(normalizeCode(code));
  if (room == nullptr) return sendError(out, conn, "room not found");
  JoinResult joined = room->addPlayer(name);
  if (!joined.ok()) return sendError(out, conn, joined.error);
  seatPlayer(conn, *room, joined, out);
}

void RoomManager::rejoin(ConnId conn, const json& payload, std::int64_t nowMs, Actions& out) {
  std::string code;
  std::string token;
  if (!readString(payload, "code", code) || !readString(payload, "token", token)) {
    return sendError(out, conn, "invalid message");
  }
  Room* room = findRoom(normalizeCode(code));
  if (room == nullptr) return sendError(out, conn, "room not found");
  JoinResult joined = room->rejoin(token, nowMs);
  if (!joined.ok()) return sendError(out, conn, joined.error);

  // protocol.md Rejoin 4: an older socket for this player is closed and replaced.
  ConnId old = connForPlayer(room->code(), joined.playerId);
  if (old != 0 && old != conn) {
    seats_.erase(old);
    out.closes.push_back(old);
  }
  seatPlayer(conn, *room, joined, out);
}

void RoomManager::watchRoom(ConnId conn, const json& payload, Actions& out) {
  std::string code;
  if (!readString(payload, "code", code)) return sendError(out, conn, "invalid message");
  Room* room = findRoom(normalizeCode(code));
  if (room == nullptr) return sendError(out, conn, "room not found");

  seats_[conn] = Seat{room->code(), ""};
  for (const Event& e : room->displaySnapshot()) send(out, conn, eventToJson(e));
}

// JOINED goes first, then whatever the room queued (ROOM_STATE, catch-up, ...).
void RoomManager::seatPlayer(ConnId conn, Room& room, const JoinResult& joined, Actions& out) {
  seats_[conn] = Seat{room.code(), joined.playerId};
  emptySince_.erase(room.code());
  send(out, conn, joinedMessage(joined.playerId, joined.token, room.code()));
  flush(room, out);
}

// ---------------------------------------------------------------- close, tick

Actions RoomManager::handleClose(ConnId conn, std::int64_t nowMs) {
  Actions out;
  auto it = seats_.find(conn);
  if (it == seats_.end()) return out;  // Never joined, or already replaced.
  Seat seat = it->second;
  seats_.erase(it);
  if (seat.playerId.empty()) return out;  // A display left. Nothing to tell anyone.

  if (Room* room = findRoom(seat.code)) {
    room->disconnect(seat.playerId, nowMs);
    flush(*room, out);
  }
  return out;
}

Actions RoomManager::tick(std::int64_t nowMs) {
  Actions out;
  for (auto it = rooms_.begin(); it != rooms_.end();) {
    Room& room = *it->second;
    room.tick(nowMs);
    flush(room, out);

    const std::string code = it->first;
    if (room.connectedCount() > 0) {
      emptySince_.erase(code);
    } else {
      auto [since, inserted] = emptySince_.emplace(code, nowMs);
      if (!inserted && nowMs - since->second >= kEmptyRoomLifetimeMs) {
        // Delete the room. Any displays still watching are no longer in a room.
        for (auto s = seats_.begin(); s != seats_.end();) {
          s = (s->second.code == code) ? seats_.erase(s) : std::next(s);
        }
        emptySince_.erase(since);
        it = rooms_.erase(it);
        continue;
      }
    }
    ++it;
  }
  return out;
}

// ---------------------------------------------------------------- helpers

// Sends each queued room event: room-wide ones to every player and display in
// the room, private ones to that player's connection only.
void RoomManager::flush(Room& room, Actions& out) {
  for (const Outgoing& o : room.takeOutbox()) {
    json message = eventToJson(o.event);
    if (o.toPlayerId.empty()) {
      for (const auto& [conn, seat] : seats_) {
        if (seat.code == room.code()) send(out, conn, message);
      }
    } else if (ConnId conn = connForPlayer(room.code(), o.toPlayerId)) {
      send(out, conn, message);
    }
    // A private event for a disconnected player is dropped; rejoin resends it.
  }
}

ConnId RoomManager::connForPlayer(const std::string& code, const std::string& playerId) const {
  for (const auto& [conn, seat] : seats_) {
    if (seat.code == code && seat.playerId == playerId) return conn;
  }
  return 0;
}

std::string RoomManager::newRoomCode() {
  for (int attempt = 0; attempt < 1000; ++attempt) {
    std::string code = makeRoomCode(rng_);
    if (rooms_.count(code) == 0) return code;
  }
  return "";
}

void RoomManager::send(Actions& out, ConnId conn, const json& message) {
  // replace: never throw on bad UTF-8 (parsed input is already valid, but be safe).
  out.sends.push_back({conn, message.dump(-1, ' ', false, json::error_handler_t::replace)});
}

void RoomManager::sendError(Actions& out, ConnId conn, const std::string& reason) {
  send(out, conn, errorMessage(reason));
}

}  // namespace feast
