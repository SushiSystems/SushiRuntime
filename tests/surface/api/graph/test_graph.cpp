/**************************************************************************/
/* test_graph.cpp                                                         */
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
 * @file test_graph.cpp
 * @brief Tests running a Graph: host and device order, reports, cancellation, chains and determinism.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>

#include <SushiRuntime/engine_handles/plan_access.hpp>

#include "run_recorder.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Declares the test-only friend access to the plan of a Graph. */
        struct NodeEventRecordsTestAccess
        {
            static const SushiRuntime::Internal::Graph::CompiledGraph& plan(const Graph& g) noexcept
            {
                return *engine_plan(g.compiled_plan());
            }
        };
    }
}

using namespace SushiRuntime;

/** @brief Tests add_host(), which runs plain C++ on a worker thread ordered by the data it touches. */
namespace NativeCpuPathTests
{
    /** @brief Tests that a host task ordered after a device kernel sees the kernel's output. */
    TEST(Medium_NativeCPU, HostTaskRunsAfterKernel)
    {
        constexpr std::size_t N = 64;

        auto rt = Runtime::create().value();
        auto a  = rt.buffer<int>(N).value();
        auto b  = rt.buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

        int* ap = a.data();
        int* bp = b.data();

        auto g = rt.graph();
        g.add(N, Reads{}, Writes(a),
              [ap](std::size_t i) { ap[i] = static_cast<int>(i) * 2; });
        g.add_host(Reads(a), Writes(b),
                   [ap, bp]() { for (std::size_t i = 0; i < N; ++i) bp[i] = ap[i] + 1; });

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(b[i], static_cast<int>(i) * 2 + 1) << "at i=" << i;
    }

    /** @brief Tests that a device kernel ordered after a host task sees the host's writes. */
    TEST(Medium_NativeCPU, KernelRunsAfterHostTask)
    {
        constexpr std::size_t N = 64;

        auto rt = Runtime::create().value();
        auto a  = rt.buffer<int>(N).value();
        auto b  = rt.buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

        int* ap = a.data();
        int* bp = b.data();

        auto g = rt.graph();
        g.add_host(Reads{}, Writes(a),
                   [ap]() { for (std::size_t i = 0; i < N; ++i) ap[i] = static_cast<int>(i) + 5; });
        g.add(N, Reads(a), Writes(b),
              [ap, bp](std::size_t i) { bp[i] = ap[i] * 10; });

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(b[i], (static_cast<int>(i) + 5) * 10) << "at i=" << i;
    }

    /** @brief Tests that an untracked host task runs once per step. */
    TEST(Medium_NativeCPU, UntrackedHostTaskRunsEveryStep)
    {
        constexpr std::size_t STEPS = 5;

        auto rt = Runtime::create().value();
        std::atomic<int> ticks{0};

        auto g = rt.graph();
        g.add_host([&ticks]() { ticks.fetch_add(1, std::memory_order_relaxed); });

        g.run(STEPS).value();

        EXPECT_EQ(ticks.load(), static_cast<int>(STEPS));
    }

    /** @brief Tests that a throwing host task fails the run and leaves the runtime usable. */
    TEST(Medium_NativeCPU, ThrowingHostTaskKeepsRuntimeUsable)
    {
        auto rt = Runtime::create().value();

        auto bad = rt.graph();
        bad.add_host([]() { throw std::runtime_error("boom"); });
        auto r1 = bad.run();
        ASSERT_FALSE(r1.has_value()) << "a throwing native task must fail the run.";

        constexpr std::size_t N = 16;
        auto buf = rt.buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
        int* p = buf.data();

        auto good = rt.graph();
        good.add_host(Reads{}, Writes(buf),
                      [p]() { for (std::size_t i = 0; i < N; ++i) p[i] = static_cast<int>(i); });
        auto r2 = good.run();
        ASSERT_TRUE(r2.has_value()) << "the pool must stay re-armable after a failure.";

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i)) << "at i=" << i;
    }
} // namespace NativeCpuPathTests

/** @brief Tests per-dispatch node event records and their pairing with the event clock. */
namespace NodeEventRecordsTests
{
    namespace
    {
        // Registration index of the one compiled node named @p name.
        std::uint32_t registration_index_of(const Internal::Graph::CompiledGraph& plan, const char* name)
        {
            const Internal::Graph::Node* found = nullptr;
            for (const auto& n : plan.all_nodes)
            {
                if (std::string(n->metadata.name) == name)
                {
                    EXPECT_EQ(found, nullptr) << "Node name '" << name << "' is not unique.";
                    found = n.get();
                }
            }
            EXPECT_NE(found, nullptr) << "No compiled node named '" << name << "'.";
            return found ? found->registration_index : 0u;
        }
    } // namespace

