#pragma once

#include <cstdio>
#include <cstdlib>

namespace aowis_test {

[[noreturn]] inline void failCheck(const char *expression, const char *file, int line)
{
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expression);
    std::fflush(stderr);
    std::abort();
}

} // namespace aowis_test

// Unlike assert(), CHECK always evaluates its expression, including in NDEBUG builds.
#define CHECK(expression) do { \
    if (!(expression)) { \
        ::aowis_test::failCheck(#expression, __FILE__, __LINE__); \
    } \
} while (false)
