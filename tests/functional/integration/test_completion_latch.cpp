/****************************************************************/
/* test_completion_latch.cpp                                    */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_completion_latch.cpp
 * @brief Tests that the reusable completion latch drives a replayed run and re-runs on one plan.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

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

    // The latch resets between the three run() calls and the sentinel still signals; see README.md.
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

    // Each single-step run goes through one reset, arm and wait cycle of the latch; see README.md.
    for (int r = 0; r < 200; ++r)
    {
        auto rep = g.run();
        ASSERT_TRUE(rep.is_successful) << "run " << r;
    }
    for (std::size_t i = 0; i < N; ++i)
        ASSERT_EQ(buf[i], static_cast<int>(i));
}
