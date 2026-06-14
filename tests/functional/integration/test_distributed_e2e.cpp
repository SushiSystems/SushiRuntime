/**************************************************************************/
/* test_distributed_e2e.cpp                                               */
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
 * @file test_distributed_e2e.cpp
 * @brief End-to-end offload over the in-process fabric, on a real SYCL device.
 *
 * A master and one in-process worker share the same process. A vector-add
 * kernel is registered under an OpID in the worker; the master registers its
 * input/output USM buffers, builds a one-task graph, and executes it. This
 * exercises the entire chain together: compile() offload-stamping → dispatch
 * branch → buffer pull → remote kernel → eager result writeback → successor
 * release → graph completion. An atomic counter inside the kernel proves the
 * work actually ran on the worker, not via the local fallback.
 */

#include <span>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
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
    // Bumped once per kernel invocation on the worker, so the test can prove the
    // task ran remotely rather than through the local host fallback.
    std::atomic<int> g_worker_runs{0};

    constexpr auto VEC_ADD = "vec.add"_op;

    // Registers out[i] = a[i] + b[i] over network-resolved local USM buffers.
    void register_vec_add(KernelRegistry& reg)
    {
        reg.register_kernel(VEC_ADD,
            [](sycl::queue& q, std::span<void* const> ins, std::span<void* const> outs,
               const TaskMetadata& meta) -> sycl::event
            {
                auto* a = static_cast<const float*>(ins[0]);
                auto* b = static_cast<const float*>(ins[1]);
                auto* o = static_cast<float*>(outs[0]);
                const size_t n = meta.get_param<uint64_t>(0);

                return q.submit([&](sycl::handler& h)
                {
                    h.host_task([=]
                    {
                        for (size_t i = 0; i < n; ++i) o[i] = a[i] + b[i];
                        g_worker_runs.fetch_add(1, std::memory_order_relaxed);
                    });
                });
            });
    }
}

/**
 * @test OffloadVectorAddRoundTrip
 * @brief One offloaded vector-add task: data goes master→worker, the kernel
 *        runs on the worker, results come back, and the graph completes.
 */
TEST(Integration_DistributedE2ETest, OffloadVectorAddRoundTrip)
{
    // The runtime needs at least one SYCL device. On CI (intel/llvm + OpenCL CPU)
    // this is present; skip cleanly if a device is unavailable.
    std::unique_ptr<RuntimeContext> master_ctx, worker_ctx;
    try
    {
        master_ctx = std::make_unique<RuntimeContext>();
        worker_ctx = std::make_unique<RuntimeContext>();
    }
    catch (const std::exception& e)
    {
        GTEST_SKIP() << "No SYCL device available: " << e.what();
    }

    g_worker_runs.store(0, std::memory_order_relaxed);

    constexpr size_t N = 1024;
    sycl::queue& mq = master_ctx->get_queue(0);

    float* a   = sycl::malloc_shared<float>(N, mq);
    float* b   = sycl::malloc_shared<float>(N, mq);
    float* out = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < N; ++i) { a[i] = float(i); b[i] = float(2 * i); out[i] = -1.0f; }

    // --- wire up the in-process cluster (1 worker) ---------------------------
    // Construct both endpoints up front so they are registered on the fabric,
    // then bring the master up BEFORE starting the worker — otherwise the
    // worker's HELLO would be sent to a master endpoint that isn't serving yet
    // and would be dropped.
    InProcessFabric fabric(1);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, /*node=*/-1);
    auto worker_transport = std::make_shared<InProcessTransport>(fabric, /*node=*/0);

    auto worker_kernels = make_sushi<KernelRegistry>();
    register_vec_add(*worker_kernels);
    WorkerAgent agent(*worker_ctx, worker_transport, worker_kernels);

    auto master_kernels = make_sushi<KernelRegistry>(); // coordinator uses HELLO, not this
    // min_offload_bytes=0 so this small payload is actually offloaded.
    auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
    master_ctx->enable_distributed_master(master_transport, master_kernels, policy);

    agent.start(); // now the master is serving; HELLO will be received

    // Wait until the worker's HELLO has registered VEC_ADD with the coordinator.
    bool supported = false;
    for (int i = 0; i < 300 && !supported; ++i)
    {
        supported = master_ctx->distributed_supports(VEC_ADD);
        if (!supported) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(supported) << "Worker never registered the kernel with the master.";

    // --- register buffers + build the one-task graph -------------------------
    master_ctx->register_distributed_buffer(a,   N * sizeof(float));
    master_ctx->register_distributed_buffer(b,   N * sizeof(float));
    master_ctx->register_distributed_buffer(out, N * sizeof(float));

    TaskGraph graph(*master_ctx);
    TaskMetadata meta{};
    meta.name  = "vec_add";
    meta.op_id = VEC_ADD;
    meta.set_param<uint64_t>(0, N);

    graph.add_task(meta, /*reads=*/{a, b}, /*writes=*/{out},
        // Host fallback (same math) — only used if the task is NOT offloaded.
        [a, b, out](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                h.host_task([=] { for (size_t i = 0; i < N; ++i) out[i] = a[i] + b[i]; });
            });
        });

    auto compiled = graph.compile();
    master_ctx->get_execution_engine().execute(std::move(compiled));

    // --- verify --------------------------------------------------------------
    EXPECT_EQ(g_worker_runs.load(std::memory_order_relaxed), 1)
        << "Task should have run on the worker, not the local fallback.";

    bool correct = true;
    for (size_t i = 0; i < N; ++i)
        if (out[i] != a[i] + b[i]) { correct = false; break; }
    EXPECT_TRUE(correct) << "Offloaded result written back to the master is wrong.";

    agent.stop();
    sycl::free(a, mq);
    sycl::free(b, mq);
    sycl::free(out, mq);
}

