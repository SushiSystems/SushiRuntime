/**************************************************************************/
/* test_async_step.cpp                                                    */
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
 * @file test_async_step.cpp
 * @brief Tests Graph::run_async(), DynamicGraph::run_async() and the RunHandle they return.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>

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

        const auto late = step.try_wait();
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
}

/** @brief Tests that run_async() returns before the step finishes. */
TEST(Integration_AsyncStep, SubmitReturnsBeforeTheStepFinishes)
{
    constexpr std::size_t N = 8;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;

    std::atomic<bool> released{false};
    std::atomic<bool> saw_release{false};
    int* p = buf.data();

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [&released, &saw_release, p]()
    {
        saw_release.store(wait_for_gate(released), std::memory_order_release);
        p[0] = 42;
    });

    auto step = g.run_async();

    // The caller is still running: this is the work a frame would overlap.
    released.store(true, std::memory_order_release);

    const Internal::Observability::RunReport report = step.wait();

    EXPECT_TRUE(saw_release.load())
        << "the step's task never observed the flag, so run_async() blocked";
    EXPECT_EQ(buf[0], 42);
    EXPECT_FALSE(step.pending()) << "wait() must consume the handle";
    EXPECT_GE(report.total_tasks_executed, 1u);
}

/** @brief Tests that an asynchronous step ends in the same State as a synchronous one. */
TEST(Integration_AsyncStep, MatchesTheSynchronousResult)
{
    constexpr std::size_t N = 32;
    constexpr std::size_t STEPS = 6;

    auto rt = Runtime::create();

    auto synchronous = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i) synchronous.current()[i] = 1;
    auto sg = rt.graph();
    sg.add(synchronous, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] * 2; });
    sg.run(STEPS);

    auto asynchronous = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i) asynchronous.current()[i] = 1;
    auto ag = rt.graph();
    ag.add(asynchronous, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] * 2; });
    for (std::size_t s = 0; s < STEPS; ++s)
        ag.run_async().wait();

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(asynchronous.current()[i], synchronous.current()[i]) << "at i=" << i;
    EXPECT_EQ(ag.compile_count(), 1u) << "replaying asynchronously must not recompile";
}

/** @brief Tests that the State advances on wait(), not on submit. */
TEST(Integration_AsyncStep, StateAdvancesOnWaitNotOnSubmit)
{
    constexpr std::size_t N = 4;

    auto rt = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i) field.current()[i] = 5;

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    const int* const before_submit = field.current();
    auto step = g.run_async();
    EXPECT_EQ(field.current(), before_submit)
        << "submitting a step must not advance the State";

    step.wait();
    EXPECT_NE(field.current(), before_submit) << "wait() must advance the State";
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(field.current()[i], 6) << "at i=" << i;
}

/**
 * @brief Tests that destroying an unwaited handle still finishes the step and leaves the graph
 *        runnable.
 */
TEST(Integration_AsyncStep, DestroyingAnUnwaitedHandleStillFinishesTheStep)
{
    constexpr std::size_t N = 8;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto g = rt.graph();
    g.add(N, Reads{}, Writes(buf), [p](std::size_t i) { p[i] += 1; });

    {
        auto discarded = g.run_async();
        (void)discarded;   // destructor settles the step
    }

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], 1) << "at i=" << i;

    // The graph is not wedged: a following run works normally.
    g.run();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], 2) << "at i=" << i;
}

