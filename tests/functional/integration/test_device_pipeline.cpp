/****************************************************************/
/* test_device_pipeline.cpp                                     */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

// Exercises the genuine device-kernel path: add_node(TaskWork, reads, writes)
// hands the engine a raw `void(sycl::handler&)` body, so the engine itself owns
// the submission, queue choice, and event wiring. The graph is a diamond:
//
//        init (write A)
//         /          \
//   doubleA          incA      (both read A -> parallel readers)
//   (A->B)            (A->C)
//         \          /
//        combine (read B,C -> write D)
//
// With A[i] = i the invariant is D[i] = 2*A[i] + (A[i] + 1) = 3i + 1.

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
    /**
     * @brief Checks if NUMA node 0 maps to a single device queue.
     *
     * The engine round-robins KERNEL nodes across a node's queues, so with more than one queue the nodes scatter across contexts and shared USM is no longer universally valid.
     * @param ctx The runtime context to query for NUMA queues.
     * @return True if NUMA node 0 has exactly one device queue, false otherwise.
     */
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
 * @brief Tests an engine-driven device kernel diamond pipeline.
 *
 * Verifies the engine resolves the RAW fan-out, keeps the readers parallel, joins the fan-in, and produces correct results across every buffer.
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
    ASSERT_TRUE(A);
    ASSERT_TRUE(B);
    ASSERT_TRUE(C);
    ASSERT_TRUE(D);

    float* a = A.get();
    float* b = B.get();
    float* c = C.get();
    float* d = D.get();

    TaskGraph graph(ctx);

    // init: A[i] = i
    graph.add_node(
        [a](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { a[i] = float(i); });
        },
        {}, {a});

    // doubleA: B[i] = A[i] * 2
    graph.add_node(
        [a, b](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { b[i] = a[i] * 2.0f; });
        },
        {a}, {b});

    // incA: C[i] = A[i] + 1  (parallel sibling of doubleA)
    graph.add_node(
        [a, c](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { c[i] = a[i] + 1.0f; });
        },
        {a}, {c});

    // combine: D[i] = B[i] + C[i]  (fan-in)
    graph.add_node(
        [b, c, d](sycl::handler& h)
        {
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i) { d[i] = b[i] + c[i]; });
        },
        {b, c}, {d});

    auto compiled = graph.compile();
    EXPECT_EQ(compiled.roots.size(), 1u) << "Only the init node should be a root of the diamond.";

    ctx.get_execution_engine().execute(std::move(compiled));
    ctx.wait_all();

    bool ok = true;
    for (size_t i = 0; i < N; ++i)
    {
        const float fi = float(i);
        if (std::abs(b[i] - (fi * 2.0f)) > 1e-3f)
        {
            ok = false;
            break;
        }
        if (std::abs(c[i] - (fi + 1.0f)) > 1e-3f)
        {
            ok = false;
            break;
        }
        if (std::abs(d[i] - (3.0f * fi + 1.0f)) > 1e-3f)
        {
            ok = false;
            break;
        }
    }
    EXPECT_TRUE(ok) << "Engine-driven device-kernel diamond produced incorrect results.";
}
