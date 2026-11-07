/**************************************************************************/
/* test_run_handle.cpp                                                    */
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
 * @file test_run_handle.cpp
 * @brief Tests an asynchronous step and the RunHandle that waits on it.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"
#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

namespace
{
    constexpr auto K_GATE_TIMEOUT = std::chrono::milliseconds(2000);

    /** @brief Blocks until the flag is set or the deadline passes and returns which happened. */
    bool wait_for_gate(const std::atomic<bool>& released)
    {
        const auto deadline = std::chrono::steady_clock::now() + K_GATE_TIMEOUT;
        while (!released.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        return released.load(std::memory_order_acquire);
    }

    /** @brief Expects the handle of a step its graph already ended to own nothing. */
    template <class G>
    void expect_step_ended_by_its_graph(RunHandle<G>& step)
    {
        EXPECT_FALSE(step.pending()) << "the graph ended the step, so the handle owns none";
        EXPECT_TRUE(step.ready());

        const auto late = step.wait();
        ASSERT_FALSE(late.has_value());
        EXPECT_EQ(late.error().code, ErrorCode::InvalidState);
    }

    /** @brief Expects a run entry's Result to carry InvalidState. */
    template <class Outcome>
    void expect_refused_as_invalid_state(const Outcome& outcome)
    {
        ASSERT_FALSE(outcome.has_value());
        EXPECT_EQ(outcome.error().code, ErrorCode::InvalidState);
    }
} // namespace

/** @brief Fixture of the asynchronous step test that needs a pool worker, which runs on the program's runtime. */
class Medium_AsyncStep : public TestSupport::RuntimeTest
{
};

/** @brief Fixture of the asynchronous step tests, which run on a host runtime and step its executor. */
class Small_AsyncStep : public TestSupport::HostRuntimeTest
{
};

/**
 * @brief Fixture of the tests of a DynamicGraph with two regions, the second of which is dropped while a step is pending.
 *
 * Region 0 and region 1 each add one to their own word. The step is started and nothing has run
 * until the test waits, so the step is pending for as long as the test needs.
 */
class Small_AsyncStepDrop : public TestSupport::HostRuntimeTest
{
    protected:
        /** @brief Records the two regions on a DynamicGraph and starts the step. */
        void SetUp() override
        {
            TestSupport::HostRuntimeTest::SetUp();
            kept_.emplace(runtime().buffer<int>(4).value());
            gone_.emplace(runtime().buffer<int>(4).value());
            (*kept_)[0] = 0;
            (*gone_)[0] = 0;
            int* pk = kept_->data();
            int* pg = gone_->data();

            world_.emplace(runtime().dynamic_graph());
            world_->region(0).add_host(Reads{}, Writes(*kept_), [pk]() { pk[0] += 1; });
            world_->region(1).add_host(Reads{}, Writes(*gone_), [pg]() { pg[0] += 1; });
            step_.emplace(world_->run_async().value());
        }

        std::optional<Buffer<int>> kept_;         /**< Written by region 0. */
        std::optional<Buffer<int>> gone_;         /**< Written by region 1. */
        std::optional<DynamicGraph> world_;       /**< The graph of the two regions. */
        std::optional<RunHandle<DynamicGraph>> step_; /**< The pending step. */
};

/**
 * @brief Fixture of the tests of a region dropped and recorded again while a step is pending.
 *
 * Key 7 holds a task that adds one to a word; the key is dropped and a task that adds one to another word is recorded under it.
 */
class Small_AsyncStepRerecord : public TestSupport::HostRuntimeTest
{
    protected:
        /** @brief Records the first region, starts the step, then drops the key and records it again. */
        void SetUp() override
        {
            TestSupport::HostRuntimeTest::SetUp();
            old_.emplace(runtime().buffer<int>(4).value());
            fresh_.emplace(runtime().buffer<int>(4).value());
            (*old_)[0] = 0;
            (*fresh_)[0] = 0;
            int* po = old_->data();
            int* pf = fresh_->data();

            world_.emplace(runtime().dynamic_graph());
            world_->region(7).add_host(Reads{}, Writes(*old_), [po]() { po[0] += 1; });

            step_.emplace(world_->run_async().value());
            world_->drop(7);
            world_->region(7).add_host(Reads{}, Writes(*fresh_), [pf]() { pf[0] += 1; });
        }