    // E.1 - a runtime with no observer must hand out no record.
    TEST(Medium_NodeEventRecords, ProfilingNotRequestedLeavesNodeEventsEmpty)
    {
        constexpr std::size_t N = 128;

        auto rt = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        Testing::RunRecorder recorder(rt);
        recorder.stop();

        auto g = rt.graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });

        auto report = g.run().value();
        EXPECT_TRUE(report.is_successful);
        EXPECT_EQ(recorder.count(), 0u);
    }

    /**
     * @brief Tests that three kernel nodes over run(5) give fifteen records with submit <= start <=
     *        end.
     */
    TEST(Medium_NodeEventRecords, RequestedGraphRecordsOnePerDispatch)
    {
        constexpr std::size_t N = 64;

        auto rt = Runtime::create().value();
        auto a = rt.buffer<int>(N).value();
        auto b = rt.buffer<int>(N).value();
        auto c = rt.buffer<int>(N).value();
        int* pa = a.data();
        int* pb = b.data();
        int* pc = c.data();

        Testing::RunRecorder recorder(rt);
        auto g = rt.graph();
        g.add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] = static_cast<int>(i); });
        g.add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] = static_cast<int>(i); });
        g.add(N, Reads{}, Writes(c), [pc](std::size_t i) { pc[i] = static_cast<int>(i); });

        g.run(std::size_t{5}).value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();
        ASSERT_EQ(observation.events.size(), 15u);

        std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> invocations_by_node;
        for (const auto& rec : observation.events)
        {
            EXPECT_LE(rec.submit_ns, rec.start_ns);
            EXPECT_LE(rec.start_ns, rec.end_ns);
            invocations_by_node[rec.node_index].push_back(rec.invocation);
        }

        ASSERT_EQ(invocations_by_node.size(), 3u) << "expected one row per kernel node";
        for (auto& [node_index, invocations] : invocations_by_node)
        {
            std::sort(invocations.begin(), invocations.end());
            ASSERT_EQ(invocations.size(), 5u) << "node " << node_index;
            for (std::uint32_t i = 0; i < 5; ++i)
                EXPECT_EQ(invocations[i], i) << "node " << node_index;
        }
    }

    // The steady-state capture case is in test_sushiruntime.cpp; see README.md.

    /** @brief Tests that node_index follows registration order, not the compiled order. */
    TEST(Medium_NodeEventRecords, NodeIndexIsRegistrationOrder)
    {
        constexpr std::size_t N = 32;

        auto rt = Runtime::create().value();
        auto a = rt.buffer<int>(N).value();
        auto b = rt.buffer<int>(N).value();
        auto c = rt.buffer<int>(N).value();
        int* pa = a.data();
        int* pb = b.data();
        int* pc = c.data();

        // The strict dependency chain forces the execution order A, B, C; see README.md.
        Testing::RunRecorder recorder(rt);
        auto g = rt.graph();
        g.add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name("A");
        g.add(N, Reads(a), Writes(b), [pa, pb](std::size_t i) { pb[i] = pa[i] + 1; }).name("B");
        g.add(N, Reads(b), Writes(c), [pb, pc](std::size_t i) { pc[i] = pb[i] + 1; }).name("C");

        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();

        const auto& plan = Detail::NodeEventRecordsTestAccess::plan(g);
        const std::uint32_t idx_a = registration_index_of(plan, "A");
        const std::uint32_t idx_b = registration_index_of(plan, "B");
        const std::uint32_t idx_c = registration_index_of(plan, "C");

        EXPECT_EQ(idx_a, 0u);
        EXPECT_EQ(idx_b, 1u);
        EXPECT_EQ(idx_c, 2u);

        std::unordered_map<std::uint32_t, std::uint64_t> earliest_submit;
        for (const auto& rec : observation.events)
        {
            auto [it, inserted] = earliest_submit.try_emplace(rec.node_index, rec.submit_ns);
            if (!inserted) it->second = std::min(it->second, rec.submit_ns);
        }
        ASSERT_EQ(earliest_submit.count(idx_a), 1u);
        ASSERT_EQ(earliest_submit.count(idx_b), 1u);
        ASSERT_EQ(earliest_submit.count(idx_c), 1u);

        EXPECT_LE(earliest_submit[idx_a], earliest_submit[idx_b])
            << "A's dependency chain must have it dispatch no later than B";
        EXPECT_LE(earliest_submit[idx_b], earliest_submit[idx_c])
            << "B's dependency chain must have it dispatch no later than C";
    }

    /**
     * @brief Tests that a host node's record, on the steady clock, lands after the kernel it depends
     *        on once the kernel's record goes through its device's pairing.
     */
    TEST(Medium_NodeEventRecords, HostNodeLandsAfterItsKernelOnTheSteadyClock)
    {
        constexpr std::size_t N = 64;

        auto rt = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        // The host node reads what the kernel writes, so it runs after the kernel; see README.md.
        Testing::RunRecorder recorder(rt);
        auto g = rt.graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); }).name("kernel");
        g.add_host(Reads(buf), Writes{}, [p]()
        {
            volatile int sink = 0;
            for (std::size_t i = 0; i < N; ++i) sink += p[i];
            (void)sink;
        });

        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();
        ASSERT_FALSE(observation.events.empty());

        const auto& plan = Detail::NodeEventRecordsTestAccess::plan(g);
        const std::uint32_t idx_kernel = registration_index_of(plan, "kernel");

        // Only a host record carries the host device index.
        const NodeEventRecord* kernel_record = nullptr;
        const NodeEventRecord* host_record = nullptr;
        for (const auto& rec : observation.events)
        {
            if (rec.node_index == idx_kernel) kernel_record = &rec;
            else if (rec.device_index == K_HOST_DEVICE_INDEX) host_record = &rec;
        }

        ASSERT_NE(kernel_record, nullptr) << "no kernel record found among the events";
        ASSERT_NE(host_record, nullptr) << "no host record found among the events";
        const ClockPairing* pairing =
            Testing::RunRecorder::pairing_of(observation, kernel_record->device_index);
        ASSERT_NE(pairing, nullptr) << "the kernel's device has no pairing in the run";

        // The tolerance covers the clock pairing's measurement uncertainty only; see README.md.
        const std::uint64_t kernel_end =
            Testing::RunRecorder::steady_ns(observation, *kernel_record, kernel_record->end_ns);
        EXPECT_GE(host_record->start_ns + pairing->uncertainty_ns, kernel_end)
            << "the host record must not land before the kernel it depends on completed";
        EXPECT_LE(host_record->start_ns, host_record->end_ns);
    }
} // namespace NodeEventRecordsTests

/** @brief Tests the caller-owned RunReport overloads of run(), run() and RunHandle::wait(). */
namespace RunReportReuseTests
{
    namespace
    {
        constexpr std::size_t N = 64;

        /**
         * @brief Zeroes the buffer and records the one task every test here runs, which writes i+1 to
         *        element i.
         */
        void record_write(Graph& g, Buffer<int>& buf)
        {
            for (std::size_t i = 0; i < N; ++i)
                buf[i] = 0;

            int* p = buf.data();
            g.add(N, Reads{}, Writes(buf),
                  [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
        }
    }

    /** @brief Tests that the reuse overload reports what the value-returning one reports. */
    TEST(Medium_RunReportReuse, ReuseReportsWhatTheValueFormReports)
    {
        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        auto g   = rt.graph();
        record_write(g, buf);

        const RunReport allocated = g.run(std::size_t{3}).value();

        RunReport reused;
        g.run(std::size_t{3}, reused).value();

        EXPECT_TRUE(reused.is_successful);
        EXPECT_EQ(reused.total_tasks_executed, allocated.total_tasks_executed);
        EXPECT_EQ(reused.numa_statistics.size(), allocated.numa_statistics.size());
        EXPECT_EQ(reused.cancelled, allocated.cancelled);
        EXPECT_TRUE(reused.warnings.empty());

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "at i=" << i;
    }

    /** @brief Tests that steady-state runs refill the report's vectors in place. */
    TEST(Medium_RunReportReuse, SteadyStateRunsDoNotReallocateTheReport)
    {
        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        auto g   = rt.graph();
        record_write(g, buf);

        RunReport report;
        g.run(report).value();

        ASSERT_FALSE(report.numa_statistics.empty());
        const void* rows_after_first = report.numa_statistics.data();
        const std::size_t capacity_after_first = report.numa_statistics.capacity();

        for (int tick = 0; tick < 8; ++tick)
        {
            g.run(report).value();
            ASSERT_EQ(static_cast<const void*>(report.numa_statistics.data()), rows_after_first)
                << "the per-run rows were reallocated on tick " << tick;
            ASSERT_EQ(report.numa_statistics.capacity(), capacity_after_first);
        }

        // Refilled, not appended: eight more runs must not leave eight more rows.
        EXPECT_EQ(report.numa_statistics.size(), 1u);
    }

    /** @brief Tests that a short run after a long one reports the short run's count. */
    TEST(Medium_RunReportReuse, EachRunOverwritesTheLastOne)
    {
        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        auto g   = rt.graph();
        record_write(g, buf);

        RunReport report;
        g.run(std::size_t{5}, report).value();
        const std::size_t five_steps = report.total_tasks_executed;
        ASSERT_GT(five_steps, 0u);

        g.run(std::size_t{1}, report).value();
        EXPECT_LT(report.total_tasks_executed, five_steps)
            << "a one-step run reported the five-step run's task count";
        EXPECT_EQ(report.numa_statistics.size(), 1u);
    }

    /** @brief Tests that a rejected run clears the report and marks it unsuccessful. */
    TEST(Medium_RunReportReuse, ARejectedRunLeavesNoStaleNumbers)
    {
        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        auto g   = rt.graph();
        record_write(g, buf);

        RunReport report;
        g.run(report).value();
        ASSERT_TRUE(report.is_successful);
        ASSERT_GT(report.total_tasks_executed, 0u);

        // An empty graph cannot run; the same report goes in again.
        auto empty = rt.graph();
        auto rejected = empty.run(report);

        EXPECT_FALSE(rejected);
        EXPECT_EQ(rejected.error().code, ErrorCode::InvalidGraph);
        EXPECT_FALSE(report.is_successful);
        EXPECT_EQ(report.total_tasks_executed, 0u);
        EXPECT_TRUE(report.numa_statistics.empty());
    }

    /** @brief Tests that run(RunReport&) returns the report it was given. */
    TEST(Medium_RunReportReuse, TryRunReturnsTheCallersReport)
    {
        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        auto g   = rt.graph();
        record_write(g, buf);

        RunReport report;
        auto result = g.run(report);

        ASSERT_TRUE(result);
        EXPECT_EQ(*result, &report);
        EXPECT_TRUE(report.is_successful);
    }

    /** @brief Tests that RunHandle::wait(RunReport&) fills the caller's report. */
    TEST(Medium_RunReportReuse, AsyncWaitFillsTheCallersReport)
    {
        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        auto g   = rt.graph();
        record_write(g, buf);

        RunReport report;
        for (int tick = 0; tick < 4; ++tick)
        {
            auto step = g.run_async().value();
            step.wait(report).value();

            ASSERT_TRUE(report.is_successful) << "on tick " << tick;
            ASSERT_EQ(report.numa_statistics.size(), 1u) << "on tick " << tick;
        }

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i) + 1) << "at i=" << i;
    }

