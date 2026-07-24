/**************************************************************************/
/* test_main.cpp                                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

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
