/**************************************************************************/
/* test_root_fanout_perf.cpp                                              */
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
 * @file test_root_fanout_perf.cpp
 * @brief Measures the per-step and per-root cost of replaying a graph of independent roots.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    struct Point { double ms_per_step; double ns_per_root_step; };

    Point run_fanout(std::size_t roots, std::size_t steps)
    {
        auto rt = Runtime::create();
        rt.rebalancer(false);

        std::vector<Buffer<int>> bufs;
        bufs.reserve(roots);
        for (std::size_t r = 0; r < roots; ++r)
            bufs.push_back(rt.buffer<int>(1));
        for (auto& b : bufs) b[0] = 0;

        auto g = rt.graph();
        for (std::size_t r = 0; r < roots; ++r)
        {
            int* p = bufs[r].data();
            // Each task touches only its own buffer -> independent -> a root.
            g.add(API::Reads{}, API::Writes(bufs[r]), 1,
                  [p](std::size_t) { p[0] += 1; });
        }

        const auto report = g.run(steps);      // compile once, replay `steps` times
        EXPECT_EQ(g.compile_count(), 1u);

        // Every root ran exactly `steps` times.
        for (std::size_t r = 0; r < roots; ++r)
            EXPECT_EQ(bufs[r][0], static_cast<int>(steps))
                << "root " << r << " did not run every step";

        const double ms_per_step = report.total_duration_ms / double(steps);
        const double ns_per_root_step =
            (report.total_duration_ms * 1e6) / double(steps) / double(roots);
        return { ms_per_step, ns_per_root_step };
    }
}

TEST(Performance_RootFanout, InjectionCostVsRootCount)
{
    // Keep step*root work bounded so the sweep stays quick; fewer steps at high R.
    struct Case { std::size_t roots; std::size_t steps; };
    const Case cases[] = {
        {1,    20000},
        {16,   10000},
        {256,  2000},
        {1024, 500},
    };

    std::printf("\n=== Root fan-out injection sweep (batched submit) ===\n");
    std::printf("  %8s | %12s | %16s\n", "roots", "ms/step", "ns/(root*step)");
    for (const auto& c : cases)
    {
        const Point pt = run_fanout(c.roots, c.steps);
        std::printf("  %8zu | %12.5f | %16.1f\n",
                    c.roots, pt.ms_per_step, pt.ns_per_root_step);
        EXPECT_GT(pt.ms_per_step, 0.0);
    }
    std::printf("  -> the per-step injection cost is now one lock + one wake for the\n"
                "     whole root set; ns/(root*step) trending down as R grows means the\n"
                "     injector is no longer scaling with fan-out (ds-1 working).\n");
}