    // The composed-plan driver shares the fill path, so it gets the same guarantee.
    TEST(Medium_RunReportReuse, DynamicGraphRunsIntoACallersReport)
    {
        auto rt = Runtime::create().value();

        auto a = rt.buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            a[i] = 0;
        int* pa = a.data();

        auto world = rt.dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(a),
                            [pa](std::size_t i) { pa[i] += 1; });

        RunReport report;
        world.run(report).value();
        ASSERT_FALSE(report.numa_statistics.empty());
        const void* rows_after_first = report.numa_statistics.data();

        world.run(std::size_t{3}, report).value();

        EXPECT_TRUE(report.is_successful);
        EXPECT_EQ(static_cast<const void*>(report.numa_statistics.data()), rows_after_first);
        EXPECT_EQ(report.numa_statistics.size(), 1u);

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(a[i], 4) << "at i=" << i;
    }
} // namespace RunReportReuseTests

/** @brief Tests that cancel() from another thread stops run_until and leaves the next run alone. */
namespace CancellationTests
{
    namespace
    {
        /// @brief Longest wait for a run to show progress before the test fails.
        constexpr std::chrono::seconds K_PROGRESS_DEADLINE{30};

        /// @brief Longest wait for run_until to return once cancel() has been sent.
        constexpr std::chrono::seconds K_CANCEL_DEADLINE{5};

        /**
         * @brief Counts the stop-predicate calls of one run and carries a stop request into it.
         */
        class RunProgress final
        {
            public:
                /**
                 * @brief Records one predicate call and wakes the waiting thread.
                 * @return True once request_stop() has been called, which ends the run.
                 */
                [[nodiscard]] bool observe_predicate_call()
                {
                    {
                        const std::lock_guard<std::mutex> lock(mutex_);
                        ++predicate_calls_;
                    }
                    changed_.notify_all();
                    return stop_requested_.load(std::memory_order_acquire);
                }

                /**
                 * @brief Blocks until the predicate has been called @p calls times.
                 * @return False when @p deadline passes first.
                 */
                [[nodiscard]] bool wait_for_predicate_calls(std::size_t calls,
                                                            std::chrono::seconds deadline)
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    return changed_.wait_for(lock, deadline,
                                             [&] { return predicate_calls_ >= calls; });
                }

                /// @brief Makes every later predicate call end the run.
                void request_stop() noexcept
                {
                    stop_requested_.store(true, std::memory_order_release);
                }

            private:
                /// @brief Guards predicate_calls_.
                std::mutex mutex_;

                /// @brief Signalled after every predicate call.
                std::condition_variable changed_;

                /// @brief Predicate calls seen so far.
                std::size_t predicate_calls_ = 0;

                /// @brief Set by request_stop(); read by the run thread.
                std::atomic<bool> stop_requested_{false};
        };

        /**
         * @brief Requests a stop on a RunProgress when it leaves scope.
         */
        class StopAtScopeExit final
        {
            public:
                explicit StopAtScopeExit(RunProgress& progress) noexcept : progress_(progress)
                {

                }

                ~StopAtScopeExit() { progress_.request_stop(); }

                StopAtScopeExit(const StopAtScopeExit&) = delete;
                StopAtScopeExit& operator=(const StopAtScopeExit&) = delete;
                StopAtScopeExit(StopAtScopeExit&&) = delete;
                StopAtScopeExit& operator=(StopAtScopeExit&&) = delete;

            private:
                /// @brief The run to stop.
                RunProgress& progress_;
        };

        /**
         * @brief Starts an unbounded run_until on another thread and cancels it once the stop
         *        predicate has been called @p predicate_calls times.
         * @param predicate_calls 1 proves the run is past its cancellation reset; 2 proves one
         *        whole step has also finished.
         * @param report Receives the cancelled run's report; untouched on a fatal failure.
         */
        template <class GraphType>
        void cancel_unbounded_run(GraphType& graph, std::size_t predicate_calls,
                                  RunReport& report)
        {
            RunProgress progress;
            auto run = std::async(std::launch::async, [&graph, &progress]
            {
                return graph.run_until([&progress] { return progress.observe_predicate_call(); })
                    .value();
            });

            // Declared after run, so the stop request precedes the join in the future's destructor.
            const StopAtScopeExit stop_run(progress);

            ASSERT_TRUE(progress.wait_for_predicate_calls(predicate_calls, K_PROGRESS_DEADLINE))
                << "run_until did not call its predicate " << predicate_calls << " time(s) within "
                << K_PROGRESS_DEADLINE.count() << " s, so no cancel() was sent";

            graph.cancel();

            ASSERT_EQ(run.wait_for(K_CANCEL_DEADLINE), std::future_status::ready)
                << "run_until did not return promptly after cancel()";

            report = run.get();
        }
    } // namespace

    /**
     * @brief A second thread cancels an unbounded run_until and it returns quickly.
     */
    TEST(Medium_Cancellation, CancelStopsRunUntilFromAnotherThread)
    {
        constexpr std::size_t N = 1024;
        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) field[i] = 0;

        auto g = rt.graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        // The second predicate call follows the first whole step, so the report holds one.
        RunReport report;
        ASSERT_NO_FATAL_FAILURE(cancel_unbounded_run(g, 2, report));

        EXPECT_TRUE(report.cancelled);
        EXPECT_GT(report.total_tasks_executed, 0u) << "some steps should have run before cancel";
    }

    /**
     * @brief After a cancelled run, the next run starts clean and completes fully.
     */
    TEST(Medium_Cancellation, NextRunIsNotCancelledAfterReset)
    {
        constexpr std::size_t N = 256;
        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) field[i] = 0;

        auto g = rt.graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        RunReport cancelled_report;
        ASSERT_NO_FATAL_FAILURE(cancel_unbounded_run(g, 1, cancelled_report));
        EXPECT_TRUE(cancelled_report.cancelled);

        // A fresh bounded run must not inherit the previous cancellation.
        auto report = g.run(3).value();
        EXPECT_FALSE(report.cancelled);
        EXPECT_EQ(report.total_tasks_executed, 3u);
    }
} // namespace CancellationTests

