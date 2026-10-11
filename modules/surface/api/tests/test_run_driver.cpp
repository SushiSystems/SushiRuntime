/**************************************************************************/
/* test_run_driver.cpp                                                    */
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
 * @file test_run_driver.cpp
 * @brief Tests the run entries StepDriver gives Graph and DynamicGraph, and their errors.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>

#include "refusing_fill.hpp"

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t N = 4;
    constexpr auto K_GATE_TIMEOUT = std::chrono::milliseconds(2000);
    constexpr const char* K_TASK_FAILURE = "boom";
    constexpr const char* K_BUILD_FAILURE = "no plan";

    /**
     * @brief Lists the run entries a graph type must expose, with their types and noexcept.
     * @tparam G Graph or DynamicGraph; instantiating the list checks it.
     */
    template <class G>
    struct RunEntries
    {
        using Report = Internal::Observability::RunReport;
        using Owned = Result<Report, Error>;
        using Lent = Result<Report*, Error>;
        using Budget = std::chrono::milliseconds;
        using Stop = bool (*)();

        /** @brief Names a graph in an unevaluated operand. */
        static G& g() noexcept;

        /** @brief Names a caller-owned report in an unevaluated operand. */
        static Report& r() noexcept;

        static_assert(std::is_same_v<decltype(g().run()), Owned>, "run()");
        static_assert(std::is_same_v<decltype(g().run(N)), Owned>, "run(steps)");
        static_assert(std::is_same_v<decltype(g().run(r())), Lent>, "run(reuse)");
        static_assert(std::is_same_v<decltype(g().run(N, r())), Lent>,
                      "run(steps, reuse)");
        static_assert(std::is_same_v<decltype(g().run_for(Budget{})), Owned>, "run_for");
        static_assert(std::is_same_v<decltype(g().run_for(Budget{}, r())), Lent>,
                      "run_for(budget, reuse)");
        static_assert(std::is_same_v<decltype(g().run_until(Stop{})), Owned>,
                      "run_until");
        static_assert(std::is_same_v<decltype(g().run_until(Stop{}, r())), Lent>,
                      "run_until(done, reuse)");

        static_assert(noexcept(g().run()) && noexcept(g().run(N)) &&
                      noexcept(g().run(r())) && noexcept(g().run(N, r())) &&
                      noexcept(g().run_for(Budget{})) &&
                      noexcept(g().run_for(Budget{}, r())) &&
                      noexcept(g().run_until(Stop{})) &&
                      noexcept(g().run_until(Stop{}, r())),
                      "every run entry is noexcept");

        static_assert(std::is_same_v<decltype(g().run_async()),
                                     Result<RunHandle<G>, Error>>,
                      "run_async");
        static_assert(noexcept(g().run_async()), "run_async is noexcept");
        static_assert(std::is_void_v<decltype(g().cancel())> && noexcept(g().cancel()),
                      "cancel is noexcept");
        static_assert(std::is_same_v<decltype(g().set_profiling(true)), G&> &&
                      noexcept(g().set_profiling(true)),
                      "set_profiling returns the graph");
        static_assert(std::is_same_v<decltype(std::declval<const G&>().is_profiling_requested()),
                                     bool>,
                      "is_profiling_requested");
    };

    template struct RunEntries<Graph>;
    template struct RunEntries<DynamicGraph>;

    /** @brief Blocks until the flag is set or the deadline passes and returns which happened. */
    bool wait_for_gate(const std::atomic<bool>& released)
    {
        const auto deadline = std::chrono::steady_clock::now() + K_GATE_TIMEOUT;
        while (!released.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        return released.load(std::memory_order_acquire);
    }

    /** @brief Throws what a failing host task throws: a plain std::runtime_error. */
    void fail_the_task()
    {
        throw std::runtime_error(K_TASK_FAILURE);
    }

    /** @brief Expects KernelFailed from every way of running a graph whose task throws. */
    template <class G>
    void expect_kernel_failed_everywhere(G& graph)
    {
        const auto run = graph.run();
        ASSERT_FALSE(run.has_value());
        EXPECT_EQ(run.error().code, ErrorCode::KernelFailed);
        EXPECT_NE(run.error().message.find(K_TASK_FAILURE), std::string::npos);

        Internal::Observability::RunReport report;
        const auto reused = graph.run(report);
        ASSERT_FALSE(reused.has_value());
        EXPECT_EQ(reused.error().code, ErrorCode::KernelFailed);
        EXPECT_FALSE(report.is_successful);

        auto step = graph.run_async();
        ASSERT_TRUE(step.has_value()) << step.error().message;
        const auto waited = step->wait();
        ASSERT_FALSE(waited.has_value());
        EXPECT_EQ(waited.error().code, ErrorCode::KernelFailed);
        EXPECT_NE(waited.error().message.find(K_TASK_FAILURE), std::string::npos);
    }

    /** @brief Calls each run entry once and expects success; twelve steps run in all. */
    template <class G>
    void expect_every_run_entry_runs(G& graph)
    {
        Internal::Observability::RunReport report;
        int calls = 0;
        const auto after_two_steps = [&calls]() { return ++calls > 2; };

        EXPECT_TRUE(graph.run().has_value());
        EXPECT_TRUE(graph.run(std::size_t{2}).has_value());
        EXPECT_TRUE(graph.run(report).has_value());
        EXPECT_TRUE(report.is_successful);
        EXPECT_TRUE(graph.run(std::size_t{2}, report).has_value());
        EXPECT_TRUE(graph.run_for(std::chrono::milliseconds(0)).has_value());
        EXPECT_TRUE(graph.run_for(std::chrono::milliseconds(0), report).has_value());
        EXPECT_TRUE(graph.run_until(after_two_steps).has_value());
        calls = 0;
        EXPECT_TRUE(graph.run_until(after_two_steps, report).has_value());
        EXPECT_TRUE(report.is_successful);
    }

    /** @brief Offloads nothing and reports a count the test moves. */
    class CountingCoordinator final : public Internal::Graph::OffloadCoordinator
    {
        public:
            /** @brief Adds to the count statistics() reports as offloaded. */
            void accept(std::uint64_t dispatches) noexcept
            {
                offloaded_.fetch_add(dispatches, std::memory_order_relaxed);
            }

            void bind(Internal::Graph::OffloadSink&) override
            {

            }

            void prepare(span<const sushi_ptr<Internal::Graph::Node>>) override
            {

            }

            [[nodiscard]] bool try_offload(Internal::Graph::Node&) override
            {
                return false;
            }

            void on_local_completion(Internal::Graph::Node&) noexcept override
            {

            }

            void share(void*, std::size_t) override
            {

            }

            [[nodiscard]] Internal::Graph::OffloadStatistics statistics() const noexcept override
            {
                Internal::Graph::OffloadStatistics now;
                now.offloaded = offloaded_.load(std::memory_order_relaxed);
                now.alive_workers = 1;
                return now;
            }

            void stop() noexcept override
            {

            }

        private:
            /// @brief Dispatches the test said a worker accepted.
            std::atomic<std::uint64_t> offloaded_{0};
    };

    constexpr std::size_t K_COUNT_CAPACITY = 8;
    constexpr std::uint32_t K_COUNT_OVER_CAPACITY = 9;
    constexpr int K_FIELD_START = 5;

    /**
     * @brief Holds a graph whose step increments a State and sizes a launch from a device count.
     *
     * A count above K_COUNT_CAPACITY makes every step record a device fault.
     */
    struct CountedStep
    {
        /** @brief Records the step on @p rt; a kernel writes @p count before the sized launch. */
        CountedStep(Runtime& rt, std::uint32_t count)
            : field(rt.state<int>(N).value()),
              counter(rt.buffer<std::uint32_t>(1, DeviceIndex{0}, Residency::Host).value()),
              marks(rt.buffer<std::uint32_t>(K_COUNT_CAPACITY).value()),
              graph(rt.graph())
        {
            for (std::size_t i = 0; i < N; ++i)
                field.current()[i] = K_FIELD_START;
            std::uint32_t* c = counter.data();
            std::uint32_t* m = marks.data();

            graph.add_rule(field,
                           [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });
            graph.add(std::size_t{1}, Reads{}, Writes(counter),
                      [c, count](std::size_t) { c[0] = count; });
            graph.add_dynamic(sized_from_device(counter), K_COUNT_CAPACITY, Reads{},
                              Writes(marks), [m](std::size_t i) { m[i] = 1u; });
        }

        State<int> field;              /**< Incremented once per step. */
        Buffer<std::uint32_t> counter; /**< The device count of the sized launch. */
        Buffer<std::uint32_t> marks;   /**< Written by the sized launch. */
        Graph graph;                   /**< The recorded step. */
    };

    /** @brief Returns whether a counted step with a count inside its capacity takes the chain. */
    bool counted_step_runs_chained(Runtime& rt)
    {
        CountedStep clean(rt, 1u);
        const auto report = clean.graph.run();
        return report.has_value() &&
               report->layer == Internal::Observability::ExecutionLayer::Chain;
    }

    /**
     * @brief Expects run_for(budget) to run until its host task cancels it at the third step.
     * @param budget A budget no steady_clock time point can hold once added to now().
     */
    template <class Budget>
    void expect_run_for_lasts_until_cancelled(Budget budget)
    {
        constexpr int STEPS_BEFORE_CANCEL = 3;
        constexpr int STEP_LIMIT = 1000;

        auto rt  = Runtime::create().value();
        auto buf = rt.buffer<int>(N).value();
        std::atomic<int> steps{0};

        auto g = rt.graph();
        Graph* const self = &g;
        g.add_host(Reads{}, Writes(buf), [&steps, self]()
        {
            const int step = steps.fetch_add(1, std::memory_order_relaxed) + 1;
            if (step == STEPS_BEFORE_CANCEL)
                self->cancel();
            if (step > STEP_LIMIT)
                throw std::runtime_error("cancel() did not stop the run");
        });

        const auto report = g.run_for(budget);

        ASSERT_TRUE(report.has_value()) << report.error().message;
        EXPECT_TRUE(report->cancelled);
        EXPECT_EQ(steps.load(), STEPS_BEFORE_CANCEL)
            << "a deadline that overflowed ends the run after its first step";
    }
} // namespace

