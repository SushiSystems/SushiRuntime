/**************************************************************************/
/* test_graph_id.cpp                                                      */
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
 * @file test_graph_id.cpp
 * @brief Tests GraphId, the process-wide identity of one owner of plans.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/api/detail/graph_id.hpp>

using SushiRuntime::Detail::GraphId;
using SushiRuntime::Detail::take_graph_id;

namespace
{
    /** @brief Takes @p per_thread ids on each of @p threads threads and returns the distinct ones. */
    std::set<std::uint64_t> ids_taken_on_threads(std::size_t threads, std::size_t per_thread)
    {
        std::vector<std::vector<std::uint64_t>> taken(threads);
        std::vector<std::thread> workers;
        for (std::size_t thread = 0; thread < threads; ++thread)
            workers.emplace_back([&taken, thread, per_thread]()
            {
                for (std::size_t i = 0; i < per_thread; ++i)
                    taken[thread].push_back(take_graph_id());
            });
        for (std::thread& worker : workers)
            worker.join();

        std::set<std::uint64_t> all;
        for (const auto& ids : taken)
            all.insert(ids.begin(), ids.end());
        return all;
    }
} // namespace

/** @brief Tests that two identities are non-zero and differ. */
TEST(Small_GraphId, IdsAreNonZeroAndDistinct)
{
    GraphId first;
    GraphId second;

    EXPECT_NE(first.value(), 0u);
    EXPECT_NE(second.value(), 0u);
    EXPECT_NE(first.value(), second.value());
}

/** @brief Tests that a moved-from identity reports 0 and the moved-to one keeps the value. */
TEST(Small_GraphId, AMovedFromIdReportsZero)
{
    GraphId first;
    const std::uint64_t kept = first.value();

    GraphId moved(std::move(first));

    EXPECT_EQ(moved.value(), kept) << "an identity survives a move";
    EXPECT_EQ(first.value(), 0u) << "the moved-from value stands for no graph";
}

/** @brief Tests that move assignment hands the identity over and leaves the source with none. */
TEST(Small_GraphId, MoveAssignmentHandsTheIdentityOver)
{
    GraphId source;
    GraphId target;
    const std::uint64_t kept = source.value();

    target = std::move(source);

    EXPECT_EQ(target.value(), kept);
    EXPECT_EQ(source.value(), 0u);
}

/** @brief Tests that the counter never repeats an id, even when several threads take them at once. */
TEST(Medium_GraphId, SeveralThreadsNeverTakeTheSameId)
{
    constexpr std::size_t K_THREADS = 4;
    constexpr std::size_t K_PER_THREAD = 500;

    const std::set<std::uint64_t> all = ids_taken_on_threads(K_THREADS, K_PER_THREAD);

    EXPECT_EQ(all.size(), K_THREADS * K_PER_THREAD);
    EXPECT_EQ(all.count(0), 0u);
}
