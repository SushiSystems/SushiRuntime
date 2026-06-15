/**************************************************************************/
/* test_steal_numa_effect_perf.cpp                                        */
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

// Quantifies NUMA-aware vs NUMA-blind stealing on the SAME hardware: the same
// workload runs once on the flat (real) topology and once on a synthetic 2-node
// topology, reporting both throughputs and the aware-side steal split. On a UMA
// box (single socket / WSL) there's no real speedup to claim, so it asserts only
// correctness + that the aware path keeps steals node-local.

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
            if (std::chrono::steady_clock::now() > deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    std::shared_ptr<RuntimeContext> make_synthetic_ctx(int nodes)
    {
        auto provider = std::make_shared<RemappingTopologyProvider>(
            std::make_shared<HwlocTopologyProvider>(), nodes);
        std::vector<std::vector<int>> matrix(nodes, std::vector<int>(nodes, 21));
        for (int i = 0; i < nodes; ++i)
            matrix[i][i] = 10;
        auto distance = std::make_shared<MatrixDistanceModel>(std::move(matrix));
        return std::make_shared<RuntimeContext>(
            Topology::HardwareFilterConfig{}, provider, distance);
    }

    struct Result
    {
        double ms = 1e300;
        size_t local = 0;
        size_t remote = 0;
    };

    // Drain TOTAL counter tasks through ctx's scheduler, best-of-N for the time,
    // keeping the steal split from the fastest round.
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

// Same workload, same cores: report the throughput delta between the flat and
// the synthetic-NUMA-aware scheduler, plus the aware steal locality.
TEST(Performance_StealNumaEffectBench, NumaAwareVsBlindThroughput)
{
    RuntimeContext blind_ctx; // real (flat on single-socket) topology
    const int workers = static_cast<int>(TaskScheduler(blind_ctx).get_worker_count());
    if (workers < 2)
        GTEST_SKIP() << "Need >= 2 worker threads to exercise stealing.";

    auto aware_ctx = make_synthetic_ctx(2);

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

    // No speedup assertion — on a UMA box there is no real locality benefit.
    EXPECT_GT(blind_thr, 0.0);
    EXPECT_GT(aware_thr, 0.0);
    if (a_steals > 1000)
        EXPECT_GT(a_localf, 0.5)
            << "Synthetic-NUMA stealing crossed nodes too often — locality broken.";
}
