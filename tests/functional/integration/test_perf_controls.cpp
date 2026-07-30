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

// Coverage for the rebalancer control: it is off unless asked for, execution is
// correct in both states, and the engine reports the flag faithfully.
//
// The default is the assertion that matters. The background thermal rebalancer
// migrates tasks between workers on a 5 ms heartbeat, which changes which worker
// folds which tile and puts a periodic wakeup under every frame — a throughput
// trade that a simulation frame is not buying anything with. Leaving it on by
// default meant every consumer paid for it and the primary one turned it off at
// every construction.

#include <cstddef>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

TEST(Integration_PerfControls, RebalancerIsOffUnlessRequested)
{
    auto rt = Runtime::create();
    EXPECT_FALSE(rt.advanced().rebalancer_enabled())
        << "the rebalancer must not run unless RuntimeConfig asked for it";

    API::RuntimeConfig config;
    config.rebalancer = true;
    auto throughput = Runtime::create({}, config);
    EXPECT_TRUE(throughput.advanced().rebalancer_enabled())
        << "RuntimeConfig::rebalancer must start the coordinator";
}

TEST(Integration_PerfControls, RebalancerToggleKeepsExecutionCorrect)
{
    constexpr std::size_t N = 128;

    auto rt = Runtime::create();

    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N,
          [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });

    // Off (the default): a run produces correct results with no background thread.
    EXPECT_FALSE(rt.advanced().rebalancer_enabled());
    g.run();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "rebalancer off, at i=" << i;

    // Enabled after construction: still correct, and the flag reflects the change.
    rt.rebalancer(true);
    EXPECT_TRUE(rt.advanced().rebalancer_enabled());
    g.run();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "rebalancer on, at i=" << i;

    // And back off again, so the transition is exercised in both directions.
    rt.rebalancer(false);
    EXPECT_FALSE(rt.advanced().rebalancer_enabled());
    g.run();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "rebalancer off again, at i=" << i;
}
