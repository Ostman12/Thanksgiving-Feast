#include "game/text.h"

#include <cctype>

namespace feast {

namespace {
bool isAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
}  // namespace

std::string trimWhitespace(const std::string& s) {
  std::size_t start = 0;
  std::size_t end = s.size();
  while (start < end && isAsciiSpace(s[start])) ++start;
  while (end > start && isAsciiSpace(s[end - 1])) --end;
  return s.substr(start, end - start);
}

std::size_t utf8Length(const std::string& s) {
  // Count every byte except UTF-8 continuation bytes (10xxxxxx).
  std::size_t count = 0;
  for (unsigned char c : s) {
    if ((c & 0xC0) != 0x80) ++count;
  }
  return count;
}

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    unsigned char x = static_cast<unsigned char>(a[i]);
    unsigned char y = static_cast<unsigned char>(b[i]);
    if (x < 128 && y < 128) {
      if (std::tolower(x) != std::tolower(y)) return false;
    } else if (x != y) {
      return false;
    }
  }
  return true;
}

bool isValidName(const std::string& trimmedName) {
  std::size_t len = utf8Length(trimmedName);
  return len >= 1 && len <= 16;
}

}  // namespace feast
