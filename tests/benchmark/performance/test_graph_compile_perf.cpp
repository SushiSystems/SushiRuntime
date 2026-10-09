/**************************************************************************/
/* test_graph_compile_perf.cpp                                            */
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
 * @file test_graph_compile_perf.cpp
 * @brief Measures how TaskGraph::compile() scales and what overlapping transfers with compute saves.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#include <SushiRuntime/graph/node.hpp> // CompiledGraph holds sushi_ptr<Node>; dtor needs full type

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;

namespace
{
    constexpr int ROUNDS = 3;

    /**
     * @brief Measures the minimum compilation time for a task graph.
     * 
     * @param ctx The runtime context.
     * @param nodes The number of nodes in the graph.
     * @return The minimum compilation time in microseconds.
     */
    double min_compile_us(RuntimeContext& ctx, size_t nodes)
    {
        // Task t reads buf[t % M] and writes buf[(t + 1) % M], so consecutive tasks depend.
        constexpr size_t M = 16;
        std::vector<int> bufs(M, 0);

        double best = 1e300;
        for (int r = 0; r < ROUNDS; ++r)
        {
            TaskGraph graph(ctx);
            for (size_t t = 0; t < nodes; ++t)
            {
                void* in  = &bufs[t % M];
                void* out = &bufs[(t + 1) % M];
                TaskMetadata meta{"n"};
                graph.add_task(meta, {in}, {out},
                    [](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event { return {}; });
            }

            const auto t0 = std::chrono::steady_clock::now();
            auto compiled = graph.compile();
            const double us = std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - t0).count();
            EXPECT_GE(compiled.roots.size(), 1u); // consume: compile must produce a DAG
            best = std::min(best, us);
        }
        return best;
    }
}

/**
 * @brief Tests TaskGraph::compile() latency scaling.
 * 
 * Measures compile() latency and us/node across increasing graph sizes.
 */
TEST(Performance_GraphCompileBench, CompileScaling)
{
    RuntimeContext ctx;
    const size_t sizes[] = { 64, 256, 1024, 4096, 16384 };

    std::printf("\n=== TaskGraph::compile() scaling ===\n");
    std::printf("  %8s | %12s | %10s\n", "nodes", "compile (us)", "us/node");
    for (size_t n : sizes)
    {
        const double us = min_compile_us(ctx, n);
        std::printf("  %8zu | %12.1f | %10.3f\n", n, us, us / double(n));
        EXPECT_GT(us, 0.0);
    }
}

// H2D / D2H transfer-compute overlap.
namespace
{
    constexpr size_t ELEMS  = 1u << 20; // 4 MiB of floats
    constexpr int    INNER  = 64;       // per-element compute weight
    constexpr int    CHUNKS = 8;

    /**
     * @brief Reference compute per element, matching the device kernel.
     * 
     * @param x The input value.
     * @return The computed result.
     */
    float expected(float x)
    {
        for (int k = 0; k < INNER; ++k)
            x = x * 1.001f + 0.5f;
        return x;
    }

    /**
     * @brief Run a function multiple times and return the best execution time.
     * 
     * @param fn The function to run.
     * @return The best time in milliseconds.
     */
    double best_of(const std::function<double()>& fn)
    {
        double best = 1e300;
        for (int r = 0; r < ROUNDS; ++r)
            best = std::min(best, fn());
        return best;
    }
}

/**
 * @brief Tests H2D/D2H transfer-compute overlap.
 * 
 * Compares sequential (copy -> compute -> copy) vs a chunked pipeline across two queues.
 */
TEST(Performance_GraphCompileBench, TransferComputeOverlapSpeedup)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    auto ctx = std::make_unique<RuntimeContext>();

    sycl::queue& base = ctx->get_queue(0);
    sycl::device dev  = base.get_device();
    sycl::context cxt = base.get_context();
    sycl::queue qa(cxt, dev, sycl::property::queue::in_order{});
    sycl::queue qb(cxt, dev, sycl::property::queue::in_order{});

    const size_t bytes = ELEMS * sizeof(float);
    float* host_in  = sycl::malloc_host<float>(ELEMS, qa);
    float* host_out = sycl::malloc_host<float>(ELEMS, qa);
    float* d        = sycl::malloc_device<float>(ELEMS, qa);
    ASSERT_NE(host_in, nullptr);
    ASSERT_NE(host_out, nullptr);
    ASSERT_NE(d, nullptr);
    for (size_t i = 0; i < ELEMS; ++i)
        host_in[i] = float(i % 97);

    auto compute = [](sycl::queue& q, float* p, size_t off, size_t n, sycl::event dep) -> sycl::event
    {
        return q.parallel_for(sycl::range<1>(n), dep, [=](sycl::id<1> i)
        {
            float x = p[off + i];
            for (int k = 0; k < INNER; ++k)
                x = x * 1.001f + 0.5f;
            p[off + i] = x;
        });
    };

    // Sequential: one blocking copy -> compute -> copy.
    auto run_sequential = [&]() -> double
    {
        const auto t0 = std::chrono::steady_clock::now();
        qa.memcpy(d, host_in, bytes).wait();
        compute(qa, d, 0, ELEMS, {}).wait();
        qa.memcpy(host_out, d, bytes).wait();
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
    };

    // Overlapped: chunks pipelined across two in-order queues.
    auto run_overlapped = [&]() -> double
    {
        const size_t chunk = (ELEMS + CHUNKS - 1) / CHUNKS;
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<sycl::event> tail;
        for (int c = 0; c < CHUNKS; ++c)
        {
            const size_t off = size_t(c) * chunk;
            if (off >= ELEMS)
                break;
            const size_t n = std::min(chunk, ELEMS - off);
            sycl::queue& q = (c & 1) ? qb : qa;
            auto e1 = q.memcpy(d + off, host_in + off, n * sizeof(float));
            auto e2 = compute(q, d, off, n, e1);
            auto e3 = q.memcpy(host_out + off, d + off, n * sizeof(float), e2);
            tail.push_back(e3);
        }
        for (auto& e : tail)
            e.wait();
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
    };

    const double seq = best_of(run_sequential);
    bool ok = true;
    for (size_t i = 0; i < ELEMS; ++i)
    {
        if (std::fabs(host_out[i] - expected(host_in[i])) > 1e-1f)
        {
            ok = false;
            break;
        }
    }

    const double ovl = best_of(run_overlapped);
    bool ok2 = true;
    for (size_t i = 0; i < ELEMS; ++i)
    {
        if (std::fabs(host_out[i] - expected(host_in[i])) > 1e-1f)
        {
            ok2 = false;
            break;
        }
    }

    const double speedup = ovl > 0 ? seq / ovl : 0.0;
    std::printf("\n=== H2D/D2H transfer-compute overlap (%zu elems, %d chunks) ===\n",
                ELEMS, CHUNKS);
    std::printf("  Sequential : %.3f ms\n", seq);
    std::printf("  Overlapped : %.3f ms\n", ovl);
    std::printf("  Speedup    : %.3fx (>1 needs a discrete GPU w/ copy engines)\n", speedup);

    EXPECT_TRUE(ok)  << "Sequential transfer/compute produced a wrong result.";
    EXPECT_TRUE(ok2) << "Overlapped transfer/compute produced a wrong result.";
    EXPECT_GT(speedup, 0.0);

    sycl::free(host_in, qa);
    sycl::free(host_out, qa);
    sycl::free(d, qa);
}
