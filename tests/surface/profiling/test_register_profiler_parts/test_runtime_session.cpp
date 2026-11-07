/**************************************************************************/
/* test_runtime_session.cpp                                               */
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
 * @file test_runtime_session.cpp
 * @brief Tests a SushiProfiler session over a runtime end to end: its zones, its device spans and their joins.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <SushiProfiler/record/run_record.hpp>

#include <SushiRuntime/profiling/device_track_name.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "profiled_pipelines.hpp"

using namespace SushiRuntime;
using namespace SessionTests;
using SushiProfiler::PeriodRecord;
using SushiProfiler::RunRecord;

namespace
{
    constexpr std::uint32_t K_WARMUP = 1;
    constexpr std::uint32_t K_ACTIVE = 2;
    constexpr std::size_t K_PERIODS = K_WARMUP + K_ACTIVE;

    /** @brief Records K_PERIODS blocking runs of @p pipeline's graph, one per period. */
    RunRecord record_blocking_runs(SessionPipeline& pipeline)
    {
        return record_periods(pipeline.runtime(), K_WARMUP, K_ACTIVE,
                              [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&)
                              {
                                  EXPECT_TRUE(pipeline.graph().run().has_value());
                              });
    }

    /** @brief Returns the text of span @p row's name in @p period. */
    std::string span_name(const RunRecord& record, const PeriodRecord& period, std::size_t row)
    {
        return std::string(record.name_of(period.spans.span(row).name));
    }

    /** @brief Returns the name of the track span @p row of @p period is on. */
    std::string span_track(const RunRecord& record, const PeriodRecord& period, std::size_t row)
    {
        return record.tracks.at(period.spans.span(row).track);
    }

    /** @brief Calls @p visit with the period and the row of every span of @p record. */
    template <class Visit>
    void for_each_span(const RunRecord& record, Visit&& visit)
    {
        for (const PeriodRecord& period : record.periods)
            for (std::size_t row = 0; row < period.spans.size(); ++row)
                visit(period, row);
    }

    /** @brief Spins until @p ready returns true. */
    template <class Ready>
    void spin_until(Ready&& ready)
    {
        while (!ready())
            std::this_thread::yield();
    }

    /** @brief Expects @p record to name the load, scale and store node each span belongs to, with its operation and one step. */
    void expect_every_span_joined_to_its_node(const RunRecord& record)
    {
        const std::string names[] = {"load", "scale", "store"};
        const std::int64_t operations[] = {
            static_cast<std::int64_t>(("pipeline.load"_op).value),
            static_cast<std::int64_t>(("pipeline.scale"_op).value),
            static_cast<std::int64_t>(("pipeline.store"_op).value)};
        for_each_span(record, [&](const PeriodRecord& period, std::size_t row)
        {
            const std::int64_t node = integer_attribute(record, period, "node", row);
            ASSERT_GE(node, 0);
            ASSERT_LT(node, 3);
            EXPECT_EQ(span_name(record, period, row), names[node]) << "row " << row;
            EXPECT_EQ(integer_attribute(record, period, "operation", row), operations[node]) << "row " << row;
            EXPECT_EQ(integer_attribute(record, period, "invocation", row), 0) << "one step per run";
        });
    }

    /** @brief Returns whether a submit zone of the thread @p span names encloses its queue time, to the pairing's slack. */
    bool is_enclosed_by_a_submit_zone(const RunRecord& record, const PeriodRecord& period,
                                      const SushiProfiler::Span& span)
    {
        for (const SushiProfiler::ZoneRun& run : period.runs)
        {
            const std::string& site = record.sites[run.site].name;
            const bool is_submit = site == "run.submit" || site == "dispatch.submit" || site == "chain.submit_step";
            // The queue time is the device's reading, placed on the steady clock to within the pairing's uncertainty.
            const std::uint64_t slack = span.uncertainty_ns;
            if (run.thread == span.submit_thread && is_submit && run.begin_ns <= span.queued_ns + slack &&
                span.queued_ns <= run.end_ns + slack)
                return true;
        }
        return false;
    }

