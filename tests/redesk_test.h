#pragma once

// REDesk minimal test harness.
//
// ADR-001 §5 (repo structure: tests/) — the stub build must be dependency-free,
// so we do NOT pull in GoogleTest/Catch2. This header is a tiny self-contained
// harness: TEST(suite, name) registers a case; ASSERT_*/EXPECT_* record
// failures; REDESK_TEST_MAIN() provides a runner that prints a summary and
// returns non-zero on any failure. Each test executable links exactly one .cpp
// that uses REDESK_TEST_MAIN(), and tests/CMakeLists.txt registers it with
// add_test() so `ctest` drives the suite.
//
// Design notes:
//   * ASSERT_* aborts the current test case (returns from the case function);
//     EXPECT_* records the failure but continues.
//   * Cases self-register at static-init time via a small registry.
//   * No threads, no exceptions required, no platform calls — portable on any
//     C++20 toolchain (configured on macOS/clang with no Qt for the stub build).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace redesk::test {

struct TestCase {
    const char* suite;
    const char* name;
    std::function<void(struct TestContext&)> fn;
};

struct TestContext {
    int failures = 0;     // failed assertions in the current case
    bool aborted = false; // set by ASSERT_* on failure to stop the case
    std::string current;  // "suite.name" for diagnostics
};

class Registry {
public:
    static Registry& instance() {
        static Registry r;
        return r;
    }
    void add(TestCase tc) { cases_.push_back(std::move(tc)); }
    const std::vector<TestCase>& cases() const { return cases_; }

private:
    std::vector<TestCase> cases_;
};

struct Registrar {
    Registrar(const char* suite, const char* name,
              std::function<void(TestContext&)> fn) {
        Registry::instance().add(TestCase{suite, name, std::move(fn)});
    }
};

inline int runAll() {
    const auto& cases = Registry::instance().cases();
    int total = static_cast<int>(cases.size());
    int passed = 0;
    int failed = 0;

    std::printf("[==========] Running %d test(s).\n", total);
    for (const auto& tc : cases) {
        TestContext ctx;
        ctx.current = std::string(tc.suite) + "." + tc.name;
        std::printf("[ RUN      ] %s\n", ctx.current.c_str());
        tc.fn(ctx);
        if (ctx.failures == 0) {
            std::printf("[       OK ] %s\n", ctx.current.c_str());
            ++passed;
        } else {
            std::printf("[  FAILED  ] %s (%d assertion failure(s))\n",
                        ctx.current.c_str(), ctx.failures);
            ++failed;
        }
    }
    std::printf("[==========] %d passed, %d failed.\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

} // namespace redesk::test

// ---------------------------------------------------------------------------
// Case registration. The body becomes a function taking a TestContext& named
// `_ctx`; assertion macros reference `_ctx`.
// ---------------------------------------------------------------------------
#define REDESK_TEST_UNIQUE_(a, b) a##b
#define REDESK_TEST_UNIQUE(a, b) REDESK_TEST_UNIQUE_(a, b)

#define TEST(suite, name)                                                     \
    static void REDESK_TEST_UNIQUE(redesk_test_fn_, __LINE__)(                 \
        ::redesk::test::TestContext& _ctx);                                   \
    static ::redesk::test::Registrar REDESK_TEST_UNIQUE(redesk_test_reg_,      \
                                                        __LINE__)(            \
        #suite, #name, &REDESK_TEST_UNIQUE(redesk_test_fn_, __LINE__));        \
    static void REDESK_TEST_UNIQUE(redesk_test_fn_, __LINE__)(                 \
        ::redesk::test::TestContext& _ctx)

// ---------------------------------------------------------------------------
// Assertion macros. EXPECT_* continue; ASSERT_* return from the case.
// ---------------------------------------------------------------------------
#define REDESK_FAIL_(msg)                                                     \
    do {                                                                       \
        ++_ctx.failures;                                                       \
        std::printf("    %s:%d: FAILURE: %s\n", __FILE__, __LINE__, (msg));    \
    } while (0)

#define EXPECT_TRUE(cond)                                                     \
    do {                                                                       \
        if (!(cond)) REDESK_FAIL_("expected true: " #cond);                    \
    } while (0)

#define EXPECT_FALSE(cond)                                                    \
    do {                                                                       \
        if ((cond)) REDESK_FAIL_("expected false: " #cond);                    \
    } while (0)

#define EXPECT_EQ(a, b)                                                       \
    do {                                                                       \
        if (!((a) == (b))) REDESK_FAIL_("expected equal: " #a " == " #b);      \
    } while (0)

#define EXPECT_NE(a, b)                                                       \
    do {                                                                       \
        if (!((a) != (b))) REDESK_FAIL_("expected not equal: " #a " != " #b);  \
    } while (0)

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                      \
        if (!(cond)) {                                                        \
            REDESK_FAIL_("ASSERT expected true: " #cond);                     \
            _ctx.aborted = true;                                             \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(cond)                                                   \
    do {                                                                      \
        if ((cond)) {                                                        \
            REDESK_FAIL_("ASSERT expected false: " #cond);                    \
            _ctx.aborted = true;                                            \
            return;                                                         \
        }                                                                   \
    } while (0)

#define ASSERT_EQ(a, b)                                                      \
    do {                                                                     \
        if (!((a) == (b))) {                                                 \
            REDESK_FAIL_("ASSERT expected equal: " #a " == " #b);            \
            _ctx.aborted = true;                                           \
            return;                                                        \
        }                                                                  \
    } while (0)

#define ASSERT_NE(a, b)                                                      \
    do {                                                                     \
        if (!((a) != (b))) {                                                 \
            REDESK_FAIL_("ASSERT expected not equal: " #a " != " #b);        \
            _ctx.aborted = true;                                           \
            return;                                                        \
        }                                                                  \
    } while (0)

#define REDESK_TEST_MAIN()                                                   \
    int main() { return ::redesk::test::runAll(); }
