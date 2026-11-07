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

#include "run_recorder.hpp"
#include "support/host_runtime.hpp"
#include "support/runtime_environment.hpp"
#include "support/tolerance.hpp"

using namespace SushiRuntime;

/** @brief Tests add_host(), which runs plain C++ on a worker thread ordered by the data it touches. */
namespace NativeCpuPathTests
{
    /** @brief Fixture of the native-task tests, which run on the program's runtime. */
    class Medium_NativeCPU : public TestSupport::RuntimeTest
    {
    };

    /** @brief Fixture of the native-task tests that need no device, which run on a host runtime. */
    class Small_NativeCPU : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Tests that a host task ordered after a device kernel sees the kernel's output. */
    TEST_F(Medium_NativeCPU, HostTaskRunsAfterKernel)
    {
        constexpr std::size_t N = 64;

        auto a  = runtime().buffer<int>(N).value();
        auto b  = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

        int* ap = a.data();
        int* bp = b.data();

        auto g = runtime().graph();
        g.add(N, Reads{}, Writes(a),
              [ap](std::size_t i) { ap[i] = static_cast<int>(i) * 2; });
        g.add_host(Reads(a), Writes(b),
                   [ap, bp]() { for (std::size_t i = 0; i < N; ++i) bp[i] = ap[i] + 1; });

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(b[i], static_cast<int>(i) * 2 + 1) << "at i=" << i;
    }

    /** @brief Tests that a device kernel ordered after a host task sees the host's writes. */
    TEST_F(Small_NativeCPU, KernelRunsAfterHostTask)
    {
        constexpr std::size_t N = 64;

        auto a  = runtime().buffer<int>(N).value();
        auto b  = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

        int* ap = a.data();
        int* bp = b.data();

        auto g = runtime().graph();
        g.add_host(Reads{}, Writes(a),
                   [ap]() { for (std::size_t i = 0; i < N; ++i) ap[i] = static_cast<int>(i) + 5; });
        g.add(N, Reads(a), Writes(b),
              [ap, bp](std::size_t i) { bp[i] = ap[i] * 10; });

        g.run().value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(b[i], (static_cast<int>(i) + 5) * 10) << "at i=" << i;
    }

    /** @brief Tests that an untracked host task runs once per step. */
    TEST_F(Small_NativeCPU, UntrackedHostTaskRunsEveryStep)
    {
        constexpr std::size_t STEPS = 5;

        int ticks = 0;

        auto g = runtime().graph();
        g.add_host([&ticks]() { ++ticks; });

        g.run(STEPS).value();

        EXPECT_EQ(ticks, static_cast<int>(STEPS));
    }

    /** @brief Tests that a throwing host task fails the run. */
    TEST_F(Small_NativeCPU, ThrowingHostTaskFailsTheRun)
    {
        auto bad = runtime().graph();
        bad.add_host([]() { throw std::runtime_error("boom"); });

        ASSERT_FALSE(bad.run().has_value()) << "a throwing native task must fail the run.";
    }

    /** @brief Tests that a graph runs on the same runtime after another graph's host task threw. */
    TEST_F(Small_NativeCPU, TheRuntimeRunsAGraphAfterAHostTaskThrew)
    {
        auto bad = runtime().graph();
        bad.add_host([]() { throw std::runtime_error("boom"); });
        ASSERT_FALSE(bad.run().has_value());

        constexpr std::size_t N = 16;
        auto buf = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
        int* p = buf.data();

        auto good = runtime().graph();
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
    /** @brief Fixture of the node-event tests, which observe runs of the program's runtime. */
    class Medium_NodeEventRecords : public TestSupport::RuntimeTest
    {
    };

    /** @brief Fixture of the node-event tests that need no device, which observe runs of a host runtime. */
    class Small_NodeEventRecords : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Tests that a run of a runtime with no observer succeeds and no record reaches an observer. */
    TEST_F(Small_NodeEventRecords, ARunWithNoObserverRecordsNothing)
    {
        constexpr std::size_t N = 128;

        auto buf = runtime().buffer<int>(N).value();
        int* p = buf.data();

        Testing::RunRecorder recorder(runtime());
        recorder.stop();

        auto g = runtime().graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });

