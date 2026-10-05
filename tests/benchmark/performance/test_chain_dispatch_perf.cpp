/****************************************************************/
/* test_chain_dispatch_perf.cpp                                 */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_chain_dispatch_perf.cpp
 * @brief Measures the per-node dispatch cost of a dependency chain across chain lengths.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    struct Point { double ms_per_step; double ns_per_node_step; Core::ExecutionLayer layer; };

    /**
     * @brief Runs a chain of @p length trivial kernel nodes for @p steps replays.
     * @param force_scheduler True to keep the plan on the scheduler (layer 0).
     * @return The per-step and per-node wall cost, and the layer that ran.
     */
    Point run_chain(std::size_t length, std::size_t steps, bool force_scheduler)
    {
        API::RuntimeConfig config;
        config.force_scheduler = force_scheduler;
        auto rt = Runtime::create({}, config);
        rt.rebalancer(false);

        std::vector<Buffer<int>> bufs;
        bufs.reserve(length + 1);
        for (std::size_t i = 0; i <= length; ++i)
            bufs.push_back(rt.buffer<int>(1));
        bufs[0][0] = 0;

        auto g = rt.graph();
        for (std::size_t i = 0; i < length; ++i)
        {
            const int* in = bufs[i].data();
            int* out      = bufs[i + 1].data();
            // Trivial work: the cost under measurement is dispatch, not compute.
            g.add(API::Reads(bufs[i]), API::Writes(bufs[i + 1]), std::size_t{1},
                  [in, out](std::size_t) { out[0] = in[0] + 1; });
        }

        const auto report = g.run(steps); // compile once, replay `steps` times
        EXPECT_EQ(g.compile_count(), 1u);
        EXPECT_EQ(bufs[length][0], static_cast<int>(length))
            << "chain of length " << length << " did not settle to the expected value";

        const double ms_per_step = report.total_duration_ms / double(steps);
        const double ns_per_node_step =
            (report.total_duration_ms * 1e6) / double(steps) / double(length);
        return { ms_per_step, ns_per_node_step, report.layer };
    }
}

/**
 * @brief Tests Graph::run() per-node wall cost over a chain of dependent kernel nodes.
 *
 * Sweeps the chain length and reports ms/step and ns/node, the baseline the
 * graph-replay design's layers are measured against (design spec §0, §G).
 */
TEST(Performance_ChainDispatch, PerNodeCostVsChainLength)
{
    // Fewer steps at large K keeps the sweep quick while still amortizing setup.
    struct Case { std::size_t length; std::size_t steps; };
    const Case cases[] = {
        {1,    20000},
        {16,   5000},
        {256,  500},
        {1024, 100},
    };

    std::printf("\n=== Chain dispatch cost (dependent-node chain) ===\n");
    std::printf("  %8s | %9s | %12s | %14s\n", "length", "layer", "ms/step", "ns/(node*step)");
    for (const auto& c : cases)
    {
        for (const bool force_scheduler : { true, false })
        {
            const Point pt = run_chain(c.length, c.steps, force_scheduler);
            const char* layer = pt.layer == Core::ExecutionLayer::Chain ? "chain" : "scheduler";
            std::printf("  %8zu | %9s | %12.5f | %14.1f\n",
                        c.length, layer, pt.ms_per_step, pt.ns_per_node_step);
            EXPECT_GT(pt.ms_per_step, 0.0);
        }
    }
    std::printf("  -> scheduler rows are layer 0, chain rows layer 1 (design spec §C);\n"
                "     both are compared with the probe's columns in §0.\n");
}
