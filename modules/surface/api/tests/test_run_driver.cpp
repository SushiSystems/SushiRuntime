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

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>

using namespace SushiRuntime;

namespace
{
    constexpr std::size_t N = 4;
    constexpr auto GATE_TIMEOUT = std::chrono::milliseconds(2000);
    constexpr const char* K_TASK_FAILURE = "boom";
    constexpr const char* K_BUILD_FAILURE = "no plan";

    /**
     * @brief Lists the run entries a graph type must expose, with their types and noexcept.
     * @tparam G API::Graph or API::DynamicGraph; instantiating the list checks it.
     */
    template <class G>
    struct RunEntries
    {
        using Report = Observability::RunReport;
        using Owned = Core::Result<Report, Core::Error>;
        using Lent = Core::Result<Report*, Core::Error>;
        using Budget = std::chrono::milliseconds;
        using Stop = bool (*)();

        /** @brief Names a graph in an unevaluated operand. */
        static G& g() noexcept;

        /** @brief Names a caller-owned report in an unevaluated operand. */
        static Report& r() noexcept;

        static_assert(std::is_same_v<decltype(g().run()), Report>, "run()");
        static_assert(std::is_same_v<decltype(g().run(N)), Report>, "run(steps)");
        static_assert(std::is_same_v<decltype(g().run(r())), Report&>, "run(reuse)");
        static_assert(std::is_same_v<decltype(g().run(N, r())), Report&>, "run(steps, reuse)");
        static_assert(std::is_same_v<decltype(g().run_for(Budget{})), Report>, "run_for");
        static_assert(std::is_same_v<decltype(g().run_for(Budget{}, r())), Report&>,
                      "run_for(budget, reuse)");
        static_assert(std::is_same_v<decltype(g().run_until(Stop{})), Report>, "run_until");
        static_assert(std::is_same_v<decltype(g().run_until(Stop{}, r())), Report&>,
                      "run_until(done, reuse)");

        static_assert(std::is_same_v<decltype(g().try_run()), Owned>, "try_run()");
        static_assert(std::is_same_v<decltype(g().try_run(N)), Owned>, "try_run(steps)");
        static_assert(std::is_same_v<decltype(g().try_run(r())), Lent>, "try_run(reuse)");
        static_assert(std::is_same_v<decltype(g().try_run(N, r())), Lent>,
                      "try_run(steps, reuse)");
        static_assert(std::is_same_v<decltype(g().try_run_for(Budget{})), Owned>, "try_run_for");
        static_assert(std::is_same_v<decltype(g().try_run_for(Budget{}, r())), Lent>,
                      "try_run_for(budget, reuse)");
        static_assert(std::is_same_v<decltype(g().try_run_until(Stop{})), Owned>,
                      "try_run_until");
        static_assert(std::is_same_v<decltype(g().try_run_until(Stop{}, r())), Lent>,
                      "try_run_until(done, reuse)");

        static_assert(noexcept(g().try_run()) && noexcept(g().try_run(N)) &&
                      noexcept(g().try_run(r())) && noexcept(g().try_run(N, r())) &&
                      noexcept(g().try_run_for(Budget{})) &&
                      noexcept(g().try_run_for(Budget{}, r())) &&
                      noexcept(g().try_run_until(Stop{})) &&
                      noexcept(g().try_run_until(Stop{}, r())),
                      "every try_ entry is noexcept");

        static_assert(std::is_same_v<decltype(g().run_async()), API::RunHandle<G>>, "run_async");
        static_assert(std::is_same_v<decltype(g().try_run_async()),
                                     Core::Result<API::RunHandle<G>, Core::Error>>,
                      "try_run_async");
        static_assert(noexcept(g().try_run_async()), "try_run_async is noexcept");
        static_assert(std::is_void_v<decltype(g().cancel())> && noexcept(g().cancel()),
                      "cancel is noexcept");
        static_assert(std::is_same_v<decltype(g().set_profiling(true)), G&> &&
                      noexcept(g().set_profiling(true)),
                      "set_profiling returns the graph");
        static_assert(std::is_same_v<decltype(std::declval<const G&>().is_profiling_requested()),
                                     bool>,
                      "is_profiling_requested");
    };

    template struct RunEntries<API::Graph>;
    template struct RunEntries<API::DynamicGraph>;

