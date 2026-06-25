/**************************************************************************/
/* test_completion_latch.cpp                                              */
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

// §5.1 coverage: the reusable completion latch. execute() no longer allocates a
// promise/future per step; one latch on the engine is reset, armed, and waited on
// each step. These tests pin the behaviour that change must preserve: a long
// replayed run drives every step and reaches the right state, and the same plan
// re-runs cleanly afterwards (the latch resets and the sentinel re-wires correctly).

#include <cstddef>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

TEST(Integration_CompletionLatch, ManyStepReplayReachesCorrectState)
{
    constexpr std::size_t N = 1024;
    constexpr std::size_t STEPS = 5000;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i) field[i] = 0;

    auto g = rt.graph();
    // Each step adds 1 to every cell: after STEPS steps every cell equals STEPS.
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    auto rep = g.run(STEPS);

    EXPECT_TRUE(rep.is_successful);
    EXPECT_FALSE(rep.cancelled);
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(field[i], static_cast<int>(STEPS)) << "at i=" << i;
}

TEST(Integration_CompletionLatch, ReRunsCleanlyOnTheSamePlan)
{
    constexpr std::size_t N = 256;

    auto rt    = Runtime::create();
    auto field = rt.state<int>(N);
    for (std::size_t i = 0; i < N; ++i) field[i] = 0;

    auto g = rt.graph();
    g.add(field, [](std::size_t i, const int* cur, int* next) { next[i] = cur[i] + 1; });

    // Three separate run() calls on one compiled plan: the latch must reset between
    // them and the once-wired sentinel must keep signalling.
    g.run(10);
    g.run(10);
    g.run(5);

    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(field[i], 25) << "at i=" << i;
}

TEST(Integration_CompletionLatch, SingleStepRunsRepeatedlyWithoutLeak)
{
    constexpr std::size_t N = 64;

    auto rt  = Runtime::create();
    auto buf = rt.buffer<int>(N);
    int* p = buf.data();

    auto g = rt.graph();
    g.add(API::Reads{}, API::Writes(buf), N, [p](std::size_t i) { p[i] = static_cast<int>(i); });

    // Many independent single-step runs stress the reset/arm/wait cycle the most:
    // each one allocates nothing on the completion path now.
    for (int r = 0; r < 200; ++r)
    {
        auto rep = g.run();
        ASSERT_TRUE(rep.is_successful) << "run " << r;
    }
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i));
}