    /** @brief Expects every span of every recorded period to be enclosed by a submit zone, and returns how many it checked. */
    std::size_t expect_every_span_enclosed_by_a_submit_zone(const RunRecord& record)
    {
        std::size_t checked = 0;
        for (const PeriodRecord& period : record.periods)
        {
            if (period.warmup)
                continue; // a warm-up period keeps its zone trees but not the individual zone runs
            EXPECT_FALSE(period.runs.empty());
            for (std::size_t row = 0; row < period.spans.size(); ++row)
            {
                EXPECT_TRUE(is_enclosed_by_a_submit_zone(record, period, period.spans.span(row)))
                    << "span " << span_name(record, period, row) << " in period " << period.index;
                ++checked;
            }
        }
        return checked;
    }

    /** @brief Expects @p period of an inference pass to hold one span per node, each with a stated cost. */
    void expect_an_inference_period(const RunRecord& record, const PeriodRecord& period,
                                    const std::vector<std::string>& node_names)
    {
        SCOPED_TRACE(period.index);
        ASSERT_EQ(period.spans.size(), node_names.size());
        std::set<std::string> seen;
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            seen.insert(span_name(record, period, row));
            EXPECT_TRUE(has_attribute(record, period, "flops", row));
            EXPECT_TRUE(has_attribute(record, period, "bytes", row));
        }
        EXPECT_EQ(seen, std::set<std::string>(node_names.begin(), node_names.end()));
    }

    /** @brief Runs a blocking run, or on period @p async_period an async step that is waited for after the next mark. */
    void run_with_a_late_wait(SessionPipeline& pipeline, std::uint32_t async_period, std::uint32_t iteration,
                              SushiProfiler::PeriodTimeline& timeline)
    {
        if (iteration != async_period)
        {
            EXPECT_TRUE(pipeline.graph().run().has_value());
            return;
        }
        auto step = pipeline.graph().run_async().value();
        // The step's device work finishes inside this period; the caller waits only after the next mark.
        spin_until([&step]() { return step.ready(); });
        timeline.mark();
        EXPECT_TRUE(step.wait().has_value());
    }

    /** @brief A host node of a pipeline that holds its step open until released. */
    class HeldHostNode
    {
        public:
            /** @brief Records the node into @p pipeline's graph. */
            explicit HeldHostNode(SessionPipeline& pipeline)
            {
                pipeline.graph().add_host(Reads{}, Writes{}, [this]()
                {
                    entered_.store(true, std::memory_order_release);
                    spin_until([this]() { return released_.load(std::memory_order_acquire); });
                });
            }

            HeldHostNode(const HeldHostNode&) = delete;
            HeldHostNode& operator=(const HeldHostNode&) = delete;

            /** @brief Spins until the node has started. */
            void wait_until_entered() const
            {
                spin_until([this]() { return entered_.load(std::memory_order_acquire); });
            }

            /** @brief Lets the node end. */
            void release() { released_.store(true, std::memory_order_release); }

        private:
            std::atomic<bool> entered_{false};  /**< Whether the node has started. */
            std::atomic<bool> released_{false}; /**< Whether the node may end. */
    };

    /**
     * @brief Runs period 0 as an async step whose held node outlives @p marks_while_held marks and then @p linger;
     *        every other period releases the node and runs blocking.
     */
    void run_with_a_held_node(SessionPipeline& pipeline, HeldHostNode& held, std::uint32_t iteration,
                              SushiProfiler::PeriodTimeline& timeline, int marks_while_held,
                              std::chrono::milliseconds linger)
    {
        if (iteration != 0)
        {
            held.release();
            EXPECT_TRUE(pipeline.graph().run().has_value());
            return;
        }
        auto step = pipeline.graph().run_async().value();
        // The held node must start inside the first period, or its span opens in a later one and is never late.
        held.wait_until_entered();
        for (int mark = 0; mark < marks_while_held; ++mark)
            timeline.mark();
        const auto deadline = std::chrono::steady_clock::now() + linger;
        spin_until([deadline]() { return std::chrono::steady_clock::now() >= deadline; });
        held.release();
        EXPECT_TRUE(step.wait().has_value());
    }
} // namespace