        std::optional<Buffer<int>> old_;          /**< Written by the first recording. */
        std::optional<Buffer<int>> fresh_;        /**< Written by the recording made while the step is pending. */
        std::optional<DynamicGraph> world_;       /**< The graph. */
        std::optional<RunHandle<DynamicGraph>> step_; /**< The pending step. */
};

/** @brief Tests that run_async() returns before the step finishes. */
TEST_F(Medium_AsyncStep, SubmitReturnsBeforeTheStepFinishes)
{
    constexpr std::size_t N = 8;

    auto buf = runtime().buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;

    std::atomic<bool> released{false};
    std::atomic<bool> saw_release{false};
    int* p = buf.data();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [&released, &saw_release, p]()
    {
        saw_release.store(wait_for_gate(released), std::memory_order_release);
        p[0] = 42;
    });

    auto step = g.run_async().value();

    // The caller is still running: this is the work a frame would overlap.
    released.store(true, std::memory_order_release);
    step.wait().value();

    EXPECT_TRUE(saw_release.load())
        << "the step's task never observed the flag, so run_async() blocked";
}

/** @brief Tests that run_async() posts the step's root and runs nothing until the caller waits. */
TEST_F(Small_AsyncStep, SubmitRunsNothingUntilTheCallerWaits)
{
    auto buf = runtime().buffer<int>(4).value();
    buf[0] = 0;
    int* p = buf.data();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] = 42; });

    auto step = g.run_async().value();
    EXPECT_EQ(executor().pending(), 1u) << "the step's root was not posted";
    EXPECT_EQ(buf[0], 0) << "run_async() ran the step's task";

    step.wait().value();
    EXPECT_EQ(buf[0], 42);
}

/** @brief Tests that wait() consumes the handle and the step's write is visible after it. */
TEST_F(Small_AsyncStep, WaitConsumesTheHandleAndTheWriteIsVisible)
{
    constexpr std::size_t N = 8;

    auto buf = runtime().buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] = 42; });

    auto step = g.run_async().value();
    const RunReport report = step.wait().value();

    EXPECT_EQ(buf[0], 42);
    EXPECT_FALSE(step.pending()) << "wait() must consume the handle";
    EXPECT_GE(report.total_tasks_executed, 1u);
}

/** @brief Tests that an asynchronous step ends in the same State as a synchronous one. */
TEST_F(Small_AsyncStep, MatchesTheSynchronousResult)
{
    constexpr std::size_t N = 32;
    constexpr std::size_t STEPS = 6;

    auto synchronous = runtime().state<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) synchronous.current()[i] = 1;
    auto sg = runtime().graph();
    sg.add_rule(synchronous, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] * 2; });
    sg.run(STEPS).value();

    auto asynchronous = runtime().state<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) asynchronous.current()[i] = 1;
    auto ag = runtime().graph();
    ag.add_rule(asynchronous, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] * 2; });
    for (std::size_t s = 0; s < STEPS; ++s)
        ag.run_async().value().wait().value();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(asynchronous.current()[i], synchronous.current()[i]) << "at i=" << i;
}

/** @brief Tests that replaying asynchronously does not recompile. */
TEST_F(Small_AsyncStep, ReplayingAsynchronouslyDoesNotRecompile)
{
    constexpr std::size_t N = 32;
    constexpr std::size_t STEPS = 6;

    auto field = runtime().state<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) field.current()[i] = 1;
    auto g = runtime().graph();
    g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] * 2; });
    for (std::size_t s = 0; s < STEPS; ++s)
        g.run_async().value().wait().value();

    EXPECT_EQ(g.compile_count(), 1u) << "replaying asynchronously must not recompile";
}

/** @brief Tests that the State advances on wait(), not on submit. */
TEST_F(Small_AsyncStep, StateAdvancesOnWaitNotOnSubmit)
{
    constexpr std::size_t N = 4;

    auto field = runtime().state<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) field.current()[i] = 5;

    auto g = runtime().graph();
    g.add_rule(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    const int* const before_submit = field.current();
    auto step = g.run_async().value();
    EXPECT_EQ(field.current(), before_submit)
        << "submitting a step must not advance the State";

    step.wait().value();
    EXPECT_NE(field.current(), before_submit) << "wait() must advance the State";
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(field.current()[i], 6) << "at i=" << i;
}

/** @brief Tests that destroying an unwaited handle still finishes the step. */
TEST_F(Small_AsyncStep, DestroyingAnUnwaitedHandleStillFinishesTheStep)
{
    constexpr std::size_t N = 8;

    auto buf = runtime().buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto g = runtime().graph();
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] += 1; });

    {
        auto discarded = g.run_async().value();
        (void)discarded;   // destructor settles the step
    }

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], 1) << "at i=" << i;
}

