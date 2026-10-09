/**************************************************************************/
/* test_distributed_e2e.cpp                                               */
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
 * @file test_distributed_e2e.cpp
 * @brief Tests end-to-end offload between a master and in-process workers on a real SYCL device.
 * @see tests/functional/integration/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/core/span.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <memory>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/sycl_compat.hpp>
#include <SushiRuntime/api/fluent.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;
using namespace SushiRuntime::Graph::Literals;
using namespace SushiRuntime::Distributed;

namespace
{
    /**
     * @brief Bumped once per kernel run on the worker.
     *
     * Used to prove the task ran remotely rather than through the local host fallback.
     */
    std::atomic<int> g_worker_runs{0};

    constexpr auto VEC_ADD = "vec.add"_op;

    /**
     * @brief Registers the vector-add kernel.
     *
     * Computes out[i] = a[i] + b[i] over network-resolved local USM buffers.
     * @param reg The kernel registry to register with.
     */
    void register_vec_add(KernelRegistry& reg)
    {
        reg.register_kernel(VEC_ADD,
            [](sycl::queue& q, SushiRuntime::span<void* const> ins, SushiRuntime::span<void* const> outs,
               const TaskMetadata& meta) -> sycl::event
            {
                auto* a = static_cast<const float*>(ins[0]);
                auto* b = static_cast<const float*>(ins[1]);
                auto* o = static_cast<float*>(outs[0]);
                const size_t n = meta.get_param<uint64_t>(0);

                return q.submit([&](sycl::handler& h)
                {
                    SushiRuntime::detail::enqueue_host_task(h, [=]
                    {
                        for (size_t i = 0; i < n; ++i)
                            o[i] = a[i] + b[i];
                        g_worker_runs.fetch_add(1, std::memory_order_relaxed);
                    });
                });
            });
    }

    /**
     * @brief Bumped once per State-step kernel run on the worker.
     *
     * Proves the distributed State step ran remotely, not via the local fallback.
     */
    std::atomic<int> g_state_worker_runs{0};

    constexpr auto STATE_STEP = "state.step"_op;

    /** @brief Registers the State-step kernel next[i] = cur[i] + (i % 7). */
    void register_state_step(KernelRegistry& reg)
    {
        reg.register_kernel(STATE_STEP,
            [](sycl::queue& q, SushiRuntime::span<void* const> ins, SushiRuntime::span<void* const> outs,
               const TaskMetadata& meta) -> sycl::event
            {
                auto* cur  = static_cast<const int*>(ins[0]);
                auto* next = static_cast<int*>(outs[0]);
                const size_t n = meta.get_param<uint64_t>(0);

                return q.submit([&](sycl::handler& h)
                {
                    SushiRuntime::detail::enqueue_host_task(h, [=]
                    {
                        for (size_t i = 0; i < n; ++i)
                            next[i] = cur[i] + static_cast<int>(i % 7);
                        g_state_worker_runs.fetch_add(1, std::memory_order_relaxed);
                    });
                });
            });
    }

    /**
     * @brief Evolves a distributed State<int> on a one-worker in-process cluster and returns its
     *        final field.
     */
    std::vector<int> evolve_distributed_state(std::size_t n, std::size_t steps,
                                              std::uint64_t& offloaded)
    {
        InProcessFabric fabric(1);
        auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
        auto worker_transport = std::make_shared<InProcessTransport>(fabric, 0);

        RuntimeContext worker_ctx;            // throws if no device
        Runtime rt = Runtime::create();       // throws if no device

        auto worker_kernels = make_sushi<KernelRegistry>();
        register_state_step(*worker_kernels);
        WorkerAgent agent(worker_ctx, worker_transport, worker_kernels);

        auto cluster = rt.cluster(master_transport, make_sushi<KernelRegistry>(),
                                  std::make_shared<LeastLoadedOffloadPolicy>(0));
        agent.start();
        if (!cluster.wait_for_workers(1, std::chrono::seconds(3)))
            throw std::runtime_error("worker never registered with the cluster");

        auto field = rt.state<int>(n).distributed();
        for (std::size_t i = 0; i < n; ++i)
        {
            field.current()[i] = static_cast<int>(i);
            field.next()[i]    = static_cast<int>(i);
        }

        auto g = rt.graph();
        g.add(STATE_STEP, field,
              [](std::size_t i, const int* cur, int* next)
              { next[i] = cur[i] + static_cast<int>(i % 7); },
              { static_cast<std::uint64_t>(n) });
        g.run(steps);

        std::vector<int> result(n);
        for (std::size_t i = 0; i < n; ++i)
            result[i] = field[i];

        offloaded = cluster.offloaded();
        agent.stop();
        return result;
    }
}

