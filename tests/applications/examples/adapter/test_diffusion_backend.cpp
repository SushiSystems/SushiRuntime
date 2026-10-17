/**************************************************************************/
/* test_diffusion_backend.cpp                                             */
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
 * @file test_diffusion_backend.cpp
 * @brief Tests the adapter example's backend: a uniform field stays uniform, and the status names.
 * @see tests/applications/examples/adapter/README.md
 * @author Mustafa Garip
 */

#include <cmath>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "adapter/diffusion_backend.hpp"

using namespace HostProgram;

// SR_TSAN_BUILD is set under ThreadSanitizer by Clang's feature test or GCC's macro.
#if defined(__has_feature)
#  if __has_feature(thread_sanitizer)
#    define SR_TSAN_BUILD 1
#  endif
#endif
#if !defined(SR_TSAN_BUILD) && defined(__SANITIZE_THREAD__)
#  define SR_TSAN_BUILD 1
#endif

/** @brief Holds the tolerance of a diffused value, absolute; the field is uniform. */
constexpr float K_TOLERANCE = 1e-5f;

/** @brief Tests that a uniform field read back after ten steps is still the same value. */
TEST(Small_DiffusionBackend, AUniformFieldStaysUniform)
{
#if defined(SR_TSAN_BUILD)
    GTEST_SKIP() << "the device ThreadSanitizer of the SYCL runtime cannot reserve its shadow "
                    "memory beside the host one; see docs/reference/KNOWN_ISSUES.md";
#endif
    BackendHandle backend;
    const Status created = create_backend(backend);
    if (created.code == StatusCode::Unsupported)
        GTEST_SKIP() << created.message;
    ASSERT_TRUE(created.is_ok()) << created.message;

    const std::vector<float> initial(64, 3.0f);
    ASSERT_TRUE(allocate_field(*backend, initial, 0.1f).is_ok());
    ASSERT_TRUE(advance(*backend, 10).is_ok());
    std::vector<float> values;
    ASSERT_TRUE(read_field(*backend, values).is_ok());
    ASSERT_EQ(values.size(), initial.size());
    for (std::size_t index = 0; index < values.size(); ++index)
        EXPECT_NEAR(values[index], 3.0f, K_TOLERANCE) << "at " << index;
}

/** @brief Tests the name of every status code. */
TEST(Small_DiffusionBackend, EveryCodeHasAName)
{
    EXPECT_STREQ(to_string(StatusCode::Ok), "Ok");
    EXPECT_STREQ(to_string(StatusCode::OutOfMemory), "OutOfMemory");
    EXPECT_STREQ(to_string(StatusCode::BackendFault), "BackendFault");
    EXPECT_TRUE(Status{}.is_ok());
}
