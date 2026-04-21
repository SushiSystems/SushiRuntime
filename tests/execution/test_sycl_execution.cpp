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

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;

/**
 * @test EndToEndVectorAddition
 * @brief Performs a full end-to-end test of the compute runtime. 
 * Allocates USM memory, dispatches an initialization host task, 
 * dispatches a SYCL device kernel for vector addition, and finally 
 * verifies the results on the host.
 */
TEST(SycleExecutionTest, EndToEndVectorAddition)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);
    
    // Fallback queue for allocation (uses the default NUMA node 0)
    sycl::queue& q = ctx.get_numa_local_queue(0);
    
    constexpr size_t VECTOR_SIZE = 1024;
    [[maybe_unused]] constexpr size_t MEMORY_SIZE = VECTOR_SIZE * sizeof(float);

    // 1. Allocate USM Shared Memory (Accessible by both Host and Device)
    float* usm_A = sycl::malloc_shared<float>(VECTOR_SIZE, q);
    float* usm_B = sycl::malloc_shared<float>(VECTOR_SIZE, q);
    float* usm_C = sycl::malloc_shared<float>(VECTOR_SIZE, q);

    ASSERT_NE(usm_A, nullptr);
    ASSERT_NE(usm_B, nullptr);
    ASSERT_NE(usm_C, nullptr);

    // Node 1: Host Task to Initialize Vectors
    TaskMetadata meta_init{"Init_Vectors"};
    auto init_work = [usm_A, usm_B, usm_C](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event 
    {
        for (size_t i = 0; i < VECTOR_SIZE; ++i)
        {
            usm_A[i] = static_cast<float>(i);
            usm_B[i] = static_cast<float>(i * 2);
            usm_C[i] = 0.0f; // Reset output
        }
        return {};
    };
    
    // Add Init Node (Writes to A, B, C)
    graph.add_task(meta_init, {}, {usm_A, usm_B, usm_C}, init_work);

    // Node 2: Device Task (SYCL Kernel) for Vector Addition
    TaskMetadata meta_compute{"Vector_Add_Kernel"};
    auto compute_work = [usm_A, usm_B, usm_C](sycl::queue& queue, const std::vector<sycl::event>& deps) -> sycl::event 
    {
        // Submit the kernel to the SYCL queue
        return queue.submit([&](sycl::handler& h) 
        {
            // Depend on the events passed by the scheduler
            h.depends_on(deps);
            
            h.parallel_for(sycl::range<1>(VECTOR_SIZE), [=](sycl::id<1> idx) 
            {
                usm_C[idx] = usm_A[idx] + usm_B[idx];
            });
        });
    };
    
    // Add Compute Node (Reads A, B. Writes C. Depends on Init via USM pointers)
    // DependencyTracker will automatically detect that this must wait for meta_init.
    graph.add_task(meta_compute, {usm_A, usm_B}, {usm_C}, compute_work);

    // Compile and Execute the Graph
    auto compiled = graph.compile();
    ctx.get_execution_engine().execute(std::move(compiled));

    // Verify Results (Host Side Verification)
    bool passed = true;
    for (size_t i = 0; i < VECTOR_SIZE; ++i)
    {
        float expected = static_cast<float>(i) + static_cast<float>(i * 2);
        if (usm_C[i] != expected)
        {
            passed = false;
            break;
        }
    }
    
    EXPECT_TRUE(passed) << "Vector addition kernel produced incorrect results!";

    // Cleanup USM memory
    sycl::free(usm_A, q);
    sycl::free(usm_B, q);
    sycl::free(usm_C, q);
}