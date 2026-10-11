/**************************************************************************/
/* test_cross_context_guard.cpp                                           */
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
 * @file test_cross_context_guard.cpp
 * @brief Tests that a node spanning two device contexts fails the run with an error.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <gtest/gtest.h>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

TEST(Integration_CrossContextGuard, NodeSpanningTwoContextsFailsCleanly)
{
    constexpr std::size_t N = 256;

    auto rt = Runtime::create();
    if (rt.advanced().device_count() < 2)
        GTEST_SKIP() << "needs at least two device contexts to straddle a boundary";

    // Two buffers pinned to different device queues (and thus contexts).
    auto a = rt.buffer<int>(N, DeviceIndex{0});
    auto b = rt.buffer<int>(N, DeviceIndex{1});
    int* pb = b.data();
    const int* pa = a.data();

    // No one queue can run a node that reads context 0 and writes context 1; see README.md.
    auto g = rt.graph();
    g.add(N, Reads(a), Writes(b),
          [pa, pb](std::size_t i) { pb[i] = pa[i] + 1; });

    // The run fails with an error and the runtime stays usable afterward; see README.md.
    auto result = g.try_run();
    ASSERT_FALSE(result) << "a cross-context node must not run successfully";
    EXPECT_EQ(result.error().code, ErrorCode::InvalidGraph);

    // A well-formed, co-located graph still runs on the same runtime afterward.
    for (std::size_t i = 0; i < N; ++i) a[i] = static_cast<int>(i);
    int* pa2 = a.data();
    auto g2 = rt.graph();
    g2.add(N, Reads{}, Writes(a), [pa2](std::size_t i) { pa2[i] *= 2; });
    auto ok = g2.try_run();
    EXPECT_TRUE(ok) << "the pool must stay re-armable after the guarded failure";
}