/** @brief Tests that a Graph reports a throwing host task as KernelFailed from every entry. */
TEST(Integration_RunDriver, GraphReportsAThrowingHostTaskAsKernelFailed)
{
    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), fail_the_task);

    expect_kernel_failed_everywhere(g);
}

/** @brief Tests that a DynamicGraph reports a throwing host task the way a Graph does. */
TEST(Integration_RunDriver, DynamicGraphReportsAThrowingHostTaskAsKernelFailed)
{
    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();

    auto world = rt.dynamic_graph();
    world.region(0).add_host(Reads{}, Writes(buf), fail_the_task);

    expect_kernel_failed_everywhere(world);
}

/** @brief Tests that an exception thrown while the plan is built is CompileFailed. */
TEST(Integration_RunDriver, AThrowingBuildIsCompileFailed)
{
    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] = 1; });
    g.add_host(Reads{}, Writes(buf),
               Testing::RefusingFill(std::make_shared<bool>(true), K_BUILD_FAILURE, p, N, 2));

    const auto run = g.run();
    ASSERT_FALSE(run.has_value());
    EXPECT_EQ(run.error().code, ErrorCode::CompileFailed);
    EXPECT_NE(run.error().message.find(K_BUILD_FAILURE), std::string::npos);

    const auto step = g.run_async();
    ASSERT_FALSE(step.has_value());
    EXPECT_EQ(step.error().code, ErrorCode::CompileFailed);
}

