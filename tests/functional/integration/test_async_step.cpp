/****************************************************************/
/* test_async_step.cpp                                          */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// Coverage for Graph::run_async() / DynamicGraph::run_async() and the RunHandle
// they return.
//
// The claim under test is that a step can be started without blocking the
// caller, so a simulation tick overlaps with work that needs no workers (a
// renderer or audio extract of the previous tick). "Did not block" is not
// observable from a return value, so the overlap tests use a rendezvous: the
// step's own task waits for a flag the caller sets AFTER run_async() returned.
// If run_async() blocked, the flag could never be set in time and the task times
// out. Every wait is bounded, so a regression reports rather than hangs.

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

    // Blocks until *released* or the deadline passes; reports which happened.
    // Used as the body of a host task so a step can be held open while the test
    // does something on the calling thread.
    bool wait_for_gate(const std::atomic<bool>& released)
    {
        const auto deadline = std::chrono::steady_clock::now() + GATE_TIMEOUT;
        while (!released.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        return released.load(std::memory_order_acquire);
    }
}

// run_async() must return before the step finishes. The step's host task waits
// for a flag the caller only sets afterwards, so it can only observe the flag if
// the caller really did keep running.
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

// An asynchronous step must be observationally identical to a synchronous one:
// the same stepped State, driven the same number of times, ends in the same place.
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

// The State advance belongs to wait(), not to submit: until the step has been
// settled its just-written field is not yet current. Getting this wrong would
// let a reader see the next field one step early.
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

// A handle that goes out of scope unwaited must still finish the step, because
// the compiled plan cannot be destroyed while workers hold pointers into it. The
// graph must also be runnable again afterwards.
TEST(Integration_AsyncStep, DestroyingAnUnwaitedHandleStillFinishesTheStep)
{
    constexpr std::size_t N = 8;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) buf[i] = 0;
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N, [p](std::size_t i) { p[i] += 1; });

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

// A second wait on the same handle has no step to wait for; it must say so
// rather than block on a latch nobody will signal again.
TEST(Integration_AsyncStep, WaitingTwiceThrows)
{
    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(4);
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), std::size_t{4},
          [p](std::size_t i) { p[i] = static_cast<int>(i); });

    auto step = g.run_async();
    step.wait();
    EXPECT_THROW(step.wait(), SushiException);
}

// Starting a second step — synchronously or asynchronously — while one is in
// flight must be rejected BEFORE anything touches the plan. A synchronous run
// re-arms every node's in-degree, so letting it proceed would rewrite the
// scheduling state of the DAG the workers are still walking.
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
    EXPECT_THROW(g.run_async(), SushiException) << "a second async step must be refused";

    released.store(true, std::memory_order_release);
    step.wait();

    EXPECT_EQ(buf[0], 7);

    // Rejection left nothing broken behind: the graph runs again.
    g.run();
    EXPECT_EQ(buf[0], 7);
}

// A task exception surfaces from wait(), and the graph stays usable — a failing
// step must not become a permanently failing graph.
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
    good.add(API::Reads{}, API::Writes(buf), std::size_t{4},
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

// ready() answers "would waiting return immediately" without blocking. It must
// be false while the step is held open and true once it has finished.
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

// The region-partitioned graph gets the same treatment: a step overlaps, and a
// pending structural change is committed before it is submitted.
TEST(Integration_AsyncStep, DynamicGraphRunsAsynchronously)
{
    constexpr std::size_t N = 16;

    auto rt = Runtime::create();
    rt.rebalancer(false);

    auto a = rt.buffer<int>(N);
    auto b = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i) { a[i] = 0; b[i] = 0; }

    int* pa = a.data();
    int* pb = b.data();

    auto world = rt.dynamic_graph();
    world.region(0).add(API::Reads{}, API::Writes(a), N, [pa](std::size_t i) { pa[i] += 1; });
    world.region(1).add(API::Reads{}, API::Writes(b), N, [pb](std::size_t i) { pb[i] += 2; });

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
    world.region(2).add(API::Reads{}, API::Writes(c), N, [pc](std::size_t i) { pc[i] += 5; });

    world.run_async().wait();
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(c[i], 5) << "at i=" << i;
    EXPECT_EQ(world.compile_count(), 2u) << "one composition per mutation, not per step";
}
