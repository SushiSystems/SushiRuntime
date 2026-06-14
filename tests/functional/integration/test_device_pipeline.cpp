/**************************************************************************/
/* test_device_pipeline.cpp                                             */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

/**
 * @file test_device_pipeline.cpp
 * @brief End-to-end test of the genuine DEVICE-KERNEL graph path.
 *
 * The other graph tests (SyclExecutionTest, HeterogeneousPipelineTest) submit
 * their kernels from inside a HostWork lambda that captures and drives its own
 * queue. That exercises ordering, but the kernel is launched by the test, not by
 * the runtime. This test instead uses TaskGraph::add_node(TaskWork, reads,
 * writes): the node carries a raw `void(sycl::handler&)` body and the
 * ExecutionEngine itself owns the submission — it picks the queue, wires the
 * predecessor SYCL events via handler.depends_on(), and runs the node in KERNEL
 * mode (TaskScheduler::execute_task -> q.submit -> node->execute(h)). So this is
 * the real device-kernel pipeline the public kernel API produces.
 *
 * Topology shape (a diamond, to cover ordering + fan-out + fan-in in one graph):
 *
 *        init (write A)
 *         /          \
 *   doubleA          incA          (both READ A -> parallel readers, RAR)
 *   (A->B)            (A->C)
 *         \          /
 *        combine (read B,C -> write D)
 *
 * Result invariant: D[i] = 2*A[i] + (A[i] + 1) with A[i] = i, i.e. D[i] = 3i+1.
 *
 * USM is context-bound. The engine routes KERNEL nodes through
 * get_numa_local_queue(0), which round-robins when NUMA node 0 exposes several
 * device queues (e.g. CPU + GPU). When that happens, consecutive nodes can land
 * on different contexts and a buffer allocated on one is invalid on another, so
 * the test SKIPS that configuration (CPU/GPU overlap is covered separately by
 * the heterogeneous and async-overlap suites). On a single-context node (e.g.
 * CPU-only CI) every node shares one queue and the pipeline runs fully.
 */

#include <cmath>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/graph/task_types.hpp>
#include <SushiRuntime/graph/node.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;
using SushiRuntime::Test::UsmShared;

namespace
{
    // True when NUMA node 0 maps to a single device queue. The engine round-robins
    // KERNEL nodes across a node's queues via a per-thread cursor, so if more than
    // one queue exists, nodes scatter across contexts and shared USM allocated on a
    // single queue is no longer universally valid.
    bool numa0_has_single_queue(RuntimeContext& ctx)
    {
        sycl::queue& first = ctx.get_numa_local_queue(0);
        for (int i = 0; i < 6; ++i)
            if (&ctx.get_numa_local_queue(0) != &first)
                return false;
        return true;
    }
}

/**
 * @test DiamondAllKernelNodes
 * @brief A four-stage diamond where EVERY node is a device kernel submitted by
 * the engine (not by the test). Verifies the engine resolves the RAW fan-out
 * (init -> doubleA, init -> incA), keeps the two readers parallel, joins the
 * fan-in (doubleA, incA -> combine), and produces correct results across every
 * intermediate and final buffer.
 */
TEST(Integration_DevicePipelineTest, DiamondAllKernelNodes)
{
    RuntimeContext ctx;

    if (!numa0_has_single_queue(ctx))
        GTEST_SKIP() << "NUMA 0 exposes multiple device queues; the engine would scatter "
                        "KERNEL nodes across USM contexts. Cross-device paths are covered "
                        "by HeterogeneousPipelineTest / AsyncOverlapBench.";

    sycl::queue& q = ctx.get_numa_local_queue(0);

    constexpr size_t N = 1024;
    UsmShared<float> A(q, N), B(q, N), C(q, N), D(q, N);
    ASSERT_TRUE(A); ASSERT_TRUE(B); ASSERT_TRUE(C); ASSERT_TRUE(D);

    float* a = A.get();
    float* b = B.get();
    float* c = C.get();
    float* d = D.get();

    TaskGraph graph(ctx);

    // Stage 1 — init: A[i] = i  (device kernel, writes A)
    graph.add_node(
        [a](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { a[i] = float(i); });
        },
        /*reads*/{}, /*writes*/{a});

    // Stage 2a — doubleA: B[i] = A[i] * 2  (reads A, writes B)
    graph.add_node(
        [a, b](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { b[i] = a[i] * 2.0f; });
        },
        /*reads*/{a}, /*writes*/{b});

    // Stage 2b — incA: C[i] = A[i] + 1  (reads A, writes C) — parallel sibling of doubleA
    graph.add_node(
        [a, c](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { c[i] = a[i] + 1.0f; });
        },
        /*reads*/{a}, /*writes*/{c});

    // Stage 3 — combine: D[i] = B[i] + C[i]  (reads B and C, writes D) — fan-in
    graph.add_node(
        [b, c, d](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { d[i] = b[i] + c[i]; });
        },
        /*reads*/{b, c}, /*writes*/{d});

    auto compiled = graph.compile();
    EXPECT_EQ(compiled.roots.size(), 1u) << "Only the init node should be a root of the diamond.";

    ctx.get_execution_engine().execute(std::move(compiled));
    ctx.wait_all(); // ensure all device kernels retired before host reads

    bool ok = true;
    for (size_t i = 0; i < N; ++i)
    {
        const float fi = float(i);
        if (std::abs(b[i] - (fi * 2.0f))        > 1e-3f) { ok = false; break; } // doubleA
        if (std::abs(c[i] - (fi + 1.0f))        > 1e-3f) { ok = false; break; } // incA
        if (std::abs(d[i] - (3.0f * fi + 1.0f)) > 1e-3f) { ok = false; break; } // combine
    }
    EXPECT_TRUE(ok) << "Engine-driven device-kernel diamond produced incorrect results.";
}
