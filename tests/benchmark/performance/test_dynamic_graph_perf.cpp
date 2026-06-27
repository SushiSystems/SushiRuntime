/**************************************************************************/
/* test_dynamic_graph_perf.cpp                                            */
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

// Scale probe for the dynamic region graph: many regions stream in, a few mutate
// between steps, and the world keeps running correctly. It asserts the
// composition contract at scale (one recompose per mutation, not per region) and
// reports the streaming cost. The first version's wiring pass is global, so this
// does not assert a recompose-speedup; it guards correctness and scale.

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

    // Stream a fresh task into a handful of regions, one mutation per run. Each
    // run must recompose exactly once regardless of how many regions exist.
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

    // Correctness survives streaming: every region ran the initial step plus one
    // step per run, and the mutated regions gained an extra increment on the run
    // that streamed their second task in.
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
