#pragma once
// JSON encoding of server -> client messages (docs/protocol.md).
// Every message is {"type": "...", "payload": {...}}.

#include <string>

#include <nlohmann/json.hpp>

#include "game/events.h"

namespace feast {

nlohmann::json eventToJson(const Event& event);

nlohmann::json joinedMessage(const std::string& playerId, const std::string& token,
                             const std::string& code);
nlohmann::json errorMessage(const std::string& reason);

}  // namespace feast
