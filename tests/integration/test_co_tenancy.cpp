/**************************************************************************/
/* test_co_tenancy.cpp                                                    */
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
 * @file test_co_tenancy.cpp
 * @brief Tests the worker count, core range and pinning policy a host sets to share its machine.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    /** @brief Runs a small graph and checks every element of its output. */
    void expect_runs_correctly(API::Runtime& rt)
    {
        constexpr std::size_t N = 512;

        auto buf = rt.buffer<int>(N);
        for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
        int* p = buf.data();

        auto g = rt.graph();
        g.add(API::Reads{}, API::Writes(buf), N,
              [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
        g.run();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "at i=" << i;
    }

    std::size_t machine_cores()
    {
        const std::size_t cores = std::thread::hardware_concurrency();
        return cores == 0 ? 1u : cores;
    }
}

/** @brief Tests that the default pool is one worker per logical core, starting at core 0. */
TEST(Integration_CoTenancy, DefaultsToOneWorkerPerCoreFromCoreZero)
{
    auto rt = Runtime::create();
    const auto pool = rt.advanced().worker_pool();

    EXPECT_EQ(pool.count, machine_cores());
    EXPECT_EQ(pool.first_core, 0u);
    EXPECT_EQ(pool.pinning, API::PinPolicy::Pin);
}

/**
 * @brief Tests that the pool takes exactly the worker count and core placement the host asked for.
 */
TEST(Integration_CoTenancy, ReservesTheCoresTheHostAskedFor)
{
    const std::size_t cores = machine_cores();
    if (cores < 4)
        GTEST_SKIP() << "needs at least 4 logical cores to reserve a range";

    const std::size_t reserved = 2;

    API::RuntimeConfig config;
    config.worker_count = cores - reserved;
    config.first_core   = reserved;

    auto rt = Runtime::create({}, config);
    const auto pool = rt.advanced().worker_pool();

    EXPECT_EQ(pool.count, cores - reserved);
    EXPECT_EQ(pool.first_core, reserved);

    expect_runs_correctly(rt);
}

/** @brief Tests that a single worker still executes the whole graph. */
TEST(Integration_CoTenancy, ASingleWorkerStillExecutesTheWholeGraph)
{
    API::RuntimeConfig config;
    config.worker_count = 1;

    auto rt = Runtime::create({}, config);
    EXPECT_EQ(rt.advanced().worker_pool().count, 1u);

    expect_runs_correctly(rt);
}

/** @brief Tests that the Float policy keeps the requested pool size and binds no worker. */
TEST(Integration_CoTenancy, FloatPolicyRunsUnpinned)
{
    API::RuntimeConfig config;
    config.worker_count = 2;
    config.pinning      = API::PinPolicy::Float;

    auto rt = Runtime::create({}, config);
    const auto pool = rt.advanced().worker_pool();

    EXPECT_EQ(pool.count, 2u);
    EXPECT_EQ(pool.pinning, API::PinPolicy::Float);

    expect_runs_correctly(rt);
}

/** @brief Tests that a core range past the end of the machine is clamped. */
TEST(Integration_CoTenancy, AnOutOfRangeCoreRangeIsClampedNotIgnored)
{
    const std::size_t cores = machine_cores();

    API::RuntimeConfig config;
    config.worker_count = 4;
    config.first_core   = cores + 8;   // entirely past the last core

    auto rt = Runtime::create({}, config);
    const auto pool = rt.advanced().worker_pool();

    EXPECT_LE(pool.first_core + pool.count, cores)
        << "the worker core range must fit inside the machine";
    EXPECT_GE(pool.count, 1u) << "clamping must not leave an empty pool";

    expect_runs_correctly(rt);
}