/** @brief Tests that a graph whose unwaited handle was destroyed stays runnable. */
TEST_F(Small_AsyncStep, ADestroyedUnwaitedHandleLeavesTheGraphRunnable)
{
    constexpr std::size_t N = 8;

    auto buf = runtime().buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto g = runtime().graph();
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] += 1; });

    {
        auto discarded = g.run_async().value();
        (void)discarded;   // destructor settles the step
    }

    g.run().value();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], 2) << "at i=" << i;
}

/** @brief Tests that a second wait on the same handle is refused as InvalidState. */
TEST_F(Small_AsyncStep, ASecondWaitIsRefusedAsInvalidState)
{
    auto buf = runtime().buffer<int>(4).value();
    int* p = buf.data();

    auto g = runtime().graph();
    g.add(std::size_t{4}, Reads{}, Writes(buf),
          [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto step = g.run_async().value();
    step.wait().value();

    const auto second = step.wait();
    ASSERT_FALSE(second.has_value());
    EXPECT_EQ(second.error().code, ErrorCode::InvalidState);
}

/** @brief Tests that a synchronous and an asynchronous run are refused while a step is in flight. */
TEST_F(Small_AsyncStep, ASecondRunIsRejectedWhileAStepIsInFlight)
{
    constexpr std::size_t N = 4;

    auto buf = runtime().buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] = 7; });

    auto step = g.run_async().value();

    EXPECT_FALSE(g.run().has_value()) << "a synchronous run must be refused";
    EXPECT_FALSE(g.run_async().has_value()) << "a second async step must be refused";

    step.wait().value();
    EXPECT_EQ(buf[0], 7);
}

/** @brief Tests that a graph whose step settled after refused runs runs again. */
TEST_F(Small_AsyncStep, TheGraphRunsAgainOnceTheStepHasSettled)
{
    constexpr std::size_t N = 4;

    auto buf = runtime().buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] += 7; });

    auto step = g.run_async().value();
    EXPECT_FALSE(g.run().has_value());

    step.wait().value();

    // Rejection left nothing broken behind: the graph runs again.
    g.run().value();
    EXPECT_EQ(buf[0], 14);
}

/** @brief Tests that a task exception surfaces from wait() as a value, and the handle then owns no step. */
TEST_F(Small_AsyncStep, TryWaitReportsFailureAsAValue)
{
    auto buf = runtime().buffer<int>(4).value();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf),
               []() { throw std::runtime_error("boom"); });

    auto step = g.run_async().value();
    auto result = step.wait();
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_FALSE(step.pending());
}

/** @brief Tests that a graph whose step failed accepts a later run, and another graph runs after it. */
TEST_F(Small_AsyncStep, AFailedStepLeavesTheGraphUsable)
{
    auto buf = runtime().buffer<int>(4).value();
    int* p = buf.data();

    {
        auto bad = runtime().graph();
        bad.add_host(Reads{}, Writes(buf),
                     []() { throw std::runtime_error("boom"); });
        auto step = bad.run_async().value();
        EXPECT_FALSE(step.wait().has_value());

        // The graph accepted the failure and is runnable again.
        auto retry = bad.run_async().value();
        EXPECT_FALSE(retry.wait().has_value());
    }

    auto good = runtime().graph();
    good.add(std::size_t{4}, Reads{}, Writes(buf),
             [p](std::size_t i) { p[i] = 3; });
    good.run_async().value().wait().value();
    for (std::size_t i = 0; i < 4; ++i)
        ASSERT_EQ(buf[i], 3) << "at i=" << i;
}

/** @brief Tests that ready() is false while the step has not run and true once it has finished. */
TEST_F(Small_AsyncStep, ReadyTracksTheStep)
{
    auto buf = runtime().buffer<int>(4).value();
    int* p = buf.data();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] = 1; });

    auto step = g.run_async().value();
    EXPECT_FALSE(step.ready()) << "a step whose task has not run must not report ready";

    step.wait().value();
    EXPECT_TRUE(step.ready()) << "a settled handle owns no step, so it is ready";
}

/** @brief Tests that a DynamicGraph step runs the regions of the graph. */
TEST_F(Small_AsyncStep, DynamicGraphRunsAsynchronously)
{
    constexpr std::size_t N = 16;

    auto a = runtime().buffer<int>(N).value();
    auto b = runtime().buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    auto world = runtime().dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
    world.region(1).add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] += 2; });

    world.run_async().value().wait().value();
    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], 1) << "at i=" << i;
        ASSERT_EQ(b[i], 2) << "at i=" << i;
    }
}

