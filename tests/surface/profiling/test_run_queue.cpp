/**************************************************************************/
/* test_run_queue.cpp                                                     */
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
 * @file test_run_queue.cpp
 * @brief Tests RunQueue, the bounded hand-over of observations from the runs' threads to the consumer.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/profiling/run_queue.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Profiling::QueuedRun;
using SushiRuntime::Internal::Profiling::RunQueue;

namespace
{
    /** @brief Builds an observation of @p records records. */
    RunObservation observation_of(std::size_t records)
    {
        RunObservation observation;
        observation.events.resize(records);
        return observation;
    }

    /** @brief Pushes the observations of runtimes 0 to @p count - 1, runtime r holding r + 1 records. */
    void push_runtimes(RunQueue& queue, std::uint32_t count)
    {
        for (std::uint32_t runtime = 0; runtime < count; ++runtime)
            queue.push(runtime, observation_of(runtime + 1));
    }

    /** @brief Pushes @p per_thread observations from each of @p threads threads and joins them. */
    void push_from_threads(RunQueue& queue, std::size_t threads, std::size_t per_thread)
    {
        std::vector<std::thread> producers;
        for (std::size_t thread = 0; thread < threads; ++thread)
            producers.emplace_back([&queue, thread, per_thread]()
            {
                for (std::size_t run = 0; run < per_thread; ++run)
                    queue.push(static_cast<std::uint32_t>(thread), observation_of(1));
            });
        for (std::thread& producer : producers)
            producer.join();
    }

    /** @brief Takes from the queue until @p pushing turns false, and returns how many observations it took. */
    std::size_t take_while_pushing(RunQueue& queue, const std::atomic<bool>& pushing)
    {
        std::size_t taken = 0;
        while (pushing.load(std::memory_order_acquire))
            taken += queue.take().size();
        return taken;
    }
} // namespace

/** @brief Tests that a queue that holds some observations hands them over oldest first. */
TEST(Small_RunQueue, TakesEveryObservationOldestFirst)
{
    RunQueue queue(8);
    queue.push(0, observation_of(1));
    queue.push(1, observation_of(2));
    queue.push(2, observation_of(3));

    const std::vector<QueuedRun> runs = queue.take();

    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[0].runtime, 0u);
    EXPECT_EQ(runs[1].runtime, 1u);
    EXPECT_EQ(runs[2].observation.events.size(), 3u);
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.take().empty());
}

/** @brief Tests that a full queue drops the oldest observations and keeps the newest. */
TEST(Small_RunQueue, DropsTheOldestWhenFull)
{
    RunQueue queue(3);
    push_runtimes(queue, 5);

    EXPECT_EQ(queue.size(), 3u);
    const std::vector<QueuedRun> runs = queue.take();
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[0].runtime, 2u) << "the two oldest were dropped";
    EXPECT_EQ(runs[2].runtime, 4u);
}

/** @brief Tests that the overflow count is the records of the dropped observations, and taking it starts it again. */
TEST(Small_RunQueue, CountsTheRecordsOfTheObservationsItDropped)
{
    RunQueue queue(3);
    push_runtimes(queue, 5);

    EXPECT_EQ(queue.take_overflow(), 1u + 2u) << "the records of the observations of runtimes 0 and 1";
    EXPECT_EQ(queue.take_overflow(), 0u) << "taking the count starts it again";
}

/** @brief Tests that a queue of capacity zero still holds one observation. */
TEST(Small_RunQueue, ACapacityOfZeroHoldsOne)
{
    RunQueue queue(0);
    queue.push(0, observation_of(1));
    queue.push(1, observation_of(1));

    const std::vector<QueuedRun> runs = queue.take();

    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].runtime, 1u);
}

/** @brief Tests that discard() drops what was never taken and says how many that was. */
TEST(Small_RunQueue, DiscardDropsWhatWasNeverCollected)
{
    RunQueue queue(8);
    queue.push(0, observation_of(1));
    queue.push(0, observation_of(1));

    EXPECT_EQ(queue.discard(), 2u);
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.take().empty());
    EXPECT_EQ(queue.discard(), 0u);
}

/** @brief Tests that a discard at the end of a session is not an overflow. */
TEST(Small_RunQueue, ADiscardIsNotAnOverflow)
{
    RunQueue queue(8);
    queue.push(0, observation_of(1));

    static_cast<void>(queue.discard());

    EXPECT_EQ(queue.take_overflow(), 0u);
}

/** @brief Tests that several threads may push while one takes, with nothing lost within the capacity. */
TEST(Medium_RunQueue, SeveralThreadsMayPushWhileOneTakes)
{
    constexpr std::size_t K_THREADS = 4;
    constexpr std::size_t K_PER_THREAD = 200;
    RunQueue queue(K_THREADS * K_PER_THREAD);
    std::atomic<bool> pushing{true};
    std::atomic<std::size_t> taken{0};
    std::thread consumer([&]() { taken.store(take_while_pushing(queue, pushing)); });

    push_from_threads(queue, K_THREADS, K_PER_THREAD);
    pushing.store(false, std::memory_order_release);
    consumer.join();

    EXPECT_EQ(taken.load() + queue.take().size(), K_THREADS * K_PER_THREAD);
    EXPECT_EQ(queue.take_overflow(), 0u);
}