/** @brief Tests that both graph types run through every run entry. */
TEST(Integration_RunDriver, EveryTryEntryRunsOnBothGraphs)
{
    constexpr int STEPS_OF_ALL_ENTRIES = 12;

    auto rt = Runtime::create().value();
    auto static_count  = rt.buffer<int>(N).value();
    auto dynamic_count = rt.buffer<int>(N).value();
    static_count[0]  = 0;
    dynamic_count[0] = 0;
    int* ps = static_count.data();
    int* pd = dynamic_count.data();

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(static_count), [ps]() { ps[0] += 1; });
    expect_every_run_entry_runs(g);
    EXPECT_EQ(static_count[0], STEPS_OF_ALL_ENTRIES);

    auto world = rt.dynamic_graph();
    world.region(0).add_host(Reads{}, Writes(dynamic_count), [pd]() { pd[0] += 1; });
    expect_every_run_entry_runs(world);
    EXPECT_EQ(dynamic_count[0], STEPS_OF_ALL_ENTRIES);
}

/** @brief Tests that a run refused for the graph's state reports InvalidState. */
TEST(Integration_RunDriver, ARunWhileAStepIsPendingIsInvalidState)
{
    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();
    std::atomic<bool> released{false};

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [&released, p]()
    {
        wait_for_gate(released);
        p[0] = 7;
    });

    auto step = g.run_async().value();

    const auto refused = g.run();
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, ErrorCode::InvalidState);

    const auto second = g.run_async();
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, ErrorCode::InvalidState);

    released.store(true, std::memory_order_release);
    EXPECT_TRUE(step.wait().has_value());
    EXPECT_EQ(buf[0], 7);

    const auto twice = step.wait();
    ASSERT_FALSE(twice.has_value());
    EXPECT_EQ(twice.error().code, ErrorCode::InvalidState);
}

