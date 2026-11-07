/**************************************************************************/
/* test_seeded_executor.cpp                                               */
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
 * @file test_seeded_executor.cpp
 * @brief Tests that SeededExecutor runs every job once, in an order its seed replays.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/seeded_executor.hpp>

using SushiRuntime::SeededExecutor;

namespace
{
    /** @brief Posts jobs 0 to count - 1, each appending its number to order, and runs them all. */
    std::vector<int> order_of(SeededExecutor& executor, int count)
    {
        std::vector<int> order;
        for (int job = 0; job < count; ++job)
            executor.post([&order, job]() { order.push_back(job); });
        executor.run_all();
        return order;
    }
} // namespace

/** @brief Tests that two executors of one seed run the same posts in the same order. */
TEST(Small_SeededExecutor, OneSeedReplaysOneOrder)
{
    SeededExecutor first(7);
    SeededExecutor second(7);

    EXPECT_EQ(order_of(first, 16), order_of(second, 16));
}

/** @brief Tests that a job posted by a running job runs within the same run_all. */
TEST(Small_SeededExecutor, AJobPostedByAJobRunsInTheSameDrain)
{
    SeededExecutor executor;
    int inner = 0;
    executor.post([&executor, &inner]() { executor.post([&inner]() { ++inner; }); });

    EXPECT_EQ(executor.run_all(), 2u);
    EXPECT_EQ(inner, 1);
}

/** @brief Tests that run_one runs one job and leaves the rest pending. */
TEST(Small_SeededExecutor, RunOneLeavesTheOtherJobsPending)
{
    SeededExecutor executor;
    executor.post([]() {});
    executor.post([]() {});

    const bool ran = executor.run_one();

    EXPECT_TRUE(ran);
    EXPECT_EQ(executor.pending(), 1u);
}
