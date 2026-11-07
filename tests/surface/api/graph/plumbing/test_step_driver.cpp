/**************************************************************************/
/* test_step_driver.cpp                                                   */
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
 * @file test_step_driver.cpp
 * @brief Tests the step driver both graphs run through.
 * @see tests/surface/api/graph/plumbing/README.md
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

#include "refusing_fill.hpp"
#include "run_recorder.hpp"
#include "support/runtime_environment.hpp"

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
        using Report = RunReport;
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

        RunReport report;
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
        RunReport report;
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
        CountedStep(const Runtime& rt, std::uint32_t count)
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

    /**
     * @brief Expects run_for(budget) to run until its host task cancels it at the third step.
     * @param budget A budget no steady_clock time point can hold once added to now().
     */
    template <class Budget>
    void expect_run_for_lasts_until_cancelled(const Runtime& rt, Budget budget)
    {
        constexpr int STEPS_BEFORE_CANCEL = 3;
        constexpr int STEP_LIMIT = 1000;

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

    /** @brief The two graph types, as the rows of the tests that hold for both. */
    enum class GraphKind
    {
        Static,  /**< A Graph. */
        Dynamic  /**< A DynamicGraph. */
    };

    /** @brief Records a host rule on a Graph. */
    template <class... Args>
    void record_host(Graph& graph, Args&&... args)
    {
        graph.add_host(std::forward<Args>(args)...);
    }

    /** @brief Records a host rule in region 0 of a DynamicGraph. */
    template <class... Args>
    void record_host(DynamicGraph& world, Args&&... args)
    {
        world.region(0).add_host(std::forward<Args>(args)...);
    }

    /** @brief Records a counted kernel rule on a Graph. */
    template <class... Args>
    void record_add(Graph& graph, Args&&... args)
    {
        graph.add(std::forward<Args>(args)...);
    }

    /** @brief Records a counted kernel rule in region 0 of a DynamicGraph. */
    template <class... Args>
    void record_add(DynamicGraph& world, Args&&... args)
    {
        world.region(0).add(std::forward<Args>(args)...);
    }

    /** @brief Returns how many times a Graph built its plan. */
    std::size_t plan_builds(const Graph& graph)
    {
        return graph.compile_count();
    }

    /** @brief Returns how many times a DynamicGraph composed its plan. */
    std::size_t plan_builds(const DynamicGraph& world)
    {
        return world.compose_count();
    }

    /** @brief Calls body with a graph of the given type recorded on rt. */
    template <class Body>
    void on_graph_of(const Runtime& rt, GraphKind kind, Body&& body)
    {
        if (kind == GraphKind::Static)
        {
            auto graph = rt.graph();
            body(graph);
        }
        else
        {
            auto world = rt.dynamic_graph();
            body(world);
        }
    }

    /** @brief The two budgets the clock cannot hold, as the rows of the run_for test. */
    enum class Overflow
    {
        Hours,       /**< std::chrono::hours::max(). */
        Nanoseconds  /**< std::chrono::nanoseconds::max(). */
    };
} // namespace

/** @brief Fixture of the run-driver tests, which run on the program's runtime. */
class Medium_RunDriver : public TestSupport::RuntimeTest
{
};

/** @brief Fixture of the rows that hold for a Graph and for a DynamicGraph. */
class Medium_RunDriverGraphKinds : public TestSupport::RuntimeTest,
                                   public ::testing::WithParamInterface<GraphKind>
{
};

/** @brief Fixture of the rows that run the same promise on the in-order chain (false) and on the worker pool (true). */
class Medium_RunDriverLayers : public TestSupport::LayerRuntimeTest
{
};

/** @brief Fixture of the rows over the budgets the clock cannot hold. */
class Medium_RunDriverBudgets : public TestSupport::RuntimeTest,
                                public ::testing::WithParamInterface<Overflow>
{
};

/** @brief Tests that a throwing host task is reported as KernelFailed from every run entry. */
TEST_P(Medium_RunDriverGraphKinds, AThrowingHostTaskIsKernelFailedFromEveryEntry)
{
    auto buf = runtime().buffer<int>(N).value();

    on_graph_of(runtime(), GetParam(), [&buf](auto& graph)
    {
        record_host(graph, Reads{}, Writes(buf), fail_the_task);
        expect_kernel_failed_everywhere(graph);
    });
}

INSTANTIATE_TEST_SUITE_P(Rows, Medium_RunDriverGraphKinds,
                         ::testing::Values(GraphKind::Static, GraphKind::Dynamic));

