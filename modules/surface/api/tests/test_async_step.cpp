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

#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    constexpr auto GATE_TIMEOUT = std::chrono::milliseconds(2000);

    /** @brief Blocks until the flag is set or the deadline passes and returns which happened. */
    bool wait_for_gate(const std::atomic<bool>& released)
    {
        const auto deadline = std::chrono::steady_clock::now() + GATE_TIMEOUT;
        while (!released.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        return released.load(std::memory_order_acquire);
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
    g.add_host(API::Reads{}, API::Writes(buf), [&released, &saw_release, p]()
    {
        saw_release.store(wait_for_gate(released), std::memory_order_release);
        p[0] = 42;
    });

    auto step = g.run_async();

    // The caller is still running: this is the work a frame would overlap.
    released.store(true, std::memory_order_release);

    const Core::RunReport report = step.wait();

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
    g.add(N, API::Reads{}, API::Writes(buf), [p](std::size_t i) { p[i] += 1; });

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
    g.add(std::size_t{4}, API::Reads{}, API::Writes(buf),
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
    g.add_host(API::Reads{}, API::Writes(buf), [&released, p]()
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
        bad.add_host(API::Reads{}, API::Writes(buf),
                     []() { throw std::runtime_error("boom"); });
        auto step = bad.run_async();
        EXPECT_THROW(step.wait(), std::runtime_error);
        EXPECT_FALSE(step.pending());

        // The graph accepted the failure and is runnable again.
        auto retry = bad.run_async();
        EXPECT_THROW(retry.wait(), std::runtime_error);
    }

    auto good = rt.graph();
    good.add(std::size_t{4}, API::Reads{}, API::Writes(buf),
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
    g.add_host(API::Reads{}, API::Writes(buf),
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
    g.add_host(API::Reads{}, API::Writes(buf), [&released, p]()
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
    world.region(0).add(N, API::Reads{}, API::Writes(a), [pa](std::size_t i) { pa[i] += 1; });
    world.region(1).add(N, API::Reads{}, API::Writes(b), [pb](std::size_t i) { pb[i] += 2; });

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
    world.region(2).add(N, API::Reads{}, API::Writes(c), [pc](std::size_t i) { pc[i] += 5; });

    world.run_async().wait();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(c[i], 5) << "at i=" << i;
    EXPECT_EQ(world.compose_count(), 2u) << "one composition per mutation, not per step";
}
