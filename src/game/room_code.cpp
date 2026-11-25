#include "game/room_code.h"

namespace feast {

namespace {
const std::string kLetters = "ABCDEFGHJKLMNPQRSTUVWXYZ";  // A-Z without I and O
const std::size_t kCodeLength = 4;
}  // namespace

std::string makeRoomCode(std::mt19937& rng) {
  std::uniform_int_distribution<std::size_t> pick(0, kLetters.size() - 1);
  std::string code;
  for (std::size_t i = 0; i < kCodeLength; ++i) code += kLetters[pick(rng)];
  return code;
}

bool isValidRoomCode(const std::string& code) {
  if (code.size() != kCodeLength) return false;
  for (char c : code) {
    if (kLetters.find(c) == std::string::npos) return false;
  }
  return true;
}

}  // namespace feast
