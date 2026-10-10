/**************************************************************************/
/* test_distributed_jepsen.cpp                                            */
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
 * @file test_distributed_jepsen.cpp
 * @brief Tests that an offload graph under seeded message faults ends byte-equal to a clean run.
 * @see modules/extension/distributed/tests/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/core/span.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <random>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/distributed/in_process_transport.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
#include <SushiRuntime/distributed/kernel_registry.hpp>
#include <SushiRuntime/distributed/worker_agent.hpp>
#include <SushiRuntime/distributed/i_offload_policy.hpp>

#include "test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Graph;
using namespace SushiRuntime::Graph::Literals;
using namespace SushiRuntime::Distributed;

namespace
{
    constexpr auto VEC_DOUBLE = "vec.double"_op;

    /**
     * @brief Registers the vec.double kernel.
     *
     * Computes o[i] = a[i] * 2 over network-resolved local USM buffers.
     * @param reg The registry to register the kernel with.
     */
    void register_vec_double(KernelRegistry& reg)
    {
        reg.register_kernel(VEC_DOUBLE,
            [](sycl::queue& q, SushiRuntime::span<void* const> ins, SushiRuntime::span<void* const> outs,
               const TaskMetadata& meta) -> sycl::event
            {
                auto* a = static_cast<const float*>(ins[0]);
                auto* o = static_cast<float*>(outs[0]);
                const size_t n = meta.get_param<uint64_t>(0);
                return q.submit([&](sycl::handler& h)
                {
                    SushiRuntime::detail::enqueue_host_task(h, [=]
                    {
                        for (size_t i = 0; i < n; ++i) o[i] = a[i] * 2.0f;
                    });
                });
            });
    }