        auto report = g.run(4).value();
        EXPECT_TRUE(report.is_successful);
        EXPECT_EQ(recorder.count(), 0u);
    }

    /**
     * @brief Tests that three kernel nodes over run(5) give fifteen records with submit <= start <=
     *        end.
     */
    TEST_F(Small_NodeEventRecords, RequestedGraphRecordsOnePerDispatch)
    {
        constexpr std::size_t N = 64;

        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();
        auto c = runtime().buffer<int>(N).value();
        int* pa = a.data();
        int* pb = b.data();
        int* pc = c.data();

        Testing::RunRecorder recorder(runtime());
        auto g = runtime().graph();
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
    TEST_F(Small_NodeEventRecords, NodeIndexIsRegistrationOrder)
    {
        constexpr std::size_t N = 32;

        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();
        auto c = runtime().buffer<int>(N).value();
        int* pa = a.data();
        int* pb = b.data();
        int* pc = c.data();

        // The strict dependency chain forces the execution order A, B, C; see README.md.
        Testing::RunRecorder recorder(runtime());
        auto g = runtime().graph();
        g.add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] = static_cast<int>(i); }).name("A");
        g.add(N, Reads(a), Writes(b), [pa, pb](std::size_t i) { pb[i] = pa[i] + 1; }).name("B");
        g.add(N, Reads(b), Writes(c), [pb, pc](std::size_t i) { pc[i] = pb[i] + 1; }).name("C");

        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();
        ASSERT_NE(observation.table, nullptr);

        const auto& rows = observation.table->rows();
        ASSERT_EQ(rows.size(), 3u);
        EXPECT_EQ(rows[0].name, "A");
        EXPECT_EQ(rows[0].index, 0u);
        EXPECT_EQ(rows[1].name, "B");
        EXPECT_EQ(rows[1].index, 1u);
        EXPECT_EQ(rows[2].name, "C");
        EXPECT_EQ(rows[2].index, 2u);
    }

    /**
     * @brief Tests that a host node's record, on the steady clock, lands after the kernel it depends
     *        on once the kernel's record goes through its device's pairing.
     */
    TEST_F(Medium_NodeEventRecords, HostNodeLandsAfterItsKernelOnTheSteadyClock)
    {
        constexpr std::size_t N = 64;

        auto buf = runtime().buffer<int>(N).value();
        int* p = buf.data();

        // The host node reads what the kernel writes, so it runs after the kernel; see README.md.
        Testing::RunRecorder recorder(runtime());
        auto g = runtime().graph();
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
        ASSERT_NE(observation.table, nullptr);

        std::uint32_t idx_kernel = 0;
        bool found_kernel = false;
        for (const auto& row : observation.table->rows())
            if (row.name == "kernel")
            {
                idx_kernel = row.index;
                found_kernel = true;
            }
        ASSERT_TRUE(found_kernel) << "no node named 'kernel' in the table";

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
    } // namespace

    /** @brief Fixture of the report-reuse tests, which run on a host runtime. */
    class Small_RunReportReuse : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Tests that the reuse overload reports what the value-returning one reports. */
    TEST_F(Small_RunReportReuse, ReuseReportsWhatTheValueFormReports)
    {
        auto buf = runtime().buffer<int>(N).value();
        auto g   = runtime().graph();
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

    /** @brief Tests that steady-state runs into one report leave one row per run, not one more each. */
    TEST_F(Small_RunReportReuse, SteadyStateRunsLeaveOneRowPerRun)
    {
        auto buf = runtime().buffer<int>(N).value();
        auto g   = runtime().graph();
        record_write(g, buf);

        RunReport report;
        g.run(report).value();
        ASSERT_FALSE(report.numa_statistics.empty());

        for (int tick = 0; tick < 8; ++tick)
            g.run(report).value();

        // Refilled, not appended: eight more runs must not leave eight more rows.
        EXPECT_EQ(report.numa_statistics.size(), 1u);
    }

    /** @brief Tests that a short run after a long one reports the short run's count. */
    TEST_F(Small_RunReportReuse, EachRunOverwritesTheLastOne)
    {
        auto buf = runtime().buffer<int>(N).value();
        auto g   = runtime().graph();
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
    TEST_F(Small_RunReportReuse, ARejectedRunLeavesNoStaleNumbers)
    {
        auto buf = runtime().buffer<int>(N).value();
        auto g   = runtime().graph();
        record_write(g, buf);

        RunReport report;
        g.run(report).value();
        ASSERT_TRUE(report.is_successful);
        ASSERT_GT(report.total_tasks_executed, 0u);

        // An empty graph cannot run; the same report goes in again.
        auto empty = runtime().graph();
        auto rejected = empty.run(report);

        EXPECT_FALSE(rejected);
        EXPECT_EQ(rejected.error().code, ErrorCode::InvalidGraph);
        EXPECT_FALSE(report.is_successful);
        EXPECT_EQ(report.total_tasks_executed, 0u);
        EXPECT_TRUE(report.numa_statistics.empty());
    }

    /** @brief Tests that run(RunReport&) returns the report it was given. */
    TEST_F(Small_RunReportReuse, TryRunReturnsTheCallersReport)
    {
        auto buf = runtime().buffer<int>(N).value();
        auto g   = runtime().graph();
        record_write(g, buf);

        RunReport report;
        auto result = g.run(report);

        ASSERT_TRUE(result);
        EXPECT_EQ(*result, &report);
        EXPECT_TRUE(report.is_successful);
    }

    /** @brief Tests that RunHandle::wait(RunReport&) fills the caller's report. */
    TEST_F(Small_RunReportReuse, AsyncWaitFillsTheCallersReport)
    {
        auto buf = runtime().buffer<int>(N).value();
        auto g   = runtime().graph();
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
    TEST_F(Small_RunReportReuse, DynamicGraphRunsIntoACallersReport)
    {
        auto a = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
            a[i] = 0;
        int* pa = a.data();

        auto world = runtime().dynamic_graph();
        world.region(0).add(N, Reads{}, Writes(a),
                            [pa](std::size_t i) { pa[i] += 1; });

        RunReport report;
        world.run(report).value();
        ASSERT_FALSE(report.numa_statistics.empty());

        world.run(std::size_t{3}, report).value();

        EXPECT_TRUE(report.is_successful);
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

    /** @brief Fixture of the cancellation tests, which run on the program's runtime. */
    class Medium_Cancellation : public TestSupport::RuntimeTest
    {
    };

    /**
     * @brief A second thread cancels an unbounded run_until and it returns quickly.
     */
    TEST_F(Medium_Cancellation, CancelStopsRunUntilFromAnotherThread)
    {
        constexpr std::size_t N = 1024;
        auto field = runtime().state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) field[i] = 0;

        auto g = runtime().graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        // The second predicate call follows the first whole step, so the report holds one.
        RunReport report;
        ASSERT_NO_FATAL_FAILURE(cancel_unbounded_run(g, 2, report));

        EXPECT_TRUE(report.cancelled);
        EXPECT_GT(report.total_tasks_executed, 0u) << "some steps should have run before cancel";
    }

    /** @brief Fixture of the cancellation tests that need no second thread, which run on a host runtime. */
    class Small_Cancellation : public TestSupport::HostRuntimeTest
    {
    };

    /**
     * @brief After a cancelled run, the next run starts clean and completes fully.
     */
    TEST_F(Small_Cancellation, NextRunIsNotCancelledAfterReset)
    {
        constexpr std::size_t N = 256;
        auto field = runtime().state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) field[i] = 0;

        auto g = runtime().graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        // The stop predicate cancels from the run's own thread once the first step has finished.
        const RunReport cancelled_report = g.run_until([&g]
        {
            g.cancel();
            return false;
        }).value();
        EXPECT_TRUE(cancelled_report.cancelled);

        // A fresh bounded run must not inherit the previous cancellation.
        auto report = g.run(3).value();
        EXPECT_FALSE(report.cancelled);
        EXPECT_EQ(report.total_tasks_executed, 3u);
    }
} // namespace CancellationTests

/** @brief Tests that a chainable plan runs as an in-order chain, bit-identical to the host model. */
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
        };

        /**
         * @brief Builds the workload on @p rt, runs it @p runs times for @p steps each, and copies the
         *        results out.
         *
         * Host values change every step: the sized() count and the when() gate
         * both come from counters their providers advance once per step. @p native_tail adds
         * a no-op host task, which keeps the plan off the chain.
         */
        Outputs run_workload(const Runtime& rt, std::size_t runs, std::size_t steps, bool native_tail)
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

            for (std::size_t r = 0; r < runs; ++r)
            {
                const auto report = g.run(steps).value();
                EXPECT_TRUE(report.is_successful);
            }

            Outputs out;
            out.field.assign(field.current(), field.current() + N);
            out.acc.assign(pa, pa + N);
            out.mix.assign(pm, pm + N);
            out.gated.assign(pg, pg + N);
            return out;
        }

        /**
         * @brief Computes on the host what run_workload() leaves after @p total_steps steps.
         *
         * The providers advance once per step, so step t reads the sized() count 1 + (t * 37) % N and
         * the gate (t % 3) != 0.
         */
        Outputs workload_model(std::size_t total_steps)
        {
            Outputs model;
            model.field.resize(N);
            model.acc.assign(N, 0u);
            model.mix.assign(N, 0u);
            model.gated.assign(N, 0u);
            for (std::size_t i = 0; i < N; ++i)
                model.field[i] = static_cast<float>(i) * 0.5f;

            for (std::size_t t = 0; t < total_steps; ++t)
            {
                for (std::size_t i = 0; i < N; ++i)
                    model.field[i] = model.field[i] * 0.75f + static_cast<float>(i % 7) * 0.125f;

                const std::size_t live = 1 + (t * 37) % N;
                for (std::size_t i = 0; i < live; ++i)
                    model.acc[i] = model.acc[i] * 31u + static_cast<std::uint32_t>(i) + 1u;
                for (std::size_t i = 0; i < N; ++i)
                    model.mix[i] = (model.mix[i] ^ (model.acc[i] * 2654435761u)) + 7u;
                if ((t % 3) != 0)
                    for (std::size_t i = 0; i < N; ++i)
                        model.gated[i] = model.gated[i] * 17u + model.mix[i];
            }
            return model;
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
         * @brief Asserts a workload result holds the integer bytes of the model and its field within the rounding bound.
         * @param steps Steps the workload ran, which bounds the rounded operations of the field.
         */
        void expect_matches_model(const Outputs& measured, const Outputs& model, std::size_t steps)
        {
            EXPECT_TRUE(same_bytes(measured.acc, model.acc)) << "sized() buffer differs";
            EXPECT_TRUE(same_bytes(measured.mix, model.mix)) << "dependent buffer differs";
            EXPECT_TRUE(same_bytes(measured.gated, model.gated)) << "when() buffer differs";
            ASSERT_EQ(measured.field.size(), model.field.size());
            for (std::size_t i = 0; i < N; ++i)
                ASSERT_TRUE(TestSupport::is_within(TestSupport::measure_relative_error<float>(
                    measured.field[i], model.field[i], 3 * steps)))
                    << "State field differs at i=" << i;
        }

        /** @brief The workload of one row: the layer it runs on and whether a host task is appended. */
        struct WorkloadRow
        {
            bool force_scheduler; /**< Whether the runtime is forced onto the scheduler. */
            bool native_tail;     /**< Whether a no-op host task keeps the plan off the chain. */
        };
    } // namespace
} // namespace ChainLayerTests

