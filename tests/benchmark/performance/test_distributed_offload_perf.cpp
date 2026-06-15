/**************************************************************************/
/* test_distributed_offload_perf.cpp                                      */
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

// Throughput / per-task latency of the master->worker offload path, plus the
// policy's load split across two workers. Built only with SUSHI_ENABLE_DISTRIBUTED.
// Uses InProcessTransport so the software round trip (encode -> pull -> remote
// kernel -> writeback -> release) is measured deterministically. Every task does
// verified vector-add work, and each worker bumps its own counter so the load
// split is what actually ran where.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <span>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;
using namespace SushiRuntime::Graph::Literals;
using namespace SushiRuntime::Distributed;

namespace
{
    constexpr auto   VEC_ADD = "vec.add"_op;
    constexpr size_t N       = 256; // floats per task
    constexpr size_t TASKS   = 256; // independent offloadable tasks per graph

    std::atomic<int> g_worker_runs[2];

    void register_vec_add(KernelRegistry& reg, int worker_idx)
    {
        reg.register_kernel(VEC_ADD,
            [worker_idx](sycl::queue& q, std::span<void* const> ins,
                         std::span<void* const> outs, const TaskMetadata& meta) -> sycl::event
            {
                auto* a = static_cast<const float*>(ins[0]);
                auto* b = static_cast<const float*>(ins[1]);
                auto* o = static_cast<float*>(outs[0]);
                const size_t n = meta.get_param<uint64_t>(0);
                return q.submit([&](sycl::handler& h)
                {
                    h.host_task([=]
                    {
                        for (size_t i = 0; i < n; ++i)
                            o[i] = a[i] + b[i];
                        g_worker_runs[worker_idx].fetch_add(1, std::memory_order_relaxed);
                    });
                });
            });
    }

    // One graph of TASKS independent vector-adds over distinct buffers.
    struct Workload
    {
        std::vector<float*> a, b, out;

        void alloc(sycl::queue& q)
        {
            a.resize(TASKS);
            b.resize(TASKS);
            out.resize(TASKS);
            for (size_t t = 0; t < TASKS; ++t)
            {
                a[t]   = sycl::malloc_shared<float>(N, q);
                b[t]   = sycl::malloc_shared<float>(N, q);
                out[t] = sycl::malloc_shared<float>(N, q);
                for (size_t i = 0; i < N; ++i)
                {
                    a[t][i]   = float(i);
                    b[t][i]   = float(2 * i);
                    out[t][i] = -1.0f;
                }
            }
        }

        void free(sycl::queue& q)
        {
            for (size_t t = 0; t < TASKS; ++t)
            {
                sycl::free(a[t], q);
                sycl::free(b[t], q);
                sycl::free(out[t], q);
            }
        }
    };

