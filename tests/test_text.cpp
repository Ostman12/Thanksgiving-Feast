#include "game/text.h"
#include "test_framework.h"

using namespace feast;

TEST(trim_removes_surrounding_whitespace) {
  CHECK_EQ(trimWhitespace("  Ann \t\n"), std::string("Ann"));
  CHECK_EQ(trimWhitespace("Mary Ann"), std::string("Mary Ann"));
  CHECK_EQ(trimWhitespace("   "), std::string(""));
  CHECK_EQ(trimWhitespace(""), std::string(""));
}

TEST(utf8_length_counts_characters_not_bytes) {
  CHECK_EQ(utf8Length("abc"), std::size_t(3));
  CHECK_EQ(utf8Length("Zoë"), std::size_t(3));
  CHECK_EQ(utf8Length("🦃"), std::size_t(1));
}

TEST(equals_ignore_case) {
  CHECK(equalsIgnoreCase("Ann", "aNN"));
  CHECK(!equalsIgnoreCase("Ann", "Anne"));
  CHECK(!equalsIgnoreCase("Ann", "Bob"));
}

TEST(name_length_1_to_16) {
  CHECK(!isValidName(""));
  CHECK(isValidName("A"));
  CHECK(isValidName("abcdefghijklmnop"));    // 16
  CHECK(!isValidName("abcdefghijklmnopq"));  // 17
  CHECK(isValidName("éééééééééééééééé"));    // 16 characters, 32 bytes
}
