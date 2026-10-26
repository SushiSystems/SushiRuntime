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
 * @file test_run_feed.cpp
 * @brief Tests RunFeed, the observer that puts a runtime's runs on the source's queue.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

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

/** @brief Tests that a feed queues each observation it hears, tagged with its runtime. */
TEST(Small_RunFeed, QueuesEachObservationTaggedWithItsRuntime)
{
    const auto queue = std::make_shared<RunQueue>(8);
    RunFeed feed(queue, 3);
    RunObservation observation;
    observation.events.resize(2);

    feed.on_run(observation);
    feed.on_run(observation);

    const std::vector<QueuedRun> runs = queue->take();
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].runtime, 3u);
    EXPECT_EQ(runs[0].observation.events.size(), 2u);
}

/** @brief Tests that two feeds of one queue keep their runtimes apart. */
TEST(Small_RunFeed, TwoFeedsOfOneQueueKeepTheirRuntimesApart)
{
    const auto queue = std::make_shared<RunQueue>(8);
    RunFeed first(queue, 0);
    RunFeed second(queue, 1);

    first.on_run(RunObservation{});
    second.on_run(RunObservation{});

    const std::vector<QueuedRun> runs = queue->take();
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].runtime, 0u);
    EXPECT_EQ(runs[1].runtime, 1u);
}

/** @brief Tests that a feed keeps its queue alive, so an observation that arrives after the owner let go is safe. */
TEST(Small_RunFeed, AFeedKeepsItsQueueAlive)
{
    std::unique_ptr<RunFeed> feed;
    {
        auto queue = std::make_shared<RunQueue>(2);
        feed = std::make_unique<RunFeed>(queue, 0);
    }

    EXPECT_NO_THROW(feed->on_run(RunObservation{}));
}
