/****************************************************************/
/* test_main.cpp                                                */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_main.cpp
 * @brief Runs every GoogleTest case with the logger in synchronous mode.
 * @author Mustafa Garip
 * @see tests/common/README.md
 */

#include <cstdio>
#include <cstdlib>
#include <gtest/gtest.h>
#include <SushiRuntime/core/logger.hpp>

// SR_TSAN_BUILD is set under ThreadSanitizer by Clang's feature test or GCC's macro.
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
    // A TSan run exits without C++ teardown; see tests/common/README.md.
    std::fflush(nullptr);
    std::_Exit(res);
#endif

    return res;
}
