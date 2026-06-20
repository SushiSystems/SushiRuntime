/**************************************************************************/
/* test_fluent_lifecycle.cpp                                              */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

#include <cstddef>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// Replay + double-buffer: one update rule increments every element by 1 each
// step. After K steps every element must equal K. That only holds if the
// compiled plan is replayed with the per-node in_degree reset between steps,
// and the State is double-buffered so each step reads the previous field and
// writes the next one. A broken reset would deadlock or skip steps; a broken
// swap would read stale data and diverge from K.
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

// Replay must also work across separate run() calls: the second run resets the
// node state the first run consumed. 10 then 5 steps must leave every element
// at 15.
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

// run_until checks its stop condition before each step and sees the State the
// previous step produced, so it must stop exactly when field[0] reaches 20.
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

// Coupled fields: u reads v, v evolves on its own. Both rules read the previous
// step's data (Jacobi), so after K steps v == K and u == sum_{j=0}^{K-1} j. If
// the swap happened per-rule instead of after the whole step, u would read v's
// updated value and diverge — so this also pins down the advance-after-step rule.
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

// Mutating the graph after a run invalidates the cached plan: the next run
// recompiles with the added rule, and existing state continues from where it
// left off.
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

// A graph with no tasks is invalid and the non-throwing channel reports it
// without throwing.
TEST(Integration_FluentLifecycle, EmptyGraphIsInvalid)
{
    auto rt  = Runtime::create();
    auto sim = rt.graph();

    auto r = sim.try_run();
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code, Errc::invalid_graph);
}
