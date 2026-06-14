/**************************************************************************/
/* test_steal_numa_effect_perf.cpp                                        */
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
 * @file test_steal_numa_effect_perf.cpp
 * @brief Quantifies NUMA-aware vs NUMA-blind work stealing on the SAME hardware.
 *
 * test_steal_locality_perf reports the steal *fraction* but never quantifies the
 * benefit. This benchmark runs the identical workload twice on the same machine:
 *   * NUMA-blind : the real (flat) topology — every worker is one tier.
 *   * NUMA-aware : a synthetic 2-node topology (RemappingTopologyProvider, core
 *                  i -> i % 2) with a 10-local / 21-remote distance matrix, so
 *                  the scheduler builds a hierarchical victim order.
 * It reports both throughputs, the speedup, and the aware-side steal split.
 *
 * FAIRNESS / HONESTY. The workload, task type, task count and physical core
 * count are identical across the two runs — only the topology *model* changes.
 * Crucially, on a single physical NUMA node (the Ryzen dev box, anything under
 * WSL) memory latency is genuinely uniform, so the synthetic split cannot
 * produce a real speedup — and the benchmark does NOT pretend otherwise: it
 * prints the delta and asserts only correctness + that the aware path keeps its
 * steals node-local (the policy is doing what it claims). A real speedup is only
 * expected on bare-metal multi-socket hardware, where this same code measures it.
 * Each side is timed best-of-N; the counter is asserted so work can't be elided.
 */

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/topology/hwloc_topology_provider.hpp>
#include <SushiRuntime/topology/matrix_distance_model.hpp>
#include <SushiRuntime/topology/remapping_topology_provider.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using namespace SushiRuntime::Topology;
using SushiRuntime::Test::AtomicCounterTask;

namespace
{
    constexpr size_t TOTAL  = 300000;
    constexpr int    ROUNDS = 3;

    bool wait_for(const std::atomic<size_t>& value, size_t target,
                  std::chrono::seconds budget)
    {
        auto deadline = std::chrono::steady_clock::now() + budget;
        while (value.load(std::memory_order_acquire) < target)
        {
            if (std::chrono::steady_clock::now() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    std::shared_ptr<RuntimeContext> make_synthetic_ctx(int nodes)
    {
        auto provider = std::make_shared<RemappingTopologyProvider>(
            std::make_shared<HwlocTopologyProvider>(), nodes);
        std::vector<std::vector<int>> matrix(nodes, std::vector<int>(nodes, 21));
        for (int i = 0; i < nodes; ++i) matrix[i][i] = 10;
        auto distance = std::make_shared<MatrixDistanceModel>(std::move(matrix));
        return std::make_shared<RuntimeContext>(
            Topology::HardwareFilterConfig{}, provider, distance);
    }

    struct Result { double ms = 1e300; size_t local = 0; size_t remote = 0; };

    // Drains TOTAL counter tasks through `ctx`'s scheduler, best-of-N for the
    // time, accumulating the steal split from the fastest round.
    Result run_workload(RuntimeContext& ctx)
    {
        Result best;
        std::atomic<size_t> executed{0};
        for (int r = 0; r < ROUNDS; ++r)
        {
            TaskScheduler sched(ctx);
            executed.store(0, std::memory_order_relaxed);
            sched.start();
            sched.arm_for_execution();

            const auto t0 = std::chrono::steady_clock::now();
            for (size_t i = 0; i < TOTAL; ++i)
                sched.submit(new AtomicCounterTask(executed));
            EXPECT_TRUE(wait_for(executed, TOTAL, std::chrono::seconds(60)));
            const double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();

            sched.stop();
            EXPECT_EQ(executed.load(), TOTAL); // consume

            if (ms < best.ms)
                best = Result{ ms, sched.get_local_steal_count(),
                                   sched.get_remote_steal_count() };
        }
        return best;
    }
}

/**
 * @test NumaAwareVsBlindThroughput
 * @brief Same workload, same cores: report the throughput delta between the flat
 *        and the synthetic-NUMA-aware scheduler, plus the aware steal locality.
 */
TEST(Performance_StealNumaEffectBench, NumaAwareVsBlindThroughput)
{
    RuntimeContext blind_ctx; // real (flat on single-socket) topology
    const int workers = static_cast<int>(TaskScheduler(blind_ctx).get_worker_count());
    if (workers < 2)
        GTEST_SKIP() << "Need >= 2 worker threads to exercise stealing.";

    auto aware_ctx = make_synthetic_ctx(/*nodes=*/2);

    const Result blind = run_workload(blind_ctx);
    const Result aware = run_workload(*aware_ctx);

    const double blind_thr = double(TOTAL) / (blind.ms / 1000.0);
    const double aware_thr = double(TOTAL) / (aware.ms / 1000.0);
    const double speedup   = blind.ms / aware.ms;
    const size_t a_steals  = aware.local + aware.remote;
    const double a_localf  = a_steals ? double(aware.local) / double(a_steals) : 1.0;

    std::printf("\n=== NUMA-aware vs blind stealing (%zu tasks, %d workers) ===\n",
                TOTAL, workers);
    std::printf("  NUMA-blind : %10.0f tasks/s  (%.2f ms)\n", blind_thr, blind.ms);
    std::printf("  NUMA-aware : %10.0f tasks/s  (%.2f ms)\n", aware_thr, aware.ms);
    std::printf("  Speedup    : %.3fx (>1 only expected on real multi-socket)\n", speedup);
    std::printf("  Aware steal split: local=%zu remote=%zu (local frac %.3f)\n",
                aware.local, aware.remote, a_localf);

    // Honest assertions: both runs completed correctly and the aware path keeps
    // its steals node-local. No speedup assertion — on a UMA box there is no
    // real locality benefit to claim.
    EXPECT_GT(blind_thr, 0.0);
    EXPECT_GT(aware_thr, 0.0);
    if (a_steals > 1000)
        EXPECT_GT(a_localf, 0.5)
            << "Synthetic-NUMA stealing crossed nodes too often — locality broken.";
}