/** @brief Tests that the first period holds the plan zones the first run builds and the run zones. */
TEST(Medium_RuntimeSession, TheFirstPeriodHoldsThePlanBuildAndTheRunZones)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    ASSERT_EQ(record.periods.size(), K_PERIODS);
    const std::set<std::string> first = zone_sites(record, record.periods[0]);
    EXPECT_TRUE(first.count("plan.build")) << "the first run builds its plan inside the first period";
    EXPECT_TRUE(first.count("plan.compile"));
    EXPECT_TRUE(first.count("run.steps"));
    EXPECT_TRUE(first.count("run.submit"));
    EXPECT_TRUE(first.count("run.wait"));
}

/** @brief Tests that the later periods hold the run zones and no plan build, since the plan is cached. */
TEST(Medium_RuntimeSession, LaterPeriodsHoldTheRunZonesAndNoPlanBuild)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    ASSERT_EQ(record.periods.size(), K_PERIODS);
    const std::set<std::string> second = zone_sites(record, record.periods[1]);
    const std::set<std::string> third = zone_sites(record, record.periods[2]);
    EXPECT_TRUE(second.count("run.steps"));
    EXPECT_FALSE(second.count("plan.build"));
    EXPECT_TRUE(third.count("run.steps"));
    EXPECT_FALSE(third.count("plan.build"));
}

/** @brief Tests that every period holds one span per node dispatch and no span was dropped. */
TEST(Medium_RuntimeSession, EveryPeriodHoldsOneSpanPerNodeDispatch)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    ASSERT_EQ(record.periods.size(), K_PERIODS);
    EXPECT_EQ(record.periods[0].spans.size(), 3u);
    EXPECT_EQ(record.periods[1].spans.size(), 3u);
    EXPECT_EQ(record.periods[2].spans.size(), 3u);
    EXPECT_EQ(record.drops.spans_dropped, 0u);
}

/** @brief Tests that every span joins a node name, its operation and its registration index. */
TEST(Medium_RuntimeSession, JoinsEverySpanToANodeName)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    ASSERT_EQ(record.periods.size(), K_PERIODS);
    expect_every_span_joined_to_its_node(record);
}

/** @brief Tests that each span is in the period whose steady span holds its start. */
TEST(Medium_RuntimeSession, PutsEachSpanInThePeriodHoldingItsStart)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    for_each_span(record, [](const PeriodRecord& period, std::size_t row)
    {
        const SushiProfiler::Span span = period.spans.span(row);
        EXPECT_GE(span.start_ns, period.begin_ns) << "period " << period.index;
        EXPECT_LT(span.start_ns, period.end_ns) << "period " << period.index;
    });
}

/** @brief Tests that every span's submitting thread resolves to the thread whose submit zone encloses the span's queue time. */
TEST(Medium_RuntimeSession, ResolvesEverySubmitThreadToAnEnclosingZone)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    EXPECT_EQ(expect_every_span_enclosed_by_a_submit_zone(record), 3u * K_ACTIVE);
}

/** @brief Tests that every span names the scheduler layer when its run took the worker pool. */
TEST(Medium_RuntimeSession, EverySpanNamesTheSchedulerLayer)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    for_each_span(record, [&record](const PeriodRecord& period, std::size_t row)
    {
        EXPECT_EQ(integer_attribute(record, period, "layer", row),
                  static_cast<std::int64_t>(record.find_name("scheduler")));
    });
}

/** @brief Tests that every span lies on the runtime's device track. */
TEST(Medium_RuntimeSession, EverySpanLiesOnTheDevicesTrack)
{
    SessionPipeline pipeline;
    const std::string track = device_track_name(pipeline.runtime(), 0);

    const RunRecord record = record_blocking_runs(pipeline);

    for_each_span(record, [&record, &track](const PeriodRecord& period, std::size_t row)
    {
        EXPECT_EQ(span_track(record, period, row), track);
    });
}