/** @brief Fixture of the rows that run the workload on a layer, with or without a host task at the end. */
class Medium_ChainLayerWorkload : public ::testing::TestWithParam<ChainLayerTests::WorkloadRow>,
                                  protected TestSupport::LayerRuntime
{
    protected:
        /** @brief Chooses the runtime of the row. */
        void SetUp() override { use_layer(GetParam().force_scheduler); }
};

/** @brief Fixture of the chain tests that need no device, which run on a host runtime. */
class Small_ChainLayer : public TestSupport::HostRuntimeTest
{
};

namespace ChainLayerTests
{
    /** @brief Tests that the workload leaves the bytes of its host model, on either layer and with a host task among the nodes. */
    TEST_P(Medium_ChainLayerWorkload, WorkloadMatchesTheHostModel)
    {
        constexpr std::size_t RUNS  = 2;
        constexpr std::size_t STEPS = 40;

        const Outputs measured = run_workload(runtime(), RUNS, STEPS, GetParam().native_tail);

        expect_matches_model(measured, workload_model(RUNS * STEPS), RUNS * STEPS);
    }

    /**
     * @brief Tests that a node that names an operation, with no cluster attached, gives the right values; see README.md.
     */
    TEST_F(Small_ChainLayer, ANamedNodeGivesTheRightValuesWithNoClusterAttached)
    {
        auto values = runtime().buffer<std::uint32_t>(N).value();
        auto field = runtime().state<std::uint32_t>(N).value();
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

        auto graph = runtime().graph();
        graph.add(N, Reads{}, Writes(values), [words](std::size_t i) { words[i] += 1u; }, named);
        graph.add_rule(field,
                       [](std::size_t i, const std::uint32_t* current, std::uint32_t* next)
                       { next[i] = current[i] + 2u; },
                       named);

        constexpr std::size_t STEPS = 3;
        const auto report = graph.run(STEPS);
        ASSERT_TRUE(report.has_value()) << report.error().message;
        for (std::size_t i = 0; i < N; ++i)
        {
            ASSERT_EQ(values[i], STEPS) << "at i=" << i;
            ASSERT_EQ(field[i], 2u * STEPS) << "at i=" << i;
        }
    }

