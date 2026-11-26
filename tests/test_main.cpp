#include <cstring>

#include "test_framework.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif

// Usage: feast_tests [-v]   (-v prints each test name before it runs)
int main(int argc, char** argv) {
#ifdef _MSC_VER
  // Debug builds show a modal dialog on a failed assertion, which hangs an
  // unattended test run. Print to stderr and abort instead.
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
  bool verbose = argc > 1 && std::strcmp(argv[1], "-v") == 0;

  int failedTests = 0;
  for (const testing::TestCase& test : testing::registry()) {
    if (verbose) std::cout << "RUN  " << test.name << std::endl;
    int before = testing::failureCount();
    try {
      test.fn();
    } catch (const std::exception& e) {
      testing::fail(__FILE__, __LINE__, std::string("exception: ") + e.what());
    } catch (...) {
      testing::fail(__FILE__, __LINE__, "unknown exception");
    }
    if (testing::failureCount() != before) {
      ++failedTests;
      std::cout << "FAIL " << test.name << "\n";
    }
  }
  std::cout << testing::registry().size() - failedTests << "/"
            << testing::registry().size() << " tests passed\n";
  return failedTests == 0 ? 0 : 1;
}