/**
 * @brief Tests a full offload vector-add round trip.
 *
 * Data goes master->worker, the kernel runs on the worker, results come back, and the graph completes.
 */
TEST(Integration_DistributedE2ETest, OffloadVectorAddRoundTrip)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    auto master_ctx = std::make_unique<RuntimeContext>();
    auto worker_ctx = std::make_unique<RuntimeContext>();

    g_worker_runs.store(0, std::memory_order_relaxed);

    constexpr size_t N = 1024;
    sycl::queue& mq = master_ctx->get_queue(0);

    float* a   = sycl::malloc_shared<float>(N, mq);
    float* b   = sycl::malloc_shared<float>(N, mq);
    float* out = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < N; ++i)
    {
        a[i]   = float(i);
        b[i]   = float(2 * i);
        out[i] = -1.0f;
    }

    // The master serves before the worker starts, or the worker's HELLO is lost; see README.md.
    InProcessFabric fabric(1);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
    auto worker_transport = std::make_shared<InProcessTransport>(fabric, 0);

    auto worker_kernels = make_sushi<KernelRegistry>();
    register_vec_add(*worker_kernels);
    WorkerAgent agent(*worker_ctx, worker_transport, worker_kernels);

    auto master_kernels = make_sushi<KernelRegistry>();
    auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0); // min_offload_bytes=0
    master_ctx->enable_distributed_master(master_transport, master_kernels, policy);

    agent.start();

    // Wait until the worker's HELLO has registered VEC_ADD with the coordinator.
    bool supported = false;
    for (int i = 0; i < 300 && !supported; ++i)
    {
        supported = master_ctx->distributed_supports(VEC_ADD);
        if (!supported)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(supported) << "Worker never registered the kernel with the master.";

    master_ctx->register_distributed_buffer(a,   N * sizeof(float));
    master_ctx->register_distributed_buffer(b,   N * sizeof(float));
    master_ctx->register_distributed_buffer(out, N * sizeof(float));

    TaskGraph graph(master_ctx->offload());
    TaskMetadata meta{};
    meta.name  = "vec_add";
    meta.op_id = VEC_ADD;
    meta.set_param<uint64_t>(0, N);

    // Host fallback (same math) — only used if the task is NOT offloaded.
    graph.add_task(meta, {a, b}, {out},
        [a, b, out](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                SushiRuntime::detail::enqueue_host_task(h, [=]
                {
                    for (size_t i = 0; i < N; ++i)
                        out[i] = a[i] + b[i];
                });
            });
        });

    auto compiled = graph.compile();
    master_ctx->get_execution_engine().execute(std::move(compiled));

    EXPECT_EQ(g_worker_runs.load(std::memory_order_relaxed), 1)
        << "Task should have run on the worker, not the local fallback.";

    bool correct = true;
    for (size_t i = 0; i < N; ++i)
    {
        if (out[i] != a[i] + b[i])
        {
            correct = false;
            break;
        }
    }
    EXPECT_TRUE(correct) << "Offloaded result written back to the master is wrong.";

    agent.stop();
    sycl::free(a, mq);
    sycl::free(b, mq);
    sycl::free(out, mq);
}

/**
 * @brief Tests fan-out of offloadable tasks across multiple workers.
 *
 * Two independent offloadable tasks fan out to two distinct workers: the policy spreads the load and each worker runs its share.
 */