/** @brief Tests that set_profiling() chains and is read back on both graph types. */
TEST(Integration_RunDriver, SetProfilingChainsAndReadsBack)
{
    auto rt = Runtime::create().value();

    auto g = rt.graph();
    EXPECT_FALSE(g.is_profiling_requested());
    EXPECT_TRUE(g.set_profiling(true).is_profiling_requested());
    EXPECT_FALSE(g.set_profiling(false).is_profiling_requested());

    auto world = rt.dynamic_graph();
    EXPECT_FALSE(world.is_profiling_requested());
    EXPECT_TRUE(world.set_profiling(true).is_profiling_requested());
    EXPECT_FALSE(world.set_profiling(false).is_profiling_requested());
}

/** @brief Tests that set_profiling() with the value already stored rebuilds no plan. */
TEST(Integration_RunDriver, SettingTheSameProfilingValueRebuildsNothing)
{
    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    g.set_profiling(true).run().value();
    EXPECT_EQ(g.compile_count(), 1u);
    g.set_profiling(true).run().value();
    EXPECT_EQ(g.compile_count(), 1u) << "the same request compiled the Graph again";
    g.set_profiling(false).run().value();
    EXPECT_EQ(g.compile_count(), 2u) << "a changed request must rebuild the plan";

    auto world = rt.dynamic_graph();
    world.region(0).add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    world.set_profiling(true).run().value();
    EXPECT_EQ(world.compose_count(), 1u);
    world.set_profiling(true).run().value();
    EXPECT_EQ(world.compose_count(), 1u) << "the same request composed the DynamicGraph again";
    world.set_profiling(false).run().value();
    EXPECT_EQ(world.compose_count(), 2u) << "a changed request must compose the plan again";
}

/** @brief Tests that a profiling toggle on a DynamicGraph composes once and times its nodes. */
TEST(Integration_RunDriver, AProfilingToggleOnADynamicGraphComposesOnceAndTimesItsNodes)
{
    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] += 1; });

    EXPECT_TRUE(world.run().value().node_timings.empty())
        << "a graph that requested nothing was timed";
    EXPECT_EQ(world.compose_count(), 1u);

    world.set_profiling(true);
    EXPECT_FALSE(world.run().value().node_timings.empty()) << "the request did not reach the plan";
    EXPECT_EQ(world.compose_count(), 2u);

    EXPECT_FALSE(world.run().value().node_timings.empty());
    EXPECT_EQ(world.compose_count(), 2u) << "a run with the request unchanged composed again";

    world.set_profiling(false);
    EXPECT_TRUE(world.run().value().node_timings.empty())
        << "the withdrawal did not reach the plan";
    EXPECT_EQ(world.compose_count(), 3u);
}

/** @brief Tests that moving a graph with a pending step ends the step and frees both sides. */
TEST(Integration_RunDriver, MovingAGraphWithAPendingStepEndsTheStep)
{
#ifndef NDEBUG
    GTEST_SKIP() << "a debug build asserts on this move; the release result is tested";
#else
    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    buf[0] = 0;
    int* p = buf.data();
    std::atomic<bool> released{false};

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [&released, p]()
    {
        wait_for_gate(released);
        p[0] += 7;
    });

    auto step = g.run_async().value();
    released.store(true, std::memory_order_release);

    Graph moved = std::move(g);
    EXPECT_EQ(buf[0], 7) << "the move waits for the pending step";
    EXPECT_FALSE(step.pending()) << "the move ended the step the handle was given";
    EXPECT_TRUE(step.ready());

    const auto late = step.wait();
    ASSERT_FALSE(late.has_value());
    EXPECT_EQ(late.error().code, ErrorCode::InvalidState);

    EXPECT_TRUE(moved.run().has_value()) << "the destination must run";
    EXPECT_EQ(buf[0], 14);
#endif
}

