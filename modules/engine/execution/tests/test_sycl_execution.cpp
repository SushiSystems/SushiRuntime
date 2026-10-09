/**************************************************************************/
/* test_sycl_execution.cpp                                                */
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
 * @file test_sycl_execution.cpp
 * @brief Tests an end-to-end vector addition and a multi-stage chain through the TaskGraph.
 * @see tests/integration/README.md
 * @author Mustafa Garip
 */

#include <optional>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/node.hpp>

#include "test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;
using SushiRuntime::Test::UsmShared;

/**
 * @brief Tests an end-to-end vector addition.
 *
 * A host task initializes the inputs, a device kernel adds them, and the host verifies the result.
 */
TEST(Integration_SyclExecutionTest, EndToEndVectorAddition)
{
    RuntimeContext ctx;
    TaskGraph graph;

    sycl::queue& q = ctx.devices().queue_or_numa_local(std::nullopt, 0);

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
        if (C[i] != expected)
        {
            passed = false;
            break;
        }
    }

    EXPECT_TRUE(passed) << "Vector addition kernel produced incorrect results!";
}

/**
 * @brief Tests a multi-stage execution chain sharing USM.
 *
 * Three dependent stages: init (host) -> scale (device kernel) -> reduce (host sum). The tracker must serialize them and the final sum must match the expected value.
 */
TEST(Integration_SyclExecutionTest, MultiStageChain)
{
    RuntimeContext ctx;
    TaskGraph graph;

    constexpr size_t N     = 2048;
    constexpr float  SCALE = 3.0f;

    // Both buffers are pinned to device 0, as a task's buffers live on one device; see README.md.
    float* data = static_cast<float*>(
        ctx.memory().allocate({Memory::UsmKind::Shared, N * sizeof(float), 0, 0}));
    double* sum = static_cast<double*>(
        ctx.memory().allocate({Memory::UsmKind::Shared, sizeof(double), 0, 0}));
    ASSERT_NE(data, nullptr);
    ASSERT_NE(sum, nullptr);

    TaskMetadata meta_init{"Init"};
    graph.add_task(meta_init, {}, {data}, [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        for (size_t i = 0; i < N; ++i)
            data[i] = static_cast<float>(i);
        return {};
    });

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

    TaskMetadata meta_reduce{"Reduce"};
    graph.add_task(meta_reduce, {data}, {sum}, [data, sum](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
    {
        double acc = 0.0;
        for (size_t i = 0; i < N; ++i)
            acc += data[i];
        *sum = acc;
        return {};
    });

    auto compiled = graph.compile();
    ctx.get_execution_engine().execute(std::move(compiled));

    // SCALE * sum_{i=0}^{N-1} i = SCALE * (N-1)*N/2.
    double expected = static_cast<double>(SCALE) *
                      (static_cast<double>(N - 1) * static_cast<double>(N) / 2.0);

    EXPECT_NEAR(*sum, expected, expected * 1e-6)
        << "Multi-stage pipeline produced an incorrect reduction.";

    ctx.memory().release(data);
    ctx.memory().release(sum);
}