/** @brief Tests that an exception thrown while the plan is built is CompileFailed. */
TEST_F(Medium_RunDriver, AThrowingBuildIsCompileFailed)
{
    auto buf = runtime().buffer<int>(N).value();
    int* p = buf.data();

    auto g = runtime().graph();
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

/** @brief Tests that both graph types run through every run entry, each step once. */
TEST_F(Medium_RunDriver, EveryTryEntryRunsOnBothGraphs)
{
    constexpr int STEPS_OF_ALL_ENTRIES = 12;

    auto static_count  = runtime().buffer<int>(N).value();
    auto dynamic_count = runtime().buffer<int>(N).value();
    static_count[0]  = 0;
    dynamic_count[0] = 0;
    int* ps = static_count.data();
    int* pd = dynamic_count.data();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(static_count), [ps]() { ps[0] += 1; });
    expect_every_run_entry_runs(g);
    EXPECT_EQ(static_count[0], STEPS_OF_ALL_ENTRIES);

    auto world = runtime().dynamic_graph();
    world.region(0).add_host(Reads{}, Writes(dynamic_count), [pd]() { pd[0] += 1; });
    expect_every_run_entry_runs(world);
    EXPECT_EQ(dynamic_count[0], STEPS_OF_ALL_ENTRIES);
}

/** @brief Tests that a synchronous and an asynchronous run are refused InvalidState while a step is pending. */
TEST_F(Medium_RunDriver, ARunWhileAStepIsPendingIsInvalidState)
{
    auto buf = runtime().buffer<int>(N).value();
    int* p = buf.data();
    std::atomic<bool> released{false};

    auto g = runtime().graph();
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
}

/** @brief Tests that add_run_observer() returns a handle that stands for the observer until it is reset. */
TEST_F(Medium_RunDriver, AddRunObserverReturnsAHandleThatRemovesIt)
{
    auto observer = std::make_shared<RecordingObserver>();

    RunObserverHandle handle = runtime().add_run_observer(observer);
    EXPECT_TRUE(static_cast<bool>(handle));

    auto buf = runtime().buffer<int>(N).value();
    int* p = buf.data();
    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    g.run().value();
    EXPECT_EQ(observer->count(), 1u);

    handle.reset();
    EXPECT_FALSE(static_cast<bool>(handle));
    g.run().value();
    EXPECT_EQ(observer->count(), 1u) << "a removed observer saw a run";
}

/** @brief Tests that adding and removing observers rebuilds no plan, and each observer sees every run. */
TEST_P(Medium_RunDriverGraphKinds, AnObserverRebuildsNothing)
{
    auto buf = runtime().buffer<int>(N).value();
    int* p = buf.data();

    on_graph_of(runtime(), GetParam(), [&](auto& graph)
    {
        record_host(graph, Reads{}, Writes(buf), [p]() { p[0] += 1; });
        graph.run().value();
        EXPECT_EQ(plan_builds(graph), 1u);

        auto first = std::make_shared<RecordingObserver>();
        auto second = std::make_shared<RecordingObserver>();
        RunObserverHandle first_handle = runtime().add_run_observer(first);
        graph.run().value();
        EXPECT_EQ(plan_builds(graph), 1u) << "the first observer rebuilt the plan";

        RunObserverHandle second_handle = runtime().add_run_observer(second);
        graph.run().value();
        EXPECT_EQ(plan_builds(graph), 1u) << "a second observer rebuilt the plan";
        EXPECT_EQ(first->count(), 2u);
        EXPECT_EQ(second->count(), 1u);

        first_handle.reset();
        graph.run().value();
        EXPECT_EQ(plan_builds(graph), 1u) << "removing one of two observers rebuilt the plan";
        EXPECT_EQ(first->count(), 2u);
        EXPECT_EQ(second->count(), 2u);

        second_handle.reset();
        graph.run().value();
        EXPECT_EQ(plan_builds(graph), 1u) << "removing the last observer rebuilt the plan";
    });
}

/** @brief Tests that an observer on a DynamicGraph times the very next run. */
TEST_F(Medium_RunDriver, AnObserverOnADynamicGraphTimesItsNodes)
{
    auto buf = runtime().buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto world = runtime().dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] += 1; });

    world.run().value();

    Testing::RunRecorder recorder(runtime());
    world.run().value();
    ASSERT_EQ(recorder.count(), 1u);
    EXPECT_FALSE(recorder.last().events.empty()) << "the plan built before the observer gave no records";

    world.run().value();
    ASSERT_EQ(recorder.count(), 2u);
    EXPECT_FALSE(recorder.last().events.empty());

    recorder.stop();
    world.run().value();
    EXPECT_EQ(recorder.count(), 2u) << "the removal did not reach the runs";
}