    /**
     * @brief Runs one randomized Jepsen scenario.
     * @param seed The random seed for the nemesis fault injector.
     * @return Empty string on success, or a failure reason description.
     */
    std::string run_one_scenario(uint64_t seed)
    {
        constexpr int    W = 2;     // workers
        constexpr int    K = 4;     // independent offloadable tasks
        constexpr size_t N = 128;   // elements per buffer

        auto master_ctx = std::make_unique<RuntimeContext>();
        auto w0_ctx = std::make_unique<RuntimeContext>();
        auto w1_ctx = std::make_unique<RuntimeContext>();

        sycl::queue& mq = master_ctx->devices().queue(0);
        float* in = sycl::malloc_shared<float>(N, mq);
        std::vector<float*> y(K, nullptr);
        if (!in) return "alloc in failed";
        for (int k = 0; k < K; ++k)
        {
            y[k] = sycl::malloc_shared<float>(N, mq);
            if (!y[k]) return "alloc y failed";
        }
        for (size_t i = 0; i < N; ++i) { in[i] = float(i) + 1.0f; for (int k = 0; k < K; ++k) y[k][i] = -1.0f; }

        InProcessFabric fabric(W);
        auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
        auto wt0 = std::make_shared<InProcessTransport>(fabric, 0);
        auto wt1 = std::make_shared<InProcessTransport>(fabric, 1);

        auto k0 = make_sushi<KernelRegistry>();
        auto k1 = make_sushi<KernelRegistry>();
        register_vec_double(*k0);
        register_vec_double(*k1);
        WorkerAgent a0(*w0_ctx, wt0, k0);
        WorkerAgent a1(*w1_ctx, wt1, k1);
        a0.set_heartbeat_interval(std::chrono::milliseconds(40));
        a1.set_heartbeat_interval(std::chrono::milliseconds(40));
        a0.set_pull_timeout(std::chrono::milliseconds(120));
        a1.set_pull_timeout(std::chrono::milliseconds(120));

        auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
        auto cluster = Distributed::Cluster::attach(*master_ctx, master_transport,
                                                    make_sushi<KernelRegistry>(), policy);
        cluster.liveness_timeout(std::chrono::milliseconds(250));
        cluster.task_deadline(std::chrono::milliseconds(300));
        a0.start();
        a1.start();

        // The cluster assembles before any fault is injected; see README.md.
        bool both = false;
        for (int i = 0; i < 500 && !both; ++i)
        {
            both = cluster.alive_workers() == W;
            if (!both) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!both) { a0.stop(); a1.stop(); sycl::free(in, mq); for (auto p : y) sycl::free(p, mq);
                     return "cluster did not assemble"; }

        master_ctx->offload()->share(in, N * sizeof(float));
        for (int k = 0; k < K; ++k)
            master_ctx->offload()->share(y[k], N * sizeof(float));

        // The fabric mutex serializes the fault policy, so one shared RNG is safe; see README.md.
        auto rng = std::make_shared<std::mt19937_64>(seed);
        fabric.set_fault_policy([rng](int, int, MsgType) -> FaultAction
        {
            std::uniform_int_distribution<int> d(0, 99);
            int r = d(*rng);
            if (r < 8)  return FaultAction::Drop;       // 8%  lost
            if (r < 12) return FaultAction::Duplicate;  // 4%  retransmit
            if (r < 20) return FaultAction::Hold;       // 8%  delayed/reordered
            return FaultAction::Deliver;
        });

        // Background nemesis: drains held traffic (reordered) and flaps partitions.
        std::atomic<bool> nem_running{true};
        std::thread nemesis([&]
        {
            std::mt19937_64 nrng(seed ^ 0x9e3779b97f4a7c15ull);
            while (nem_running.load())
            {
                fabric.flush_held(nrng());
                if ((nrng() & 7) == 0)
                {
                    int w = static_cast<int>(nrng() % W);
                    fabric.set_partitioned(w, true);
                    std::this_thread::sleep_for(std::chrono::milliseconds(60));
                    fabric.set_partitioned(w, false);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
        });

        TaskGraph graph(master_ctx->offload());
        for (int k = 0; k < K; ++k)
        {
            float* yk = y[k];
            TaskMetadata m{};
            m.name  = "double";
            m.op_id = VEC_DOUBLE;
            m.set_param<uint64_t>(0, N);
            graph.add_task(m, {in}, {yk},
                [in, yk](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
                {
                    return q.submit([&](sycl::handler& h)
                    {
                        SushiRuntime::detail::enqueue_host_task(h, [=]
                        {
                            for (size_t i = 0; i < N; ++i) yk[i] = in[i] * 2.0f;
                        });
                    });
                });
        }

        // A side thread runs the graph, so a liveness regression fails by timeout; see README.md.
        auto compiled = std::make_shared<CompiledGraph>(graph.compile());
        std::promise<void> done;
        auto fut = done.get_future();
        std::thread runner([&]
        {
            master_ctx->get_execution_engine().execute(*compiled);
            done.set_value();
        });

        const bool returned = fut.wait_for(std::chrono::seconds(30)) == std::future_status::ready;

        // In-flight deliveries settle before results are read and memory is freed; see README.md.
        nem_running.store(false);
        if (nemesis.joinable()) nemesis.join();
        for (int w = 0; w < W; ++w) fabric.set_partitioned(w, false);
        fabric.set_fault_policy(nullptr);
        fabric.flush_held();
        std::string result;
        if (!returned)
        {
            result = "execute() hung under faults (liveness violation)";
            runner.detach(); // already failed; do not join a wedged thread
        }
        else
        {
            runner.join();
            a0.stop();
            a1.stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(30)); // drain late delivery
            for (int k = 0; k < K && result.empty(); ++k)
                for (size_t i = 0; i < N; ++i)
                    if (y[k][i] != (float(i) + 1.0f) * 2.0f)
                    {
                        result = "safety violation: y[" + std::to_string(k) + "][" +
                                 std::to_string(i) + "] = " + std::to_string(y[k][i]);
                        break;
                    }
        }

        if (returned) { /* agents already stopped */ }
        sycl::free(in, mq);
        for (auto p : y) sycl::free(p, mq);
        return result;
    }
}

/**
 * @brief Soaks across several random seeds to assert determinism.
 *
 * Every randomized fault schedule must end with the correct (fault-free) result and a run that actually returned.
 */
TEST(Integration_DistributedJepsenTest, DeterminismUnderRandomFaults)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    for (uint64_t seed = 1; seed <= 8; ++seed)
    {
        std::string r = run_one_scenario(seed);
        EXPECT_TRUE(r.empty()) << "seed " << seed << ": " << r;
    }
}

/**
 * @brief Tests that registration survives severe message loss.
 *
 * A fault policy drops traffic before the worker connects. The worker must keep re-announcing until acknowledged, and the offloaded task must still produce the correct result.
 */
TEST(Integration_DistributedJepsenTest, RegistrationSurvivesMessageLoss)
{
    if (!SushiRuntime::Test::has_any_sycl_device())
        GTEST_SKIP() << "SYCL reports no device.";

    auto master_ctx = std::make_unique<RuntimeContext>();
    auto worker_ctx = std::make_unique<RuntimeContext>();

    constexpr size_t N = 128;
    sycl::queue& mq = master_ctx->devices().queue(0);
    float* in  = sycl::malloc_shared<float>(N, mq);
    float* out = sycl::malloc_shared<float>(N, mq);
    ASSERT_NE(in, nullptr);
    ASSERT_NE(out, nullptr);
    for (size_t i = 0; i < N; ++i) { in[i] = float(i) + 1.0f; out[i] = -1.0f; }

    InProcessFabric fabric(1);
    auto master_transport = std::make_shared<InProcessTransport>(fabric, -1);
    auto wt = std::make_shared<InProcessTransport>(fabric, 0);

    // Drop ~20% of everything, from the very start — registration included.
    auto rng = std::make_shared<std::mt19937_64>(12345);
    fabric.set_fault_policy([rng](int, int, MsgType) -> FaultAction
    {
        std::uniform_int_distribution<int> d(0, 99);
        return d(*rng) < 20 ? FaultAction::Drop : FaultAction::Deliver;
    });

    auto kernels = make_sushi<KernelRegistry>();
    register_vec_double(*kernels);
    WorkerAgent agent(*worker_ctx, wt, kernels);
    agent.set_heartbeat_interval(std::chrono::milliseconds(30)); // fast HELLO retry
    agent.set_pull_timeout(std::chrono::milliseconds(150));

    auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
    auto cluster = Distributed::Cluster::attach(*master_ctx, master_transport,
                                                make_sushi<KernelRegistry>(), policy);
    cluster.task_deadline(std::chrono::milliseconds(300));
    agent.start();

    // The HELLO retry must punch through the loss within a generous window.
    bool up = false;
    for (int i = 0; i < 500 && !up; ++i)
    {
        up = cluster.alive_workers() == 1;
        if (!up) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_TRUE(up) << "Cluster never assembled under registration loss.";

    master_ctx->offload()->share(in,  N * sizeof(float));
    master_ctx->offload()->share(out, N * sizeof(float));

    TaskGraph graph(master_ctx->offload());
    TaskMetadata m{};
    m.name  = "double";
    m.op_id = VEC_DOUBLE;
    m.set_param<uint64_t>(0, N);
    graph.add_task(m, {in}, {out},
        [in, out](sycl::queue& q, const std::vector<sycl::event>&) -> sycl::event
        {
            return q.submit([&](sycl::handler& h)
            {
                SushiRuntime::detail::enqueue_host_task(h, [=]
                { for (size_t i = 0; i < N; ++i) out[i] = in[i] * 2.0f; });
            });
        });

    auto compiled = std::make_shared<CompiledGraph>(graph.compile());
    std::promise<void> done;
    auto fut = done.get_future();
    std::thread runner([&] { master_ctx->get_execution_engine().execute(*compiled); done.set_value(); });
    const bool returned = fut.wait_for(std::chrono::seconds(30)) == std::future_status::ready;
    EXPECT_TRUE(returned) << "execute() hung under registration loss.";
    if (returned) runner.join(); else runner.detach();

    fabric.set_fault_policy(nullptr);
    agent.stop();
    if (returned)
        for (size_t i = 0; i < N; ++i)
            ASSERT_EQ(out[i], (float(i) + 1.0f) * 2.0f) << "wrong result at " << i;

    sycl::free(in, mq);
    sycl::free(out, mq);
}
