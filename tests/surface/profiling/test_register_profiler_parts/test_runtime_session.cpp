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

#include <gtest/gtest.h>

#include <SushiProfiler/record/run_record.hpp>

#include <SushiRuntime/profiling/device_track_name.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "session_pipeline.hpp"

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
} // namespace

/** @brief Tests that the record holds the plan zones, the run zones and the three device spans of every period. */
TEST(Medium_RuntimeSession, RecordsPlanRunAndDeviceSpansOfAGraph)
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
    for (std::size_t period = 1; period < K_PERIODS; ++period)
    {
        const std::set<std::string> later = zone_sites(record, record.periods[period]);
        EXPECT_TRUE(later.count("run.steps")) << "period " << period;
        EXPECT_FALSE(later.count("plan.build")) << "the plan is cached after the first run, period " << period;
    }
    for (const PeriodRecord& period : record.periods)
        EXPECT_EQ(period.spans.size(), 3u) << "one span per node dispatch, period " << period.index;
    EXPECT_EQ(record.drops.spans_dropped, 0u);
}

/** @brief Tests that every span joins a node name, its operation and its registration index. */
TEST(Medium_RuntimeSession, JoinsEverySpanToANodeName)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    ASSERT_EQ(record.periods.size(), K_PERIODS);
    const std::string names[] = {"load", "scale", "store"};
    const std::int64_t operations[] = {
        static_cast<std::int64_t>(("pipeline.load"_op).value),
        static_cast<std::int64_t>(("pipeline.scale"_op).value),
        static_cast<std::int64_t>(("pipeline.store"_op).value)};
    for (const PeriodRecord& period : record.periods)
    {
        ASSERT_EQ(period.spans.size(), 3u);
        std::set<std::string> seen;
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            const std::int64_t node = integer_attribute(record, period, "node", row);
            ASSERT_GE(node, 0);
            ASSERT_LT(node, 3);
            EXPECT_EQ(span_name(record, period, row), names[node]) << "row " << row;
            EXPECT_EQ(integer_attribute(record, period, "operation", row), operations[node]) << "row " << row;
            EXPECT_EQ(integer_attribute(record, period, "invocation", row), 0) << "one step per run";
            seen.insert(span_name(record, period, row));
        }
        EXPECT_EQ(seen, (std::set<std::string>{"load", "scale", "store"}));
    }
}

/** @brief Tests that each span is in the period whose steady span holds its start. */
TEST(Medium_RuntimeSession, PutsEachSpanInThePeriodHoldingItsStart)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    for (const PeriodRecord& period : record.periods)
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            const SushiProfiler::Span span = period.spans.span(row);
            EXPECT_GE(span.start_ns, period.begin_ns) << "period " << period.index;
            EXPECT_LT(span.start_ns, period.end_ns) << "period " << period.index;
        }
}

/** @brief Tests that every span's submitting thread resolves to the thread whose submit zone encloses the span's queue time. */
TEST(Medium_RuntimeSession, ResolvesEverySubmitThreadToAnEnclosingZone)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    std::size_t checked = 0;
    for (const PeriodRecord& period : record.periods)
    {
        if (period.warmup)
            continue; // a warm-up period keeps its zone trees but not the individual zone runs
        ASSERT_FALSE(period.runs.empty());
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            const SushiProfiler::Span span = period.spans.span(row);
            bool enclosed = false;
            for (const SushiProfiler::ZoneRun& run : period.runs)
            {
                const std::string& site = record.sites[run.site].name;
                if (run.thread != span.submit_thread ||
                    (site != "run.submit" && site != "dispatch.submit" && site != "chain.submit_step"))
                    continue;
                // The queue time is the device's reading, placed on the steady clock to within the pairing's uncertainty.
                const std::uint64_t slack = span.uncertainty_ns;
                if (run.begin_ns <= span.queued_ns + slack && span.queued_ns <= run.end_ns + slack)
                    enclosed = true;
            }
            EXPECT_TRUE(enclosed) << "span " << span_name(record, period, row) << " in period " << period.index
                                  << " names thread " << span.submit_thread;
            ++checked;
        }
    }
    EXPECT_EQ(checked, 3u * K_ACTIVE);
}

