/**************************************************************************/
/* test_worker_pool_sizing.cpp                                           */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_worker_pool_sizing.cpp
 * @brief Tests size_worker_pool(): the default count, the cap and the clamped core range.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/core/common.hpp>
#include <SushiRuntime/scheduler/worker_pool_sizing.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Scheduler;

namespace
{
    /** @brief Returns pool settings of @p workers workers from core @p first_core, pinned or floating. */
    WorkerPoolSettings pool_of(std::size_t workers, std::size_t first_core, PinPolicy pinning)
    {
        WorkerPoolSettings pool;
        pool.worker_count = workers;
        pool.first_core = first_core;
        pool.pinning = pinning;
        return pool;
    }
} // namespace

/** @brief Tests that a pool of 0 workers runs one per core. */
TEST(Small_WorkerPoolSizing, ZeroWorkersRunsOnePerCore)
{
    EXPECT_EQ(size_worker_pool(pool_of(0, 0, PinPolicy::Pin), 8).workers, 8u);
}

/** @brief Tests that a machine that reports no core counts as one. */
TEST(Small_WorkerPoolSizing, AMachineOfNoCoresCountsAsOne)
{
    EXPECT_EQ(size_worker_pool(pool_of(0, 0, PinPolicy::Float), 0).workers, 1u);
}

/** @brief Tests that a count above the maximum is capped. */
TEST(Small_WorkerPoolSizing, ACountAboveTheMaximumIsCapped)
{
    const PoolSize size = size_worker_pool(pool_of(K_MAX_WORKER_THREADS + 5, 0, PinPolicy::Float), 4);

    EXPECT_EQ(size.workers, K_MAX_WORKER_THREADS);
}

/** @brief Tests that a pinned range that starts past the last core starts again at core 0. */
TEST(Small_WorkerPoolSizing, APinnedRangeThatStartsPastTheLastCoreStartsAtZero)
{
    const PoolSize size = size_worker_pool(pool_of(4, 9, PinPolicy::Pin), 8);

    EXPECT_EQ(size.first_core, 0u);
    EXPECT_EQ(size.workers, 4u);
}

/** @brief Tests that a pinned range wider than the machine keeps the cores from its start to the last. */
TEST(Small_WorkerPoolSizing, APinnedRangeWiderThanTheMachineKeepsTheCoresLeft)
{
    const PoolSize size = size_worker_pool(pool_of(6, 2, PinPolicy::Pin), 4);

    EXPECT_EQ(size.first_core, 2u);
    EXPECT_EQ(size.workers, 2u);
}

/** @brief Tests that a floating pool is never clamped to the core range. */
TEST(Small_WorkerPoolSizing, AFloatingPoolIsNotClamped)
{
    const PoolSize size = size_worker_pool(pool_of(6, 2, PinPolicy::Float), 4);

    EXPECT_EQ(size.first_core, 2u);
    EXPECT_EQ(size.workers, 6u);
}
