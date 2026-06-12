/**************************************************************************/
/* test_sycl_execution.cpp                                                */
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

/**
 * @test EndToEndVectorAddition
 * @brief Performs a full end-to-end test of the compute runtime.
 * Allocates USM memory, dispatches an initialization host task,
 * dispatches a SYCL device kernel for vector addition, and finally
 * verifies the results on the host.
 */
TEST(SyclExecutionTest, EndToEndVectorAddition)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    sycl::queue& q = ctx.get_numa_local_queue(0);

    constexpr size_t VECTOR_SIZE = 1024;

    UsmShared<float> usm_A(q, VECTOR_SIZE);
    UsmShared<float> usm_B(q, VECTOR_SIZE);
    UsmShared<float> usm_C(q, VECTOR_SIZE);

    ASSERT_TRUE(usm_A);
    ASSERT_TRUE(usm_B);
    ASSERT_TRUE(usm_C);

    float* A = usm_A.get();
    float* B = usm_B.get();
    float* C = usm_C.get();

    // Node 1: Host Task to Initialize Vectors
    TaskMetadata meta_init{"Init_Vectors"};
    auto init_work = [A, B, C](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        for (size_t i = 0; i < VECTOR_SIZE; ++i)
        {
            A[i] = static_cast<float>(i);
            B[i] = static_cast<float>(i * 2);
            C[i] = 0.0f;
        }
        return {};
    };
    graph.add_task(meta_init, {}, {A, B, C}, init_work);

    // Node 2: Device Task (SYCL Kernel) for Vector Addition
    TaskMetadata meta_compute{"Vector_Add_Kernel"};
    auto compute_work = [A, B, C](sycl::queue& queue, const std::vector<sycl::event>& deps) -> sycl::event
    {
        return queue.submit([&](sycl::handler& h)
        {
            h.depends_on(deps);
            h.parallel_for(sycl::range<1>(VECTOR_SIZE), [=](sycl::id<1> idx)
            {
                C[idx] = A[idx] + B[idx];
            });
        });
    };
    graph.add_task(meta_compute, {A, B}, {C}, compute_work);

    auto compiled = graph.compile();
    ctx.get_execution_engine().execute(std::move(compiled));

    bool passed = true;
    for (size_t i = 0; i < VECTOR_SIZE; ++i)
    {
        float expected = static_cast<float>(i) + static_cast<float>(i * 2);
        if (C[i] != expected) { passed = false; break; }
    }

    EXPECT_TRUE(passed) << "Vector addition kernel produced incorrect results!";
}

/**
 * @test MultiStageChain
 * @brief Verifies a three-stage dependent pipeline: init (host) -> scale
 * (device kernel) -> reduce (host sum). Each stage reads the previous stage's
 * output via shared USM, so the dependency tracker must serialize them and the
 * final host-side sum must match the closed-form expectation.
 */
TEST(SyclExecutionTest, MultiStageChain)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    sycl::queue& q = ctx.get_numa_local_queue(0);

    constexpr size_t N = 2048;
    constexpr float SCALE = 3.0f;

    UsmShared<float> usm_data(q, N);
    UsmShared<double> usm_sum(q, 1);
    ASSERT_TRUE(usm_data);
    ASSERT_TRUE(usm_sum);

    float* data = usm_data.get();
    double* sum = usm_sum.get();

    // Stage 1: init data[i] = i (host write).
    TaskMetadata meta_init{"Init"};
    graph.add_task(meta_init, {}, {data}, [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        for (size_t i = 0; i < N; ++i) data[i] = static_cast<float>(i);
        return {};
    });

    // Stage 2: data[i] *= SCALE (device kernel, reads+writes data).
    TaskMetadata meta_scale{"Scale"};
    graph.add_task(meta_scale, {data}, {data}, [data](sycl::queue& queue, const std::vector<sycl::event>& deps) -> sycl::event
    {
        return queue.submit([&](sycl::handler& h)
        {
            h.depends_on(deps);
            h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> idx)
            {
                data[idx] = data[idx] * SCALE;
            });
        });
    });

    // Stage 3: sum all elements (host read of data, write of sum).
    TaskMetadata meta_reduce{"Reduce"};
    graph.add_task(meta_reduce, {data}, {sum}, [data, sum](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        double acc = 0.0;
        for (size_t i = 0; i < N; ++i) acc += data[i];
        *sum = acc;
        return {};
    });

    auto compiled = graph.compile();
    ctx.get_execution_engine().execute(std::move(compiled));

    // Expected: SCALE * sum_{i=0}^{N-1} i = SCALE * (N-1)*N/2.
    double expected = static_cast<double>(SCALE) *
                      (static_cast<double>(N - 1) * static_cast<double>(N) / 2.0);

    EXPECT_NEAR(*sum, expected, expected * 1e-6)
        << "Multi-stage pipeline produced an incorrect reduction.";
}
