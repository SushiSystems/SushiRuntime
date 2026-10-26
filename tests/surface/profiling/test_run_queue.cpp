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

/** @brief Tests that a full queue drops the oldest observation and counts the records it held. */
TEST(Small_RunQueue, DropsTheOldestWhenFullAndCountsIt)
{
    RunQueue queue(3);
    for (std::uint32_t runtime = 0; runtime < 5; ++runtime)
        queue.push(runtime, observation_of(runtime + 1));

    EXPECT_EQ(queue.size(), 3u);
    const std::vector<QueuedRun> runs = queue.take();
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[0].runtime, 2u) << "the two oldest were dropped";
    EXPECT_EQ(runs[2].runtime, 4u);
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
TEST(Small_RunQueue, EndDropsWhatWasNeverCollected)
{
    RunQueue queue(8);
    queue.push(0, observation_of(1));
    queue.push(0, observation_of(1));

    EXPECT_EQ(queue.discard(), 2u);

    EXPECT_EQ(queue.size(), 0u);
    EXPECT_TRUE(queue.take().empty());
    EXPECT_EQ(queue.discard(), 0u);
    EXPECT_EQ(queue.take_overflow(), 0u) << "a discard at the end of a session is not an overflow";
}

/** @brief Tests that several threads may push while one takes, with nothing lost within the capacity. */
TEST(Small_RunQueue, SeveralThreadsMayPushWhileOneTakes)
{
    constexpr std::size_t K_THREADS = 4;
    constexpr std::size_t K_PER_THREAD = 200;
    RunQueue queue(K_THREADS * K_PER_THREAD);
    std::atomic<std::size_t> taken{0};
    std::atomic<bool> pushing{true};

    std::thread consumer([&]()
    {
        while (pushing.load(std::memory_order_acquire))
            taken.fetch_add(queue.take().size());
    });
    std::vector<std::thread> producers;
    for (std::size_t thread = 0; thread < K_THREADS; ++thread)
        producers.emplace_back([&queue, thread]()
        {
            for (std::size_t run = 0; run < K_PER_THREAD; ++run)
                queue.push(static_cast<std::uint32_t>(thread), observation_of(1));
        });
    for (std::thread& producer : producers)
        producer.join();
    pushing.store(false, std::memory_order_release);
    consumer.join();
    taken.fetch_add(queue.take().size());

    EXPECT_EQ(taken.load(), K_THREADS * K_PER_THREAD);
    EXPECT_EQ(queue.take_overflow(), 0u);
}
