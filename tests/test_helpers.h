#pragma once
// Shared helpers for Room tests.

#include <string>
#include <variant>
#include <vector>

#include "game/room.h"

namespace testhelp {

using namespace feast;

inline const std::int64_t T0 = 1'000'000;  // Arbitrary "now" for tests.

// A room with `count` players named P0..Pn. players[0] is the host.
struct Setup {
  Room room;
  std::vector<JoinResult> players;
  explicit Setup(int count, bool testMode = false) : room("ABCD", testMode, 42) {
    for (int i = 0; i < count; ++i) players.push_back(room.addPlayer("P" + std::to_string(i)));
    room.takeOutbox();  // Ignore join events.
  }
  const std::string& host() const { return players[0].playerId; }
  const std::string& id(int i) const { return players[i].playerId; }
};

template <class T>
std::vector<T> eventsOf(const std::vector<Outgoing>& out) {
  std::vector<T> found;
  for (const Outgoing& o : out) {
    if (const T* e = std::get_if<T>(&o.event)) found.push_back(*e);
  }
  return found;
}

// Players dealt a non-impostor YOUR_WORD in these events, in deal order.
inline std::vector<std::string> nonImpostors(const std::vector<Outgoing>& out) {
  std::vector<std::string> ids;
  for (const Outgoing& o : out) {
    const YourWordEvent* w = std::get_if<YourWordEvent>(&o.event);
    if (w != nullptr && !w->isImpostor) ids.push_back(o.toPlayerId);
  }
  return ids;
}

// Host presses Next until the room reaches `phase`, at most 50 times.
inline void nextUntil(Setup& s, Phase phase) {
  for (int i = 0; i < 50 && s.room.phase() != phase; ++i) s.room.next(s.room.hostId(), T0);
}

}  // namespace testhelp
