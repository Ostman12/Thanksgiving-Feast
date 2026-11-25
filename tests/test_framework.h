#pragma once
// A tiny test harness, so we need no dependency.
//
//   TEST(some_name) { CHECK(x == 1); CHECK_EQ(a, b); }
//
// test_main.cpp runs every TEST and prints failures with file:line.

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace testing {

struct TestCase {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

inline int& failureCount() {
  static int count = 0;
  return count;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    registry().push_back({name, std::move(fn)});
  }
};

inline void fail(const char* file, int line, const std::string& message) {
  ++failureCount();
  std::cout << "  FAILED " << file << ":" << line << ": " << message << "\n";
}

}  // namespace testing

#define TEST(name)                                                  \
  static void test_##name();                                        \
  static testing::Registrar registrar_##name(#name, test_##name);   \
  static void test_##name()

#define CHECK(cond)                                                 \
  do {                                                              \
    if (!(cond)) testing::fail(__FILE__, __LINE__, #cond);          \
  } while (0)

#define CHECK_EQ(actual, expected)                                  \
  do {                                                              \
    auto actual_ = (actual);                                        \
    auto expected_ = (expected);                                    \
    if (!(actual_ == expected_)) {                                  \
      testing::fail(__FILE__, __LINE__,                             \
                    std::string(#actual " == " #expected));         \
    }                                                               \
  } while (0)