/** @brief Tests that a chainable plan runs as an in-order chain, bit-identical to the scheduler. */
namespace ChainLayerTests
{
    namespace
    {
        constexpr std::size_t N = 1024;

        /** @brief Holds every buffer the workload writes, copied out after the run. */
        struct Outputs
        {
            std::vector<float> field;          /**< The State's current field. */
            std::vector<std::uint32_t> acc;    /**< Written over a host-sized prefix each step. */
            std::vector<std::uint32_t> mix;    /**< Reads acc, so it orders after it. */
            std::vector<std::uint32_t> gated;  /**< Written only on the steps its when() allows. */
            /** @brief Layer of the last run. */
            ExecutionLayer layer = ExecutionLayer::Scheduler;
            std::size_t tasks = 0;             /**< total_tasks_executed summed over the runs. */
        };

        /**
         * @brief Builds the workload on @p rt, runs it @p runs times for @p steps each, and copies the
         *        results out.
         *
         * Host values change every step: the sized() count and the when() gate
         * both come from counters their providers advance. @p native_tail adds
         * a no-op host task, which keeps the plan off the chain.
         */
        Outputs run_workload(Runtime& rt, std::size_t runs, std::size_t steps, bool native_tail)
        {
            auto field = rt.state<float>(N).value();
            auto acc   = rt.buffer<std::uint32_t>(N).value();
            auto mix   = rt.buffer<std::uint32_t>(N).value();
            auto gated = rt.buffer<std::uint32_t>(N).value();
            for (std::size_t i = 0; i < N; ++i)
            {
                field[i] = static_cast<float>(i) * 0.5f;
                acc[i] = 0;
                mix[i] = 0;
                gated[i] = 0;
            }

            std::uint32_t* pa = acc.data();
            std::uint32_t* pm = mix.data();
            std::uint32_t* pg = gated.data();

            std::size_t size_tick = 0;
            std::size_t gate_tick = 0;

            auto g = rt.graph();
            g.add_rule(field, [](std::size_t i, const float* cur, float* next)
            {
                next[i] = cur[i] * 0.75f + static_cast<float>(i % 7) * 0.125f;
            });
            g.add_dynamic(sized([&size_tick] { return 1 + (size_tick++ * 37) % N; }),
                          N, Reads{}, Writes(acc),
                          [pa](std::size_t i) { pa[i] = pa[i] * 31u + static_cast<std::uint32_t>(i) + 1u; });
            g.add(N, Reads(acc), Writes(mix),
                  [pa, pm](std::size_t i) { pm[i] = (pm[i] ^ (pa[i] * 2654435761u)) + 7u; });
            g.add_dynamic(when([&gate_tick] { return (gate_tick++ % 3) != 0; }),
                          N, Reads(mix), Writes(gated),
                          [pm, pg](std::size_t i) { pg[i] = pg[i] * 17u + pm[i]; });
            if (native_tail)
                g.add_host([] {});

            Outputs out;
            for (std::size_t r = 0; r < runs; ++r)
            {
                const auto report = g.run(steps).value();
                EXPECT_TRUE(report.is_successful);
                out.layer = report.layer;
                out.tasks += report.total_tasks_executed;
            }

            out.field.assign(field.current(), field.current() + N);
            out.acc.assign(pa, pa + N);
            out.mix.assign(pm, pm + N);
            out.gated.assign(pg, pg + N);
            return out;
        }

        /**
         * @brief Compares two element vectors byte for byte.
         * @return True when both hold the same bytes.
         */
        template <class T>
        bool same_bytes(const std::vector<T>& a, const std::vector<T>& b)
        {
            return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0;
        }

        /**
         * @brief Asserts two workload results are bit-identical, buffer by buffer.
         */
        void expect_identical(const Outputs& a, const Outputs& b)
        {
            EXPECT_TRUE(same_bytes(a.field, b.field)) << "State field differs";
            EXPECT_TRUE(same_bytes(a.acc, b.acc)) << "sized() buffer differs";
            EXPECT_TRUE(same_bytes(a.mix, b.mix)) << "dependent buffer differs";
            EXPECT_TRUE(same_bytes(a.gated, b.gated)) << "when() buffer differs";
            EXPECT_EQ(a.tasks, b.tasks);
        }
    } // namespace

    TEST(Medium_ChainLayer, ChainMatchesForcedSchedulerBitForBit)
    {
        constexpr std::size_t RUNS  = 3;
        constexpr std::size_t STEPS = 40;

        auto chained_rt = Runtime::create().value();
        RuntimeConfig forced;
        forced.force_scheduler = true;
        auto scheduled_rt = Runtime::create(forced).value();

        const Outputs chained   = run_workload(chained_rt, RUNS, STEPS, false);
        const Outputs scheduled = run_workload(scheduled_rt, RUNS, STEPS, false);

        EXPECT_EQ(chained.layer, ExecutionLayer::Chain)
            << "a plan of tracked kernels on one device must take the chain";
        EXPECT_EQ(scheduled.layer, ExecutionLayer::Scheduler)
            << "force_scheduler must keep every plan on the scheduler";
        expect_identical(chained, scheduled);
    }

    TEST(Medium_ChainLayer, NonChainablePlanMatchesChainBitForBit)
    {
        constexpr std::size_t RUNS  = 2;
        constexpr std::size_t STEPS = 64;

        auto rt = Runtime::create().value();

        const Outputs chained = run_workload(rt, RUNS, STEPS, false);
        const Outputs native  = run_workload(rt, RUNS, STEPS, true);

        EXPECT_EQ(chained.layer, ExecutionLayer::Chain);
        EXPECT_EQ(native.layer, ExecutionLayer::Scheduler)
            << "a native host task must keep the plan on the scheduler";

        EXPECT_TRUE(same_bytes(chained.field, native.field));
        EXPECT_TRUE(same_bytes(chained.acc, native.acc));
        EXPECT_TRUE(same_bytes(chained.mix, native.mix));
        EXPECT_TRUE(same_bytes(chained.gated, native.gated));
    }

