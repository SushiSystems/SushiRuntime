/**************************************************************************/
/* test_cancellation.cpp                                                  */
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
 * @file test_cancellation.cpp
 * @brief Tests that cancel() from another thread stops run_until and leaves the next run alone.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <mutex>
#include <gtest/gtest.h>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

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
TEST(Integration_Cancellation, CancelStopsRunUntilFromAnotherThread)
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
TEST(Integration_Cancellation, NextRunIsNotCancelledAfterReset)
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
