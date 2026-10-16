/**************************************************************************/
/* test_main.cpp                                                          */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_main.cpp
 * @brief Runs every GoogleTest case with the logger in synchronous mode.
 * @author Mustafa Garip
 */

#include <cstdio>
#include <cstdlib>

#include <gtest/gtest.h>

#include <SushiRuntime/log/logger.hpp>

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

#if defined(SR_TSAN_BUILD)
    // A TSan run exits without C++ teardown; see modules/foundation/test_runner/README.md.
    std::fflush(nullptr);
    std::_Exit(res);
#endif

    return res;
}