    /**
     * @brief Tests that a node that names an operation runs on the chain when no cluster is
     *        attached; see README.md.
     */
    TEST(Medium_ChainLayer, ANamedNodeRunsOnTheChainWithNoClusterAttached)
    {
        auto rt = Runtime::create().value();
        auto values = rt.buffer<std::uint32_t>(N).value();
        auto field = rt.state<std::uint32_t>(N).value();
        for (std::size_t i = 0; i < N; ++i)
        {
            values[i] = 0;
            field[i] = 0;
        }
        std::uint32_t* words = values.data();

        const std::uint64_t parameters[] = {N};
        NodeOptions named;
        named.operation = "chain_layer.named"_op;
        named.parameters = parameters;

        auto graph = rt.graph();
        graph.add(N, Reads{}, Writes(values), [words](std::size_t i) { words[i] += 1u; }, named);
        graph.add_rule(field,
                       [](std::size_t i, const std::uint32_t* current, std::uint32_t* next)
                       { next[i] = current[i] + 2u; },
                       named);

        constexpr std::size_t STEPS = 3;
        const auto report = graph.run(STEPS);
        ASSERT_TRUE(report.has_value()) << report.error().message;
        EXPECT_EQ(report->layer, ExecutionLayer::Chain)
            << "a named node kept the plan off the chain although nothing can take it";
        for (std::size_t i = 0; i < N; ++i)
        {
            ASSERT_EQ(values[i], STEPS) << "at i=" << i;
            ASSERT_EQ(field[i], 2u * STEPS) << "at i=" << i;
        }
    }

    TEST(Medium_ChainLayer, RunUntilSeesEveryStepsResult)
    {
        constexpr std::uint32_t TARGET = 9;

        auto rt = Runtime::create().value();
        auto counter = rt.buffer<std::uint32_t>(1).value();
        counter[0] = 0;
        std::uint32_t* pc = counter.data();

        auto g = rt.graph();
        g.add(1, Reads{}, Writes(counter), [pc](std::size_t) { pc[0] += 1u; });

        // The stop test reads a device result, so the chain must wait after every step.
        const auto report = g.run_until([&counter] { return counter[0] >= TARGET; }).value();

        EXPECT_EQ(report.layer, ExecutionLayer::Chain);
        EXPECT_EQ(report.total_tasks_executed, static_cast<std::size_t>(TARGET));
        EXPECT_EQ(counter[0], TARGET);
    }

    TEST(Medium_ChainLayer, ProfiledChainRecordsEveryDispatch)
    {
        constexpr std::size_t STEPS = 12;

        auto rt = Runtime::create().value();
        auto a = rt.buffer<int>(N).value();
        auto b = rt.buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
        {
            a[i] = 0;
            b[i] = 0;
        }
        int* pa = a.data();
        int* pb = b.data();

        Testing::RunRecorder recorder(rt);
        auto g = rt.graph();
        g.add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
        g.add(N, Reads(a), Writes(b), [pa, pb](std::size_t i) { pb[i] += pa[i]; });

        const auto report = g.run(STEPS).value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();

        EXPECT_EQ(report.layer, ExecutionLayer::Chain)
            << "profiling must not move a chainable plan off layer 1";
        EXPECT_EQ(observation.layer, ExecutionLayer::Chain);
        ASSERT_EQ(observation.events.size(), 2 * STEPS);

        std::vector<std::size_t> per_step(STEPS, 0);
        for (const auto& rec : observation.events)
        {
            ASSERT_LT(rec.invocation, STEPS);
            ++per_step[rec.invocation];
            EXPECT_LE(rec.start_ns, rec.end_ns);
        }
        for (std::size_t s = 0; s < STEPS; ++s)
            EXPECT_EQ(per_step[s], 2u) << "step " << s;

        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 2u);
        for (const auto& row : observation.table->rows())
        {
            std::size_t dispatches = 0;
            for (const auto& rec : observation.events)
                if (rec.node_index == row.index)
                    ++dispatches;
            EXPECT_EQ(dispatches, STEPS) << "node " << row.index;
        }

        // b[i] after k steps is 1 + 2 + ... + k.
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(b[i], static_cast<int>(STEPS * (STEPS + 1) / 2)) << "at i=" << i;
    }
} // namespace ChainLayerTests

/** @brief Tests that the reusable completion latch drives a replayed run and re-runs on one plan. */
namespace CompletionLatchTests
{
    TEST(Medium_CompletionLatch, ManyStepReplayReachesCorrectState)
    {
        constexpr std::size_t N = 1024;
        constexpr std::size_t STEPS = 5000;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) field[i] = 0;

        auto g = rt.graph();
        // Each step adds 1 to every cell: after STEPS steps every cell equals STEPS.
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        auto rep = g.run(STEPS).value();

        EXPECT_TRUE(rep.is_successful);
        EXPECT_FALSE(rep.cancelled);
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(field[i], static_cast<int>(STEPS)) << "at i=" << i;
    }

    TEST(Medium_CompletionLatch, ReRunsCleanlyOnTheSamePlan)
    {
        constexpr std::size_t N = 256;

        auto rt    = Runtime::create().value();
        auto field = rt.state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) field[i] = 0;

        auto g = rt.graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        // The latch resets between the three run() calls and the sentinel still signals; see README.md.
        g.run(10).value();
        g.run(10).value();
        g.run(5).value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(field[i], 25) << "at i=" << i;
    }

    TEST(Medium_CompletionLatch, SingleStepRunsRepeatedlyWithoutLeak)
    {
        constexpr std::size_t N = 64;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        auto g = rt.graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });

        // Each single-step run goes through one reset, arm and wait cycle of the latch; see README.md.
        for (int r = 0; r < 200; ++r)
        {
            auto rep = g.run().value();
            ASSERT_TRUE(rep.is_successful) << "run " << r;
        }
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(buf[i], static_cast<int>(i));
    }
} // namespace CompletionLatchTests

/** @brief Tests that a node spanning two device contexts fails the run with an error. */
namespace CrossContextGuardTests
{
    TEST(Medium_CrossContextGuard, NodeSpanningTwoContextsFailsCleanly)
    {
        constexpr std::size_t N = 256;

        auto rt = Runtime::create().value();
        if (rt.advanced().device_count() < 2)
            GTEST_SKIP() << "needs at least two device contexts to straddle a boundary";

        // Two buffers pinned to different device queues (and thus contexts).
        auto a = rt.buffer<int>(N, DeviceIndex{0}).value();
        auto b = rt.buffer<int>(N, DeviceIndex{1}).value();
        int* pb = b.data();
        const int* pa = a.data();

        // No one queue can run a node that reads context 0 and writes context 1; see README.md.
        auto g = rt.graph();
        g.add(N, Reads(a), Writes(b),
              [pa, pb](std::size_t i) { pb[i] = pa[i] + 1; });

        // The run fails with an error and the runtime stays usable afterward; see README.md.
        auto result = g.run();
        ASSERT_FALSE(result) << "a cross-context node must not run successfully";
        EXPECT_EQ(result.error().code, ErrorCode::InvalidGraph);

        // A well-formed, co-located graph still runs on the same runtime afterward.
        for (std::size_t i = 0; i < N; ++i) a[i] = static_cast<int>(i);
        int* pa2 = a.data();
        auto g2 = rt.graph();
        g2.add(N, Reads{}, Writes(a), [pa2](std::size_t i) { pa2[i] *= 2; });
        auto ok = g2.run();
        EXPECT_TRUE(ok) << "the pool must stay re-armable after the guarded failure";
    }
} // namespace CrossContextGuardTests