/** @brief Tests that a report counts the dispatches offloaded during its own run. */
TEST(Integration_RunDriver, OffloadedCountIsOfThisRun)
{
    constexpr std::uint64_t BEFORE_ANY_RUN = 5;
    constexpr std::uint64_t PER_STEP = 3;

    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();

    const auto coordinator = std::make_shared<CountingCoordinator>();
    rt.unsafe_context().install_offload(coordinator);
    coordinator->accept(BEFORE_ANY_RUN);

    CountingCoordinator* counter = coordinator.get();
    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [counter]() { counter->accept(PER_STEP); });

    const Internal::Observability::RunReport first = g.run().value();
    EXPECT_EQ(first.distributed_statistics.tasks_offloaded_to_network, PER_STEP);
    EXPECT_EQ(first.distributed_statistics.remote_nodes_involved, 1u);

    const Internal::Observability::RunReport second = g.run(std::size_t{2}).value();
    EXPECT_EQ(second.distributed_statistics.tasks_offloaded_to_network, 2 * PER_STEP);

    const Internal::Observability::RunReport third = g.run_async().value().wait().value();
    EXPECT_EQ(third.distributed_statistics.tasks_offloaded_to_network, PER_STEP);
}

/** @brief Tests that a failed chained run of one step advances no State. */
TEST(Integration_RunDriver, AFailedChainedRunOfOneStepAdvancesNoState)
{
    auto rt = Runtime::create().value();
    if (!counted_step_runs_chained(rt))
        GTEST_SKIP() << "the counted step does not take the chain on this runtime";

    CountedStep step(rt, K_COUNT_OVER_CAPACITY);
    const int* const before = step.field.current();

    const auto failed = step.graph.run();

    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(step.field.current(), before) << "the failed step advanced the State";
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(step.field.current()[i], K_FIELD_START) << "at i=" << i;
}

/** @brief Tests that a failed chained run of three steps advances the two before the last. */
TEST(Integration_RunDriver, AFailedChainedRunOfSeveralStepsAdvancesAllButTheLast)
{
    constexpr std::size_t STEPS = 3;

    auto rt = Runtime::create().value();
    if (!counted_step_runs_chained(rt))
        GTEST_SKIP() << "the counted step does not take the chain on this runtime";

    CountedStep step(rt, K_COUNT_OVER_CAPACITY);

    const auto failed = step.graph.run(STEPS);

    ASSERT_FALSE(failed.has_value());
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(step.field.current()[i], K_FIELD_START + static_cast<int>(STEPS) - 1)
            << "at i=" << i;
}

/** @brief Tests that a budget the clock cannot hold runs until cancelled, not for one step. */
TEST(Integration_RunDriver, ABudgetPastTheClocksRangeRunsUntilCancelled)
{
    expect_run_for_lasts_until_cancelled(std::chrono::hours::max());
    expect_run_for_lasts_until_cancelled(std::chrono::nanoseconds::max());
}

/** @brief Tests that step hooks feed eight replays of one Graph plan, in the order added. */
TEST(Integration_RunDriver, AStepHookFeedsEveryReplayOfOnePlan)
{
    constexpr std::size_t K_STEPS = 8;
    constexpr std::uint32_t K_BASE = 100;

    auto rt   = Runtime::create().value();
    auto word = rt.buffer<std::uint32_t>(2).value();
    auto seen = rt.buffer<std::uint32_t>(K_STEPS).value();
    for (std::size_t i = 0; i < K_STEPS; ++i)
        seen[i] = 0;
    std::uint32_t* w = word.data();
    std::uint32_t* s = seen.data();

    auto g = rt.graph();
    g.add(K_STEPS, Reads(word), Writes(seen), [w, s](std::size_t i)
    {
        if (i == w[0])
            s[i] = w[1];
    });

    std::vector<std::uint32_t> observed;
    g.before_each_step([w](std::size_t finished_steps)
     {
         w[0] = static_cast<std::uint32_t>(finished_steps);
         w[1] = K_BASE + static_cast<std::uint32_t>(finished_steps);
     })
     .before_each_step([w, &observed](std::size_t) { observed.push_back(w[1]); });

    g.run(K_STEPS).value();

    EXPECT_EQ(g.compile_count(), 1u);
    ASSERT_EQ(observed.size(), K_STEPS);
    for (std::size_t i = 0; i < K_STEPS; ++i)
    {
        EXPECT_EQ(seen[i], K_BASE + static_cast<std::uint32_t>(i)) << "replay " << i;
        EXPECT_EQ(observed[i], K_BASE + static_cast<std::uint32_t>(i)) << "replay " << i;
    }

    g.run().value();

    EXPECT_EQ(g.compile_count(), 1u) << "a hook must not rebuild the plan";
    EXPECT_EQ(observed.back(), K_BASE) << "a new run counts its steps from zero";
}