    /** @brief Blocks until the flag is set or the deadline passes and returns which happened. */
    bool wait_for_gate(const std::atomic<bool>& released)
    {
        const auto deadline = std::chrono::steady_clock::now() + GATE_TIMEOUT;
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

    /** @brief Returns the code of the SushiException call throws, or ok when it returns. */
    template <class Call>
    Core::Errc code_thrown_by(Call&& call)
    {
        try
        {
            call();
        }
        catch (const Core::SushiException& e)
        {
            return e.error().code;
        }
        return Core::Errc::ok;
    }

    /** @brief Expects kernel_failed from every way of running a graph whose task throws. */
    template <class G>
    void expect_kernel_failed_everywhere(G& graph)
    {
        const auto run = graph.try_run();
        ASSERT_FALSE(run.has_value());
        EXPECT_EQ(run.error().code, Core::Errc::kernel_failed);
        EXPECT_NE(run.error().message.find(K_TASK_FAILURE), std::string::npos);

        Observability::RunReport report;
        const auto reused = graph.try_run(report);
        ASSERT_FALSE(reused.has_value());
        EXPECT_EQ(reused.error().code, Core::Errc::kernel_failed);
        EXPECT_FALSE(report.is_successful);

        auto step = graph.try_run_async();
        ASSERT_TRUE(step.has_value()) << step.error().message;
        const auto waited = step->try_wait();
        ASSERT_FALSE(waited.has_value());
        EXPECT_EQ(waited.error().code, Core::Errc::kernel_failed);
        EXPECT_NE(waited.error().message.find(K_TASK_FAILURE), std::string::npos);

        EXPECT_EQ(code_thrown_by([&graph]() { graph.run(); }), Core::Errc::kernel_failed);
        EXPECT_EQ(code_thrown_by([&graph]() { graph.run_async().wait(); }),
                  Core::Errc::kernel_failed);
    }

    /** @brief Calls each try_ entry once and expects success; twelve steps run in all. */
    template <class G>
    void expect_every_try_entry_runs(G& graph)
    {
        Observability::RunReport report;
        int calls = 0;
        const auto after_two_steps = [&calls]() { return ++calls > 2; };

        EXPECT_TRUE(graph.try_run().has_value());
        EXPECT_TRUE(graph.try_run(std::size_t{2}).has_value());
        EXPECT_TRUE(graph.try_run(report).has_value());
        EXPECT_TRUE(report.is_successful);
        EXPECT_TRUE(graph.try_run(std::size_t{2}, report).has_value());
        EXPECT_TRUE(graph.try_run_for(std::chrono::milliseconds(0)).has_value());
        EXPECT_TRUE(graph.try_run_for(std::chrono::milliseconds(0), report).has_value());
        EXPECT_TRUE(graph.try_run_until(after_two_steps).has_value());
        calls = 0;
        EXPECT_TRUE(graph.try_run_until(after_two_steps, report).has_value());
        EXPECT_TRUE(report.is_successful);
    }

    /** @brief Offloads nothing and reports a count the test moves. */
    class CountingCoordinator final : public Graph::OffloadCoordinator
    {
        public:
            /** @brief Adds to the count statistics() reports as offloaded. */
            void accept(std::uint64_t dispatches) noexcept
            {
                offloaded_.fetch_add(dispatches, std::memory_order_relaxed);
            }

            void bind(Graph::OffloadSink&) override
            {

            }

            void prepare(span<const sushi_ptr<Graph::Node>>) override
            {

            }

            [[nodiscard]] bool try_offload(Graph::Node&) override
            {
                return false;
            }

            void on_local_completion(Graph::Node&) noexcept override
            {

            }

            void share(void*, std::size_t) override
            {

            }

            [[nodiscard]] Graph::OffloadStatistics statistics() const noexcept override
            {
                Graph::OffloadStatistics now;
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
} // namespace

/** @brief Tests that a Graph reports a throwing host task as kernel_failed from every entry. */
TEST(Integration_RunDriver, GraphReportsAThrowingHostTaskAsKernelFailed)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);

    auto g = rt.graph();
    g.add_host(API::Reads{}, API::Writes(buf), fail_the_task);

    expect_kernel_failed_everywhere(g);
}

/** @brief Tests that a DynamicGraph reports a throwing host task the way a Graph does. */
TEST(Integration_RunDriver, DynamicGraphReportsAThrowingHostTaskAsKernelFailed)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);

    auto world = rt.dynamic_graph();
    world.region(0).add_host(API::Reads{}, API::Writes(buf), fail_the_task);

    expect_kernel_failed_everywhere(world);
}

/** @brief Tests that an exception thrown while the plan is built is compile_failed. */
TEST(Integration_RunDriver, AThrowingBuildIsCompileFailed)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.add_host(API::Reads{}, API::Writes(buf), [p]() { p[0] = 1; });
    g.add_emitter([](Graph::TaskGraph&) { throw std::runtime_error(K_BUILD_FAILURE); });

    const auto run = g.try_run();
    ASSERT_FALSE(run.has_value());
    EXPECT_EQ(run.error().code, Core::Errc::compile_failed);
    EXPECT_NE(run.error().message.find(K_BUILD_FAILURE), std::string::npos);

    const auto step = g.try_run_async();
    ASSERT_FALSE(step.has_value());
    EXPECT_EQ(step.error().code, Core::Errc::compile_failed);
}

