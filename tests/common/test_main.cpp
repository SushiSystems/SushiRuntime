/****************************************************************/
/* test_main.cpp                                                */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <SushiRuntime/core/logger.hpp>

// Detect a ThreadSanitizer build across compilers (Clang/IntelLLVM feature test
// and the GCC-style predefined macro).
#if defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define SR_TSAN_BUILD 1
#  endif
#endif
#if !defined(SR_TSAN_BUILD) && defined(__SANITIZE_THREAD__)
#  define SR_TSAN_BUILD 1
#endif

/**
 * @brief Entry point for the test suite.
 *
 * Initializes the logger and GoogleTest, then runs all test cases.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return 0 on success, non-zero if any tests fail.
 */
int main(int argc, char** argv)
{
    // Stream logs synchronously so they don't interleave with GTest output.
    SR_LOGGER_SET_SYNC_MODE(true);

    ::testing::InitGoogleTest(&argc, argv);

    int res = RUN_ALL_TESTS();
    SR_LOG_INFO("All tests finished with result: {}", res);

#if defined(SR_TSAN_BUILD)
    // Under the TSan CI lane the Intel oneAPI SYCL runtime's DeviceSanitizer layer
    // crashes deterministically in its own global/atexit teardown — after every
    // test has already passed and the worker pool has joined — turning green runs
    // into a SEGV and failing the job. The crash is entirely inside the
    // uninstrumented Intel runtime's shutdown, not our code (workers are joined and
    // all RuntimeContexts destroyed by this point). Data races are reported live
    // during the run (halt_on_error=1 aborts immediately), so nothing of value is
    // lost by skipping the C++ teardown: flush our output and _Exit with the real
    // gtest result, bypassing the third-party destructors that fault. Scoped to TSan
    // so ASan's leak check and normal builds keep a full, ordered shutdown.
    std::fflush(nullptr);
    std::_Exit(res);
#endif

    return res;
}