TEST(Integration_DistributedE2ETest, FanOutAcrossTwoWorkers)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    auto master_ctx = std::make_unique<RuntimeContext>();
    auto w0_ctx = std::make_unique<RuntimeContext>();
    auto w1_ctx = std::make_unique<RuntimeContext>();

    // The per-worker counters record which worker ran which task; see README.md.
    std::atomic<int> c0{0}, c1{0};
    auto make_kernel = [](std::atomic<int>* counter)
    {
        return [counter](sycl::queue& q, SushiRuntime::span<void* const> ins, SushiRuntime::span<void* const> outs,
                         const TaskMetadata& meta) -> sycl::event
        {
            auto* a = static_cast<const float*>(ins[0]);
            auto* o = static_cast<float*>(outs[0]);
            const size_t n = meta.get_param<uint64_t>(0);
            return q.submit([&](sycl::handler& h)
            {
                SushiRuntime::detail::enqueue_host_task(h, [=]
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    for (size_t i = 0; i < n; ++i)
                        o[i] = a[i] + a[i];
                    counter->fetch_add(1, std::memory_order_relaxed);
                });
            });
        };
    };

    constexpr size_t N = 256;
    sycl::queue& mq = master_ctx->get_queue(0);
    float* in = sycl::malloc_shared<float>(N, mq);
    float* y0 = sycl::malloc_shared<float>(N, mq);
    float* y1 = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(in, nullptr);
    ASSERT_NE(y0, nullptr);
    ASSERT_NE(y1, nullptr);
    for (size_t i = 0; i < N; ++i)
    {
        in[i] = float(i);
        y0[i] = -1.0f;
        y1[i] = -1.0f;
    }

    InProcessFabric fabric(2);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
    auto w0_transport     = std::make_shared<InProcessTransport>(fabric, 0);
    auto w1_transport     = std::make_shared<InProcessTransport>(fabric, 1);

    auto k0 = make_sushi<KernelRegistry>();
    auto k1 = make_sushi<KernelRegistry>();
    k0->register_kernel(VEC_ADD, make_kernel(&c0));
    k1->register_kernel(VEC_ADD, make_kernel(&c1));
    WorkerAgent a0(*w0_ctx, w0_transport, k0);
    WorkerAgent a1(*w1_ctx, w1_transport, k1);

    auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
    master_ctx->enable_distributed_master(master_transport, make_sushi<KernelRegistry>(), policy);
    a0.start();
    a1.start();

    // Wait until BOTH workers have registered, so the policy can spread the work.
    bool both = false;
    for (int i = 0; i < 500 && !both; ++i)
    {
        both = master_ctx->distributed_alive_workers() == 2;
        if (!both)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(both) << "Both workers should have registered.";

    master_ctx->register_distributed_buffer(in, N * sizeof(float));
    master_ctx->register_distributed_buffer(y0, N * sizeof(float));
    master_ctx->register_distributed_buffer(y1, N * sizeof(float));

    TaskGraph graph(master_ctx->offload());
    for (float* y : {y0, y1})
    {
        TaskMetadata m{};
        m.name  = "fanout";
        m.op_id = VEC_ADD;
        m.set_param<uint64_t>(0, N);
        graph.add_task(m, {in}, {y},
            [in, y](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
            {
                return q.submit([&](sycl::handler& h)
                {
                    SushiRuntime::detail::enqueue_host_task(h, [=]
                    {
                        for (size_t i = 0; i < N; ++i)
                            y[i] = in[i] + in[i];
                    });
                });
            });
    }

    auto compiled = graph.compile();
    master_ctx->get_execution_engine().execute(std::move(compiled));

    EXPECT_EQ(master_ctx->distributed_offloaded_count(), 2u);
    // Only the total is asserted, because which worker gets which task is a race; see README.md.
    EXPECT_EQ(c0.load() + c1.load(), 2) << "Both tasks should have run on workers.";

    bool ok0 = true, ok1 = true;
    for (size_t i = 0; i < N; ++i)
    {
        if (y0[i] != 2.0f * in[i])
            ok0 = false;
        if (y1[i] != 2.0f * in[i])
            ok1 = false;
    }
    EXPECT_TRUE(ok0);
    EXPECT_TRUE(ok1);

    a0.stop();
    a1.stop();
    sycl::free(in, mq);
    sycl::free(y0, mq);
    sycl::free(y1, mq);
}

/**
 * @brief Tests automatic version bumping across a local write followed by an offloaded read.
 *
 * Proves the local write auto-bumps X's version (so the worker pulls fresh data) and the offloaded result reflects the locally written values.
 */
TEST(Integration_DistributedE2ETest, AutoVersionBumpAcrossLocalWriteThenOffloadedRead)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    auto master_ctx = std::make_unique<RuntimeContext>();
    auto worker_ctx = std::make_unique<RuntimeContext>();

    g_worker_runs.store(0, std::memory_order_relaxed);

    constexpr size_t N = 512;
    sycl::queue& mq = master_ctx->get_queue(0);
    float* x = sycl::malloc_shared<float>(N, mq);
    float* y = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(x, nullptr);
    ASSERT_NE(y, nullptr);
    for (size_t i = 0; i < N; ++i)
    {
        x[i] = -7.0f;
        y[i] = 0.0f;
    }

    InProcessFabric fabric(1);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
    auto worker_transport = std::make_shared<InProcessTransport>(fabric, 0);

    auto worker_kernels = make_sushi<KernelRegistry>();
    register_vec_add(*worker_kernels);
    WorkerAgent agent(*worker_ctx, worker_transport, worker_kernels);

    auto master_kernels = make_sushi<KernelRegistry>();
    auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
    master_ctx->enable_distributed_master(master_transport, master_kernels, policy);
    agent.start();

    bool supported = false;
    for (int i = 0; i < 300 && !supported; ++i)
    {
        supported = master_ctx->distributed_supports(VEC_ADD);
        if (!supported)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(supported);

    master_ctx->register_distributed_buffer(x, N * sizeof(float));
    master_ctx->register_distributed_buffer(y, N * sizeof(float));

    const auto x_before = master_ctx->distributed_buffer(x);
    ASSERT_TRUE(x_before.has_value());

    TaskGraph graph(master_ctx->offload());

    // Task A: LOCAL writer (no op_id) — fills x[i] = i + 1.
    TaskMetadata write_meta{};
    write_meta.name = "local_fill";
    graph.add_task(write_meta, {}, {x},
        [x](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                SushiRuntime::detail::enqueue_host_task(h, [=]
                {
                    for (size_t i = 0; i < N; ++i)
                        x[i] = float(i + 1);
                });
            });
        });

    // Task B: OFFLOADED reader — y = x + x. Depends on A via the RAW hazard on x.
    TaskMetadata add_meta{};
    add_meta.name  = "double_via_offload";
    add_meta.op_id = VEC_ADD;
    add_meta.set_param<uint64_t>(0, N);
    graph.add_task(add_meta, {x, x}, {y},
        [x, y](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                SushiRuntime::detail::enqueue_host_task(h, [=]
                {
                    for (size_t i = 0; i < N; ++i)
                        y[i] = x[i] + x[i];
                });
            });
        });

    auto compiled = graph.compile();
    master_ctx->get_execution_engine().execute(std::move(compiled));

    const auto x_after = master_ctx->distributed_buffer(x);
    ASSERT_TRUE(x_after.has_value());
    EXPECT_GT(x_after->version, x_before->version)
        << "Local write to a registered buffer should auto-bump its version.";

    EXPECT_EQ(g_worker_runs.load(std::memory_order_relaxed), 1);
    bool correct = true;
    for (size_t i = 0; i < N; ++i)
    {
        if (y[i] != 2.0f * float(i + 1))
        {
            correct = false;
            break;
        }
    }
    EXPECT_TRUE(correct) << "Offloaded reader did not see the locally-written values.";

    agent.stop();
    sycl::free(x, mq);
    sycl::free(y, mq);
}