/** @brief Tests that the same graph produces byte-identical output for every worker count. */
namespace DeterminismTests
{
    namespace
    {
        constexpr std::size_t N     = 40000;
        constexpr std::size_t K_STEPS = 4;

        /**
         * @brief Returns one large value followed by many small ones, so a sum depends on its
         *        combination order.
         */
        float order_sensitive_value(std::size_t i)
        {
            return (i == 0) ? 1.0e8f : 1.0f;
        }

        /** @brief Returns a second pattern that alternates in sign. */
        float alternating_value(std::size_t i)
        {
            const float magnitude = (i % 512 == 0) ? 1.0e7f : 0.5f;
            return (i % 2 == 0) ? magnitude : -magnitude;
        }

        constexpr std::size_t K_TILE  = 256;
        constexpr std::size_t K_TILES = (N + K_TILE - 1) / K_TILE;

        /**
         * @brief Records one rule of two nodes that totals N values in an order fixed by K_TILE.
         *
         * One work-item per tile adds its values left to right into recorder scratch, then one
         * work-item adds the partials left to right into total[0].
         * @param input Holds N values; must outlive the recorder.
         */
        void record_tiled_total(Recorder& recorder, const Buffer<float>& input,
                                const Buffer<float>& total)
        {
            float* partials =
                recorder.scratch<float>(K_TILES, input.device_index(), input.residency()).value();
            const BufferRegion partial_region{partials, 0, K_TILES * sizeof(float)};
            const float* in = input.data();
            float* out = total.data();

            auto rule = recorder.rule();
            recorder.kernel(K_TILES, Access(Reads(input), Writes(partial_region)),
                            [in, partials](std::size_t t)
                            {
                                const std::size_t begin = t * K_TILE;
                                const std::size_t end = (begin + K_TILE < N) ? begin + K_TILE : N;
                                float sum = in[begin];
                                for (std::size_t i = begin + 1; i < end; ++i)
                                    sum = sum + in[i];
                                partials[t] = sum;
                            });
            recorder.kernel(std::size_t{1}, Access(Reads(partial_region), Writes(total)),
                            [partials, out](std::size_t)
                            {
                                float sum = partials[0];
                                for (std::size_t t = 1; t < K_TILES; ++t)
                                    sum = sum + partials[t];
                                out[0] = sum;
                            });
        }

        // What one configuration produced, and the pool it actually ran on.
        struct Outcome
        {
            std::vector<float> output;  /**< The graph's output buffer, copied out. */
            float              first_total  = 0.0f;
            float              second_total = 0.0f;
            std::size_t        workers      = 0;
        };

        /**
         * @brief Runs the fixed workload on a runtime built for the given worker count and copies the
         *        results out.
         * @param rebalancer Whether the background rebalancer runs during the workload.
         */
        Outcome run_workload(std::size_t worker_count, bool rebalancer = false)
        {
            RuntimeConfig config;
            config.worker_count = worker_count;
            config.rebalancer   = rebalancer;

            auto rt = Runtime::create(config).value();

            auto first   = rt.buffer<float>(N).value();
            auto second  = rt.buffer<float>(N).value();
            auto total_a = rt.buffer<float>(1).value();
            auto total_b = rt.buffer<float>(1).value();
            auto out     = rt.buffer<float>(N).value();

            for (std::size_t i = 0; i < N; ++i)
            {
                first[i]  = order_sensitive_value(i);
                second[i] = alternating_value(i);
                out[i]    = 0.0f;
            }
            total_a[0] = 0.0f;
            total_b[0] = 0.0f;

            const float* pf = first.data();
            const float* ps = second.data();
            const float* pa = total_a.data();
            const float* pb = total_b.data();
            float*       po = out.data();

            auto g = rt.graph();
            record_tiled_total(g.recorder(), first, total_a);
            record_tiled_total(g.recorder(), second, total_b);
            g.add(N, Reads(first, second, total_a, total_b), Writes(out),
                  [pf, ps, pa, pb, po](std::size_t i)
                  {
                      po[i] = pf[i] * pa[0] + ps[i] * pb[0];
                  });

            g.run(K_STEPS).value();

            Outcome outcome;
            outcome.output.resize(N);
            for (std::size_t i = 0; i < N; ++i)
                outcome.output[i] = out[i];
            outcome.first_total  = total_a[0];
            outcome.second_total = total_b[0];
            outcome.workers      = rt.advanced().worker_pool().count;
            return outcome;
        }

        /** @brief Returns whether two float vectors are equal byte for byte. */
        bool bytes_equal(const std::vector<float>& a, const std::vector<float>& b)
        {
            return a.size() == b.size() &&
                   std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
        }