    /** @brief Tests that run_until stops on each step's device result, after exactly the steps the target needs. */
    TEST_F(Small_ChainLayer, RunUntilSeesEveryStepsResult)
    {
        constexpr std::uint32_t TARGET = 9;

        auto counter = runtime().buffer<std::uint32_t>(1).value();
        counter[0] = 0;
        std::uint32_t* pc = counter.data();

        auto g = runtime().graph();
        g.add(1, Reads{}, Writes(counter), [pc](std::size_t) { pc[0] += 1u; });

        // The stop test reads a device result, so the run must wait after every step.
        const auto report = g.run_until([&counter] { return counter[0] >= TARGET; }).value();

        EXPECT_EQ(report.total_tasks_executed, static_cast<std::size_t>(TARGET));
        EXPECT_EQ(counter[0], TARGET);
    }

    /** @brief Tests that a profiled run records each dispatch once per step and per node. */
    TEST_F(Small_ChainLayer, ProfiledRunRecordsEveryDispatch)
    {
        constexpr std::size_t STEPS = 12;

        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();
        for (std::size_t i = 0; i < N; ++i)
        {
            a[i] = 0;
            b[i] = 0;
        }
        int* pa = a.data();
        int* pb = b.data();

        Testing::RunRecorder recorder(runtime());
        auto g = runtime().graph();
        g.add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
        g.add(N, Reads(a), Writes(b), [pa, pb](std::size_t i) { pb[i] += pa[i]; });

        g.run(STEPS).value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();

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

INSTANTIATE_TEST_SUITE_P(Rows, Medium_ChainLayerWorkload,
                         ::testing::Values(ChainLayerTests::WorkloadRow{false, false},
                                           ChainLayerTests::WorkloadRow{true, false},
                                           ChainLayerTests::WorkloadRow{false, true}));

/** @brief Tests that the reusable completion latch drives a replayed run and re-runs on one plan. */
namespace CompletionLatchTests
{
    /** @brief Fixture of the latch tests, which run on the program's runtime. */
    class Medium_CompletionLatch : public TestSupport::RuntimeTest
    {
    };

    /** @brief Fixture of the latch tests that need no worker thread, which run on a host runtime. */
    class Small_CompletionLatch : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Tests that a replay of many steps leaves every cell at the step count. */
    TEST_F(Small_CompletionLatch, ManyStepReplayReachesCorrectState)
    {
        constexpr std::size_t N = 16;
        constexpr std::size_t STEPS = 5000;

        auto field = runtime().state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) field[i] = 0;

        auto g = runtime().graph();
        // Each step adds 1 to every cell: after STEPS steps every cell equals STEPS.
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        auto rep = g.run(STEPS).value();

        EXPECT_TRUE(rep.is_successful);
        EXPECT_FALSE(rep.cancelled);
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(field[i], static_cast<int>(STEPS)) << "at i=" << i;
    }

    /** @brief Tests that three runs on one plan add up their steps. */
    TEST_F(Small_CompletionLatch, ReRunsCleanlyOnTheSamePlan)
    {
        constexpr std::size_t N = 256;

        auto field = runtime().state<int>(N).value();
        for (std::size_t i = 0; i < N; ++i) field[i] = 0;

        auto g = runtime().graph();
        g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

        // The latch resets between the three run() calls and the sentinel still signals; see README.md.
        g.run(10).value();
        g.run(10).value();
        g.run(5).value();

        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(field[i], 25) << "at i=" << i;
    }

    /** @brief Tests that each of 200 single-step runs on one plan completes with the right values. */
    TEST_F(Medium_CompletionLatch, EachOfManySingleStepRunsCompletesWithTheRightValues)
    {
        constexpr std::size_t N = 64;

        auto buf = runtime().buffer<int>(N).value();
        int* p = buf.data();

        auto g = runtime().graph();
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
    /** @brief Fixture of the cross-context tests, which skip on a machine with fewer than two devices. */
    class Medium_CrossContextGuard : public TestSupport::RuntimeTest
    {
    protected:
        /** @brief Skips the test below two device contexts. */
        void SetUp() override
        {
            if (runtime().advanced().device_count() < 2)
                GTEST_SKIP() << "needs at least two device contexts to straddle a boundary";
        }
    };

    /** @brief Tests that a node that reads one device context and writes another fails the run with InvalidGraph. */
    TEST_F(Medium_CrossContextGuard, NodeSpanningTwoContextsFailsCleanly)
    {
        constexpr std::size_t N = 256;

        // Two buffers pinned to different device queues (and thus contexts).
        auto a = runtime().buffer<int>(N, DeviceIndex{0}).value();
        auto b = runtime().buffer<int>(N, DeviceIndex{1}).value();
        int* pb = b.data();
        const int* pa = a.data();

        // No one queue can run a node that reads context 0 and writes context 1; see README.md.
        auto g = runtime().graph();
        g.add(N, Reads(a), Writes(b),
              [pa, pb](std::size_t i) { pb[i] = pa[i] + 1; });

        auto result = g.run();
        ASSERT_FALSE(result) << "a cross-context node must not run successfully";
        EXPECT_EQ(result.error().code, ErrorCode::InvalidGraph);
    }

    /** @brief Tests that a co-located graph runs on the same runtime after the guarded failure. */
    TEST_F(Medium_CrossContextGuard, ACoLocatedGraphRunsAfterTheGuardedFailure)
    {
        constexpr std::size_t N = 256;

        auto a = runtime().buffer<int>(N, DeviceIndex{0}).value();
        auto b = runtime().buffer<int>(N, DeviceIndex{1}).value();
        int* pb = b.data();
        const int* pa = a.data();

        auto g = runtime().graph();
        g.add(N, Reads(a), Writes(b),
              [pa, pb](std::size_t i) { pb[i] = pa[i] + 1; });
        ASSERT_FALSE(g.run());

        // A well-formed, co-located graph still runs on the same runtime afterward.
        for (std::size_t i = 0; i < N; ++i) a[i] = static_cast<int>(i);
        int* pa2 = a.data();
        auto g2 = runtime().graph();
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
         * @brief Runs the fixed workload on a runtime and copies the results out.
         * @param rt The runtime the workload runs on.
         */
        Outcome run_workload(const Runtime& rt)
        {
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

        /**
         * @brief Runs the fixed workload on the program's runtime of the given worker count and copies the
         *        results out.
         * @param rebalancer Whether the background rebalancer runs during the workload.
         */
        Outcome run_workload(std::size_t worker_count, bool rebalancer = false)
        {
            TestSupport::RuntimeShape shape;
            shape.worker_count = worker_count;
            shape.rebalancer   = rebalancer;

            return run_workload(TestSupport::RuntimeEnvironment::runtime(shape));
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
    } // namespace

    /** @brief Fixture of the determinism tests that run on the program's runtime. */
    class Medium_Determinism : public TestSupport::RuntimeTest
    {
    };

    /** @brief Fixture of the determinism tests that need no worker pool, which run on a host runtime. */
    class Small_Determinism : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Tests that the compiler does not reassociate host arithmetic. */
    TEST_F(Small_Determinism, HostArithmeticIsNotReassociated)
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
    TEST_F(Medium_Determinism, OutputIsByteEqualAcrossWorkerCounts)
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

    /** @brief Tests that the same configuration run again lands on the same bytes. */
    TEST_F(Small_Determinism, RepeatedConfigurationsAgreeByteForByte)
    {
        const Outcome first  = run_workload(runtime());
        const Outcome second = run_workload(runtime());

        ASSERT_EQ(first.workers, second.workers);
        EXPECT_TRUE(bytes_equal(first.output, second.output))
            << "two identical runs diverged at element "
            << first_difference(first.output, second.output);
    }

    /** @brief Tests that a run with the background rebalancer on lands on the same bytes. */
    TEST_F(Medium_Determinism, TheRebalancerDoesNotChangeTheBytes)
    {
        const Outcome quiet  = run_workload(0, false);
        const Outcome moving = run_workload(0, true);

        ASSERT_EQ(quiet.workers, moving.workers);
        EXPECT_TRUE(bytes_equal(quiet.output, moving.output))
            << "the rebalancer changed element " << first_difference(quiet.output, moving.output);
    }

    /** @brief Tests that the result of a replayed rule after many steps equals its result after one. */
    TEST_F(Small_Determinism, ReplayDoesNotDriftAcrossSteps)
    {
        runtime().set_rebalancer_enabled(false);

        auto in    = runtime().buffer<float>(N).value();
        auto total = runtime().buffer<float>(1).value();
        for (std::size_t i = 0; i < N; ++i)
            in[i] = order_sensitive_value(i);
        total[0] = 0.0f;

        auto g = runtime().graph();
        record_tiled_total(g.recorder(), in, total);

        g.run().value();
        const float after_one_step = total[0];

        total[0] = 0.0f;
        g.run(K_STEPS).value();

        EXPECT_EQ(std::memcmp(&after_one_step, &total[0], sizeof(float)), 0)
            << "a replayed rule drifted: one step gave " << after_one_step
            << ", " << K_STEPS << " steps gave " << total[0];
    }

    /** @brief Tests that a replay does not recompile. */
    TEST_F(Small_Determinism, AReplayDoesNotRecompile)
    {
        runtime().set_rebalancer_enabled(false);

        auto in    = runtime().buffer<float>(N).value();
        auto total = runtime().buffer<float>(1).value();
        for (std::size_t i = 0; i < N; ++i)
            in[i] = order_sensitive_value(i);
        total[0] = 0.0f;

        auto g = runtime().graph();
        record_tiled_total(g.recorder(), in, total);

        g.run().value();
        g.run(K_STEPS).value();

        EXPECT_EQ(g.compile_count(), 1u) << "a replay must not recompile";
    }
} // namespace DeterminismTests

/** @brief Tests the per-graph profiling switch and the records a node without declared access leaves. */
namespace GraphProfilingSwitchTests
{
    /** @brief Fixture of the profiling-switch tests, which observe runs of a host runtime. */
    class Small_GraphProfilingSwitch : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Tests that an observer added to a runtime turns timing on for every graph of it. */
    TEST_F(Small_GraphProfilingSwitch, TwoGraphsBothRecordWhileAnObserverIsAdded)
    {
        constexpr std::size_t N = 256;

        auto a = runtime().buffer<int>(N).value();
        auto b = runtime().buffer<int>(N).value();
        int* pa = a.data();
        int* pb = b.data();

        auto first = runtime().graph();
        first.add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] = static_cast<int>(i); });

        auto second = runtime().graph();
        second.add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] = static_cast<int>(i); });

        // The two graphs are built with no observer; adding one turns timing on for both.
        first.run().value();
        second.run().value();

        Testing::RunRecorder recorder(runtime());
        first.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        EXPECT_FALSE(recorder.last().events.empty()) << "the first graph must collect device timing";

        second.run().value();
        ASSERT_EQ(recorder.count(), 2u);
        EXPECT_FALSE(recorder.last().events.empty()) << "the second graph must collect device timing too";
    }

    /** @brief Tests that a plan built before the observer gives full records after the add and none after the removal. */
    TEST_F(Small_GraphProfilingSwitch, ObserverAddedAfterTheBuildGivesRecordsAndRemovalStopsThem)
    {
        constexpr std::size_t N = 128;

        auto buf = runtime().buffer<int>(N).value();
        int* p = buf.data();

        auto g = runtime().graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });

        g.run().value();

        // A plan built before the observer gives full records on its first run after the add.
        Testing::RunRecorder recorder(runtime());
        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        EXPECT_FALSE(recorder.last().events.empty());

        g.run().value();
        ASSERT_EQ(recorder.count(), 2u);
        EXPECT_FALSE(recorder.last().events.empty());

        // Removing the last observer records nothing.
        recorder.stop();
        g.run().value();
        EXPECT_EQ(recorder.count(), 2u);
    }

    /** @brief Tests that a node without declared Reads or Writes still leaves a record of its dispatch. */
    TEST_F(Small_GraphProfilingSwitch, ANodeWithoutDeclaredAccessIsRecorded)
    {
        constexpr std::size_t N = 256;

        auto buf = runtime().buffer<int>(N).value();
        int* p = buf.data();

        Testing::RunRecorder recorder(runtime());
        auto g = runtime().graph();
        g.add(N, Reads{}, Writes{}, [p](std::size_t i) { p[i] = static_cast<int>(i); });

        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        EXPECT_FALSE(recorder.last().events.empty())
            << "a node without declared access must still leave a record";
    }
} // namespace GraphProfilingSwitchTests

