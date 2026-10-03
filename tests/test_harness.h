#pragma once

#include <cmath>
#include <cstdio>

class AowisTestHarness
{
public:
    void expectTrue(bool condition, const char *message)
    {
        ++this->check_count;
        if (condition)
            return;

        std::fprintf(stderr, "FAIL [%s]: %s\n",
                     this->current_case_name != nullptr ? this->current_case_name : "unregistered",
                     message);
        ++this->failure_count;
    }

    void expectNear(double actual, double expected, double tolerance, const char *message)
    {
        ++this->check_count;
        if (std::isfinite(actual) && std::abs(actual - expected) <= tolerance)
            return;

        std::fprintf(stderr,
                     "FAIL [%s]: %s (actual=%.12f expected=%.12f tolerance=%.12f)\n",
                     this->current_case_name != nullptr ? this->current_case_name : "unregistered",
                     message, actual, expected, tolerance);
        ++this->failure_count;
    }

    void runCase(const char *case_name, void (*test_function)())
    {
        ++this->case_count;
        this->current_case_name = case_name;
        test_function();
        this->current_case_name = nullptr;
    }

    int finish() const
    {
        std::printf("TEST SUMMARY: %d test case(s), %d check(s), %d failure(s)\n",
                    this->case_count, this->check_count, this->failure_count);
        return this->failure_count == 0 ? 0 : 1;
    }

private:
    int case_count = 0;
    int check_count = 0;
    int failure_count = 0;
    const char *current_case_name = nullptr;
};