/** @brief Tests that a node that stated a cost writes it as attributes and a node that stated none writes none. */
TEST(Medium_RuntimeSession, WritesTheCostAttributesWhereACostWasStated)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    for (const PeriodRecord& period : record.periods)
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            const std::int64_t node = integer_attribute(record, period, "node", row);
            if (node == K_LOAD)
            {
                EXPECT_EQ(integer_attribute(record, period, "flops", row),
                          static_cast<std::int64_t>(SessionPipeline::K_ELEMENTS));
                EXPECT_EQ(integer_attribute(record, period, "bytes", row),
                          static_cast<std::int64_t>(SessionPipeline::K_ELEMENTS * sizeof(float)));
                EXPECT_EQ(integer_attribute(record, period, "dtype", row),
                          static_cast<std::int64_t>(record.find_name("f32")));
            }
            else if (node == K_SCALE)
            {
                EXPECT_TRUE(has_attribute(record, period, "flops", row)) << "a stated zero is written";
                EXPECT_EQ(integer_attribute(record, period, "flops", row), 0);
                EXPECT_GT(integer_attribute(record, period, "bytes", row), 0);
            }
            else
            {
                EXPECT_FALSE(has_attribute(record, period, "flops", row));
                EXPECT_FALSE(has_attribute(record, period, "bytes", row));
                EXPECT_FALSE(has_attribute(record, period, "dtype", row));
            }
        }
}

/** @brief Tests that every span names the execution layer of its run and that the tracks are the runtime's. */
TEST(Medium_RuntimeSession, EverySpanNamesItsLayerAndLiesOnADeviceTrack)
{
    SessionPipeline pipeline;
    const std::string track = device_track_name(pipeline.runtime(), 0);

    const RunRecord record = record_blocking_runs(pipeline);

    for (const PeriodRecord& period : record.periods)
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            EXPECT_EQ(integer_attribute(record, period, "layer", row),
                      static_cast<std::int64_t>(record.find_name("scheduler")));
            EXPECT_EQ(span_track(record, period, row), track);
        }
}

/** @brief Tests that a plan that runs as a chain names the chain layer. */
TEST(Medium_RuntimeSession, ARunOnTheChainPathNamesTheChainLayer)
{
    SessionPipeline pipeline(SessionPipeline::chain_config());

    const RunRecord record = record_blocking_runs(pipeline);

    ASSERT_EQ(record.periods.size(), K_PERIODS);
    if (!zone_sites(record, record.periods[1]).count("chain.submit_step"))
        GTEST_SKIP() << "this device's plan does not classify as a chain";
    for (const PeriodRecord& period : record.periods)
        for (std::size_t row = 0; row < period.spans.size(); ++row)
            EXPECT_EQ(integer_attribute(record, period, "layer", row),
                      static_cast<std::int64_t>(record.find_name("chain")));
}

/** @brief Tests that the record states the runtime's devices, its drivers and the build of the library. */
TEST(Medium_RuntimeSession, TheRecordStatesTheDevicesAndTheBuild)
{
    SessionPipeline pipeline;

    const RunRecord record = record_blocking_runs(pipeline);

    bool version = false;
    bool device_name = false;
    bool driver = false;
    for (const SushiProfiler::Fact& fact : record.environment)
    {
        if (fact.name == "sushiruntime.version")
            version = true;
        if (fact.name.find(".device.0.name") != std::string::npos)
            device_name = true;
        if (fact.name.find(".device.0.driver") != std::string::npos)
            driver = !fact.value.empty();
    }
    EXPECT_TRUE(version);
    EXPECT_TRUE(device_name);
    EXPECT_TRUE(driver) << "the driver reads \"unknown\" where it is not answerable, never empty";
    bool build_type = false;
    for (const SushiProfiler::Fact& fact : record.build)
        if (fact.name == "sushiruntime.build_type")
            build_type = fact.value == "release" || fact.value == "debug";
    EXPECT_TRUE(build_type);
}

