/**************************************************************************/
/* distributed_demo.cpp                                                   */
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
 * @file distributed_demo.cpp
 * @brief Runs the distributed layer's master or worker role over TCP with a vector-add kernel.
 * @author Mustafa Garip
 * @see applications/examples/README.md
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sycl/sycl_compat.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/distributed/tcp_transport.hpp>
#include <SushiRuntime/distributed/cluster.hpp>
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
    constexpr auto VEC_ADD = "vec.add"_op;
    std::atomic<bool> g_stop{false};

    // out[i] = a[i] + b[i] over network-resolved local USM buffers.
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
                    });
                });
            });
    }

    // Minimal "--key=value" / "--key value" argv lookup.
    std::string arg_value(int argc, char** argv, const std::string& key, const std::string& def)
    {
        const std::string pfx = key + "=";
        for (int i = 1; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a.rfind(pfx, 0) == 0)
                return a.substr(pfx.size());
            if (a == key && i + 1 < argc)
                return argv[i + 1];
        }
        return def;
    }

    int run_worker(const std::string& master_host, uint16_t port)
    {
        RuntimeContext ctx;
        auto transport = TcpTransport::make_worker(master_host, port);
        auto kernels   = make_sushi<KernelRegistry>();
        register_vec_add(*kernels);

        WorkerAgent agent(ctx, transport, kernels);
        agent.start();
        std::cout << "[worker] connected to " << master_host << ":" << port
                  << " — waiting for tasks (Ctrl-C to stop)\n";

        std::signal(SIGINT,  [](int) { g_stop.store(true); });
        std::signal(SIGTERM, [](int) { g_stop.store(true); });
        while (!g_stop.load())
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

        std::cout << "[worker] stopping\n";
        agent.stop();
        return 0;
    }

    int run_master(uint16_t port, int workers, size_t n)
    {
        RuntimeContext ctx;

        auto transport = TcpTransport::make_master(port, workers);
        std::cout << "[master] listening on port " << transport->bound_port()
                  << " for " << workers << " worker(s)\n";

        auto policy = std::make_shared<LeastLoadedOffloadPolicy>(0);
        auto cluster = Distributed::Cluster::attach(ctx, transport,
                                                    make_sushi<KernelRegistry>(), policy);

        // Wait for the cluster to assemble.
        for (int i = 0; i < 600 && cluster.alive_workers() < workers; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (cluster.alive_workers() < workers)
        {
            std::cerr << "[master] only " << cluster.alive_workers()
                      << "/" << workers << " workers registered; aborting.\n";
            return 1;
        }
        std::cout << "[master] " << cluster.alive_workers() << " worker(s) ready\n";

        sycl::queue& q = ctx.devices().queue(0);
        float* a   = sycl::malloc_shared<float>(n, q);
        float* b   = sycl::malloc_shared<float>(n, q);
        float* out = sycl::malloc_shared<float>(n, q);
        for (size_t i = 0; i < n; ++i)
        {
            a[i]   = float(i);
            b[i]   = float(2 * i);
            out[i] = 0.0f;
        }

        ctx.offload()->share(a,   n * sizeof(float));
        ctx.offload()->share(b,   n * sizeof(float));
        ctx.offload()->share(out, n * sizeof(float));

        TaskGraph graph(ctx.offload());
        TaskMetadata meta{};
        meta.name  = "demo_vec_add";
        meta.op_id = VEC_ADD;
        meta.set_param<uint64_t>(0, n);
        graph.add_task(meta, {a, b}, {out},
            [a, b, out, n](sycl::queue& qq, const std::vector<sycl::event>&) -> sycl::event
            {
                return qq.submit([&](sycl::handler& h)
                {
                    SushiRuntime::detail::enqueue_host_task(h, [=]
                    {
                        for (size_t i = 0; i < n; ++i)
                            out[i] = a[i] + b[i];
                    });
                });
            });

        ctx.get_execution_engine().execute(graph.compile());

        bool ok = true;
        for (size_t i = 0; i < n; ++i)
        {
            if (out[i] != a[i] + b[i])
            {
                ok = false;
                break;
            }
        }

        std::cout << "[master] result " << (ok ? "CORRECT" : "WRONG")
                  << " | offloaded=" << cluster.offloaded()
                  << " declined=" << cluster.declined()
                  << " | out[1]=" << out[1] << " out[" << (n - 1) << "]=" << out[n - 1] << "\n";

        sycl::free(a, q);
        sycl::free(b, q);
        sycl::free(out, q);
        return ok ? 0 : 2;
    }
}

int main(int argc, char** argv)
{
    const std::string role = arg_value(argc, argv, "--distributed", "master");
    const size_t      n    = std::strtoul(arg_value(argc, argv, "--n", "1024").c_str(), nullptr, 10);

    try
    {
        if (role == "worker")
        {
            std::string master = arg_value(argc, argv, "--master", "127.0.0.1:5555");
            auto colon = master.find(':');
            std::string host = master.substr(0, colon);
            uint16_t    port = static_cast<uint16_t>(
                std::strtoul(colon == std::string::npos ? "5555" : master.substr(colon + 1).c_str(),
                             nullptr, 10));
            return run_worker(host, port);
        }

        const uint16_t port    = static_cast<uint16_t>(
            std::strtoul(arg_value(argc, argv, "--port", "5555").c_str(), nullptr, 10));
        const int      workers = static_cast<int>(
            std::strtoul(arg_value(argc, argv, "--workers", "1").c_str(), nullptr, 10));
        return run_master(port, workers, n);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[fatal] " << e.what() << "\n";
        return 1;
    }
}