/** @brief Tests that a region added between steps is committed by the asynchronous path, one composition per mutation. */
TEST_F(Small_AsyncStep, ARegionAddedBetweenStepsIsCommittedByTheAsynchronousPath)
{
    constexpr std::size_t N = 16;

    auto a = runtime().buffer<int>(N).value();
    auto c = runtime().buffer<int>(N).value();
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; c[i] = 0; }

    int* pa = a.data();
    int* pc = c.data();

    auto world = runtime().dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
    world.run_async().value().wait().value();

    world.region(2).add(N, Reads{}, Writes(c), [pc](std::size_t i) { pc[i] += 5; });
    world.run_async().value().wait().value();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(c[i], 5) << "at i=" << i;
    EXPECT_EQ(world.compose_count(), 2u) << "one composition per mutation, not per step";
}

/** @brief Tests that other graphs' run entries are refused while a step is pending, and their tasks do not run. */
TEST_F(Small_AsyncStep, AnotherGraphIsRefusedWhileAStepIsPending)
{
    auto first = runtime().buffer<int>(4).value();
    auto other = runtime().buffer<int>(4).value();
    first[0] = 0;
    other[0] = 0;
    int* pf = first.data();
    int* po = other.data();

    auto g1 = runtime().graph();
    g1.add_host(Reads{}, Writes(first), [pf]() { pf[0] = 7; });
    auto g2 = runtime().graph();
    g2.add_host(Reads{}, Writes(other), [po]() { po[0] += 1; });

    auto step = g1.run_async().value();

    ASSERT_NO_FATAL_FAILURE(expect_refused_as_invalid_state(g2.run()));
    ASSERT_NO_FATAL_FAILURE(expect_refused_as_invalid_state(g2.run(std::size_t{2})));
    ASSERT_NO_FATAL_FAILURE(expect_refused_as_invalid_state(g2.run_async()));
    EXPECT_EQ(other[0], 0) << "a refused run ran its task";

    const RunReport report = step.wait().value();
    EXPECT_TRUE(report.is_successful);
    EXPECT_EQ(first[0], 7);
}

/** @brief Tests that the runtime is free for another graph once the pending step has settled. */
TEST_F(Small_AsyncStep, TheRuntimeIsFreeOnceTheStepHasSettled)
{
    auto first = runtime().buffer<int>(4).value();
    auto other = runtime().buffer<int>(4).value();
    first[0] = 0;
    other[0] = 0;
    int* pf = first.data();
    int* po = other.data();

    auto g1 = runtime().graph();
    g1.add_host(Reads{}, Writes(first), [pf]() { pf[0] = 7; });
    auto g2 = runtime().graph();
    g2.add_host(Reads{}, Writes(other), [po]() { po[0] += 1; });

    auto step = g1.run_async().value();
    step.wait().value();

    EXPECT_TRUE(g2.run().has_value()) << "the runtime is free once the step is settled";
    EXPECT_EQ(other[0], 1);
}

/** @brief Tests that cancel() on an idle graph does not stop the step of another graph. */
TEST_F(Small_AsyncStep, CancelOnAnIdleGraphSparesAnotherGraphsStep)
{
    auto buf = runtime().buffer<int>(4).value();
    auto idle_buf = runtime().buffer<int>(4).value();
    int* p = buf.data();
    int* pi = idle_buf.data();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] = 1; });
    auto idle = runtime().graph();
    idle.add_host(Reads{}, Writes(idle_buf), [pi]() { pi[0] = 1; });

    auto spared = g.run_async().value();
    idle.cancel();
    EXPECT_FALSE(spared.wait().value().cancelled)
        << "cancel() on an idle graph stopped another's step";
}

/** @brief Tests that cancel() stops the pending step of its own graph. */
TEST_F(Small_AsyncStep, CancelStopsItsOwnPendingStep)
{
    auto buf = runtime().buffer<int>(4).value();
    int* p = buf.data();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] = 1; });

    auto stopped = g.run_async().value();
    g.cancel();
    EXPECT_TRUE(stopped.wait().value().cancelled) << "cancel() did not reach its own pending step";
}

/** @brief Tests that cancel() with no run in flight does not mark the next run. */
TEST_F(Small_AsyncStep, CancelWithNoRunInFlightDoesNotMarkTheNextRun)
{
    auto buf = runtime().buffer<int>(4).value();
    int* p = buf.data();

    auto g = runtime().graph();
    g.add_host(Reads{}, Writes(buf), [p]() { p[0] = 1; });

    g.cancel();
    EXPECT_FALSE(g.run().value().cancelled) << "cancel() with no run in flight marked the next run";
}

