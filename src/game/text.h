#pragma once
#include <cstddef>
#include <string>

namespace feast {

// Removes leading and trailing ASCII whitespace.
std::string trimWhitespace(const std::string& s);

// Number of characters (Unicode code points) in a UTF-8 string, so "é" counts as 1.
std::size_t utf8Length(const std::string& s);

// Case-insensitive comparison (ASCII letters only; other bytes must match exactly).
bool equalsIgnoreCase(const std::string& a, const std::string& b);

// rules.md: names are 1-16 characters after trimming. Pass the already-trimmed name.
bool isValidName(const std::string& trimmedName);

}  // namespace feast
