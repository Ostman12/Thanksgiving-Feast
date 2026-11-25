#pragma once
#include <random>
#include <string>

namespace feast {

// rules.md: 4 uppercase letters, no I or O.
std::string makeRoomCode(std::mt19937& rng);
bool isValidRoomCode(const std::string& code);

}  // namespace feast