/**
 * @test FanOutAcrossTwoWorkers
 * @brief Two independent offloadable tasks fan out to two distinct workers.
 *        Proves the multi-node core of the "5 machines" goal: the policy spreads
 *        load and each worker actually runs its share.
 */
TEST(Integration_DistributedE2ETest, FanOutAcrossTwoWorkers)
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

    // Per-worker run counters prove which worker executed which task. A short
    // sleep keeps both tasks in-flight so the least-loaded policy spreads them.
    std::atomic<int> c0{0}, c1{0};
    auto make_kernel = [](std::atomic<int>* counter)
    {
        return [counter](sycl::queue& q, std::span<void* const> ins, std::span<void* const> outs,
                         const TaskMetadata& meta) -> sycl::event
        {
            auto* a = static_cast<const float*>(ins[0]);
            auto* o = static_cast<float*>(outs[0]);
            const size_t n = meta.get_param<uint64_t>(0);
            return q.submit([&](sycl::handler& h)
            {
                h.host_task([=]
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    for (size_t i = 0; i < n; ++i) o[i] = a[i] + a[i];
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
    ASSERT_NE(in, nullptr); ASSERT_NE(y0, nullptr); ASSERT_NE(y1, nullptr);
    for (size_t i = 0; i < N; ++i) { in[i] = float(i); y0[i] = y1[i] = -1.0f; }

    InProcessFabric fabric(2);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
    auto w0_transport     = std::make_shared<InProcessTransport>(fabric, 0);
    auto w1_transport     = std::make_shared<InProcessTransport>(fabric, 1);

    auto k0 = make_sushi<KernelRegistry>(); k0->register_kernel(VEC_ADD, make_kernel(&c0));
    auto k1 = make_sushi<KernelRegistry>(); k1->register_kernel(VEC_ADD, make_kernel(&c1));
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
        if (!both) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(both) << "Both workers should have registered.";

    master_ctx->register_distributed_buffer(in, N * sizeof(float));
    master_ctx->register_distributed_buffer(y0, N * sizeof(float));
    master_ctx->register_distributed_buffer(y1, N * sizeof(float));

    TaskGraph graph(*master_ctx);
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
                { h.host_task([=] { for (size_t i = 0; i < N; ++i) y[i] = in[i] + in[i]; }); });
            });
    }

    auto compiled = graph.compile();
    master_ctx->get_execution_engine().execute(std::move(compiled));

    EXPECT_EQ(master_ctx->distributed_offloaded_count(), 2u);
    EXPECT_EQ(c0.load(), 1) << "Worker 0 should have run exactly one task.";
    EXPECT_EQ(c1.load(), 1) << "Worker 1 should have run exactly one task.";

    bool ok0 = true, ok1 = true;
    for (size_t i = 0; i < N; ++i)
    {
        if (y0[i] != 2.0f * in[i]) ok0 = false;
        if (y1[i] != 2.0f * in[i]) ok1 = false;
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
 * @test AutoVersionBumpAcrossLocalWriteThenOffloadedRead
 * @brief A local task writes a registered buffer X; a dependent offloaded task
 *        reads X (Y = X + X). Proves (1) the local write auto-bumps X's version
 *        so the worker pulls fresh data, and (2) the offloaded result reflects
 *        the locally-written values.
 */
TEST(Integration_DistributedE2ETest, AutoVersionBumpAcrossLocalWriteThenOffloadedRead)
{
    std::unique_ptr<RuntimeContext> master_ctx, worker_ctx;
    try
    {
        master_ctx = std::make_unique<RuntimeContext>();
        worker_ctx = std::make_unique<RuntimeContext>();
    }
    catch (const std::exception& e)
    {
        GTEST_SKIP() << "No SYCL device available: " << e.what();
    }

    g_worker_runs.store(0, std::memory_order_relaxed);

    constexpr size_t N = 512;
    sycl::queue& mq = master_ctx->get_queue(0);
    float* x = sycl::malloc_shared<float>(N, mq);
    float* y = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(x, nullptr);
    ASSERT_NE(y, nullptr);
    for (size_t i = 0; i < N; ++i) { x[i] = -7.0f; y[i] = 0.0f; }

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
        if (!supported) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(supported);

    master_ctx->register_distributed_buffer(x, N * sizeof(float));
    master_ctx->register_distributed_buffer(y, N * sizeof(float));

    const auto x_before = master_ctx->distributed_buffer(x);
    ASSERT_TRUE(x_before.has_value());

    TaskGraph graph(*master_ctx);

    // Task A: LOCAL writer (no op_id) — fills x[i] = i + 1.
    TaskMetadata write_meta{};
    write_meta.name = "local_fill";
    graph.add_task(write_meta, /*reads=*/{}, /*writes=*/{x},
        [x](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                h.host_task([=] { for (size_t i = 0; i < N; ++i) x[i] = float(i + 1); });
            });
        });

    // Task B: OFFLOADED reader — y = x + x. Depends on A via the RAW hazard on x.
    TaskMetadata add_meta{};
    add_meta.name  = "double_via_offload";
    add_meta.op_id = VEC_ADD;
    add_meta.set_param<uint64_t>(0, N);
    graph.add_task(add_meta, /*reads=*/{x, x}, /*writes=*/{y},
        [x, y](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                h.host_task([=] { for (size_t i = 0; i < N; ++i) y[i] = x[i] + x[i]; });
            });
        });

    auto compiled = graph.compile();
    master_ctx->get_execution_engine().execute(std::move(compiled));

    // The local write to x must have auto-bumped its version.
    const auto x_after = master_ctx->distributed_buffer(x);
    ASSERT_TRUE(x_after.has_value());
    EXPECT_GT(x_after->version, x_before->version)
        << "Local write to a registered buffer should auto-bump its version.";

    // The offloaded task ran on the worker and saw the freshly-written x.
    EXPECT_EQ(g_worker_runs.load(std::memory_order_relaxed), 1);
    bool correct = true;
    for (size_t i = 0; i < N; ++i)
        if (y[i] != 2.0f * float(i + 1)) { correct = false; break; }
    EXPECT_TRUE(correct) << "Offloaded reader did not see the locally-written values.";

    agent.stop();
    sycl::free(x, mq);
    sycl::free(y, mq);
}

