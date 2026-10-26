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
 * @file test_recording_observer.cpp
 * @brief Tests RecordingObserver, the observer the runtime's tests keep observations with.
 * @see tests/foundation/test_runner/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/test_runner/recording_observer.hpp>

using namespace SushiRuntime;
using TestRunner::RecordingObserver;

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
TEST(Small_RecordingObserver, KeepsObservationsFromSeveralThreads)
{
    constexpr std::size_t K_THREADS = 4;
    constexpr std::size_t K_PER_THREAD = 100;
    RecordingObserver observer;
    std::vector<std::thread> threads;
    for (std::size_t thread = 0; thread < K_THREADS; ++thread)
        threads.emplace_back([&observer]()
        {
            for (std::size_t run = 0; run < K_PER_THREAD; ++run)
                observer.on_run(observation_of(1));
        });
    for (std::thread& thread : threads)
        thread.join();

    EXPECT_EQ(observer.count(), K_THREADS * K_PER_THREAD);
}