/** @brief Tests the node table, the node names and the transfer accounting around a run. */
namespace ObservabilityTests
{
    /** @brief Fixture of the observability tests, which observe runs of a host runtime. */
    class Small_Observability : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief The add() shapes whose node can be named, as the rows of one test. */
    enum class Shape
    {
        Plain,    /**< Tracked per-element add(). */
        Captured, /**< Variadic capture shape. */
        Stateful, /**< State rule. */
        Host,     /**< Host task. */
        Dynamic   /**< Dynamic per-element add. */
    };

    /** @brief Fixture of the rows that name one add() shape each. */
    class Small_ObservabilityShapes : public TestSupport::HostRuntimeTest,
                                       public ::testing::WithParamInterface<Shape>
    {
    };

    TEST_F(Small_Observability, ProfilingOnCapturesPerNodeTiming)
    {
        constexpr std::size_t N = 4096;
        constexpr std::size_t STEPS = 8;

        Testing::RunRecorder recorder(runtime());

        auto a = runtime().buffer<float>(N).value();
        auto b = runtime().buffer<float>(N).value();
        float* pa = a.data();
        float* pb = b.data();

        auto g = runtime().graph();
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
    TEST_F(Small_Observability, NamingTheLastNodeReachesTheNodeTable)
    {
        constexpr std::size_t N = 512;
        constexpr std::size_t STEPS = 4;

        Testing::RunRecorder recorder(runtime());

        auto buf = runtime().buffer<int>(N).value();
        int* p = buf.data();

        auto g = runtime().graph();
        g.add(N, Reads{}, Writes(buf),
              [p](std::size_t i) { p[i] = static_cast<int>(i); }).name("seed");

        g.run(STEPS).value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();

        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 1u);
        EXPECT_EQ(observation.table->rows()[0].name, "seed");
    }

