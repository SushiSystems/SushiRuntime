/**************************************************************************/
/* test_heterogeneous_pipeline.cpp                                        */
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
 * @file test_heterogeneous_pipeline.cpp
 * @brief Tests many independent host, device, host chains running at once through the scheduler.
 * @see modules/engine/execution/tests/README.md
 * @author Mustafa Garip
 */

#include <cmath>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/task_types.hpp>
#include <SushiRuntime/task/node.hpp>

#include "test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;
using SushiRuntime::Test::UsmShared;

/**
 * @brief Tests concurrent execution of multiple heterogeneous pipelines.
 *
 * Independent init -> scale -> bias chains run concurrently, split across GPU as kernels and CPU as host work.
 */
TEST(Integration_HeterogeneousPipelineTest, ManyChainsHostDeviceHost)
{
    RuntimeContext ctx;
    TaskGraph graph;

    constexpr int    CHAINS = 16;
    constexpr size_t N      = 2048;
    constexpr float  SCALE  = 3.0f;
    constexpr float  BIAS   = 5.0f;

    // The queues come from ctx's deduplicated set, which get_devices() can exceed; see README.md.
    std::vector<sycl::queue*> device_queues;
    for (size_t i = 0; i < ctx.devices().count(); ++i)
    {
        sycl::queue& q = ctx.devices().queue(i);
        if (q.get_device().is_gpu() || q.get_device().is_cpu())
            device_queues.push_back(&q);
    }
    ASSERT_FALSE(device_queues.empty()) << "No CPU/GPU device queues available.";

    // Pin each chain to one device queue so its buffer and kernels share a context.
    std::vector<UsmShared<float>> buffers;
    buffers.reserve(CHAINS);
    for (int c = 0; c < CHAINS; ++c)
    {
        sycl::queue& dq = *device_queues[static_cast<size_t>(c) % device_queues.size()];
        buffers.emplace_back(dq, N);
        ASSERT_TRUE(buffers.back()) << "USM allocation failed for chain " << c;
    }

    for (int c = 0; c < CHAINS; ++c)
    {
        float* data = buffers[c].get();
        sycl::queue* dq = device_queues[static_cast<size_t>(c) % device_queues.size()];
        const bool on_gpu = dq->get_device().is_gpu();

        if (on_gpu)
        {
            TaskMetadata meta_init{"chain_init_dev"};
            graph.add_task(meta_init, {}, {data},
                [data, c, dq](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    return dq->submit([&](sycl::handler& h)
                    {
                        h.parallel_for(sycl::range<1>(N), [=](sycl::id<1> i)
                        {
                            data[i] = float(i) + float(c);
                        });
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
                        {
                            data[i] = data[i] * SCALE;
                        });
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
                        {
                            data[i] = data[i] + BIAS;
                        });
                    });
                });
        }
        else
        {
            TaskMetadata meta_init{"chain_init_host"};
            graph.add_task(meta_init, {}, {data},
                [data, c](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    for (size_t i = 0; i < N; ++i)
                        data[i] = float(i) + float(c);
                    return {};
                });

            TaskMetadata meta_scale{"chain_scale_host"};
            graph.add_task(meta_scale, {data}, {data},
                [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    for (size_t i = 0; i < N; ++i)
                        data[i] = data[i] * SCALE;
                    return {};
                });

            TaskMetadata meta_bias{"chain_bias_host"};
            graph.add_task(meta_bias, {data}, {data},
                [data](sycl::queue&, const std::vector<sycl::event>&) -> sycl::event
                {
                    for (size_t i = 0; i < N; ++i)
                        data[i] = data[i] + BIAS;
                    return {};
                });
        }
    }

    auto compiled = graph.compile();
    ctx.get_execution_engine().execute(std::move(compiled));
    ctx.devices().wait_all();

    for (int c = 0; c < CHAINS; ++c)
    {
        const float* data = buffers[c].get();
        bool ok = true;
        for (size_t i = 0; i < N; ++i)
        {
            const float expected = (float(i) + float(c)) * SCALE + BIAS;
            if (std::abs(data[i] - expected) > 1e-3f)
            {
                ok = false;
                break;
            }
        }
        EXPECT_TRUE(ok) << "Chain " << c << " produced incorrect results.";
    }
}