/** @brief Tests that a run_async step waited for after its period closed still lands in the period it ran in. */
TEST(Medium_RuntimeSession, ARunAsyncStepWaitedForLateStillLandsInItsPeriod)
{
    SessionPipeline pipeline;
    bool landed = false;

    // Periods: 0 a blocking run; 1 the async step, finished in it; 2 opened by a mark before the wait, empty; 3 a blocking run.
    const RunRecord record = record_periods(
        pipeline.runtime(), 1, 3,
        [&pipeline, &landed](std::uint32_t iteration, SushiProfiler::PeriodTimeline& timeline)
        {
            if (iteration != 1)
            {
                EXPECT_TRUE(pipeline.graph().run().has_value());
                return;
            }
            auto step = pipeline.graph().run_async().value();
            // The step's device work finishes inside this period; the caller waits only after the next mark.
            while (!step.ready())
                std::this_thread::yield();
            timeline.mark();
            EXPECT_TRUE(step.wait().has_value());
            landed = true;
        });

    ASSERT_TRUE(landed);
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
    std::atomic<bool> gate{false};
    std::atomic<bool> entered{false};
    pipeline.graph().add_host(Reads{}, Writes{}, [&gate, &entered]()
    {
        entered.store(true, std::memory_order_release);
        while (!gate.load(std::memory_order_acquire))
            std::this_thread::yield();
    });

    const RunRecord record = record_periods(
        pipeline.runtime(), 0, 3,
        [&pipeline, &gate, &entered](std::uint32_t iteration, SushiProfiler::PeriodTimeline& timeline)
        {
            if (iteration != 0)
            {
                gate.store(true, std::memory_order_release);
                EXPECT_TRUE(pipeline.graph().run().has_value());
                return;
            }
            auto step = pipeline.graph().run_async().value();
            // The host node starts inside the first period, then the period ends while the node is still held.
            while (!entered.load(std::memory_order_acquire))
                std::this_thread::yield();
            timeline.mark();
            gate.store(true, std::memory_order_release);
            EXPECT_TRUE(step.wait().has_value());
        });

    // The step ended well inside the two seconds the source asks the consumer to keep a period open.
    ASSERT_GE(record.periods.size(), 1u);
    EXPECT_GE(record.periods[0].spans.size(), 1u) << "the held node's span is in the period it started in";
    EXPECT_EQ(record.drops.spans_dropped, 0u) << "work inside the maximum delay is never late";
}

/** @brief Tests that a step whose own work finishes after its period closed and the maximum delay passed is counted in the drops. */
TEST(Medium_RuntimeSession, AStepWhoseWorkFinishedAfterTheLingerIsCountedInTheDrops)
{
    SessionPipeline pipeline;
    std::atomic<bool> gate{false};
    pipeline.graph().add_host(Reads{}, Writes{}, [&gate]()
    {
        while (!gate.load(std::memory_order_acquire))
            std::this_thread::yield();
    });
    std::size_t nodes = 0;

    const RunRecord record = record_periods(
        pipeline.runtime(), 0, 3,
        [&pipeline, &gate, &nodes](std::uint32_t iteration, SushiProfiler::PeriodTimeline& timeline)
        {
            if (iteration != 0)
            {
                gate.store(true, std::memory_order_release);
                EXPECT_TRUE(pipeline.graph().run().has_value());
                return;
            }
            // A host node of the step blocks until the first period has ended and the source's two-second delay has passed.
            auto step = pipeline.graph().run_async().value();
            timeline.mark();
            timeline.mark();
            // The consumer holds a closed period open for the source's maximum delay, 2 s, then collects it on its own thread.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
            while (std::chrono::steady_clock::now() < deadline)
                std::this_thread::yield();
            gate.store(true, std::memory_order_release);
            EXPECT_TRUE(step.wait().has_value());
            nodes = pipeline.graph().node_count();
        });

    EXPECT_EQ(nodes, 4u);
    EXPECT_GE(record.drops.spans_dropped, 1u) << "the step's spans arrived after their period was collected";
}

/** @brief Tests that an inference-shaped graph on the CPU device records every node of every pass. */
TEST(Medium_RuntimeSession, AnInferenceShapedGraphOnTheCpuDeviceRecordsEveryNode)
{
    InferencePipeline pipeline;
    const std::size_t nodes = 2 * InferencePipeline::K_LAYERS;

    const RunRecord record = record_periods(
        pipeline.runtime(), K_WARMUP, K_ACTIVE,
        [&pipeline](std::uint32_t, SushiProfiler::PeriodTimeline&)
        {
            EXPECT_TRUE(pipeline.graph().run().has_value());
        });

    ASSERT_EQ(record.periods.size(), K_PERIODS);
    for (const PeriodRecord& period : record.periods)
    {
        ASSERT_EQ(period.spans.size(), nodes) << "period " << period.index;
        std::set<std::string> seen;
        for (std::size_t row = 0; row < period.spans.size(); ++row)
        {
            seen.insert(span_name(record, period, row));
            EXPECT_TRUE(has_attribute(record, period, "flops", row));
            EXPECT_TRUE(has_attribute(record, period, "bytes", row));
        }
        EXPECT_EQ(seen, std::set<std::string>(pipeline.names().begin(), pipeline.names().end()));
    }
    EXPECT_EQ(record.drops.spans_dropped, 0u);
}
