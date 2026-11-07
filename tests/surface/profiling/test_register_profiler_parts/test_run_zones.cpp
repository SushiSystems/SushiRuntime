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
 * @file test_run_zones.cpp
 * @brief Tests the zones the runtime writes while it runs a graph on the worker pool and on the chain.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/profile/profile_config.hpp>
#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/report/recording_observer.hpp>

#include "profiled_pipelines.hpp"

using namespace SushiRuntime;
using namespace SessionTests;
using SushiRuntime::RecordingObserver;
using SushiProfiler::PeriodRecord;
using SushiProfiler::RunRecord;

namespace
{
    /** @brief The deepest zone level this library was compiled at; 0 when profiling is off. */
    constexpr int K_LIBRARY_LEVEL = SR_ENABLE_PROFILER != 0 ? SR_PROFILE_LEVEL : 0;

    /** @brief Records two periods around two runs of @p pipeline with no source. */
    RunRecord record_two_runs(SessionPipeline& pipeline)
    {
        return record_periods_without_source(0, 2, [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&)
                                             {
                                                 EXPECT_TRUE(pipeline.graph().run().has_value());
                                             });
    }

    /** @brief Returns the trees of @p period that hold a zone called @p name. */
    std::vector<const SushiProfiler::ZoneTree*> trees_holding(const RunRecord& record, const PeriodRecord& period,
                                                              const char* name)
    {
        std::vector<const SushiProfiler::ZoneTree*> trees;
        for (const SushiProfiler::ZoneTree& tree : period.zones)
            if (find_zone(record, tree, name) >= 0)
                trees.push_back(&tree);
        return trees;
    }

    /** @brief Returns whether the thread @p tree was written on is one of the pool's workers. */
    bool is_written_by_a_worker(const RunRecord& record, const SushiProfiler::ZoneTree& tree)
    {
        for (const SushiProfiler::ThreadRecord& thread : record.threads)
            if (thread.index == tree.thread)
                return thread.name.rfind("sr-worker-", 0) == 0;
        return false;
    }

    /** @brief Returns how many threads of @p record carry a name that starts with @p prefix. */
    std::size_t threads_named(const RunRecord& record, const std::string& prefix)
    {
        std::size_t named = 0;
        for (const SushiProfiler::ThreadRecord& thread : record.threads)
            if (thread.name.rfind(prefix, 0) == 0)
                ++named;
        return named;
    }

    /** @brief Returns whether any period of @p record holds a zone called @p name. */
    bool any_period_has_zone(const RunRecord& record, const char* name)
    {
        for (const PeriodRecord& period : record.periods)
            if (zone_sites(record, period).count(name) != 0)
                return true;
        return false;
    }

    /** @brief Expects @p period to hold run.submit and run.wait under run.steps, with the wait's two children. */
    void expect_submit_and_wait_under_the_run(const RunRecord& record, const PeriodRecord& period)
    {
        SCOPED_TRACE(period.index);
        const auto submits = trees_holding(record, period, "run.submit");
        ASSERT_FALSE(submits.empty());
        const SushiProfiler::ZoneTree& tree = *submits.front();
        const int submit = find_zone(record, tree, "run.submit");
        const int wait = find_zone(record, tree, "run.wait");
        const int latch = find_zone(record, tree, "run.wait.latch");
        const int quiesce = find_zone(record, tree, "run.wait.quiesce");
        ASSERT_GE(wait, 0);
        ASSERT_GE(latch, 0);
        ASSERT_GE(quiesce, 0);
        EXPECT_EQ(parent_zone(record, tree, submit), "run.steps");
        EXPECT_EQ(parent_zone(record, tree, wait), "run.steps");
        EXPECT_EQ(parent_zone(record, tree, latch), "run.wait");
        EXPECT_EQ(parent_zone(record, tree, quiesce), "run.wait");
    }

    /** @brief Skips a test that needs the fine zones of level 2 when the library was compiled below it. */
    class Medium_RunZonesAtLevelTwo : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                if (K_LIBRARY_LEVEL < 2)
                    GTEST_SKIP() << "the library is compiled below level 2";
            }
    };

    /** @brief Skips a test about the absence of the per-element zones when the library was compiled with them. */
    class Medium_RunZonesBelowLevelThree : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                if (K_LIBRARY_LEVEL >= 3)
                    GTEST_SKIP() << "the library is compiled at level 3";
            }
    };
} // namespace

/** @brief Tests that on the worker pool a run writes run.submit and run.wait under run.steps, with their children. */
TEST(Medium_RunZones, TheSchedulerPathWritesSubmitAndWaitUnderTheRun)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    ASSERT_EQ(record.periods.size(), 2u);
    expect_submit_and_wait_under_the_run(record, record.periods[0]);
    expect_submit_and_wait_under_the_run(record, record.periods[1]);
}

/** @brief Tests that the dispatches of a run write dispatch.task and dispatch.submit on the worker threads. */
TEST_F(Medium_RunZonesAtLevelTwo, DispatchZonesAreWritten)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    ASSERT_EQ(record.periods.size(), 2u);
    const std::set<std::string> sites = zone_sites(record, record.periods[1]);
    EXPECT_TRUE(sites.count("dispatch.task"));
    EXPECT_TRUE(sites.count("dispatch.submit"));
}

/** @brief Tests that the per-element zones of the steal path are not compiled into a library below level 3. */
TEST_F(Medium_RunZonesBelowLevelThree, TheStealZoneIsAbsent)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    EXPECT_FALSE(any_period_has_zone(record, "worker.steal"));
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
    EXPECT_TRUE(sites.count("chain.submit_step"));
    EXPECT_TRUE(sites.count("chain.wait"));
    EXPECT_TRUE(sites.count("chain.record_events") || K_LIBRARY_LEVEL == 1)
        << "recording the events is a fine zone, level 2";
    EXPECT_FALSE(sites.count("run.submit"));
}

/** @brief Tests that a worker thread of the pool carries its name in the record. */
TEST(Medium_RunZones, WorkerThreadsCarryTheirNames)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    EXPECT_GE(threads_named(record, "sr-worker-"), 1u) << "a worker that recorded a zone has the name sr-worker-N";
}

/** @brief Tests that the stage zones of a blocking run are written on the caller's thread, not on a worker's. */
TEST(Medium_RunZones, TheRunZonesAreOnTheCallersThread)
{
    SessionPipeline pipeline;

    const RunRecord record = record_two_runs(pipeline);

    const auto trees = trees_holding(record, record.periods[1], "run.steps");
    ASSERT_EQ(trees.size(), 1u);
    EXPECT_FALSE(is_written_by_a_worker(record, *trees.front())) << "run.steps ran on a worker";
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
    const auto trees = trees_holding(record, record.periods[0], "wait.queues");
    ASSERT_FALSE(trees.empty());
    EXPECT_EQ(parent_zone(record, *trees.front(), find_zone(record, *trees.front(), "wait.queues")), "wait.runtime");
}