/**
 * @brief Tests vector-add offload over a real raw-socket TCP transport.
 *
 * Exercises the wire path a real cluster uses over 127.0.0.1 loopback.
 */
TEST(Integration_DistributedE2ETest, OffloadOverTcpLoopback)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    auto master_ctx = std::make_unique<RuntimeContext>();
    auto worker_ctx = std::make_unique<RuntimeContext>();

    g_worker_runs.store(0, std::memory_order_relaxed);

    constexpr size_t N = 1024;
    sycl::queue& mq = master_ctx->get_queue(0);
    float* a   = sycl::malloc_shared<float>(N, mq);
    float* b   = sycl::malloc_shared<float>(N, mq);
    float* out = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < N; ++i)
    {
        a[i]   = float(i);
        b[i]   = float(3 * i);
        out[i] = -1.0f;
    }

    // Master binds an OS-assigned port; the worker connects over loopback.
    auto master_transport = TcpTransport::make_master(0, 1);
    const uint16_t port   = master_transport->bound_port();
    ASSERT_NE(port, 0);
    auto worker_transport = TcpTransport::make_worker("127.0.0.1", port);

    auto worker_kernels = make_sushi<KernelRegistry>();
    register_vec_add(*worker_kernels);
    WorkerAgent agent(*worker_ctx, worker_transport, worker_kernels);

    auto master_kernels = make_sushi<KernelRegistry>();
    auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
    master_ctx->enable_distributed_master(master_transport, master_kernels, policy);
    agent.start();

    bool supported = false;
    for (int i = 0; i < 500 && !supported; ++i)
    {
        supported = master_ctx->distributed_supports(VEC_ADD);
        if (!supported)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(supported) << "Worker never registered over TCP.";

    master_ctx->register_distributed_buffer(a,   N * sizeof(float));
    master_ctx->register_distributed_buffer(b,   N * sizeof(float));
    master_ctx->register_distributed_buffer(out, N * sizeof(float));

    TaskGraph graph(master_ctx->offload());
    TaskMetadata meta{};
    meta.name  = "vec_add_tcp";
    meta.op_id = VEC_ADD;
    meta.set_param<uint64_t>(0, N);
    graph.add_task(meta, {a, b}, {out},
        [a, b, out](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                SushiRuntime::detail::enqueue_host_task(h, [=]
                {
                    for (size_t i = 0; i < N; ++i)
                        out[i] = a[i] + b[i];
                });
            });
        });

    auto compiled = graph.compile();
    master_ctx->get_execution_engine().execute(std::move(compiled));

    EXPECT_EQ(g_worker_runs.load(std::memory_order_relaxed), 1);
    bool correct = true;
    for (size_t i = 0; i < N; ++i)
    {
        if (out[i] != a[i] + b[i])
        {
            correct = false;
            break;
        }
    }
    EXPECT_TRUE(correct) << "Offloaded-over-TCP result is wrong.";

    agent.stop();
    sycl::free(a, mq);
    sycl::free(b, mq);
    sycl::free(out, mq);
}

