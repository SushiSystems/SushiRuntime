/**************************************************************************/
/* test_advanced_controls.cpp                                             */
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
 * @file test_advanced_controls.cpp
 * @brief Tests the rebalancer control.
 * @see tests/surface/api/runtime/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

TEST(Medium_PerfControls, RebalancerIsOffUnlessRequested)
{
    auto rt = Runtime::create().value();
    EXPECT_FALSE(rt.advanced().is_rebalancer_enabled())
        << "the rebalancer must not run unless RuntimeConfig asked for it";

    RuntimeConfig config;
    config.rebalancer = true;
    auto throughput = Runtime::create(config).value();
    EXPECT_TRUE(throughput.advanced().is_rebalancer_enabled())
        << "RuntimeConfig::rebalancer must start the coordinator";
}

TEST(Medium_PerfControls, RebalancerToggleKeepsExecutionCorrect)
{
    constexpr std::size_t N = 128;

    auto rt = Runtime::create().value();

    auto buf = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto g = rt.graph();
    g.add(N, Reads{}, Writes(buf),
          [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });

    // Off (the default): a run produces correct results with no background thread.
    EXPECT_FALSE(rt.advanced().is_rebalancer_enabled());
    g.run().value();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "rebalancer off, at i=" << i;

    // Enabled after construction: still correct, and the flag reflects the change.
    rt.set_rebalancer_enabled(true);
    EXPECT_TRUE(rt.advanced().is_rebalancer_enabled());
    g.run().value();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "rebalancer on, at i=" << i;

    // And back off again, so the transition is exercised in both directions.
    rt.set_rebalancer_enabled(false);
    EXPECT_FALSE(rt.advanced().is_rebalancer_enabled());
    g.run().value();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "rebalancer off again, at i=" << i;
}