    CompiledGraph build_graph(RuntimeContext& ctx, Workload& w)
    {
        TaskGraph graph(ctx);
        for (size_t t = 0; t < TASKS; ++t)
        {
            ctx.register_distributed_buffer(w.a[t],   N * sizeof(float));
            ctx.register_distributed_buffer(w.b[t],   N * sizeof(float));
            ctx.register_distributed_buffer(w.out[t], N * sizeof(float));

            TaskMetadata meta{};
            meta.name  = "vec_add";
            meta.op_id = VEC_ADD;
            meta.set_param<uint64_t>(0, N);
            float* a = w.a[t];
            float* b = w.b[t];
            float* o = w.out[t];
            graph.add_task(meta, {a, b}, {o},
                [a, b, o](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
                {
                    return q.submit([&](sycl::handler& h)
                    {
                        h.host_task([=]
                        {
                            for (size_t i = 0; i < N; ++i)
                                o[i] = a[i] + b[i];
                        });
                    });
                });
        }
        return graph.compile();
    }
}

// Drive TASKS offloadable tasks across two in-process workers; report throughput,
// per-task round-trip latency, and the worker load split.
TEST(Performance_DistributedOffloadBench, OffloadRoundTripThroughput)
{
    std::unique_ptr<RuntimeContext> master_ctx, w0_ctx, w1_ctx;
    try
    {
        master_ctx = std::make_unique<RuntimeContext>();
        w0_ctx     = std::make_unique<RuntimeContext>();
        w1_ctx     = std::make_unique<RuntimeContext>();
    }
    catch (const std::exception& e)
    {
        GTEST_SKIP() << "No SYCL device available: " << e.what();
    }

    g_worker_runs[0].store(0);
    g_worker_runs[1].store(0);

    // Cluster: master + 2 in-process workers.
    InProcessFabric fabric(2);
    auto master_t = std::make_shared<InProcessTransport>(fabric, -1);
    auto w0_t     = std::make_shared<InProcessTransport>(fabric, 0);
    auto w1_t     = std::make_shared<InProcessTransport>(fabric, 1);

    auto k0 = make_sushi<KernelRegistry>();
    auto k1 = make_sushi<KernelRegistry>();
    register_vec_add(*k0, 0);
    register_vec_add(*k1, 1);
    WorkerAgent a0(*w0_ctx, w0_t, k0);
    WorkerAgent a1(*w1_ctx, w1_t, k1);

    auto master_k = make_sushi<KernelRegistry>();
    auto policy   = std::make_shared<LeastLoadedOffloadPolicy>(0); // offload everything
    master_ctx->enable_distributed_master(master_t, master_k, policy);
    a0.start();
    a1.start();

    bool supported = false;
    for (int i = 0; i < 300 && !supported; ++i)
    {
        supported = master_ctx->distributed_supports(VEC_ADD);
        if (!supported)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(supported) << "Workers never registered the kernel.";

    sycl::queue& mq = master_ctx->get_queue(0);

    // Warm-up graph (primes worker threads / first-touch).
    {
        Workload warm;
        warm.alloc(mq);
        master_ctx->get_execution_engine().execute(build_graph(*master_ctx, warm));
        warm.free(mq);
    }

    // Timed run.
    Workload work;
    work.alloc(mq);
    const int runs_before = g_worker_runs[0].load() + g_worker_runs[1].load();

    const auto t0 = std::chrono::steady_clock::now();
    master_ctx->get_execution_engine().execute(build_graph(*master_ctx, work));
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    // Verify correctness (a fast wrong answer must fail).
    bool correct = true;
    for (size_t t = 0; t < TASKS && correct; ++t)
    {
        for (size_t i = 0; i < N; ++i)
        {
            if (work.out[t][i] != work.a[t][i] + work.b[t][i])
            {
                correct = false;
                break;
            }
        }
    }

    const int    r0          = g_worker_runs[0].load();
    const int    r1          = g_worker_runs[1].load();
    const int    offloaded   = (r0 + r1) - runs_before;
    const double thr         = ms > 0 ? double(TASKS) / (ms / 1000.0) : 0.0;
    const double per_task_us = ms * 1000.0 / double(TASKS);

    std::printf("\n=== Distributed offload (in-process, %zu tasks, %zu floats) ===\n",
                TASKS, N);
    std::printf("  Throughput      : %10.0f tasks/s  (%.2f ms total)\n", thr, ms);
    std::printf("  Per-task latency: %8.2f us (encode+pull+exec+writeback)\n", per_task_us);
    std::printf("  Offloaded       : %d / %zu   (declined %llu)\n",
                offloaded, TASKS,
                (unsigned long long)master_ctx->distributed_declined_count());
    std::printf("  Worker split    : w0=%d  w1=%d\n", r0, r1);

    EXPECT_TRUE(correct) << "Offloaded results written back to the master are wrong.";
    EXPECT_GT(thr, 0.0);
    EXPECT_GT(offloaded, 0) << "Nothing was offloaded — the path was not exercised.";

    work.free(mq);
    a0.stop();
    a1.stop();
}