/**
 * @brief Tests master resilience and local fallback upon worker death.
 *
 * A worker accepts an offloaded task then dies without replying. The master must not hang: the liveness monitor declares it dead, re-dispatches the task, and runs it locally.
 */
TEST(Integration_DistributedE2ETest, WorkerDeathFallsBackToLocal)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    auto master_ctx = std::make_unique<RuntimeContext>();

    constexpr size_t N = 1024;
    sycl::queue& mq = master_ctx->get_queue(0);
    float* a   = sycl::malloc_shared<float>(N, mq);
    float* b   = sycl::malloc_shared<float>(N, mq);
    float* out = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < N; ++i)
    {
        a[i]   = float(i);
        b[i]   = float(2 * i);
        out[i] = -1.0f;
    }

    InProcessFabric fabric(1);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
    auto worker_transport = std::make_shared<InProcessTransport>(fabric, 0);

    // The fake worker heartbeats until its first dispatch, then never answers; see README.md.
    std::atomic<bool> got_dispatch{false};
    std::atomic<bool> hb_running{true};
    worker_transport->on_message(
        [&](int, MsgType type, SushiRuntime::span<const std::byte>)
        {
            if (type == MsgType::TASK_DISPATCH)
                got_dispatch.store(true, std::memory_order_relaxed);
        });

    auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
    master_ctx->enable_distributed_master(master_transport, make_sushi<KernelRegistry>(), policy);
    master_ctx->distributed_set_liveness_timeout(std::chrono::milliseconds(300));

    worker_transport->start();
    {
        ByteWriter w;
        w.put<uint32_t>(PROTOCOL_VERSION);
        w.put<uint32_t>(1);
        w.put<uint32_t>(VEC_ADD.value);
        worker_transport->send(0, MsgType::REGISTER_HELLO, w.view());
    }

    // Keep the worker "alive" only until the dispatch lands, then fall silent.
    std::thread heartbeat([&]
    {
        while (hb_running.load() && !got_dispatch.load(std::memory_order_relaxed))
        {
            worker_transport->send(0, MsgType::HEARTBEAT, {});
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    });

    bool supported = false;
    for (int i = 0; i < 300 && !supported; ++i)
    {
        supported = master_ctx->distributed_supports(VEC_ADD);
        if (!supported)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(supported) << "Fake worker never registered the kernel.";

    master_ctx->register_distributed_buffer(a,   N * sizeof(float));
    master_ctx->register_distributed_buffer(b,   N * sizeof(float));
    master_ctx->register_distributed_buffer(out, N * sizeof(float));

    TaskGraph graph(master_ctx->offload());
    TaskMetadata meta{};
    meta.name  = "vec_add_death";
    meta.op_id = VEC_ADD;
    meta.set_param<uint64_t>(0, N);
    graph.add_task(meta, {a, b}, {out},
        [a, b, out](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                SushiRuntime::detail::enqueue_host_task(h, [=]
                {
                    for (size_t i = 0; i < N; ++i)
                        out[i] = a[i] + b[i];
                });
            });
        });

    // This call must return although the worker the task went to never answered; see README.md.
    master_ctx->get_execution_engine().execute(graph.compile());

    EXPECT_TRUE(got_dispatch.load(std::memory_order_relaxed))
        << "Task should have been offloaded before the worker died.";
    EXPECT_GE(master_ctx->distributed_redispatched_count(), 1u)
        << "The dead worker's task should have been re-dispatched.";

    bool correct = true;
    for (size_t i = 0; i < N; ++i)
    {
        if (out[i] != a[i] + b[i])
        {
            correct = false;
            break;
        }
    }
    EXPECT_TRUE(correct) << "Local fallback after worker death produced a wrong result.";

    hb_running.store(false);
    if (heartbeat.joinable())
        heartbeat.join();
    worker_transport->stop();
    sycl::free(a, mq);
    sycl::free(b, mq);
    sycl::free(out, mq);
}