/** @brief Tests that both graph types run through every try_ entry. */
TEST(Integration_RunDriver, EveryTryEntryRunsOnBothGraphs)
{
    constexpr int STEPS_OF_ALL_ENTRIES = 12;

    auto rt = Runtime::create();
    auto static_count  = rt.buffer<int>(N);
    auto dynamic_count = rt.buffer<int>(N);
    static_count[0]  = 0;
    dynamic_count[0] = 0;
    int* ps = static_count.data();
    int* pd = dynamic_count.data();

    auto g = rt.graph();
    g.add_host(API::Reads{}, API::Writes(static_count), [ps]() { ps[0] += 1; });
    expect_every_try_entry_runs(g);
    EXPECT_EQ(static_count[0], STEPS_OF_ALL_ENTRIES);

    auto world = rt.dynamic_graph();
    world.region(0).add_host(API::Reads{}, API::Writes(dynamic_count), [pd]() { pd[0] += 1; });
    expect_every_try_entry_runs(world);
    EXPECT_EQ(dynamic_count[0], STEPS_OF_ALL_ENTRIES);
}

/** @brief Tests that a run refused for the graph's state reports invalid_state. */
TEST(Integration_RunDriver, ARunWhileAStepIsPendingIsInvalidState)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();
    std::atomic<bool> released{false};

    auto g = rt.graph();
    g.add_host(API::Reads{}, API::Writes(buf), [&released, p]()
    {
        wait_for_gate(released);
        p[0] = 7;
    });

    auto step = g.run_async();

    const auto refused = g.try_run();
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, Core::Errc::invalid_state);

    const auto second = g.try_run_async();
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, Core::Errc::invalid_state);

    released.store(true, std::memory_order_release);
    EXPECT_TRUE(step.try_wait().has_value());
    EXPECT_EQ(buf[0], 7);

    const auto twice = step.try_wait();
    ASSERT_FALSE(twice.has_value());
    EXPECT_EQ(twice.error().code, Core::Errc::invalid_state);
}

/** @brief Tests that set_profiling() chains and is read back on both graph types. */
TEST(Integration_RunDriver, SetProfilingChainsAndReadsBack)
{
    auto rt = Runtime::create();

    auto g = rt.graph();
    EXPECT_FALSE(g.is_profiling_requested());
    EXPECT_TRUE(g.set_profiling(true).is_profiling_requested());
    EXPECT_FALSE(g.set_profiling(false).is_profiling_requested());

    auto world = rt.dynamic_graph();
    EXPECT_FALSE(world.is_profiling_requested());
    EXPECT_TRUE(world.set_profiling(true).is_profiling_requested());
    EXPECT_FALSE(world.set_profiling(false).is_profiling_requested());
}

/** @brief Tests that moving a graph with a pending step ends the step and frees both sides. */
TEST(Integration_RunDriver, MovingAGraphWithAPendingStepEndsTheStep)
{
#ifndef NDEBUG
    GTEST_SKIP() << "a debug build asserts on this move; the release result is tested";
#else
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    buf[0] = 0;
    int* p = buf.data();
    std::atomic<bool> released{false};

    auto g = rt.graph();
    g.add_host(API::Reads{}, API::Writes(buf), [&released, p]()
    {
        wait_for_gate(released);
        p[0] += 7;
    });

    auto step = g.run_async();
    released.store(true, std::memory_order_release);

    API::Graph moved = std::move(g);
    EXPECT_EQ(buf[0], 7) << "the move waits for the pending step";

    const auto late = step.try_wait();
    ASSERT_FALSE(late.has_value());
    EXPECT_EQ(late.error().code, Core::Errc::invalid_state);

    EXPECT_TRUE(moved.try_run().has_value()) << "the destination must run";
    EXPECT_EQ(buf[0], 14);
#endif
}

/** @brief Tests that a report counts the dispatches offloaded during its own run. */
TEST(Integration_RunDriver, OffloadedCountIsOfThisRun)
{
    constexpr std::uint64_t BEFORE_ANY_RUN = 5;
    constexpr std::uint64_t PER_STEP = 3;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);

    const auto coordinator = std::make_shared<CountingCoordinator>();
    rt.unsafe_context().install_offload(coordinator);
    coordinator->accept(BEFORE_ANY_RUN);

    CountingCoordinator* counter = coordinator.get();
    auto g = rt.graph();
    g.add_host(API::Reads{}, API::Writes(buf), [counter]() { counter->accept(PER_STEP); });

    const Observability::RunReport first = g.run();
    EXPECT_EQ(first.distributed_statistics.tasks_offloaded_to_network, PER_STEP);
    EXPECT_EQ(first.distributed_statistics.remote_nodes_involved, 1u);

    const Observability::RunReport second = g.run(std::size_t{2});
    EXPECT_EQ(second.distributed_statistics.tasks_offloaded_to_network, 2 * PER_STEP);

    const Observability::RunReport third = g.run_async().wait();
    EXPECT_EQ(third.distributed_statistics.tasks_offloaded_to_network, PER_STEP);
}
