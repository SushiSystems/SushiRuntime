/**************************************************************************/
/* test_fluent_lifecycle.cpp                                              */
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
 * @file test_fluent_lifecycle.cpp
 * @brief Tests State replay, run_until, coupled fields, recompilation and errors in the fluent API.
 * @author Mustafa Garip
 */

#include <cstddef>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

/**
 * @brief Tests state replay and double-buffering during fluent execution.
 *
 * One update rule increments every element by 1 each step. Verifies that the compiled plan is properly replayed and State is double-buffered so each step reads the previous field and writes the next one.
 */
TEST(Integration_FluentLifecycle, StateReplayIncrement)
{
    constexpr std::size_t N     = 1024;
    constexpr std::size_t STEPS = 64;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        field[i] = 0;

    auto sim = rt.graph();
    sim.add(field, [](std::size_t i, const int* cur, int* next)
    {
        next[i] = cur[i] + 1;
    });

    auto report = sim.run(STEPS);
    EXPECT_EQ(report.total_tasks_executed, STEPS); // one task replayed STEPS times

    bool ok = true;
    for (std::size_t i = 0; i < N; ++i)
        if (field[i] != static_cast<int>(STEPS)) { ok = false; break; }
    EXPECT_TRUE(ok) << "State replay/double-buffer produced wrong values.";
}

/**
 * @brief Tests replay across multiple run() calls.
 *
 * Verifies that the second run resets the node state the first run consumed, ensuring continuous execution.
 */
TEST(Integration_FluentLifecycle, ReplayAcrossCalls)
{
    constexpr std::size_t N = 256;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        field[i] = 0;

    auto sim = rt.graph();
    sim.add(field, [](std::size_t i, const int* cur, int* next)
    {
        next[i] = cur[i] + 1;
    });

    sim.run(10);
    sim.run(5);

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(field[i], 15);
}

/**
 * @brief Tests that run_until stops exactly when the condition is met.
 *
 * Checks its stop condition before each step using the State the previous step produced.
 */
TEST(Integration_FluentLifecycle, RunUntilStopsOnCondition)
{
    constexpr std::size_t N = 64;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        field[i] = 0;

    auto sim = rt.graph();
    sim.add(field, [](std::size_t i, const int* cur, int* next)
    {
        next[i] = cur[i] + 1;
    });

    sim.run_until([&]() { return field[0] >= 20; });

    EXPECT_EQ(field[0], 20);
}

/**
 * @brief Tests that coupled fields evolve using Jacobi-style updates.
 *
 * Both rules read the previous step's data, verifying the advance-after-step rule rather than per-rule swapping.
 */
TEST(Integration_FluentLifecycle, CoupledFieldsAreJacobi)
{
    constexpr std::size_t N     = 128;
    constexpr std::size_t STEPS = 10;

    auto rt = Runtime::create();
    auto u  = rt.state<int>(N);
    auto v  = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i) { u[i] = 0; v[i] = 0; }

    auto sim = rt.graph();
    sim.add(v, [](std::size_t i, const int* vc, int* vn)
    {
        vn[i] = vc[i] + 1;
    });
    sim.add(u, v, [](std::size_t i, const int* uc, int* un, const int* vc)
    {
        un[i] = uc[i] + vc[i];
    });

    sim.run(STEPS);

    constexpr int expected_v = static_cast<int>(STEPS);             // 10
    constexpr int expected_u = static_cast<int>(STEPS * (STEPS - 1) / 2); // 45
    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(v[i], expected_v);
        ASSERT_EQ(u[i], expected_u);
    }
}

/**
 * @brief Tests that mutating the graph recompiles the plan.
 *
 * The next run recompiles with the added rule, and existing state continues from where it left off.
 */
TEST(Integration_FluentLifecycle, MutationRecompiles)
{
    constexpr std::size_t N = 128;

    auto rt = Runtime::create();
    auto a  = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i) a[i] = 0;

    auto sim = rt.graph();
    sim.add(a, [](std::size_t i, const int* c, int* n) { n[i] = c[i] + 1; });
    sim.run(3); // a == 3

    auto b = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i) b[i] = 0;
    sim.add(b, [](std::size_t i, const int* c, int* n) { n[i] = c[i] + 2; });
    sim.run(2); // recompiles; a += 2 -> 5, b == 4

    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(a[i], 5);
        ASSERT_EQ(b[i], 4);
    }
}

/**
 * @brief Tests that an empty graph is reported as invalid.
 *
 * The non-throwing try_run() channel reports an invalid_graph error without throwing an exception.
 */
TEST(Integration_FluentLifecycle, EmptyGraphIsInvalid)
{
    auto rt  = Runtime::create();
    auto sim = rt.graph();

    auto r = sim.try_run();
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, Errc::invalid_graph);
}

/**
 * @brief A graph that outlives a Buffer it recorded fails cleanly, not with UB.
 *
 * The graph tracks the liveness of every typed handle it touches, so a run after
 * the Buffer was freed returns a dangling_resource error through try_run() instead
 * of dispatching a kernel against released memory.
 */
TEST(Integration_FluentLifecycle, DanglingResourceIsReported)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create();
    auto sim = rt.graph();

    {
        auto buf = rt.buffer<int>(N);
        int* p = buf.data();
        sim.add(Reads(), Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); });
    }   // buf is destroyed here, before the graph runs

    auto r = sim.try_run();
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, Errc::dangling_resource);
}
