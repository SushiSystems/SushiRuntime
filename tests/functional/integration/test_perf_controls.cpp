/**************************************************************************/
/* test_perf_controls.cpp                                                 */
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

// WP-PERF coverage: the rebalancer opt-out. Execution must stay correct with the
// background thermal rebalancer disabled (the low-jitter / deterministic mode)
// and after re-enabling it, and the engine must report the flag faithfully.

#include <cstddef>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

TEST(Integration_PerfControls, RebalancerToggleKeepsExecutionCorrect)
{
    constexpr std::size_t N = 128;

    auto rt = Runtime::create();

    // On by default.
    EXPECT_TRUE(rt.context().get_execution_engine().rebalancer_enabled());

    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N,
          [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });

    // Disabled: a run still produces correct results without the background thread.
    rt.rebalancer(false);
    EXPECT_FALSE(rt.context().get_execution_engine().rebalancer_enabled());
    g.run();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "rebalancer off, at i=" << i;

    // Re-enabled: still correct, and the flag reflects the change.
    rt.rebalancer(true);
    EXPECT_TRUE(rt.context().get_execution_engine().rebalancer_enabled());
    g.run();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "rebalancer on, at i=" << i;
}
