#pragma once
// RoomManager: every room plus which connection is which player or display.
// It parses and validates client messages (docs/protocol.md), calls into the
// game classes, and returns what to send to which connection. It knows
// nothing about sockets: connections are plain numbers, so it is unit tested
// without a network. main.cpp maps those numbers to real WebSockets.
//
// Not thread-safe by itself. main.cpp calls it under one mutex.

#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "game/room.h"

namespace feast {

using ConnId = std::uint64_t;

struct OutgoingText {
  ConnId conn;
  std::string text;
};

struct Actions {
  std::vector<OutgoingText> sends;  // In order.
  std::vector<ConnId> closes;       // Old sockets replaced by a REJOIN.
};

constexpr std::int64_t kEmptyRoomLifetimeMs = 10 * 60 * 1000;  // rules.md: 10 minutes

class RoomManager {
 public:
  // withAroundTheTable false = the server's --no-around-the-table flag.
  RoomManager(bool testMode, std::uint32_t seed, bool withAroundTheTable = true);

  Actions handleMessage(ConnId conn, const std::string& text, std::int64_t nowMs);
  Actions handleClose(ConnId conn, std::int64_t nowMs);
  // Fires deadlines and deletes rooms nobody has been connected to for 10 min.
  Actions tick(std::int64_t nowMs);

  std::size_t roomCount() const { return rooms_.size(); }
  Room* findRoom(const std::string& code);

 private:
  // What a connection is. playerId empty means a display.
  struct Seat {
    std::string code;
    std::string playerId;
  };

  void createRoom(ConnId conn, const nlohmann::json& payload, Actions& out);
  void joinRoom(ConnId conn, const nlohmann::json& payload, Actions& out);
  void rejoin(ConnId conn, const nlohmann::json& payload, std::int64_t nowMs, Actions& out);
  void watchRoom(ConnId conn, const nlohmann::json& payload, Actions& out);
  std::string playerAction(const Seat& seat, const std::string& type,
                           const nlohmann::json& payload, std::int64_t nowMs);

  void seatPlayer(ConnId conn, Room& room, const JoinResult& joined, Actions& out);
  void flush(Room& room, Actions& out);  // Room outbox -> connections.
  ConnId connForPlayer(const std::string& code, const std::string& playerId) const;
  std::string newRoomCode();

  static void send(Actions& out, ConnId conn, const nlohmann::json& message);
  static void sendError(Actions& out, ConnId conn, const std::string& reason);

  bool testMode_;
  bool withAroundTheTable_;
  std::mt19937 rng_;
  std::map<std::string, std::unique_ptr<Room>> rooms_;
  std::map<ConnId, Seat> seats_;
  std::map<std::string, std::int64_t> emptySince_;  // code -> when its last player left
};

}  // namespace feast
