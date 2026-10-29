#include "job/job.hpp"
#include "test_util.hpp"

using namespace seicure;

namespace {

void check_size(std::uint64_t bytes, const wchar_t* expected, int line) {
    const std::wstring actual = job::format_size(bytes);
    if (actual != expected) {
        // Narrow the wide strings for the failure message.
        std::string want, got;
        for (wchar_t c : std::wstring(expected)) want.push_back(static_cast<char>(c));
        for (wchar_t c : actual) got.push_back(static_cast<char>(c));
        ::tst::fail("format_size mismatch\n      want: " + want +
                        "\n      got:  " + got,
                    __FILE__, line);
    }
}

}  // namespace

TEST(format_size_keeps_the_whole_unit) {
    // Regression: in a wide printf, %s means a narrow string, so L"KB" was
    // being truncated to "K" on MinGW. The unit suffix must survive intact.
    check_size(0, L"0 B", __LINE__);
    check_size(1, L"1 B", __LINE__);
    check_size(999, L"999 B", __LINE__);
    check_size(1024, L"1.0 KB", __LINE__);
    check_size(50000, L"48.8 KB", __LINE__);
    check_size(1024ULL * 1024ULL, L"1.0 MB", __LINE__);
    check_size(41943040ULL, L"40.0 MB", __LINE__);
    check_size(1024ULL * 1024ULL * 1024ULL, L"1.0 GB", __LINE__);
    check_size(1024ULL * 1024ULL * 1024ULL * 1024ULL, L"1.0 TB", __LINE__);
}

TEST(format_size_never_returns_empty) {
    for (std::uint64_t n : {std::uint64_t{0}, std::uint64_t{1023},
                            std::uint64_t{1025}, ~std::uint64_t{0}}) {
        CHECK(!job::format_size(n).empty());
    }
}