/**
 * @brief Tests that stale writebacks from zombie workers are fenced out.
 *
 * Proves that a late writeback from a superseded worker does not clobber the master's authoritative buffer, preventing data-corruption races.
 */
TEST(Integration_DistributedReliabilityTest, StaleWritebackIsFenced)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    auto master_ctx = std::make_unique<RuntimeContext>();

    constexpr size_t N = 16;
    sycl::queue& mq = master_ctx->get_queue(0);
    float* out = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < N; ++i) out[i] = 7.0f; // authoritative good value

    InProcessFabric fabric(1);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
    auto worker_transport = std::make_shared<InProcessTransport>(fabric, 0);

    auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
    master_ctx->enable_distributed_master(master_transport, make_sushi<KernelRegistry>(), policy);
    worker_transport->start();

    master_ctx->register_distributed_buffer(out, N * sizeof(float)); // version 1
    master_ctx->bump_distributed_buffer(out);                        // -> 2
    master_ctx->bump_distributed_buffer(out);                        // -> 3
    auto cur = master_ctx->distributed_buffer(out);
    ASSERT_TRUE(cur.has_value());
    ASSERT_EQ(cur->version, 3u);

    // Stale writeback: an older version carrying poison. Must be rejected.
    {
        BufferPushMsg push;
        push.handle = *cur;
        push.handle.version = 2; // < current (3)
        std::vector<float> poison(N, 999.0f);
        push.data.resize(N * sizeof(float));
        std::memcpy(push.data.data(), poison.data(), N * sizeof(float));
        worker_transport->send(0, MsgType::BUFFER_PUSH, push.serialize());
    }

    // Wait until the fence has observed and rejected it.
    bool fenced = false;
    for (int i = 0; i < 300 && !fenced; ++i)
    {
        fenced = master_ctx->distributed_stale_writeback_count() >= 1;
        if (!fenced) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(fenced) << "Stale writeback should have been rejected by the version fence.";
    for (size_t i = 0; i < N; ++i)
        EXPECT_EQ(out[i], 7.0f) << "Stale writeback clobbered the authoritative buffer at " << i;

    // Fresh writeback at the current version: must be accepted.
    {
        BufferPushMsg push;
        push.handle = *cur;
        push.handle.version = 3; // == current
        std::vector<float> good(N, 42.0f);
        push.data.resize(N * sizeof(float));
        std::memcpy(push.data.data(), good.data(), N * sizeof(float));
        worker_transport->send(0, MsgType::BUFFER_PUSH, push.serialize());
    }
    bool applied = false;
    for (int i = 0; i < 300 && !applied; ++i)
    {
        applied = out[0] == 42.0f;
        if (!applied) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(applied) << "A current-version writeback should have been accepted.";

    worker_transport->stop();
    sycl::free(out, mq);
}

/**
 * @brief Tests that a lost buffer push times out cleanly.
 *
 * A worker that never receives the buffer it pulls must time out, failing the task cleanly without hanging the master's execute thread.
 */
TEST(Integration_DistributedReliabilityTest, LostBufferPushTimesOutInsteadOfHanging)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    auto master_ctx = std::make_unique<RuntimeContext>();
    auto worker_ctx = std::make_unique<RuntimeContext>();

    constexpr size_t N = 64;
    sycl::queue& mq = master_ctx->get_queue(0);
    float* a   = sycl::malloc_shared<float>(N, mq);
    float* b   = sycl::malloc_shared<float>(N, mq);
    float* out = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < N; ++i) { a[i] = float(i); b[i] = float(2 * i); out[i] = -1.0f; }

    InProcessFabric fabric(1);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
    auto worker_transport = std::make_shared<InProcessTransport>(fabric, 0);

    // With every buffer push dropped, the worker's input pull can never complete; see README.md.
    fabric.set_fault_policy([](int from, int to, MsgType type)
    {
        if (from == -1 && to == 0 && type == MsgType::BUFFER_PUSH)
            return FaultAction::Drop;
        return FaultAction::Deliver;
    });

    auto worker_kernels = make_sushi<KernelRegistry>();
    register_vec_add(*worker_kernels);
    WorkerAgent agent(*worker_ctx, worker_transport, worker_kernels);
    agent.set_pull_timeout(std::chrono::milliseconds(200)); // fail fast for the test

    auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
    master_ctx->enable_distributed_master(master_transport, make_sushi<KernelRegistry>(), policy);
    agent.start();

    bool supported = false;
    for (int i = 0; i < 300 && !supported; ++i)
    {
        supported = master_ctx->distributed_supports(VEC_ADD);
        if (!supported) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(supported);

    master_ctx->register_distributed_buffer(a,   N * sizeof(float));
    master_ctx->register_distributed_buffer(b,   N * sizeof(float));
    master_ctx->register_distributed_buffer(out, N * sizeof(float));

    TaskGraph graph(master_ctx->offload());
    TaskMetadata meta{};
    meta.name  = "vec_add_lost_push";
    meta.op_id = VEC_ADD;
    meta.set_param<uint64_t>(0, N);
    graph.add_task(meta, {a, b}, {out},
        [a, b, out](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                SushiRuntime::detail::enqueue_host_task(h, [=]
                {
                    for (size_t i = 0; i < N; ++i) out[i] = a[i] + b[i];
                });
            });
        });

    // A side thread runs the graph so that a hang fails the test by timeout; see README.md.
    auto compiled = std::make_shared<CompiledGraph>(graph.compile());
    std::promise<void> done;
    auto fut = done.get_future();
    std::thread runner([&]
    {
        master_ctx->get_execution_engine().execute(*compiled);
        done.set_value();
    });

    const auto status = fut.wait_for(std::chrono::seconds(10));
    EXPECT_EQ(status, std::future_status::ready)
        << "execute() hung: a lost buffer push must time out, not block forever.";

    if (status == std::future_status::ready)
        runner.join();
    else
        runner.detach(); // leak rather than join a wedged thread; the test already failed

    agent.stop();
    sycl::free(a, mq);
    sycl::free(b, mq);
    sycl::free(out, mq);
}

