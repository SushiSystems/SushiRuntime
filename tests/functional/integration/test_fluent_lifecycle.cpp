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
