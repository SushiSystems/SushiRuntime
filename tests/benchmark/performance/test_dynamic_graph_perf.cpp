/****************************************************************/
/* test_dynamic_graph_perf.cpp                                  */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_dynamic_graph_perf.cpp
 * @brief Tests that a 256-region dynamic graph recomposes once per mutation and stays correct.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

#include <chrono>
#include <cstddef>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

TEST(Performance_DynamicGraph, ManyRegionsStreamingMutations)
{
    constexpr std::size_t N        = 8;
    constexpr std::size_t REGIONS  = 256;
    constexpr std::size_t MUTATES  = 16;   // distinct regions touched between runs

    auto rt = Runtime::create();
    rt.rebalancer(false);

    std::vector<Buffer<int>> bufs;
    bufs.reserve(REGIONS);
    for (std::size_t r = 0; r < REGIONS; ++r)
        bufs.push_back(rt.buffer<int>(N));
    for (auto& buf : bufs)
        for (std::size_t i = 0; i < N; ++i)
            buf[i] = 0;

    auto world = rt.dynamic_graph();
    for (std::size_t r = 0; r < REGIONS; ++r)
    {
        int* p = bufs[r].data();
        world.region(r).add(API::Reads{}, API::Writes(bufs[r]), N,
                            [p](std::size_t i) { p[i] += 1; });
    }

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    world.run(1);
    const auto t1 = clock::now();
    EXPECT_EQ(world.compile_count(), 1u);

    // Each run recomposes exactly once, whatever the region count.
    for (std::size_t m = 0; m < MUTATES; ++m)
    {
        const std::size_t r = (m * 7 + 3) % REGIONS;
        int* p = bufs[r].data();
        world.region(r).add(API::Reads{}, API::Writes(bufs[r]), N,
                            [p](std::size_t i) { p[i] += 1; });
        world.run(1);
        EXPECT_EQ(world.compile_count(), 2u + m) << "one recompose per mutation at m=" << m;
    }

    const double initial_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    RecordProperty("regions", static_cast<int>(REGIONS));
    RecordProperty("initial_compose_run_ms", static_cast<int>(initial_ms));

    // A region counts every run, plus one per run after its second task streamed in.
    const std::size_t total_runs = 1 + MUTATES;
    for (std::size_t r = 0; r < REGIONS; ++r)
    {
        std::size_t extra = 0;
        for (std::size_t m = 0; m < MUTATES; ++m)
            if ((m * 7 + 3) % REGIONS == r)
                extra += (MUTATES - m);  // the second task ran on every run after it was added
        const int expected = static_cast<int>(total_runs + extra);
        for (std::size_t i = 0; i < N; ++i)
            ASSERT_EQ(bufs[r][i], expected) << "region " << r << " at i=" << i;
    }
}
