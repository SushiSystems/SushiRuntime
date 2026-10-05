/****************************************************************/
/* test_perf_controls.cpp                                       */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_perf_controls.cpp
 * @brief Tests that the rebalancer is off unless requested and that runs are correct either way.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

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