/** @brief Tests that a step hook feeds eight replays of one DynamicGraph plan. */
TEST(Integration_RunDriver, AStepHookFeedsEveryReplayOfADynamicGraph)
{
    constexpr std::size_t K_STEPS = 8;
    constexpr std::uint32_t K_BASE = 100;

    auto rt   = Runtime::create().value();
    auto word = rt.buffer<std::uint32_t>(2).value();
    auto seen = rt.buffer<std::uint32_t>(K_STEPS).value();
    for (std::size_t i = 0; i < K_STEPS; ++i)
        seen[i] = 0;
    std::uint32_t* w = word.data();
    std::uint32_t* s = seen.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(K_STEPS, Reads(word), Writes(seen), [w, s](std::size_t i)
    {
        if (i == w[0])
            s[i] = w[1];
    });
    world.before_each_step([w](std::size_t finished_steps)
    {
        w[0] = static_cast<std::uint32_t>(finished_steps);
        w[1] = K_BASE + static_cast<std::uint32_t>(finished_steps);
    });

    world.run(K_STEPS).value();

    EXPECT_EQ(world.compose_count(), 1u);
    for (std::size_t i = 0; i < K_STEPS; ++i)
        EXPECT_EQ(seen[i], K_BASE + static_cast<std::uint32_t>(i)) << "replay " << i;
}

/** @brief Tests that a moved graph keeps its step hooks and its source has none. */
TEST(Integration_RunDriver, AStepHookMovesWithItsGraph)
{
    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    std::size_t calls = 0;
    auto source = rt.graph();
    source.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    source.before_each_step([&calls](std::size_t) { ++calls; });

    auto moved = std::move(source);
    moved.run().value();
    EXPECT_EQ(calls, 1u);

    source.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    source.run().value();
    EXPECT_EQ(calls, 1u) << "the source of a move must have no hook";
}

/** @brief Tests that a hook too large for the inline storage is held and called. */
TEST(Integration_RunDriver, AStepHookLargerThanTheInlineStorageRuns)
{
    auto rt  = Runtime::create().value();
    auto buf = rt.buffer<int>(N).value();
    int* p = buf.data();

    std::array<std::uint64_t, 16> payload{};
    payload[15] = 7;
    std::uint64_t seen = 0;

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    g.before_each_step([payload, &seen](std::size_t) { seen = payload[15]; });
    g.run().value();

    EXPECT_EQ(seen, 7u);
}

#if defined(SYCL_IMPLEMENTATION_ONEAPI)
/**
 * @brief Tests that an error SYCL reports only through its asynchronous handler fails the run on
 *        both layers; see README.md.
 */
TEST(Integration_RunDriver, AnErrorOnlyTheAsynchronousHandlerSeesFailsTheRunOnBothLayers)
{
    for (const bool force_scheduler : {false, true})
    {
        RuntimeConfig config;
        config.force_scheduler = force_scheduler;
        auto rt  = Runtime::create(config).value();
        auto buf = rt.buffer<int>(N).value();

        auto g = rt.graph();
        g.add_task(Reads{}, Writes(buf),
                   [](sycl::queue& queue, const std::vector<sycl::event>& events)
        {
            // A host_task that throws reaches the caller through the asynchronous handler only.
            return queue.submit([&events](sycl::handler& handler)
            {
                handler.depends_on(events);
                handler.host_task([]() { throw std::runtime_error(K_TASK_FAILURE); });
            });
        });

        const auto run = g.run();
        EXPECT_FALSE(run.has_value())
            << "force_scheduler = " << force_scheduler << ": the run reported success";
        if (!run.has_value())
            EXPECT_EQ(run.error().code, ErrorCode::KernelFailed)
                << "force_scheduler = " << force_scheduler;

        // An error the run did not return is still in the queue and comes out of this wait.
        const auto drained = rt.wait();
        EXPECT_TRUE(drained.has_value())
            << "force_scheduler = " << force_scheduler
            << ": the error outlived the run that caused it";
    }
}
#endif
