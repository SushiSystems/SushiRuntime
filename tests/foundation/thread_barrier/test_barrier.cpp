/**************************************************************************/
/* test_barrier.cpp                                                       */
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
 * @file test_barrier.cpp
 * @brief Tests Barrier: no thread passes before the last arrives, and it serves again.
 * @see tests/foundation/thread_barrier/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/thread_barrier/barrier.hpp>

using namespace SushiRuntime::Test;

namespace
{
    /** @brief Holds the threads that meet at the barrier. */
    constexpr std::size_t K_THREADS = 4;

    /** @brief Holds what the threads of a run counted. */
    struct BarrierRun
    {
        std::size_t arrivals;      /**< Arrivals made over every round. */
        std::size_t early_passes;  /**< Passes made before every thread of the round had arrived. */
    };

    /** @brief Runs @p threads threads through @p rounds rounds at one barrier and counts early passes. */
    BarrierRun meet_at_barrier(std::size_t threads, int rounds)
    {
        Barrier barrier(threads);
        std::atomic<std::size_t> arrived{0};
        std::atomic<std::size_t> early{0};
        std::vector<std::thread> workers;
        for (std::size_t index = 0; index < threads; ++index)
        {
            workers.emplace_back([&]
            {
                for (int round = 1; round <= rounds; ++round)
                {
                    arrived.fetch_add(1);
                    barrier.arrive_and_wait();
                    early.fetch_add(arrived.load() < threads * static_cast<std::size_t>(round) ? 1 : 0);
                    barrier.arrive_and_wait();
                }
            });
        }
        for (std::thread& worker : workers)
            worker.join();
        return BarrierRun{arrived.load(), early.load()};
    }
} // namespace

/** @brief Tests that every thread arrived before any passed, over two rounds. */
TEST(Medium_Barrier, NoThreadPassesBeforeTheLastArrives)
{
    const BarrierRun run = meet_at_barrier(K_THREADS, 2);

    EXPECT_EQ(run.early_passes, 0u) << "a thread passed before every thread arrived";
    EXPECT_EQ(run.arrivals, 2 * K_THREADS);
}

/** @brief Tests that a barrier of one returns at once, on the calling thread, both times it is used. */
TEST(Small_Barrier, ABarrierOfOneNeverWaits)
{
    Barrier barrier(1);
    int passes = 0;

    barrier.arrive_and_wait();
    ++passes;
    barrier.arrive_and_wait();
    ++passes;

    EXPECT_EQ(passes, 2);
}
