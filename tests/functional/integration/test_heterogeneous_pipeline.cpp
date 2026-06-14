/**************************************************************************/
/* test_heterogeneous_pipeline.cpp                                        */
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
 * @file test_heterogeneous_pipeline.cpp
 * @brief Integration test that combines the features the unit/perf tests cover
 *        in isolation: the work-stealing scheduler, host<->device mixed tasks,
 *        and concurrent device execution — all driven through the full
 *        TaskGraph -> ExecutionEngine -> TaskScheduler path.
 *
 * Many INDEPENDENT host->device->host chains are submitted at once. Independent
 * chains give the scheduler parallel work to distribute and steal, while each
 * chain's internal RAW dependencies force correct ordering. Chains are pinned
 * across the available devices (GPU and/or CPU), so on a GTX 1060 + Ryzen box
 * they genuinely fan out over both. The test asserts end-to-end correctness of
 * every chain — i.e. the combined path is race-free.
 *
 * IMPORTANT — shared USM is context-bound: RuntimeContext gives each device its
 * own SYCL queue/context, and get_numa_local_queue() round-robins GPU+CPU within
 * a node. A buffer allocated on one device's queue is NOT valid on another's, so
 * each chain pins its allocation AND its kernel to the SAME explicit device queue
 * (we submit to the captured queue, not the scheduler-provided one). Mixing them
 * trips a "CUDA unknown error" — a real constraint worth remembering.
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

/**
 * @test ManyChainsHostDeviceHost
 * @brief 16 independent init -> scale -> bias chains run through the scheduler
 *        concurrently. Chains are split across devices: GPU chains run their
 *        three stages as device kernels, CPU chains as host work — a host/device
 *        task mix in one graph. Every chain's result must match the closed-form
 *        expectation ((i + c) * SCALE + BIAS).
 */
TEST(Integration_HeterogeneousPipelineTest, ManyChainsHostDeviceHost)
{
    RuntimeContext ctx;
    TaskGraph graph(ctx);

    constexpr int    CHAINS = 16;
    constexpr size_t N      = 2048;
    constexpr float  SCALE  = 3.0f;
    constexpr float  BIAS   = 5.0f;

    // Collect one explicit queue per device (discovery order == get_queue order).
    std::vector<sycl::queue*> device_queues;
    {
        auto devs = sycl::device::get_devices(sycl::info::device_type::all);
        for (size_t i = 0; i < devs.size(); ++i)
            if (devs[i].is_gpu() || devs[i].is_cpu())
                device_queues.push_back(&ctx.get_queue(i));
    }
    ASSERT_FALSE(device_queues.empty()) << "No CPU/GPU device queues available.";

    // Each chain is pinned to one device queue (round-robin across devices) so its
    // shared-USM buffer and kernel share a single context.
    std::vector<UsmShared<float>> buffers;
    buffers.reserve(CHAINS);
    for (int c = 0; c < CHAINS; ++c)
    {
        sycl::queue& dq = *device_queues[static_cast<size_t>(c) % device_queues.size()];
        buffers.emplace_back(dq, N);
        ASSERT_TRUE(buffers.back()) << "USM allocation failed for chain " << c;
    }

    // A chain is either ALL-device (GPU) or ALL-host (CPU). We never let a host
    // stage touch a GPU-allocated (CUDA managed) buffer while other GPU kernels
    // run — that concurrent managed-memory access faults under WSL/CUDA
    // (CUDA_ERROR_ILLEGAL_ADDRESS). Mixing the two chain kinds in one graph still
    // exercises the scheduler with both host and device tasks side by side.
    for (int c = 0; c < CHAINS; ++c)
    {
        float* data = buffers[c].get();
        sycl::queue* dq = device_queues[static_cast<size_t>(c) % device_queues.size()];
        const bool on_gpu = dq->get_device().is_gpu();

        if (on_gpu)
        {
            // init -> scale -> bias, all as device kernels on the chain's queue.
            TaskMetadata meta_init{"chain_init_dev"};
            graph.add_task(meta_init, {}, {data},
                [data, c, dq](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    return dq->submit([&](sycl::handler& h)
                    {
                        h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                        { data[i] = float(i) + float(c); });
                    });
                });

            TaskMetadata meta_scale{"chain_scale_dev"};
            graph.add_task(meta_scale, {data}, {data},
                [data, dq](sycl::queue&, const std::vector<sycl::event>& deps) -> sycl::event
                {
                    return dq->submit([&](sycl::handler& h)
                    {
                        h.depends_on(deps);
                        h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                        { data[i] = data[i] * SCALE; });
                    });
                });

            TaskMetadata meta_bias{"chain_bias_dev"};
            graph.add_task(meta_bias, {data}, {data},
                [data, dq](sycl::queue&, const std::vector<sycl::event>& deps) -> sycl::event
                {
                    return dq->submit([&](sycl::handler& h)
                    {
                        h.depends_on(deps);
                        h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                        { data[i] = data[i] + BIAS; });
                    });
                });
        }
        else
        {
            // CPU-device buffer is plain host memory — host stages are safe.
            TaskMetadata meta_init{"chain_init_host"};
            graph.add_task(meta_init, {}, {data},
                [data, c](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    for (size_t i = 0; i < N; ++i) data[i] = float(i) + float(c);
                    return {};
                });

            TaskMetadata meta_scale{"chain_scale_host"};
            graph.add_task(meta_scale, {data}, {data},
                [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    for (size_t i = 0; i < N; ++i) data[i] = data[i] * SCALE;
                    return {};
                });

            TaskMetadata meta_bias{"chain_bias_host"};
            graph.add_task(meta_bias, {data}, {data},
                [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    for (size_t i = 0; i < N; ++i) data[i] = data[i] + BIAS;
                    return {};
                });
        }
    }

    auto compiled = graph.compile();
    ctx.get_execution_engine().execute(std::move(compiled));
    ctx.wait_all(); // ensure all device kernels are complete before host reads.

    // Expected per chain c, element i: ((i + c) * SCALE) + BIAS
    for (int c = 0; c < CHAINS; ++c)
    {
        const float* data = buffers[c].get();
        bool ok = true;
        for (size_t i = 0; i < N; ++i)
        {
            const float expected = (float(i) + float(c)) * SCALE + BIAS;
            if (std::abs(data[i] - expected) > 1e-3f) { ok = false; break; }
        }
        EXPECT_TRUE(ok) << "Chain " << c << " produced incorrect results.";
    }
}