    /** @brief Tests that every add() shape can be named. */
    TEST_P(Small_ObservabilityShapes, AShapeCanBeNamed)
    {
        constexpr std::size_t N = 256;

        Testing::RunRecorder recorder(runtime());

        auto a = runtime().buffer<float>(N).value();
        auto b = runtime().buffer<float>(N).value();
        auto c = runtime().buffer<float>(N).value();
        auto s = runtime().state<float>(N).value();
        float* pa = a.data();
        float* pb = b.data();

        auto g = runtime().graph();

        switch (GetParam())
        {
            case Shape::Plain:
                g.add(N, Reads(a), Writes(b),
                      [pa, pb](std::size_t i) { pb[i] = pa[i] + 1.0f; }).name("named");
                break;
            case Shape::Captured:
                // Variadic capture shape: the pack's last element is the callable.
                g.add(Extent<1>{N}, In(b), Out(c),
                      [](sycl::id<1> i, const float* in, float* out) { out[i] = in[i] * 2.0f; })
                 .name("named");
                break;
            case Shape::Stateful:
                g.add_rule(s, [](std::size_t i, const float* cur, float* next) { next[i] = cur[i] + 1.0f; })
                 .name("named");
                break;
            case Shape::Host:
                g.add_host(Reads(c), Writes{}, [] {}).name("named");
                break;
            case Shape::Dynamic:
                g.add_dynamic(sized([] { return N; }), N, Reads(a), Writes(b),
                              [pa, pb](std::size_t i) { pb[i] = pa[i]; }).name("named");
                break;
        }

        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();
        ASSERT_NE(observation.table, nullptr);

        std::vector<std::string> names;
        for (const auto& row : observation.table->rows())
            names.push_back(row.name);

        EXPECT_NE(std::find(names.begin(), names.end(), "named"), names.end())
            << "no node reported as 'named'";
    }

