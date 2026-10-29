#include <cstdio>
#include <exception>

#include "test_util.hpp"

int main() {
    int passed = 0;
    int failed = 0;

    for (const auto& tc : tst::registry()) {
        try {
            tc.fn();
            std::printf("  PASS  %s\n", tc.name.c_str());
            ++passed;
        } catch (const std::exception& e) {
            std::printf("  FAIL  %s\n    %s\n", tc.name.c_str(), e.what());
            ++failed;
        } catch (...) {
            std::printf("  FAIL  %s\n    unknown exception\n", tc.name.c_str());
            ++failed;
        }
    }

    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
