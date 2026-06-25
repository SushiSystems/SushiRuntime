/**************************************************************************/
/* test_late_bindings.cpp                                                 */
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

// WP-1 coverage: the persistent-graph / late-binding path. A graph whose live
// size or whose per-step participation changes between steps must be compiled
// exactly once and replayed, never recompiled. Each test asserts both the
// numerical result and Graph::compile_count() == 1.

#include <cstddef>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

// sized() shrinks the iteration range to a live prefix each step. Driving the
// live count from 1..CAP over CAP successive run(1) calls increments buf[i]
// exactly on the steps where i is inside the live range, so buf[i] == CAP - i.
// The plan must compile once and follow the changing size on replay.
TEST(Integration_LateBindings, DynamicSizePerElement)
{
    constexpr std::size_t CAP = 8;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(CAP);
    for (std::size_t i = 0; i < CAP; ++i)
        buf[i] = 0;

    int* p = buf.data();

    std::size_t live = 0;
    auto g = rt.graph();
    g.add(API::sized([&] { return live; }),
          API::Reads{}, API::Writes(buf), CAP,
          [p](std::size_t i) { p[i] += 1; });

    for (std::size_t k = 1; k <= CAP; ++k)
    {
        live = k;
        g.run();
    }

    EXPECT_EQ(g.compile_count(), 1u) << "size changes must not recompile.";
    for (std::size_t i = 0; i < CAP; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(CAP - i)) << "at i=" << i;
}

// when() gates whether the node runs at all. Toggling the predicate on/off over
// ten steps runs the increment only on the enabled steps, so each element ends
// at the count of enabled steps — with a single compile.
TEST(Integration_LateBindings, EnableTogglePerElement)
{
    constexpr std::size_t N     = 16;
    constexpr std::size_t STEPS = 10;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    for (std::size_t i = 0; i < N; ++i)
        buf[i] = 0;

    int* p = buf.data();

    bool on = true;
    auto g = rt.graph();
    g.add(API::when([&] { return on; }),
          API::Reads{}, API::Writes(buf), N,
          [p](std::size_t i) { p[i] += 1; });

    std::size_t enabled_steps = 0;
    for (std::size_t k = 0; k < STEPS; ++k)
    {
        on = (k % 2 == 0);          // enabled on even steps only
        if (on) ++enabled_steps;
        g.run();
    }

    EXPECT_EQ(g.compile_count(), 1u) << "enable toggles must not recompile.";
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(enabled_steps)) << "at i=" << i;
}

// A disabled step must release its successors so the rest of the graph still
// runs. A downstream task that always runs should advance every step regardless
// of the upstream node's enable state, proving instant-complete drains the DAG.
TEST(Integration_LateBindings, DisabledNodeStillReleasesSuccessors)
{
    constexpr std::size_t N     = 8;
    constexpr std::size_t STEPS = 6;

    auto rt   = Runtime::create();
    auto gate = rt.buffer<int>(N);   // written by the toggled upstream node
    auto sink = rt.buffer<int>(N);   // written by the always-on downstream node
    for (std::size_t i = 0; i < N; ++i) { gate[i] = 0; sink[i] = 0; }

    int* gp = gate.data();
    int* sp = sink.data();

    bool on = false;
    auto g = rt.graph();
    // Upstream writes gate; downstream reads gate and writes sink, so the
    // tracker orders downstream after upstream on the gate buffer.
    g.add(API::when([&] { return on; }),
          API::Reads{}, API::Writes(gate), N,
          [gp](std::size_t i) { gp[i] += 1; });
    g.add(API::Reads(gate), API::Writes(sink), N,
          [sp](std::size_t i) { sp[i] += 1; });

    for (std::size_t k = 0; k < STEPS; ++k)
        g.run();                     // upstream stays disabled the whole run

    EXPECT_EQ(g.compile_count(), 1u);
    for (std::size_t i = 0; i < N; ++i)
    {
        ASSERT_EQ(gate[i], 0) << "disabled upstream must not run, at i=" << i;
        ASSERT_EQ(sink[i], static_cast<int>(STEPS))
            << "downstream must run every step, at i=" << i;
    }
}

// sized() on a stepped State: the live prefix grows each step while the graph is
// compiled once. Cells active on every step accumulate one increment per step,
// which is what a reference single-threaded loop would produce for that prefix.
TEST(Integration_LateBindings, DynamicSizeState)
{
    constexpr std::size_t CAP   = 8;
    constexpr std::size_t STEPS = 4;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(CAP);
    for (std::size_t i = 0; i < CAP; ++i)
        field[i] = 0;

    std::size_t live = 0;
    auto g = rt.graph();
    g.add(API::sized([&] { return live; }), field,
          [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    // Grow the active prefix 2,4,6,8; the first two cells are active every step.
    for (std::size_t k = 0; k < STEPS; ++k)
    {
        live = 2 * (k + 1);
        g.run();
    }

    EXPECT_EQ(g.compile_count(), 1u) << "state size changes must not recompile.";
    EXPECT_EQ(field[0], static_cast<int>(STEPS));
    EXPECT_EQ(field[1], static_cast<int>(STEPS));
}

// when() and sized() compose: the node runs only while enabled, and when it
// runs it iterates the live count. Over the run, an element is incremented once
// per step that is both enabled and within range — still a single compile.
TEST(Integration_LateBindings, ComposedWhenAndSized)
{
    constexpr std::size_t CAP   = 4;
    constexpr std::size_t STEPS = 8;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(CAP);
    for (std::size_t i = 0; i < CAP; ++i)
        buf[i] = 0;

    int* p = buf.data();

    bool on = true;
    std::size_t live = CAP;
    auto g = rt.graph();
    g.add(API::when([&] { return on; }).and_sized([&] { return live; }),
          API::Reads{}, API::Writes(buf), CAP,
          [p](std::size_t i) { p[i] += 1; });

    int expected0 = 0;               // increments seen by element 0
    for (std::size_t k = 0; k < STEPS; ++k)
    {
        on   = (k % 3 != 0);         // disabled every third step
        live = (k % 2 == 0) ? CAP : 2;
        if (on && live > 0) ++expected0;   // element 0 is in range whenever live > 0
        g.run();
    }

    EXPECT_EQ(g.compile_count(), 1u);
    EXPECT_EQ(buf[0], expected0);
}