    INSTANTIATE_TEST_SUITE_P(Rows, Small_ObservabilityShapes,
                             ::testing::Values(Shape::Plain, Shape::Captured, Shape::Stateful,
                                               Shape::Host, Shape::Dynamic));

    /** @brief Tests that an unnamed node reports the documented default label. */
    TEST_F(Small_Observability, UnnamedNodeStillReportsTheDefaultLabel)
    {
        constexpr std::size_t N = 128;

        Testing::RunRecorder recorder(runtime());

        auto buf = runtime().buffer<int>(N).value();
        int* p = buf.data();

        auto g = runtime().graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i); });
        g.run().value();
        ASSERT_EQ(recorder.count(), 1u);
        const RunObservation observation = recorder.last();

        ASSERT_NE(observation.table, nullptr);
        ASSERT_EQ(observation.table->size(), 1u);
        EXPECT_EQ(observation.table->rows()[0].name, "unnamed_task");
    }

    /** @brief Tests that a profiled run of 16 steps has one event per dispatch. */
    TEST_F(Small_Observability, AProfiledRunHasOneEventPerStep)
    {
        constexpr std::size_t N = 1024;

        Testing::RunRecorder recorder(runtime());
        auto buf = runtime().buffer<int>(N).value();
        int* p = buf.data();

        auto g = runtime().graph();
        g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] = static_cast<int>(i) + 1; });
        g.run(16).value();
        ASSERT_EQ(recorder.count(), 1u);

        EXPECT_EQ(recorder.last().events.size(), 16u);
    }

    /** @brief Tests that an explicit host-to-device copy is counted in the host-to-device bytes. */
    TEST_F(Small_Observability, AHostToDeviceCopyIsAccounted)
    {
        constexpr std::size_t N = 512;

        auto buf = runtime().buffer<int>(N).value();
        const std::size_t before = runtime().advanced().host_to_device_bytes();

        std::vector<int> src(N, 7);
        buf.write_range({0, N}, src).value();

        EXPECT_EQ(runtime().advanced().host_to_device_bytes() - before, N * sizeof(int));
    }

    /** @brief Tests that an explicit device-to-host copy is counted in the device-to-host bytes. */
    TEST_F(Small_Observability, ADeviceToHostCopyIsAccounted)
    {
        constexpr std::size_t N = 512;

        auto buf = runtime().buffer<int>(N).value();
        const std::size_t before = runtime().advanced().device_to_host_bytes();

        auto out = buf.read_range({0, N}).value();
        ASSERT_EQ(out.size(), N);

        EXPECT_EQ(runtime().advanced().device_to_host_bytes() - before, N * sizeof(int));
    }

    TEST_F(Small_Observability, TransferBytesAttributedToTheRunWindow)
    {
        constexpr std::size_t N = 128;

        auto buf = runtime().buffer<int>(N).value();
        std::vector<int> src(N, 3);

        // The bytes a host node pushes during the run land in the runtime's counters; see README.md.
        const std::size_t h2d_before = runtime().advanced().host_to_device_bytes();
        const std::size_t d2h_before = runtime().advanced().device_to_host_bytes();
        auto g = runtime().graph();
        g.add_host([&buf, &src]() { buf.write_range({0, N}, src).value(); });
        g.run().value();

        EXPECT_EQ(runtime().advanced().host_to_device_bytes() - h2d_before, N * sizeof(int));
        EXPECT_EQ(runtime().advanced().device_to_host_bytes() - d2h_before, 0u);
    }
} // namespace ObservabilityTests