/**
 * @brief Tests the fluent API for cluster offload.
 *
 * Proves the fluent surface can build and run a distributed graph end to end with a local fallback without dropping to the low-level TaskGraph.
 */
TEST(Integration_DistributedE2ETest, FluentClusterOffload)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    g_worker_runs.store(0, std::memory_order_relaxed);

    InProcessFabric fabric(1);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
    auto worker_transport = std::make_shared<InProcessTransport>(fabric, 0);

    auto worker_ctx = std::make_unique<RuntimeContext>();
    auto rt = std::make_unique<Runtime>(Runtime::create());

    auto worker_kernels = make_sushi<KernelRegistry>();
    register_vec_add(*worker_kernels);
    WorkerAgent agent(*worker_ctx, worker_transport, worker_kernels);

    auto cluster = rt->cluster(master_transport, make_sushi<KernelRegistry>(),
                               std::make_shared<LeastLoadedOffloadPolicy>(0));
    agent.start();
    ASSERT_TRUE(cluster.wait_for_workers(1, std::chrono::seconds(3)))
        << "Worker never registered with the fluent cluster.";

    constexpr size_t N = 1024;
    auto a   = rt->buffer<float>(N).distributed();
    auto b   = rt->buffer<float>(N).distributed();
    auto out = rt->buffer<float>(N).distributed();
    for (size_t i = 0; i < N; ++i) { a[i] = float(i); b[i] = float(2 * i); out[i] = -1.0f; }

    float* ap = a.data();
    float* bp = b.data();
    float* op = out.data();

    auto g = rt->graph();
    g.add(VEC_ADD, N,
          { a.dependency_key(), b.dependency_key() }, { out.dependency_key() },
          [ap, bp, op](std::size_t i) { op[i] = ap[i] + bp[i]; },
          { static_cast<std::uint64_t>(N) });
    g.run();

    EXPECT_GE(cluster.offloaded(), 1u) << "Fluent task should have been offloaded.";
    bool correct = true;
    for (size_t i = 0; i < N; ++i)
        if (out[i] != a[i] + b[i]) { correct = false; break; }
    EXPECT_TRUE(correct) << "Fluent offloaded result is wrong.";

    agent.stop();
}

/** @brief Tests offload of a double-buffered State across even and odd step counts. */
TEST(Integration_DistributedE2ETest, FluentDistributedStateParity)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    constexpr std::size_t N = 1024;

    std::vector<int> even_result, odd_result;
    std::uint64_t off_even = 0, off_odd = 0;
    g_state_worker_runs.store(0, std::memory_order_relaxed);
    even_result = evolve_distributed_state(N, 4, off_even);   // even step count
    g_state_worker_runs.store(0, std::memory_order_relaxed);
    odd_result  = evolve_distributed_state(N, 5, off_odd);    // odd step count

    EXPECT_GE(off_even, 1u) << "Even-step State should have offloaded at least once.";
    EXPECT_GE(off_odd, 1u)  << "Odd-step State should have offloaded at least once.";

    for (std::size_t i = 0; i < N; ++i)
    {
        const int base = static_cast<int>(i);
        const int step = static_cast<int>(i % 7);
        EXPECT_EQ(even_result[i], base + 4 * step)
            << "Even-step distributed State result wrong at " << i;
        EXPECT_EQ(odd_result[i], base + 5 * step)
            << "Odd-step distributed State result wrong at " << i;
    }
}