/** @brief Tests that a second wait on the same handle throws. */
TEST(Integration_AsyncStep, WaitingTwiceThrows)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(4);
    int* p = buf.data();

    auto g = rt.graph();
    g.add(std::size_t{4}, Reads{}, Writes(buf),
          [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto step = g.run_async();
    step.wait();
    EXPECT_THROW(step.wait(), SushiException);
}

/** @brief Tests that a second run is rejected while a step is in flight. */
TEST(Integration_AsyncStep, ASecondRunIsRejectedWhileAStepIsInFlight)
{
    constexpr std::size_t N = 4;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    std::atomic<bool> released{false};

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [&released, p]()
    {
        wait_for_gate(released);
        p[0] = 7;
    });

    auto step = g.run_async();

    EXPECT_THROW(g.run(), SushiException) << "a synchronous run must be refused";
    EXPECT_THROW(static_cast<void>(g.run_async()), SushiException) << "a second async step must be refused";

    released.store(true, std::memory_order_release);
    step.wait();

    EXPECT_EQ(buf[0], 7);

    // Rejection left nothing broken behind: the graph runs again.
    g.run();
    EXPECT_EQ(buf[0], 7);
}

/** @brief Tests that a task exception surfaces from wait() and leaves the graph usable. */
TEST(Integration_AsyncStep, TaskExceptionSurfacesOnWaitAndLeavesTheGraphUsable)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(4);
    int* p = buf.data();

    {
        auto bad = rt.graph();
        bad.add_host(Reads{}, Writes(buf),
                     []() { throw std::runtime_error("boom"); });
        auto step = bad.run_async();
        EXPECT_THROW(step.wait(), std::runtime_error);
        EXPECT_FALSE(step.pending());

        // The graph accepted the failure and is runnable again.
        auto retry = bad.run_async();
        EXPECT_THROW(retry.wait(), std::runtime_error);
    }

    auto good = rt.graph();
    good.add(std::size_t{4}, Reads{}, Writes(buf),
             [p](std::size_t i) { p[i] = 3; });
    good.run_async().wait();
    for (std::size_t i = 0; i < 4; ++i)
        ASSERT_EQ(buf[i], 3) << "at i=" << i;
}

// try_wait() reports the same failure as a value instead of an exception.
TEST(Integration_AsyncStep, TryWaitReportsFailureAsAValue)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(4);

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf),
               []() { throw std::runtime_error("boom"); });

    auto step = g.run_async();
    auto result = step.try_wait();
    EXPECT_FALSE(static_cast<bool>(result));
    EXPECT_FALSE(step.pending());
}

/** @brief Tests that ready() is false while the step is held open and true once it has finished. */
TEST(Integration_AsyncStep, ReadyTracksTheStep)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(4);
    int* p = buf.data();

    std::atomic<bool> released{false};

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [&released, p]()
    {
        wait_for_gate(released);
        p[0] = 1;
    });

    auto step = g.run_async();
    EXPECT_FALSE(step.ready()) << "a step held open must not report ready";

    released.store(true, std::memory_order_release);
    step.wait();
    EXPECT_TRUE(step.ready()) << "a settled handle owns no step, so it is ready";
}

/**
 * @brief Tests that a DynamicGraph step overlaps the caller and commits a pending structural change
 *        first.
 */
TEST(Integration_AsyncStep, DynamicGraphRunsAsynchronously)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create();
    rt.set_rebalancer_enabled(false);

    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(N, Reads{}, Writes(a), [pa](std::size_t i) { pa[i] += 1; });
    world.region(1).add(N, Reads{}, Writes(b), [pb](std::size_t i) { pb[i] += 2; });

    world.run_async().wait();
    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], 1) << "at i=" << i;
        ASSERT_EQ(b[i], 2) << "at i=" << i;
    }

    // A region added between steps is committed by the asynchronous path too.
    auto c = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) c[i] = 0;
    int* pc = c.data();
    world.region(2).add(N, Reads{}, Writes(c), [pc](std::size_t i) { pc[i] += 5; });

    world.run_async().wait();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(c[i], 5) << "at i=" << i;
    EXPECT_EQ(world.compose_count(), 2u) << "one composition per mutation, not per step";
}