/** @brief Tests that a replayed chain and a replayed fan-out of roots run every node of every step. */
namespace GraphReplayTests
{
    namespace
    {
        constexpr std::size_t K_CHAIN_LENGTH = 16;
        constexpr std::size_t K_ROOTS        = 16;
        constexpr std::size_t K_REPLAYS      = 5;

        /**
         * @brief Replays a chain of dependent kernels; node i writes buffer i + 1 as buffer i plus one.
         * @return The value of the last buffer after one compiling run and K_REPLAYS replays.
         */
        int settle_chain(const Runtime& rt)
        {
            std::vector<Buffer<int>> bufs;
            bufs.reserve(K_CHAIN_LENGTH + 1);
            for (std::size_t i = 0; i <= K_CHAIN_LENGTH; ++i)
                bufs.push_back(rt.buffer<int>(1).value());
            bufs[0][0] = 0;

            auto g = rt.graph();
            for (std::size_t i = 0; i < K_CHAIN_LENGTH; ++i)
            {
                const int* in = bufs[i].data();
                int* out      = bufs[i + 1].data();
                g.add(std::size_t{1}, Reads(bufs[i]), Writes(bufs[i + 1]),
                      [in, out](std::size_t) { out[0] = in[0] + 1; });
            }

            g.run(std::size_t{1}).value();
            g.run(K_REPLAYS).value();
            return bufs[K_CHAIN_LENGTH][0];
        }

        /**
         * @brief Replays a graph of independent roots and counts those that missed a step.
         *
         * Each root adds one to its own buffer per step, so after one compiling run and
         * K_REPLAYS replays every buffer holds one more than K_REPLAYS.
         *
         * @return The number of roots whose buffer does not hold that count.
         */
        std::size_t count_roots_that_missed_a_step(const Runtime& rt)
        {
            std::vector<Buffer<int>> bufs;
            bufs.reserve(K_ROOTS);
            for (std::size_t r = 0; r < K_ROOTS; ++r)
                bufs.push_back(rt.buffer<int>(1).value());
            for (auto& b : bufs)
                b[0] = 0;

            auto g = rt.graph();
            for (std::size_t r = 0; r < K_ROOTS; ++r)
            {
                int* p = bufs[r].data();
                g.add(1, Reads{}, Writes(bufs[r]), [p](std::size_t) { p[0] += 1; });
            }

            g.run(std::size_t{1}).value();
            g.run(K_REPLAYS).value();

            std::size_t missed = 0;
            for (auto& b : bufs)
                if (b[0] != static_cast<int>(1 + K_REPLAYS))
                    ++missed;
            return missed;
        }
    } // namespace

    /** @brief Fixture of the replay tests, which run on a host runtime. */
    class Small_GraphReplay : public TestSupport::HostRuntimeTest
    {
    };

    /** @brief Tests that a replayed chain settles to its length. */
    TEST_F(Small_GraphReplay, AChainSettlesToItsLength)
    {
        EXPECT_EQ(settle_chain(runtime()), static_cast<int>(K_CHAIN_LENGTH));
    }

    /** @brief Tests that every independent root runs on every step of a replay. */
    TEST_F(Small_GraphReplay, EveryIndependentRootRunsOnEveryStep)
    {
        EXPECT_EQ(count_roots_that_missed_a_step(runtime()), 0u);
    }
} // namespace GraphReplayTests
