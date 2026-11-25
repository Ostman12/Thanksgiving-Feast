#include "game/room.h"

#include <utility>

#include "game/text.h"

namespace feast {

const char* phaseName(Phase phase) {
  switch (phase) {
    case Phase::Lobby: return "lobby";
    case Phase::MysteryDiscuss: return "mystery_discuss";
    case Phase::MysteryVote: return "mystery_vote";
    case Phase::MysteryGuess: return "mystery_guess";
    case Phase::MysteryReveal: return "mystery_reveal";
    case Phase::WyrChoose: return "wyr_choose";
    case Phase::WyrReveal: return "wyr_reveal";
    case Phase::Paused: return "paused";
    case Phase::MatchOver: return "match_over";
  }
  return "unknown";
}

Room::Room(std::string code, bool testMode, std::uint32_t seed)
    : code_(std::move(code)), testMode_(testMode), rng_(seed) {}

JoinResult Room::addPlayer(const std::string& rawName) {
  JoinResult result;
  std::string name = trimWhitespace(rawName);

  if (!isValidName(name)) {
    result.error = "invalid name";
    return result;
  }
  if (phase_ != Phase::Lobby) {
    result.error = "match already started";
    return result;
  }
  // Disconnected players keep their seat, so they count toward the limit.
  if (players_.size() >= kMaxPlayers) {
    result.error = "room is full";
    return result;
  }
  for (const Player& p : players_) {
    if (equalsIgnoreCase(p.name, name)) {
      result.error = "name taken";
      return result;
    }
  }

  Player player;
  player.id = "p" + std::to_string(nextPlayerNumber_++);
  player.name = name;
  player.token = makeToken();
  players_.push_back(player);

  if (hostId_.empty()) hostId_ = player.id;

  result.playerId = player.id;
  result.token = player.token;
  return result;
}

JoinResult Room::rejoin(const std::string& token) {
  JoinResult result;
  for (Player& p : players_) {
    if (!token.empty() && p.token == token) {
      p.connected = true;
      reassignHostIfNeeded();
      result.playerId = p.id;
      result.token = p.token;
      return result;
    }
  }
  result.error = "invalid rejoin token";
  return result;
}

void Room::disconnect(const std::string& playerId) {
  Player* p = findPlayerMutable(playerId);
  if (p == nullptr) return;
  p->connected = false;
  reassignHostIfNeeded();
}

const Player* Room::findPlayer(const std::string& playerId) const {
  for (const Player& p : players_) {
    if (p.id == playerId) return &p;
  }
  return nullptr;
}

std::size_t Room::connectedCount() const {
  std::size_t count = 0;
  for (const Player& p : players_) {
    if (p.connected) ++count;
  }
  return count;
}

Player* Room::findPlayerMutable(const std::string& playerId) {
  for (Player& p : players_) {
    if (p.id == playerId) return &p;
  }
  return nullptr;
}

// rules.md: if the host disconnects, host passes to the connected player who
// joined earliest. If nobody is connected, the host stays as is until someone
// comes back (then that player becomes host).
void Room::reassignHostIfNeeded() {
  const Player* host = findPlayer(hostId_);
  if (host != nullptr && host->connected) return;
  for (const Player& p : players_) {  // players_ is in join order
    if (p.connected) {
      hostId_ = p.id;
      return;
    }
  }
}

std::string Room::makeToken() {
  static const char kHex[] = "0123456789abcdef";
  std::uniform_int_distribution<int> pick(0, 15);
  std::string token;
  for (int i = 0; i < 32; ++i) token += kHex[pick(rng_)];
  return token;
}

}  // namespace feast