/** @brief Tests that another graph's run entries are refused while a step is pending. */
TEST(Integration_AsyncStep, AnotherGraphIsRefusedWhileAStepIsPending)
{
    auto rt    = Runtime::create();
    auto first = rt.buffer<int>(4);
    auto other = rt.buffer<int>(4);
    first[0] = 0;
    other[0] = 0;
    int* pf = first.data();
    int* po = other.data();

    std::atomic<bool> released{false};

    auto g1 = rt.graph();
    g1.add_host(Reads{}, Writes(first), [&released, pf]()
    {
        wait_for_gate(released);
        pf[0] = 7;
    });
    auto g2 = rt.graph();
    g2.add_host(Reads{}, Writes(other), [po]() { po[0] += 1; });

    auto step = g1.run_async();

    try
    {
        g2.run();
        ADD_FAILURE() << "run() on another graph started while a step was pending";
    }
    catch (const SushiException& refusal)
    {
        EXPECT_EQ(refusal.error().code, ErrorCode::InvalidState);
    }
    ASSERT_NO_FATAL_FAILURE(expect_refused_as_invalid_state(g2.try_run()));
    ASSERT_NO_FATAL_FAILURE(expect_refused_as_invalid_state(g2.try_run_async()));
    EXPECT_EQ(other[0], 0) << "a refused run ran its task";

    released.store(true, std::memory_order_release);
    const Internal::Observability::RunReport report = step.wait();
    EXPECT_TRUE(report.is_successful);
    EXPECT_EQ(first[0], 7);

    EXPECT_TRUE(g2.try_run().has_value()) << "the runtime is free once the step is settled";
    EXPECT_EQ(other[0], 1);
}

/** @brief Tests that cancel() stops the pending step of its own graph and of no other. */
TEST(Integration_AsyncStep, CancelReachesOnlyTheGraphItIsCalledOn)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(4);
    auto idle_buf = rt.buffer<int>(4);
    int* p = buf.data();
    int* pi = idle_buf.data();

    std::atomic<bool> released{false};

    auto g = rt.graph();
    g.add_host(Reads{}, Writes(buf), [&released, p]()
    {
        wait_for_gate(released);
        p[0] = 1;
    });
    auto idle = rt.graph();
    idle.add_host(Reads{}, Writes(idle_buf), [pi]() { pi[0] = 1; });

    auto spared = g.run_async();
    idle.cancel();
    released.store(true, std::memory_order_release);
    EXPECT_FALSE(spared.wait().cancelled) << "cancel() on an idle graph stopped another's step";

    released.store(false, std::memory_order_release);
    auto stopped = g.run_async();
    g.cancel();
    released.store(true, std::memory_order_release);
    EXPECT_TRUE(stopped.wait().cancelled) << "cancel() did not reach its own pending step";

    g.cancel();
    EXPECT_FALSE(g.run().cancelled) << "cancel() with no run in flight marked the next run";
}

/** @brief Tests that a Graph destroyed with a step pending waits for it and ends it. */
TEST(Integration_AsyncStep, AGraphDestroyedWithAPendingStepEndsTheStep)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(4);
    buf[0] = 0;
    int* p = buf.data();

    std::atomic<bool> released{false};
    RunHandle<Graph> step;

    {
        auto g = rt.graph();
        g.add_host(Reads{}, Writes(buf), [&released, p]()
        {
            wait_for_gate(released);
            p[0] = 9;
        });
        step = g.run_async();
        EXPECT_TRUE(step.pending());
        released.store(true, std::memory_order_release);
    }

    EXPECT_EQ(buf[0], 9) << "the graph's destructor returned before the step had run";
    ASSERT_NO_FATAL_FAILURE(expect_step_ended_by_its_graph(step));

    auto next = rt.graph();
    next.add_host(Reads{}, Writes(buf), [p]() { p[0] += 1; });
    EXPECT_TRUE(next.try_run().has_value()) << "the destroyed graph left the runtime busy";
    EXPECT_EQ(buf[0], 10);
}