        /** @brief Returns the index of the first differing element. */
        std::size_t first_difference(const std::vector<float>& a, const std::vector<float>& b)
        {
            const std::size_t n = (a.size() < b.size()) ? a.size() : b.size();
            for (std::size_t i = 0; i < n; ++i)
                if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0)
                    return i;
            return n;
        }
    }

    /** @brief Tests that the floating-point compile contract reaches a consumer translation unit. */
    TEST(Medium_Determinism, TheFloatingPointContractReachesConsumerCode)
    {
    #ifndef SR_DETERMINISTIC_FP
        GTEST_SKIP() << "built with SR_DETERMINISTIC_FP=OFF; byte-equal replay is "
                        "not claimed for this configuration";
    #else
    #if defined(__FAST_MATH__) || defined(_M_FP_FAST)
        constexpr bool fast_math_enabled = true;
    #else
        constexpr bool fast_math_enabled = false;
    #endif
        EXPECT_FALSE(fast_math_enabled)
            << "fast-math is enabled: reassociation is licensed, so the same source "
               "can fold to different bits between builds and replay cannot be "
               "byte-equal";
    #endif
    }

    /** @brief Tests that the compiler does not reassociate host arithmetic. */
    TEST(Medium_Determinism, HostArithmeticIsNotReassociated)
    {
        volatile double big   = 1.0e16;
        volatile double minus = -1.0e16;
        volatile double one   = 1.0;

        const double left  = (big + minus) + one;  // 0.0 + 1.0 == 1.0
        const double right = big + (minus + one);  // 1e16 + -1e16 == 0.0

        EXPECT_NE(left, right)
            << "the compiler reassociated a floating-point sum: the same source can "
               "fold to different bits between builds";
        EXPECT_DOUBLE_EQ(left, 1.0);
        EXPECT_DOUBLE_EQ(right, 0.0);
    }

    /** @brief Tests that one worker, two workers and the whole machine agree on every output byte. */
    TEST(Medium_Determinism, OutputIsByteEqualAcrossWorkerCounts)
    {
        const Outcome single = run_workload(1);
        const Outcome pair   = run_workload(2);
        const Outcome full   = run_workload(0);   // 0 == the whole machine

        // With fewer than two workers every configuration is the same pool; see README.md.
        if (full.workers < 2)
            GTEST_SKIP() << "single-core machine (pool = " << full.workers
                         << "); worker-count variation is not observable here";

        EXPECT_EQ(single.workers, 1u) << "worker_count = 1 did not produce a one-worker pool";
        EXPECT_GT(full.workers, single.workers)
            << "the default pool is no wider than the one-worker pool; the comparison "
               "below would not be varying anything";

        EXPECT_TRUE(bytes_equal(single.output, pair.output))
            << "1 vs 2 workers diverged at element " << first_difference(single.output, pair.output);
        EXPECT_TRUE(bytes_equal(single.output, full.output))
            << "1 vs " << full.workers << " workers diverged at element "
            << first_difference(single.output, full.output);

        // The two totals themselves, so a failure says which half moved.
        EXPECT_EQ(std::memcmp(&single.first_total, &pair.first_total, sizeof(float)), 0);
        EXPECT_EQ(std::memcmp(&single.first_total, &full.first_total, sizeof(float)), 0);
        EXPECT_EQ(std::memcmp(&single.second_total, &pair.second_total, sizeof(float)), 0);
        EXPECT_EQ(std::memcmp(&single.second_total, &full.second_total, sizeof(float)), 0);
    }

    /** @brief Tests that the same configuration run again from scratch lands on the same bytes. */
    TEST(Medium_Determinism, RepeatedConfigurationsAgreeByteForByte)
    {
        const Outcome first  = run_workload(0);
        const Outcome second = run_workload(0);

        ASSERT_EQ(first.workers, second.workers);
        EXPECT_TRUE(bytes_equal(first.output, second.output))
            << "two identical runs diverged at element "
            << first_difference(first.output, second.output);
    }

    /** @brief Tests that a run with the background rebalancer on lands on the same bytes. */
    TEST(Medium_Determinism, TheRebalancerDoesNotChangeTheBytes)
    {
        const Outcome quiet  = run_workload(0, false);
        const Outcome moving = run_workload(0, true);

        ASSERT_EQ(quiet.workers, moving.workers);
        EXPECT_TRUE(bytes_equal(quiet.output, moving.output))
            << "the rebalancer changed element " << first_difference(quiet.output, moving.output);
    }

    /** @brief Tests that the first and the last step of a multi-step run produce the same bytes. */
    TEST(Medium_Determinism, ReplayDoesNotDriftAcrossSteps)
    {
        RuntimeConfig config;
        config.rebalancer = false;
        auto rt = Runtime::create(config).value();

        auto in    = rt.buffer<float>(N).value();
        auto total = rt.buffer<float>(1).value();
        for (std::size_t i = 0; i < N; ++i)
            in[i] = order_sensitive_value(i);
        total[0] = 0.0f;

        auto g = rt.graph();
        record_tiled_total(g.recorder(), in, total);

        g.run().value();
        const float after_one_step = total[0];

        total[0] = 0.0f;
        g.run(K_STEPS).value();

        EXPECT_EQ(std::memcmp(&after_one_step, &total[0], sizeof(float)), 0)
            << "a replayed rule drifted: one step gave " << after_one_step
            << ", " << K_STEPS << " steps gave " << total[0];
        EXPECT_EQ(g.compile_count(), 1u) << "a replay must not recompile";
    }
} // namespace DeterminismTests

/** @brief Tests the per-graph profiling switch and the profiling queue each dispatch branch uses. */
namespace GraphProfilingSwitchTests
{
    namespace
    {
        // True once any device queue this runtime built carries enable_profiling.
        bool any_profiling_queue_built(Internal::Execution::RuntimeContext& ctx)
        {
            for (std::size_t i = 0; i < ctx.devices().count(); ++i)
                if (ctx.devices().has_profiling_queue(i))
                    return true;
            return false;
        }
    } // namespace

    TEST(Medium_GraphProfilingSwitch, TwoGraphsBothRecordWhileAnObserverIsAdded)
    {
        constexpr std::size_t N = 256;

        auto rt = Runtime::create().value(); // no observer, so no timing
        ASSERT_FALSE(any_profiling_queue_built(SushiRuntime::Internal::ContextAccess::context(rt)));

        auto a = rt.buffer<int>(N).value();
        auto b = rt.buffer<int>(N).value();
        int* pa = a.data();
        int* pb = b.data();

        auto first = rt.graph();
        first.add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] = static_cast<int>(i); });

        auto second = rt.graph();
        second.add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] = static_cast<int>(i); });

        // Without an observer neither graph builds a profiling queue.
        first.run().value();
        second.run().value();
        EXPECT_FALSE(any_profiling_queue_built(SushiRuntime::Internal::ContextAccess::context(rt)));

        // Adding the observer turns timing on for both graphs of the runtime.
        Testing::RunRecorder recorder(rt);
        first.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        EXPECT_FALSE(recorder.last().events.empty()) << "the first graph must collect device timing";
        EXPECT_TRUE(any_profiling_queue_built(SushiRuntime::Internal::ContextAccess::context(rt)))
            << "running a timed graph must have built a profiling queue";

        second.run().value();
        ASSERT_EQ(recorder.count(), 2u);
        EXPECT_FALSE(recorder.last().events.empty()) << "the second graph must collect device timing too";
    }

    TEST(Medium_GraphProfilingSwitch, AddingAnObserverRecompilesNothingAndSwitches)
    {
        constexpr std::size_t N = 128;

        auto rt = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        auto g = rt.graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });

        g.run().value();
        const std::size_t compiles_after_first_run = g.compile_count();
        EXPECT_EQ(compiles_after_first_run, 1u);

        // A plan built before the observer gives full records on its first run after the add.
        Testing::RunRecorder recorder(rt);
        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        EXPECT_FALSE(recorder.last().events.empty());
        EXPECT_EQ(g.compile_count(), compiles_after_first_run)
            << "adding an observer must not recompile the plan";

        g.run().value();
        ASSERT_EQ(recorder.count(), 2u);
        EXPECT_FALSE(recorder.last().events.empty());
        EXPECT_EQ(g.compile_count(), compiles_after_first_run);

        // Removing the last observer recompiles nothing and records nothing.
        recorder.stop();
        g.run().value();
        EXPECT_EQ(recorder.count(), 2u);
        EXPECT_EQ(g.compile_count(), compiles_after_first_run)
            << "removing the last observer must not recompile the plan";
    }

    /**
     * @brief Tests that a node without declared Reads or Writes takes the NUMA fallback branch and
     *        still uses a profiling queue.
     */
    TEST(Medium_GraphProfilingSwitch, NUMAFallbackBranchUsesProfilingQueue)
    {
        constexpr std::size_t N = 256;

        auto rt = Runtime::create().value(); // no observer until the recorder below
        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        Testing::RunRecorder recorder(rt);
        auto g = rt.graph();
        g.add(N, Reads{}, Writes{}, [p](std::size_t i) { p[i] = static_cast<int>(i); });

        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        EXPECT_FALSE(recorder.last().events.empty())
            << "the NUMA-fallback branch must still dispatch on a profiling queue";
        EXPECT_TRUE(any_profiling_queue_built(SushiRuntime::Internal::ContextAccess::context(rt)))
            << "reaching this branch under profiling must build a profiling queue";
    }
} // namespace GraphProfilingSwitchTests

