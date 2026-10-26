/**************************************************************************/
/* test_dispatch_events.cpp                                               */
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
 * @file test_dispatch_events.cpp
 * @brief Tests that a task body which submits several kernels is timed across every event it hands over.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/api/graph/dispatch_events.hpp>
#include <SushiRuntime/report/run_observer.hpp>
#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/test_runner/recording_observer.hpp>

using namespace SushiRuntime;
using TestRunner::RecordingObserver;

namespace
{
    constexpr std::size_t K_WIDTH = 4096;

    /** @brief Keeps the events a body submitted, so the test can read their own timestamps. */
    struct SubmittedEvents
    {
        std::mutex mutex;                /**< Guards the events; the body runs on a worker. */
        std::vector<sycl::event> events; /**< The two kernels' events, in submission order. */
    };

    /** @brief The command_start of @p event. */
    std::uint64_t start_of(const sycl::event& event)
    {
        return event.get_profiling_info<sycl::info::event_profiling::command_start>();
    }

    /** @brief The command_end of @p event. */
    std::uint64_t end_of(const sycl::event& event)
    {
        return event.get_profiling_info<sycl::info::event_profiling::command_end>();
    }

    /** @brief The command_submit of @p event. */
    std::uint64_t submit_of(const sycl::event& event)
    {
        return event.get_profiling_info<sycl::info::event_profiling::command_submit>();
    }

    /** @brief Records a graph of one task that submits two dependent kernels and runs it once. */
    struct TwoKernelRun
    {
        std::vector<NodeEventRecord> records; /**< What the observer received. */
        std::vector<sycl::event> events;      /**< The kernels' events. */
    };

    /**
     * @brief Runs a one-node graph whose task submits two kernels and returns what was observed.
     * @param hand_over Whether the body hands both events over.
     * @param force_scheduler Whether the run takes the worker pool and not the in-order chain.
     */
    TwoKernelRun run_two_kernels(bool hand_over, bool force_scheduler)
    {
        RuntimeConfig config;
        config.force_scheduler = force_scheduler;
        auto rt = Runtime::create(config).value();
        auto buf = rt.buffer<std::uint32_t>(K_WIDTH).value();
        std::uint32_t* p = buf.data();
        SubmittedEvents submitted;

        auto g = rt.graph();
        NodeOptions options;
        options.name = "two_kernels";
        g.add_task(Reads{}, Writes(buf),
                   [p, hand_over, &submitted](sycl::queue& queue, const std::vector<sycl::event>& after)
                   {
                       const sycl::event first = queue.submit([&](sycl::handler& h)
                       {
                           h.depends_on(after);
                           h.parallel_for(sycl::range<1>(K_WIDTH), [p](sycl::id<1> i) { p[i] = 1u; });
                       });
                       if (hand_over)
                           note_dispatch_event(first);
                       const sycl::event second = queue.submit([&](sycl::handler& h)
                       {
                           h.depends_on(first);
                           h.parallel_for(sycl::range<1>(K_WIDTH), [p](sycl::id<1> i) { p[i] += 1u; });
                       });
                       if (hand_over)
                           note_dispatch_event(second);
                       {
                           const std::lock_guard<std::mutex> lock(submitted.mutex);
                           submitted.events = {first, second};
                       }
                       return second;
                   },
                   options);

        const auto observer = std::make_shared<RecordingObserver>();
        const RunObserverHandle handle = rt.add_run_observer(observer);
        EXPECT_TRUE(g.run().has_value());

        TwoKernelRun result;
        if (observer->count() == 1)
            result.records = observer->last().events;
        const std::lock_guard<std::mutex> lock(submitted.mutex);
        result.events = submitted.events;
        EXPECT_EQ(p[0], 2u) << "the body's two kernels ran";
        return result;
    }

    /** @brief Checks the record of a body that handed both events over spans both. */
    void expect_spans_both(bool force_scheduler)
    {
        const TwoKernelRun run = run_two_kernels(true, force_scheduler);

        ASSERT_EQ(run.records.size(), 1u);
        ASSERT_EQ(run.events.size(), 2u);
        const NodeEventRecord& record = run.records[0];
        const std::uint64_t earliest_start = std::min(start_of(run.events[0]), start_of(run.events[1]));
        const std::uint64_t latest_end = std::max(end_of(run.events[0]), end_of(run.events[1]));
        const std::uint64_t earliest_submit = std::min(submit_of(run.events[0]), submit_of(run.events[1]));
        EXPECT_EQ(record.start_ns, earliest_start) << "force_scheduler = " << force_scheduler;
        EXPECT_EQ(record.end_ns, latest_end) << "force_scheduler = " << force_scheduler;
        EXPECT_EQ(record.submit_ns, earliest_submit) << "force_scheduler = " << force_scheduler;
        EXPECT_NE(record.start_ns, start_of(run.events[1]))
            << "the span starts with the first kernel, which the returned event alone would miss";
    }

    /** @brief Checks the record of a body that handed none over is the returned event's alone. */
    void expect_times_the_returned_event(bool force_scheduler)
    {
        const TwoKernelRun run = run_two_kernels(false, force_scheduler);

        ASSERT_EQ(run.records.size(), 1u);
        ASSERT_EQ(run.events.size(), 2u);
        const NodeEventRecord& record = run.records[0];
        EXPECT_EQ(record.start_ns, start_of(run.events[1])) << "force_scheduler = " << force_scheduler;
        EXPECT_EQ(record.end_ns, end_of(run.events[1])) << "force_scheduler = " << force_scheduler;
    }
} // namespace

/** @brief Tests that on the worker pool a body handing over two events is timed from the earliest start to the latest end. */
TEST(Medium_DispatchEvents, ABodyHandingOverTwoEventsIsTimedAcrossBoth)
{
    expect_spans_both(true);
}

/** @brief Tests the same on the in-order chain path. */
TEST(Medium_DispatchEvents, ABodyHandingOverTwoEventsIsTimedAcrossBothOnTheChain)
{
    expect_spans_both(false);
}

/** @brief Tests that a body handing over nothing is timed by the event it returns, as before. */
TEST(Medium_DispatchEvents, ABodyHandingOverNoneIsTimedAsBefore)
{
    expect_times_the_returned_event(true);
}

/** @brief Tests the same on the in-order chain path. */
TEST(Medium_DispatchEvents, ABodyHandingOverNoneIsTimedAsBeforeOnTheChain)
{
    expect_times_the_returned_event(false);
}

/** @brief Tests that handing an event over outside any body does nothing. */
TEST(Medium_DispatchEvents, HandingAnEventOverOutsideABodyDoesNothing)
{
    EXPECT_NO_THROW(note_dispatch_event(sycl::event{}));
}