#ifdef NDEBUG
/** @brief Tests that moving a graph waits for the pending step and ends the step the handle holds. */
TEST_F(Medium_RunDriver, MovingAGraphWithAPendingStepEndsTheStep)
{
    auto buf = runtime().buffer<int>(N).value();
    buf[0] = 0;
    int* p = buf.data();
    std::atomic<bool> released{false};

    auto g = runtime().graph();
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
}

/** @brief Tests that the destination of a move runs after the move ended a pending step. */
TEST_F(Medium_RunDriver, TheDestinationOfAMoveRunsAfterAPendingStepEnded)
{
    auto buf = runtime().buffer<int>(N).value();
    buf[0] = 0;
    int* p = buf.data();
    std::atomic<bool> released{false};

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [&released, p]()
    {
        wait_for_gate(released);
        p[0] += 7;
    });

    auto step = g.run_async().value();
    released.store(true, std::memory_order_release);

    Graph moved = std::move(g);
    ASSERT_TRUE(moved.run().has_value()) << "the destination must run";
    EXPECT_EQ(buf[0], 14);
}
#endif

/** @brief Tests that a failed chained run of one step advances no State. */
TEST_F(Medium_RunDriver, AFailedRunOfOneStepAdvancesNoState)
{
    CountedStep step(runtime(), K_COUNT_OVER_CAPACITY);
    const int* const before = step.field.current();

    const auto failed = step.graph.run();

    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(step.field.current(), before) << "the failed step advanced the State";
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(step.field.current()[i], K_FIELD_START) << "at i=" << i;
}

/** @brief Tests that a failed run of three steps never advances the State of its failed step. */
TEST_F(Medium_RunDriver, AFailedRunOfSeveralStepsNeverAdvancesTheFailedStep)
{
    constexpr std::size_t STEPS = 3;

    CountedStep step(runtime(), K_COUNT_OVER_CAPACITY);

    const auto failed = step.graph.run(STEPS);

    ASSERT_FALSE(failed.has_value());
    for (std::size_t i = 0; i < N; ++i)
    {
        EXPECT_GE(step.field.current()[i], K_FIELD_START) << "at i=" << i;
        EXPECT_LE(step.field.current()[i], K_FIELD_START + static_cast<int>(STEPS) - 1)
            << "the failed step advanced the State, at i=" << i;
    }
}

/** @brief Tests that a budget the clock cannot hold runs until cancelled, not for one step. */
TEST_P(Medium_RunDriverBudgets, ABudgetPastTheClocksRangeRunsUntilCancelled)
{
    if (GetParam() == Overflow::Hours)
        expect_run_for_lasts_until_cancelled(runtime(), std::chrono::hours::max());
    else
        expect_run_for_lasts_until_cancelled(runtime(), std::chrono::nanoseconds::max());
}

INSTANTIATE_TEST_SUITE_P(Rows, Medium_RunDriverBudgets,
                         ::testing::Values(Overflow::Hours, Overflow::Nanoseconds));

/** @brief Tests that step hooks feed eight replays of one plan, in the order added. */
TEST_P(Medium_RunDriverGraphKinds, StepHooksFeedEveryReplayInTheOrderAdded)
{
    constexpr std::size_t K_STEPS = 8;
    constexpr std::uint32_t K_BASE = 100;

    auto word = runtime().buffer<std::uint32_t>(2).value();
    auto seen = runtime().buffer<std::uint32_t>(K_STEPS).value();
    for (std::size_t i = 0; i < K_STEPS; ++i)
        seen[i] = 0;
    std::uint32_t* w = word.data();
    std::uint32_t* s = seen.data();

    on_graph_of(runtime(), GetParam(), [&](auto& graph)
    {
        record_add(graph, K_STEPS, Reads(word), Writes(seen), [w, s](std::size_t i)
        {
            if (i == w[0])
                s[i] = w[1];
        });

        std::vector<std::uint32_t> observed;
        graph.before_each_step([w](std::size_t finished_steps)
        {
            w[0] = static_cast<std::uint32_t>(finished_steps);
            w[1] = K_BASE + static_cast<std::uint32_t>(finished_steps);
        });
        graph.before_each_step([w, &observed](std::size_t) { observed.push_back(w[1]); });

        graph.run(K_STEPS).value();

        ASSERT_EQ(observed.size(), K_STEPS);
        for (std::size_t i = 0; i < K_STEPS; ++i)
        {
            EXPECT_EQ(seen[i], K_BASE + static_cast<std::uint32_t>(i)) << "replay " << i;
            EXPECT_EQ(observed[i], K_BASE + static_cast<std::uint32_t>(i)) << "replay " << i;
        }
    });
}

