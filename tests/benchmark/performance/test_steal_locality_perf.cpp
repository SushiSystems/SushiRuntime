/**************************************************************************/
/* test_steal_locality_perf.cpp                                           */
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
 * @file test_steal_locality_perf.cpp
 * @brief Performance/behaviour benchmark for NUMA-aware hierarchical stealing.
 *
 * Drives the work-stealing scheduler with a large batch of host tasks and reports
 * throughput plus the local-vs-remote steal split. NUMA-aware stealing should keep
 * the vast majority of steals node-local; a high remote fraction means the locality
 * policy is not holding.
 *
 * SCOPE & SKIP: the locality benefit only exists across real NUMA nodes, and only
 * bare-metal multi-socket hardware exposes them. On a single-node box (e.g. the
 * Ryzen 7600X dev machine, or anything under WSL) every worker is on node 0, so the
 * split is meaningless — the test SKIPS. The synthetic-topology *logic* check that
 * runs everywhere lives in NumaStealTest; this benchmark is the real-hardware
 * counterpart.
 */

#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <cstdio>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>

#include "../../common/test_helpers.hpp"

using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using SushiRuntime::Test::AtomicCounterTask;

namespace
{
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

    int distinct_numa_nodes(const std::vector<int>& core_to_numa)
    {
        std::set<int> nodes(core_to_numa.begin(), core_to_numa.end());
        return static_cast<int>(nodes.size());
    }
}

/**
 * @test StealStaysNodeLocal
 * @brief Under a heavy host-task load on real multi-NUMA hardware, the scheduler
 *        must satisfy most steals from node-local victims.
 */
TEST(Performance_StealLocalityBench, StealStaysNodeLocal)
{
    RuntimeContext ctx;

    const int nodes = distinct_numa_nodes(ctx.cpu_core_to_numa());
    if (nodes < 2)
        GTEST_SKIP() << "Single NUMA node (" << nodes << ") — steal locality is "
                        "unmeasurable here; runs only on bare-metal multi-socket.";

    TaskScheduler sched(ctx);

    std::atomic<size_t> executed{0};
    constexpr size_t TOTAL = 300000;

    sched.start();
    sched.arm_for_execution();

    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < TOTAL; ++i)
        sched.submit(new AtomicCounterTask(executed));

    ASSERT_TRUE(wait_for(executed, TOTAL, std::chrono::seconds(30)))
        << "Scheduler did not drain the steal-locality workload in time.";
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    sched.stop();

    const size_t local  = sched.get_local_steal_count();
    const size_t remote = sched.get_remote_steal_count();
    const size_t steals = local + remote;
    const double local_frac = steals ? double(local) / double(steals) : 1.0;
    const double thr = ms > 0 ? (double(TOTAL) / (ms / 1000.0)) : 0.0;

    std::printf("\n=== Steal locality (%d NUMA nodes, %zu tasks) ===\n", nodes, TOTAL);
    std::printf("  Throughput   : %10.0f tasks/s  (%.2f ms)\n", thr, ms);
    std::printf("  Local steals : %zu\n", local);
    std::printf("  Remote steals: %zu\n", remote);
    std::printf("  Local frac   : %.3f\n", local_frac);

    EXPECT_EQ(executed.load(), TOTAL);

    // Only assert locality once there were enough steals for the ratio to mean
    // something; a perfectly balanced drain can legitimately produce few steals.
    if (steals > 1000)
        EXPECT_GT(local_frac, 0.5)
            << "Most steals crossed NUMA nodes — hierarchical locality is not holding.";
}
