#include "game/avatar.h"

#include <algorithm>

namespace feast {

// Keep these in sync with rules.md and AVATAR_* in client/common.js.
const std::vector<std::string>& avatarEmojis() {
  static const std::vector<std::string> names = {
      "turkey", "pie",   "leaf",    "corn", "maple",  "bear", "fox",  "dog",
      "cat",    "frog",  "panda",   "penguin", "owl", "cowboy", "cool", "alien"};
  return names;
}

const std::vector<std::string>& avatarColors() {
  static const std::vector<std::string> names = {"orange", "cranberry", "gold", "green",
                                                 "blue",   "purple",    "pink", "brown"};
  return names;
}

const std::vector<std::string>& avatarAccessories() {
  static const std::vector<std::string> names = {"none", "tophat",  "crown", "bow",
                                                 "cap",  "gradcap", "flower"};
  return names;
}

namespace {
bool onList(const std::vector<std::string>& list, const std::string& name) {
  return std::find(list.begin(), list.end(), name) != list.end();
}

const std::string& pick(const std::vector<std::string>& list, std::mt19937& rng) {
  return list[std::uniform_int_distribution<std::size_t>(0, list.size() - 1)(rng)];
}
}  // namespace

bool isValidAvatar(const Avatar& avatar) {
  return onList(avatarEmojis(), avatar.emoji) && onList(avatarColors(), avatar.color) &&
         onList(avatarAccessories(), avatar.accessory);
}

Avatar randomAvatar(std::mt19937& rng) {
  Avatar avatar;
  avatar.emoji = pick(avatarEmojis(), rng);
  avatar.color = pick(avatarColors(), rng);
  avatar.accessory = "none";
  return avatar;
}

}  // namespace feast
