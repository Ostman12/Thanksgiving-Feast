#pragma once
// Player avatars (rules.md, Players and rooms): an emoji, a background color
// and an accessory, each a name from a fixed list. The server only checks and
// stores names; client/common.js turns them into emoji and colors.

#include <random>
#include <string>
#include <vector>

namespace feast {

struct Avatar {
  std::string emoji = "turkey";
  std::string color = "orange";
  std::string accessory = "none";
};

const std::vector<std::string>& avatarEmojis();
const std::vector<std::string>& avatarColors();
const std::vector<std::string>& avatarAccessories();  // includes "none"

// Every part is a name on its list.
bool isValidAvatar(const Avatar& avatar);

// A random emoji and color, no accessory (players add their own).
Avatar randomAvatar(std::mt19937& rng);

}  // namespace feast