/** @brief Tests that a run_async step waited for after its period closed still lands in the period it ran in. */
TEST(Medium_RuntimeSession, ARunAsyncStepWaitedForLateStillLandsInItsPeriod)
{
    SessionPipeline pipeline;

    // Periods: 0 a blocking run; 1 the async step, finished in it; 2 opened by a mark before the wait, empty; 3 a blocking run.
    const RunRecord record = record_periods(
        pipeline.runtime(), 1, 3,
        [&pipeline](std::uint32_t iteration, SushiProfiler::PeriodTimeline& timeline)
        { run_with_a_late_wait(pipeline, 1, iteration, timeline); });

    ASSERT_EQ(record.periods.size(), 4u);
    EXPECT_EQ(record.periods[0].spans.size(), 3u);
    EXPECT_EQ(record.periods[1].spans.size(), 3u) << "the step reported when its work finished, not when it was waited for";
    EXPECT_EQ(record.periods[2].spans.size(), 0u) << "the period opened before the wait holds nothing of the step";
    EXPECT_EQ(record.periods[3].spans.size(), 3u);
    EXPECT_EQ(record.drops.spans_dropped, 0u) << "work that finished in time loses nothing to a late wait";
}

/** @brief Tests that a step finishing after its period ended, within the source's maximum delay, lands in its period and is not late. */
TEST(Medium_RuntimeSession, ASpanThatFinishesWithinTheMaxDelayLandsInItsPeriodAndIsNotLate)
{
    SessionPipeline pipeline;
    HeldHostNode held(pipeline);

    const RunRecord record = record_periods(
        pipeline.runtime(), 0, 3,
        [&pipeline, &held](std::uint32_t iteration, SushiProfiler::PeriodTimeline& timeline)
        { run_with_a_held_node(pipeline, held, iteration, timeline, 1, std::chrono::milliseconds(0)); });

    // The step ended well inside the two seconds the source asks the consumer to keep a period open.
    ASSERT_GE(record.periods.size(), 1u);
    EXPECT_GE(record.periods[0].spans.size(), 1u) << "the held node's span is in the period it started in";
    EXPECT_EQ(record.drops.spans_dropped, 0u) << "work inside the maximum delay is never late";
}

/** @brief Tests that a step whose own work finishes after its period closed and the maximum delay passed is counted in the drops. */
TEST(Medium_RuntimeSession, AStepWhoseWorkFinishedAfterTheLingerIsCountedInTheDrops)
{
    SessionPipeline pipeline;
    HeldHostNode held(pipeline);

    // The held node outlives both marks and the 2 s maximum delay the consumer waits before it collects.
    const RunRecord record = record_periods(
        pipeline.runtime(), 0, 3,
        [&pipeline, &held](std::uint32_t iteration, SushiProfiler::PeriodTimeline& timeline)
        { run_with_a_held_node(pipeline, held, iteration, timeline, 2, std::chrono::milliseconds(2500)); });

    EXPECT_EQ(pipeline.graph().node_count(), 4u);
    EXPECT_GE(record.drops.spans_dropped, 1u) << "the step's spans arrived after their period was collected";
}

/** @brief Tests that an inference-shaped graph on the CPU device records every node of every pass. */
TEST(Medium_RuntimeSession, AnInferenceShapedGraphOnTheCpuDeviceRecordsEveryNode)
{
    InferencePipeline pipeline;

    const RunRecord record = record_periods(
        pipeline.runtime(), K_WARMUP, K_ACTIVE,
        [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&)
        {
            EXPECT_TRUE(pipeline.graph().run().has_value());
        });

    ASSERT_EQ(record.periods.size(), K_PERIODS);
    expect_an_inference_period(record, record.periods[0], pipeline.names());
    expect_an_inference_period(record, record.periods[1], pipeline.names());
    expect_an_inference_period(record, record.periods[2], pipeline.names());
    EXPECT_EQ(record.drops.spans_dropped, 0u);
}
