/**************************************************************************/
/* test_run_feed.cpp                                                      */
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
 * @file test_run_feed.cpp
 * @brief Tests RunFeed, the observer that puts a runtime's runs on the source's queue.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/profiling/run_feed.hpp>
#include <SushiRuntime/profiling/run_queue.hpp>
#include <SushiRuntime/report/run_observer.hpp>

using namespace SushiRuntime;
using SushiRuntime::Internal::Profiling::QueuedRun;
using SushiRuntime::Internal::Profiling::RunFeed;
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

/** @brief Tests that feeds of one queue queue each observation they hear tagged with their own runtime. */
TEST(Small_RunFeed, QueuesEachObservationTaggedWithItsRuntime)
{
    const auto queue = std::make_shared<RunQueue>(8);
    RunFeed first(queue, 3);
    RunFeed second(queue, 1);

    first.on_run(observation_of(2));
    second.on_run(observation_of(1));

    const std::vector<QueuedRun> runs = queue->take();
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].runtime, 3u);
    EXPECT_EQ(runs[0].observation.events.size(), 2u);
    EXPECT_EQ(runs[1].runtime, 1u);
    EXPECT_EQ(runs[1].observation.events.size(), 1u);
}

/** @brief Tests that a feed keeps its queue alive, so an observation that arrives after the owner let go is queued. */
TEST(Small_RunFeed, AFeedKeepsItsQueueAlive)
{
    auto queue = std::make_shared<RunQueue>(2);
    const std::weak_ptr<RunQueue> observer = queue;
    RunFeed feed(queue, 0);
    queue.reset();

    feed.on_run(observation_of(1));

    const std::shared_ptr<RunQueue> alive = observer.lock();
    ASSERT_NE(alive, nullptr) << "the feed holds the queue after the owner let go";
    EXPECT_EQ(alive->take().size(), 1u);
}
