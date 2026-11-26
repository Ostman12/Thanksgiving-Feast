#pragma once
// Game content compiled into the server, so there are no files to lose or
// fail to parse on game night.

#include <vector>

namespace feast {

struct WordPair {
  const char* mainWord;
  const char* impostorWord;
};

const std::vector<WordPair>& wordPairs();

}  // namespace feast