/** @brief Tests that a Graph destroyed with a step pending waits for it and the handle reports it ended. */
TEST_F(Small_AsyncStep, AGraphDestroyedWithAPendingStepEndsTheStep)
{
    auto buf = runtime().buffer<int>(4).value();
    buf[0] = 0;
    int* p = buf.data();

    RunHandle<Graph> step;

    {
        auto g = runtime().graph();
        g.add_host(Reads{}, Writes(buf), [p]() { p[0] = 9; });
        step = g.run_async().value();
        EXPECT_TRUE(step.pending());
    }

    EXPECT_EQ(buf[0], 9) << "the graph's destructor returned before the step had run";
    ASSERT_NO_FATAL_FAILURE(expect_step_ended_by_its_graph(step));
}

/** @brief Tests that the runtime is left free for another graph by a Graph destroyed with a pending step. */
TEST_F(Small_AsyncStep, AGraphDestroyedWithAPendingStepLeavesTheRuntimeFree)
{
    auto buf = runtime().buffer<int>(4).value();
    buf[0] = 0;
    int* p = buf.data();

    {
        auto g = runtime().graph();
        g.add_host(Reads{}, Writes(buf), [p]() { p[0] = 9; });
        auto step = g.run_async().value();
        (void)step;
    }

    auto next = runtime().graph();
    next.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    EXPECT_TRUE(next.run().has_value()) << "the destroyed graph left the runtime busy";
    EXPECT_EQ(buf[0], 10);
}

/** @brief Tests that a DynamicGraph destroyed with a step pending waits for it and the handle reports it ended. */
TEST_F(Small_AsyncStep, ADynamicGraphDestroyedWithAPendingStepEndsTheStep)
{
    auto buf = runtime().buffer<int>(4).value();
    buf[0] = 0;
    int* p = buf.data();

    RunHandle<DynamicGraph> step;

    {
        auto world = runtime().dynamic_graph();
        world.region(0).add_host(Reads{}, Writes(buf), [p]() { p[0] = 9; });
        step = world.run_async().value();
        EXPECT_TRUE(step.pending());
    }

    EXPECT_EQ(buf[0], 9) << "the graph's destructor returned before the step had run";
    ASSERT_NO_FATAL_FAILURE(expect_step_ended_by_its_graph(step));
}

/** @brief Tests that drop() removes the region from the count while a step is pending. */
TEST_F(Small_AsyncStepDrop, DropRemovesTheRegionFromTheCount)
{
    world_->drop(1);
    EXPECT_FALSE(world_->has_region(1));
    EXPECT_EQ(world_->region_count(), 1u);

    step_->wait().value();
}

/** @brief Tests that the pending step finishes the task of a region dropped while it is pending. */
TEST_F(Small_AsyncStepDrop, ThePendingStepFinishesTheDroppedTask)
{
    world_->drop(1);

    const auto settled = step_->wait();
    ASSERT_TRUE(settled.has_value()) << settled.error().message;
    EXPECT_TRUE((*settled).is_successful);
    EXPECT_EQ((*kept_)[0], 1);
    EXPECT_EQ((*gone_)[0], 1) << "the dropped region's task did not finish its write";
}

/** @brief Tests that the run after a drop does not run the dropped region and composes once. */
TEST_F(Small_AsyncStepDrop, TheNextRunDoesNotRunTheDroppedRegionAndComposesOnce)
{
    world_->drop(1);
    step_->wait().value();

    const RunReport next = world_->run().value();
    EXPECT_TRUE(next.is_successful);
    EXPECT_EQ((*kept_)[0], 2);
    EXPECT_EQ((*gone_)[0], 1) << "the dropped region ran again";
    EXPECT_EQ(world_->compose_count(), 2u) << "the drop composes once, at the next run";
}

/** @brief Tests that the pending step runs the recording it started with, not one made after. */
TEST_F(Small_AsyncStepRerecord, ThePendingStepRunsTheRecordingItStartedWith)
{
    EXPECT_TRUE(step_->wait().has_value());
    EXPECT_EQ((*old_)[0], 1) << "the pending step ran the recording it was started with";
    EXPECT_EQ((*fresh_)[0], 0) << "a recording made while the step was pending ran in that step";
}

/** @brief Tests that the next run runs the recording made while the step was pending. */
TEST_F(Small_AsyncStepRerecord, TheNextRunRunsTheRecordingMadeWhileTheStepWasPending)
{
    step_->wait().value();

    EXPECT_TRUE(world_->run().has_value());
    EXPECT_EQ((*old_)[0], 1) << "the dropped recording ran again";
    EXPECT_EQ((*fresh_)[0], 1);
}