/**
 * @test OffloadOverTcpLoopback
 * @brief The same vector-add offload, but over the real raw-socket TCP transport
 *        on 127.0.0.1. Proves the wire path that the 5-machine lab will use; the
 *        coordinator and worker agent are byte-for-byte the same as in-process.
 */
TEST(Integration_DistributedE2ETest, OffloadOverTcpLoopback)
{
    std::unique_ptr<RuntimeContext> master_ctx, worker_ctx;
    try
    {
        master_ctx = std::make_unique<RuntimeContext>();
        worker_ctx = std::make_unique<RuntimeContext>();
    }
    catch (const std::exception& e)
    {
        GTEST_SKIP() << "No SYCL device available: " << e.what();
    }

    g_worker_runs.store(0, std::memory_order_relaxed);

    constexpr size_t N = 1024;
    sycl::queue& mq = master_ctx->get_queue(0);
    float* a   = sycl::malloc_shared<float>(N, mq);
    float* b   = sycl::malloc_shared<float>(N, mq);
    float* out = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < N; ++i) { a[i] = float(i); b[i] = float(3 * i); out[i] = -1.0f; }

    // Master binds an OS-assigned port; the worker connects to it over loopback.
    auto master_transport = TcpTransport::make_master(/*port=*/0, /*workers=*/1);
    const uint16_t port   = master_transport->bound_port();
    ASSERT_NE(port, 0);
    auto worker_transport = TcpTransport::make_worker("127.0.0.1", port);

    auto worker_kernels = make_sushi<KernelRegistry>();
    register_vec_add(*worker_kernels);
    WorkerAgent agent(*worker_ctx, worker_transport, worker_kernels);

    auto master_kernels = make_sushi<KernelRegistry>();
    auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
    master_ctx->enable_distributed_master(master_transport, master_kernels, policy);
    agent.start(); // connects + HELLO over TCP

    bool supported = false;
    for (int i = 0; i < 500 && !supported; ++i)
    {
        supported = master_ctx->distributed_supports(VEC_ADD);
        if (!supported) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(supported) << "Worker never registered over TCP.";

    master_ctx->register_distributed_buffer(a,   N * sizeof(float));
    master_ctx->register_distributed_buffer(b,   N * sizeof(float));
    master_ctx->register_distributed_buffer(out, N * sizeof(float));

    TaskGraph graph(*master_ctx);
    TaskMetadata meta{};
    meta.name  = "vec_add_tcp";
    meta.op_id = VEC_ADD;
    meta.set_param<uint64_t>(0, N);
    graph.add_task(meta, {a, b}, {out},
        [a, b, out](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            { h.host_task([=] { for (size_t i = 0; i < N; ++i) out[i] = a[i] + b[i]; }); });
        });

    auto compiled = graph.compile();
    master_ctx->get_execution_engine().execute(std::move(compiled));

    EXPECT_EQ(g_worker_runs.load(std::memory_order_relaxed), 1);
    bool correct = true;
    for (size_t i = 0; i < N; ++i)
        if (out[i] != a[i] + b[i]) { correct = false; break; }
    EXPECT_TRUE(correct) << "Offloaded-over-TCP result is wrong.";

    agent.stop();
    sycl::free(a, mq);
    sycl::free(b, mq);
    sycl::free(out, mq);
}

