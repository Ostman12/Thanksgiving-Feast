#include "test_framework.h"

int main() {
  int failedTests = 0;
  for (const testing::TestCase& test : testing::registry()) {
    int before = testing::failureCount();
    test.fn();
    if (testing::failureCount() != before) {
      ++failedTests;
      std::cout << "FAIL " << test.name << "\n";
    }
  }
  std::cout << testing::registry().size() - failedTests << "/"
            << testing::registry().size() << " tests passed\n";
  return failedTests == 0 ? 0 : 1;
}
