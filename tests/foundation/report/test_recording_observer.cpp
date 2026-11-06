/**************************************************************************/
/* test_recording_observer.cpp                                            */
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
 * @file test_recording_observer.cpp
 * @brief Tests RecordingObserver, the observer the runtime's tests keep observations with.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/report/recording_observer.hpp>
#include <SushiRuntime/report/run_observer.hpp>

using namespace SushiRuntime;
using SushiRuntime::RecordingObserver;

namespace
{
    /** @brief Builds an observation of @p records records. */
    RunObservation observation_of(std::size_t records)
    {
        RunObservation observation;
        observation.events.resize(records);
        return observation;
    }

    /** @brief Posts @p per_thread observations to @p observer from each of @p threads threads, and joins them. */
    void post_from_threads(RecordingObserver& observer, std::size_t threads, std::size_t per_thread)
    {
        std::vector<std::thread> workers;
        for (std::size_t worker = 0; worker < threads; ++worker)
            workers.emplace_back([&observer, per_thread]()
            {
                for (std::size_t run = 0; run < per_thread; ++run)
                    observer.on_run(observation_of(1));
            });
        for (std::thread& worker : workers)
            worker.join();
    }
} // namespace

/** @brief Tests that an observer that saw nothing holds nothing. */
TEST(Small_RecordingObserver, StartsEmpty)
{
    const RecordingObserver observer;

    EXPECT_EQ(observer.count(), 0u);
    EXPECT_TRUE(observer.runs().empty());
}

/** @brief Tests that it keeps a copy of every observation in the order they came. */
TEST(Small_RecordingObserver, KeepsEveryObservationInOrder)
{
    RecordingObserver observer;
    RunObserver& as_observer = observer;

    as_observer.on_run(observation_of(1));
    as_observer.on_run(observation_of(2));
    as_observer.on_run(observation_of(3));

    ASSERT_EQ(observer.count(), 3u);
    EXPECT_EQ(observer.runs()[0].events.size(), 1u);
    EXPECT_EQ(observer.last().events.size(), 3u);
}

/** @brief Tests that clear() forgets every observation. */
TEST(Small_RecordingObserver, ClearForgetsEveryObservation)
{
    RecordingObserver observer;
    observer.on_run(observation_of(1));

    observer.clear();

    EXPECT_EQ(observer.count(), 0u);
}

/** @brief Tests that observations arriving from several threads are all kept. */
TEST(Medium_RecordingObserver, KeepsObservationsFromSeveralThreads)
{
    RecordingObserver observer;

    post_from_threads(observer, 4, 100);

    EXPECT_EQ(observer.count(), 400u);
}
