/**************************************************************************/
/* test_sycl_execution.cpp                                                */
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
 * @brief Tests an end-to-end vector addition.
 *
 * A host task initializes the inputs, a device kernel adds them, and the host verifies the result.
 */
TEST(Integration_SyclExecutionTest, EndToEndVectorAddition)
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
    TaskGraph graph(ctx);

    constexpr size_t N     = 2048;
    constexpr float  SCALE = 3.0f;

    // Pin both buffers to one device so they share a context: the Reduce task
    // touches data and sum together, and the co-location guard requires a task's
    // buffers to live on one device. malloc_shared(numa) round-robins across the
    // devices on a NUMA node, which would scatter the two across contexts on a
    // multi-device host; malloc_shared_on fixes the device.
    float*  data = static_cast<float*>(ctx.malloc_shared_on(N * sizeof(float), 0));
    double* sum  = static_cast<double*>(ctx.malloc_shared_on(sizeof(double), 0));
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

    ctx.free_usm(data);
    ctx.free_usm(sum);
}