/** @brief Tests the node table, the node names and the transfer accounting around a run. */
namespace ObservabilityTests
{
    TEST(Medium_Observability, ProfilingOffLeavesBreakdownEmpty)
    {
        constexpr std::size_t N = 256;

        auto rt  = Runtime::create().value();                 // no observer
        Testing::RunRecorder recorder(rt);
        recorder.stop();

        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        auto g = rt.graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
        auto rep = g.run(4).value();

        EXPECT_TRUE(rep.is_successful);
        EXPECT_EQ(recorder.count(), 0u);
    }

    TEST(Medium_Observability, ProfilingOnCapturesPerNodeTiming)
    {
        constexpr std::size_t N = 4096;
        constexpr std::size_t STEPS = 8;

        auto rt = Runtime::create().value();
        Testing::RunRecorder recorder(rt);

        auto a = rt.buffer<float>(N).value();
        auto b = rt.buffer<float>(N).value();
        float* pa = a.data();
        float* pb = b.data();

        auto g = rt.graph();
        // A heavier kernel and a lighter one, two distinct plan nodes.
        g.add(N, Reads{}, Writes(a), [pa](std::size_t i)
        {
            float v = static_cast<float>(i);
            for (int k = 0; k < 64; ++k) v = v * 1.0001f + 1.0f;
            pa[i] = v;
        });
        g.add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] = static_cast<float>(i); });

        g.run(STEPS).value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();

        // Each kernel is one dispatch per step, and only the counts are asserted; see README.md.
        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 2u);
        for (const auto& row : observation.table->rows())
        {
            std::size_t dispatches = 0;
            for (const auto& rec : observation.events)
            {
                if (rec.node_index != row.index)
                    continue;
                ++dispatches;
                EXPECT_LE(rec.start_ns, rec.end_ns) << "node " << row.index;
            }
            EXPECT_EQ(dispatches, STEPS) << "node " << row.index;
        }
    }

    /** @brief Tests that a name given through name() reaches the node table. */
    TEST(Medium_Observability, NamingTheLastNodeReachesTheNodeTable)
    {
        constexpr std::size_t N = 512;
        constexpr std::size_t STEPS = 4;

        auto rt = Runtime::create().value();
        Testing::RunRecorder recorder(rt);

        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        auto g = rt.graph();
        g.add(N, Reads{}, Writes(buf),
              [p](std::size_t i) { p[i] = static_cast<int>(i); }).name("seed");

        g.run(STEPS).value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();

        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 1u);
        EXPECT_EQ(observation.table->rows()[0].name, "seed");
    }

    /** @brief Tests that every add() shape can be named, the two variadic ones included. */
    TEST(Medium_Observability, EveryAddShapeCanBeNamed)
    {
        constexpr std::size_t N = 256;

        auto rt = Runtime::create().value();
        Testing::RunRecorder recorder(rt);

        auto a = rt.buffer<float>(N).value();
        auto b = rt.buffer<float>(N).value();
        auto c = rt.buffer<float>(N).value();
        auto s = rt.state<float>(N).value();
        float* pa = a.data();
        float* pb = b.data();

        auto g = rt.graph();

        // Plain tracked per-element.
        g.add(N, Reads(a), Writes(b),
              [pa, pb](std::size_t i) { pb[i] = pa[i] + 1.0f; }).name("plain");

        // Variadic capture shape: the pack's last element is the callable.
        g.add(Extent<1>{N}, In(b), Out(c),
              [](sycl::id<1> i, const float* in, float* out) { out[i] = in[i] * 2.0f; })
         .name("captured");

        // State shape.
        g.add_rule(s, [](std::size_t i, const float* cur, float* next) { next[i] = cur[i] + 1.0f; })
         .name("stateful");

        // Host shape.
        g.add_host(Reads(c), Writes{}, [] {}).name("host");

        // Dynamic per-element shape.
        g.add_dynamic(sized([] { return N; }), N, Reads(a), Writes(b),
                      [pa, pb](std::size_t i) { pb[i] = pa[i]; }).name("dynamic");

        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();
        ASSERT_NE(observation.table, nullptr);

        std::vector<std::string> names;
        for (const auto& row : observation.table->rows())
            names.push_back(row.name);

        for (const char* expected : {"plain", "captured", "stateful", "host", "dynamic"})
            EXPECT_NE(std::find(names.begin(), names.end(), expected), names.end())
                << "no node reported as '" << expected << "'";
    }

    /** @brief Tests that an unnamed node reports the documented default label. */
    TEST(Medium_Observability, UnnamedNodeStillReportsTheDefaultLabel)
    {
        constexpr std::size_t N = 128;

        auto rt = Runtime::create().value();
        Testing::RunRecorder recorder(rt);

        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        auto g = rt.graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();

        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 1u);
        EXPECT_EQ(observation.table->rows()[0].name, "unnamed_task");
    }

    TEST(Medium_Observability, ProfilingOnPopulatesWorkerSplit)
    {
        constexpr std::size_t N = 1024;

        // The scheduler layer is what the observation names, so keep this plan off the chain.
        RuntimeConfig config;
        config.force_scheduler = true;
        auto rt = Runtime::create(config).value();
        Testing::RunRecorder recorder(rt);
        auto buf = rt.buffer<int>(N).value();
        int* p = buf.data();

        auto g = rt.graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
        auto rep = g.run(16).value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();

        EXPECT_EQ(rep.layer, ExecutionLayer::Scheduler);
        EXPECT_EQ(observation.layer, ExecutionLayer::Scheduler);
        EXPECT_EQ(observation.events.size(), 16u);
    }

    TEST(Medium_Observability, ExplicitTransfersAreAccounted)
    {
        constexpr std::size_t N = 512;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();

        // The context counts the runtime-issued copies directly.
        std::vector<int> src(N, 7);
        buf.write_range({0, N}, src).value();
        EXPECT_EQ(rt.advanced().host_to_device_bytes(), N * sizeof(int));

        auto out = buf.read_range({0, N}).value();
        ASSERT_EQ(out.size(), N);
        EXPECT_EQ(rt.advanced().device_to_host_bytes(), N * sizeof(int));
    }

    TEST(Medium_Observability, TransferBytesAttributedToTheRunWindow)
    {
        constexpr std::size_t N = 128;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        std::vector<int> src(N, 3);

        // The bytes a host node pushes during the run land in the runtime's counters; see README.md.
        const std::size_t h2d_before = rt.advanced().host_to_device_bytes();
        const std::size_t d2h_before = rt.advanced().device_to_host_bytes();
        auto g = rt.graph();
        g.add_host([&buf, &src]() { buf.write_range({0, N}, src).value(); });
        g.run().value();

        EXPECT_EQ(rt.advanced().host_to_device_bytes() - h2d_before, N * sizeof(int));
        EXPECT_EQ(rt.advanced().device_to_host_bytes() - d2h_before, 0u);
    }
} // namespace ObservabilityTests
