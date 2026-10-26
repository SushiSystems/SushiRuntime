/**************************************************************************/
/* test_run_zones.cpp                                                     */
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
 * @file test_run_zones.cpp
 * @brief Tests the zones the runtime writes while it runs a graph on the worker pool and on the chain.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/test_runner/recording_observer.hpp>

#include "session_pipeline.hpp"

using namespace SushiRuntime;
using namespace SessionTests;
using TestRunner::RecordingObserver;
using SushiProfiler::PeriodRecord;
using SushiProfiler::RunRecord;

namespace
{
    /** @brief Records two periods around two runs of @p pipeline with no source. */
    RunRecord record_two_runs(SessionPipeline& pipeline)
    {
        return record_periods_without_source(0, 2, [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&)
                                             {
                                                 EXPECT_TRUE(pipeline.graph().run().has_value());
                                             });
    }

    /** @brief Returns the value of the build fact @p name in @p record, or an empty string. */
    std::string build_fact(const RunRecord& record, const char* name)
    {
        for (const SushiProfiler::Fact& fact : record.build)
            if (fact.name == name)
                return fact.value;
        return {};
    }
} // namespace

/** @brief Tests that on the worker pool a run writes run.submit and run.wait under run.steps, with their children. */
TEST(Medium_RunZones, TheSchedulerPathWritesSubmitAndWaitUnderTheRun)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    ASSERT_EQ(record.periods.size(), 2u);
    for (const PeriodRecord& period : record.periods)
    {
        bool found = false;
        for (const SushiProfiler::ZoneTree& tree : period.zones)
        {
            const int submit = find_zone(record, tree, "run.submit");
            const int wait = find_zone(record, tree, "run.wait");
            if (submit < 0 || wait < 0)
                continue;
            found = true;
            EXPECT_EQ(parent_zone(record, tree, submit), "run.steps");
            EXPECT_EQ(parent_zone(record, tree, wait), "run.steps");
            const int latch = find_zone(record, tree, "run.wait.latch");
            const int quiesce = find_zone(record, tree, "run.wait.quiesce");
            ASSERT_GE(latch, 0);
            ASSERT_GE(quiesce, 0);
            EXPECT_EQ(parent_zone(record, tree, latch), "run.wait");
            EXPECT_EQ(parent_zone(record, tree, quiesce), "run.wait");
        }
        EXPECT_TRUE(found) << "period " << period.index;
    }
}

/** @brief Tests that the dispatches of a run write dispatch.task and dispatch.submit on the worker threads. */
TEST(Medium_RunZones, DispatchZonesAreWrittenAtLevelTwo)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    if (build_fact(record, "sushiruntime.profile_level") == "1" || build_fact(record, "sushiruntime.profile_level") == "0")
        GTEST_SKIP() << "the library is compiled below level 2";
    ASSERT_EQ(record.periods.size(), 2u);
    const std::set<std::string> sites = zone_sites(record, record.periods[1]);
    EXPECT_TRUE(sites.count("dispatch.task"));
    EXPECT_TRUE(sites.count("dispatch.submit"));
}

/** @brief Tests that the per-element zones of the steal path are not compiled into a level-2 library. */
TEST(Medium_RunZones, TheStealZoneIsAbsentBelowLevelThree)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    if (build_fact(record, "sushiruntime.profile_level") == "3")
        GTEST_SKIP() << "the library is compiled at level 3";
    for (const PeriodRecord& period : record.periods)
        EXPECT_FALSE(zone_sites(record, period).count("worker.steal"));
}

/** @brief Tests that on the in-order chain a run writes its own zones and none of the pool's. */
TEST(Medium_RunZones, TheChainPathWritesItsOwnZones)
{
    SessionPipeline pipeline(SessionPipeline::chain_config());
    // The events are read only while a run is timed, and an added observer is what wants the timing (R20, R21).
    const RunObserverHandle observer = pipeline.runtime().add_run_observer(std::make_shared<RecordingObserver>());

    const RunRecord record = record_two_runs(pipeline);

    ASSERT_EQ(record.periods.size(), 2u);
    const std::set<std::string> sites = zone_sites(record, record.periods[1]);
    if (!sites.count("chain.submit_step"))
        GTEST_SKIP() << "this device's plan does not classify as a chain";
    EXPECT_TRUE(sites.count("chain.wait"));
    EXPECT_TRUE(sites.count("chain.record_events") ||
                build_fact(record, "sushiruntime.profile_level") == "1")
        << "recording the events is a fine zone, level 2";
    EXPECT_FALSE(sites.count("run.submit"));
}

/** @brief Tests that a worker thread of the pool carries its name in the record. */
TEST(Medium_RunZones, WorkerThreadsCarryTheirNames)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    std::size_t named = 0;
    for (const SushiProfiler::ThreadRecord& thread : record.threads)
        if (thread.name.rfind("sr-worker-", 0) == 0)
            ++named;
    EXPECT_GE(named, 1u) << "a worker that recorded a zone has the name sr-worker-N";
}

/** @brief Tests that the stage zones of a blocking run are written on the caller's thread, not on a worker's. */
TEST(Medium_RunZones, TheRunZonesAreOnTheCallersThread)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    std::size_t trees = 0;
    for (const SushiProfiler::ZoneTree& tree : record.periods[1].zones)
        if (find_zone(record, tree, "run.steps") >= 0)
        {
            ++trees;
            for (const SushiProfiler::ThreadRecord& thread : record.threads)
                if (thread.index == tree.thread)
                    EXPECT_NE(thread.name.rfind("sr-worker-", 0), 0u) << "run.steps ran on a worker";
        }
    EXPECT_EQ(trees, 1u);
}

/** @brief Tests that Runtime::wait writes wait.runtime around wait.queues. */
TEST(Medium_RunZones, RuntimeWaitWritesWaitRuntimeAroundWaitQueues)
{
    SessionPipeline pipeline;

    const RunRecord record = record_periods_without_source(
        0, 1, [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&)
        {
            EXPECT_TRUE(pipeline.graph().run().has_value());
            EXPECT_TRUE(pipeline.runtime().wait().has_value());
        });

    ASSERT_EQ(record.periods.size(), 1u);
    bool found = false;
    for (const SushiProfiler::ZoneTree& tree : record.periods[0].zones)
    {
        const int queues = find_zone(record, tree, "wait.queues");
        if (queues < 0)
            continue;
        found = true;
        EXPECT_EQ(parent_zone(record, tree, queues), "wait.runtime");
    }
    EXPECT_TRUE(found);
}