/** @brief Tests that a DynamicGraph destroyed with a step pending waits for it and ends it. */
TEST(Integration_AsyncStep, ADynamicGraphDestroyedWithAPendingStepEndsTheStep)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(4);
    buf[0] = 0;
    int* p = buf.data();

    std::atomic<bool> released{false};
    RunHandle<DynamicGraph> step;

    {
        auto world = rt.dynamic_graph();
        world.region(0).add_host(Reads{}, Writes(buf), [&released, p]()
        {
            wait_for_gate(released);
            p[0] = 9;
        });
        step = world.run_async();
        EXPECT_TRUE(step.pending());
        released.store(true, std::memory_order_release);
    }

    EXPECT_EQ(buf[0], 9) << "the graph's destructor returned before the step had run";
    ASSERT_NO_FATAL_FAILURE(expect_step_ended_by_its_graph(step));
}

/** @brief Tests that a region dropped while a step is pending finishes that step and no later one. */
TEST(Integration_AsyncStep, ARegionDroppedWhileAStepIsPendingFinishesTheStep)
{
    auto rt   = Runtime::create();
    auto kept = rt.buffer<int>(4);
    auto gone = rt.buffer<int>(4);
    kept[0] = 0;
    gone[0] = 0;
    int* pk = kept.data();
    int* pg = gone.data();

    std::atomic<bool> released{false};

    auto world = rt.dynamic_graph();
    world.region(0).add_host(Reads{}, Writes(kept), [&released, pk]()
    {
        wait_for_gate(released);
        pk[0] += 1;
    });
    world.region(1).add_host(Reads{}, Writes(gone), [&released, pg]()
    {
        wait_for_gate(released);
        pg[0] += 1;
    });

    auto step = world.run_async();
    world.drop(1);
    EXPECT_FALSE(world.has_region(1));
    EXPECT_EQ(world.region_count(), 1u);

    released.store(true, std::memory_order_release);
    const auto settled = step.try_wait();
    ASSERT_TRUE(settled.has_value()) << settled.error().message;
    EXPECT_TRUE((*settled).is_successful);
    EXPECT_EQ(kept[0], 1);
    EXPECT_EQ(gone[0], 1) << "the dropped region's task did not finish its write";

    const Internal::Observability::RunReport next = world.run();
    EXPECT_TRUE(next.is_successful);
    EXPECT_EQ(kept[0], 2);
    EXPECT_EQ(gone[0], 1) << "the dropped region ran again";
    EXPECT_EQ(world.compose_count(), 2u) << "the drop composes once, at the next run";
}

/** @brief Tests that a key dropped and recorded again while a step is pending runs its new work. */
TEST(Integration_AsyncStep, ARegionDroppedAndRecordedAgainWhileAStepIsPendingRunsItsNewWork)
{
    auto rt    = Runtime::create();
    auto old   = rt.buffer<int>(4);
    auto fresh = rt.buffer<int>(4);
    old[0] = 0;
    fresh[0] = 0;
    int* po = old.data();
    int* pf = fresh.data();

    std::atomic<bool> released{false};

    auto world = rt.dynamic_graph();
    world.region(7).add_host(Reads{}, Writes(old), [&released, po]()
    {
        wait_for_gate(released);
        po[0] += 1;
    });

    auto step = world.run_async();
    world.drop(7);
    world.region(7).add_host(Reads{}, Writes(fresh), [pf]() { pf[0] += 1; });

    released.store(true, std::memory_order_release);
    EXPECT_TRUE(step.try_wait().has_value());
    EXPECT_EQ(old[0], 1) << "the pending step ran the recording it was started with";
    EXPECT_EQ(fresh[0], 0) << "a recording made while the step was pending ran in that step";

    EXPECT_TRUE(world.try_run().has_value());
    EXPECT_EQ(old[0], 1) << "the dropped recording ran again";
    EXPECT_EQ(fresh[0], 1);
}