/** @brief Tests that a step hook does not rebuild the plan. */
TEST_P(Medium_RunDriverGraphKinds, AStepHookRebuildsNothing)
{
    constexpr std::size_t K_STEPS = 8;

    auto word = runtime().buffer<std::uint32_t>(2).value();
    std::uint32_t* w = word.data();

    on_graph_of(runtime(), GetParam(), [&](auto& graph)
    {
        record_add(graph, std::size_t{1}, Reads{}, Writes(word), [w](std::size_t) { w[0] += 1u; });
        graph.before_each_step([w](std::size_t finished_steps)
        {
            w[1] = static_cast<std::uint32_t>(finished_steps);
        });

        graph.run(K_STEPS).value();
        graph.run().value();

        EXPECT_EQ(plan_builds(graph), 1u) << "a hook must not rebuild the plan";
    });
}

/** @brief Tests that a new run counts the steps its hooks are given from zero. */
TEST_P(Medium_RunDriverGraphKinds, ANewRunCountsItsStepsFromZero)
{
    constexpr std::size_t K_STEPS = 8;

    auto word = runtime().buffer<std::uint32_t>(2).value();
    std::uint32_t* w = word.data();

    on_graph_of(runtime(), GetParam(), [&](auto& graph)
    {
        record_add(graph, std::size_t{1}, Reads{}, Writes(word), [w](std::size_t) { w[0] += 1u; });
        graph.before_each_step([w](std::size_t finished_steps)
        {
            w[1] = static_cast<std::uint32_t>(finished_steps);
        });

        graph.run(K_STEPS).value();
        ASSERT_EQ(w[1], K_STEPS - 1);

        graph.run().value();
        EXPECT_EQ(w[1], 0u) << "a new run counts its steps from zero";
    });
}

/** @brief Tests that a moved graph keeps its step hooks. */
TEST_F(Medium_RunDriver, AMovedGraphKeepsItsStepHooks)
{
    auto buf = runtime().buffer<int>(N).value();
    int* p = buf.data();

    std::size_t calls = 0;
    auto source = runtime().graph();
    source.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    source.before_each_step([&calls](std::size_t) { ++calls; });

    auto moved = std::move(source);
    moved.run().value();
    EXPECT_EQ(calls, 1u);
}

/** @brief Tests that the source of a move has no step hook. */
TEST_F(Medium_RunDriver, TheSourceOfAMoveHasNoStepHook)
{
    auto buf = runtime().buffer<int>(N).value();
    int* p = buf.data();

    std::size_t calls = 0;
    auto source = runtime().graph();
    source.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    source.before_each_step([&calls](std::size_t) { ++calls; });

    auto moved = std::move(source);
    moved.run().value();
    ASSERT_EQ(calls, 1u);

    source.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    source.run().value();
    EXPECT_EQ(calls, 1u) << "the source of a move must have no hook";
}

/** @brief Tests that a hook too large for the inline storage is held and called. */
TEST_F(Medium_RunDriver, AStepHookLargerThanTheInlineStorageRuns)
{
    auto buf = runtime().buffer<int>(N).value();
    int* p = buf.data();

    std::array<std::uint64_t, 16> payload{};
    payload[15] = 7;
    std::uint64_t seen = 0;

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    g.before_each_step([payload, &seen](std::size_t) { seen = payload[15]; });
    g.run().value();

    EXPECT_EQ(seen, 7u);
}

#if defined(SYCL_IMPLEMENTATION_ONEAPI)
namespace
{
    /** @brief Records a graph whose task throws from a host_task, an error SYCL reports only asynchronously. */
    Graph record_asynchronous_failure(const Runtime& rt, Buffer<int>& buf)
    {
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
        return g;
    }
} // namespace

/** @brief Tests that an error SYCL reports only through its asynchronous handler fails the run. */
TEST_P(Medium_RunDriverLayers, AnErrorOnlyTheAsynchronousHandlerSeesFailsTheRun)
{
    auto buf = runtime().buffer<int>(N).value();
    auto g = record_asynchronous_failure(runtime(), buf);

    const auto run = g.run();
    ASSERT_FALSE(run.has_value()) << "the run reported success";
    EXPECT_EQ(run.error().code, ErrorCode::KernelFailed);
}

/** @brief Tests that an error only the asynchronous handler sees does not outlive the run in the queue. */
TEST_P(Medium_RunDriverLayers, AnAsynchronousErrorDoesNotOutliveTheRun)
{
    auto buf = runtime().buffer<int>(N).value();
    auto g = record_asynchronous_failure(runtime(), buf);
    (void)g.run();

    // An error the run did not return is still in the queue and comes out of this wait.
    const auto drained = runtime().wait();
    EXPECT_TRUE(drained.has_value()) << "the error outlived the run that caused it";
}

INSTANTIATE_TEST_SUITE_P(Rows, Medium_RunDriverLayers, ::testing::Values(false, true));
#endif