/**
 * @test WorkerDeathFallsBackToLocal
 * @brief A worker accepts an offloaded task and then dies (stops heartbeating)
 *        without ever replying. The master must NOT hang: the liveness monitor
 *        declares the worker dead, re-dispatches the orphaned task, and — with no
 *        survivor — runs it locally, so the graph still completes with the right
 *        result. This is the core worker-death-resilience guarantee.
 *
 * A controllable fake worker (a bare transport endpoint, no SYCL needed on its
 * side) makes the timing deterministic: it heartbeats until it receives the
 * dispatch, then falls permanently silent.
 */
TEST(Integration_DistributedE2ETest, WorkerDeathFallsBackToLocal)
{
    std::unique_ptr<RuntimeContext> master_ctx;
    try
    {
        master_ctx = std::make_unique<RuntimeContext>();
    }
    catch (const std::exception& e)
    {
        GTEST_SKIP() << "No SYCL device available: " << e.what();
    }

    constexpr size_t N = 1024;
    sycl::queue& mq = master_ctx->get_queue(0);
    float* a   = sycl::malloc_shared<float>(N, mq);
    float* b   = sycl::malloc_shared<float>(N, mq);
    float* out = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(a, nullptr); ASSERT_NE(b, nullptr); ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < N; ++i) { a[i] = float(i); b[i] = float(2 * i); out[i] = -1.0f; }

    InProcessFabric fabric(1);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
    auto worker_transport = std::make_shared<InProcessTransport>(fabric, 0);

    // Fake worker: advertises VEC_ADD, heartbeats until it gets a dispatch, then
    // goes silent forever (never sends TASK_RESULT) — i.e. crashes mid-task.
    std::atomic<bool> got_dispatch{false};
    std::atomic<bool> hb_running{true};
    worker_transport->on_message(
        [&](int, MsgType type, std::span<const std::byte>)
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
        if (!supported) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(supported) << "Fake worker never registered the kernel.";

    master_ctx->register_distributed_buffer(a,   N * sizeof(float));
    master_ctx->register_distributed_buffer(b,   N * sizeof(float));
    master_ctx->register_distributed_buffer(out, N * sizeof(float));

    TaskGraph graph(*master_ctx);
    TaskMetadata meta{};
    meta.name  = "vec_add_death";
    meta.op_id = VEC_ADD;
    meta.set_param<uint64_t>(0, N);
    graph.add_task(meta, {a, b}, {out},
        [a, b, out](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            { h.host_task([=] { for (size_t i = 0; i < N; ++i) out[i] = a[i] + b[i]; }); });
        });

    // The key assertion is implicit: this call must RETURN (not deadlock) even
    // though the worker the task was offloaded to never answered.
    master_ctx->get_execution_engine().execute(graph.compile());

    EXPECT_TRUE(got_dispatch.load(std::memory_order_relaxed))
        << "Task should have been offloaded before the worker died.";
    EXPECT_GE(master_ctx->distributed_redispatched_count(), 1u)
        << "The dead worker's task should have been re-dispatched.";

    bool correct = true;
    for (size_t i = 0; i < N; ++i)
        if (out[i] != a[i] + b[i]) { correct = false; break; }
    EXPECT_TRUE(correct) << "Local fallback after worker death produced a wrong result.";

    hb_running.store(false);
    if (heartbeat.joinable()) heartbeat.join();
    worker_transport->stop();
    sycl::free(a, mq);
    sycl::free(b, mq);
    sycl::free(out, mq);
}
