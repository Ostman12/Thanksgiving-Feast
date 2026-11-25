#include "game/room_code.h"
#include "test_framework.h"

using namespace feast;

TEST(generated_room_codes_are_valid) {
  std::mt19937 rng(123);
  for (int i = 0; i < 1000; ++i) {
    std::string code = makeRoomCode(rng);
    CHECK(isValidRoomCode(code));
    CHECK(code.find('I') == std::string::npos);
    CHECK(code.find('O') == std::string::npos);
  }
}

TEST(room_code_validation) {
  CHECK(isValidRoomCode("ABCD"));
  CHECK(!isValidRoomCode("abcd"));
  CHECK(!isValidRoomCode("ABC"));
  CHECK(!isValidRoomCode("ABCDE"));
  CHECK(!isValidRoomCode("ABCI"));
  CHECK(!isValidRoomCode("OBCD"));
  CHECK(!isValidRoomCode("AB1D"));
}
