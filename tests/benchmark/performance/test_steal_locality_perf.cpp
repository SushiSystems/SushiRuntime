/**************************************************************************/
/* test_steal_locality_perf.cpp                                           */
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
 * @file test_steal_locality_perf.cpp
 * @brief Measures scheduler throughput and the local-to-remote steal split on multi-NUMA hardware.
 * @author Mustafa Garip
 * @see tests/benchmark/README.md
 */

#include <atomic>
#include <chrono>
#include <set>
#include <thread>
#include <cstdio>
#include <gtest/gtest.h>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/scheduler/task_scheduler.hpp>
#include <SushiRuntime/topology/topology_view.hpp>

#include "../../common/test_helpers.hpp"
#include "../../common/scheduler_fixture.hpp"

using namespace SushiRuntime::Execution;
using namespace SushiRuntime::Scheduler;
using SushiRuntime::Test::make_scheduler;
using SushiRuntime::Test::AtomicCounterTask;

namespace
{
    /**
     * @brief Wait for an atomic value to reach a target within a budget.
     * 
     * @param value The atomic value to watch.
     * @param target The target value.
     * @param budget The time budget.
     * @return True if the target was reached, false on timeout.
     */
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

    /**
     * @brief Count the number of distinct NUMA nodes.
     * 
     * @param core_to_numa A mapping of CPU cores to NUMA node indices.
     * @return The number of distinct NUMA nodes.
     */
    int distinct_numa_nodes(const std::vector<int>& core_to_numa)
    {
        std::set<int> nodes(core_to_numa.begin(), core_to_numa.end());
        return static_cast<int>(nodes.size());
    }
}

/**
 * @brief Tests steal locality on multi-NUMA hardware.
 * 
 * Under heavy load on real multi-NUMA hardware, most steals must be node-local.
 */
TEST(Performance_StealLocalityBench, StealStaysNodeLocal)
{
    RuntimeContext ctx;

    const int nodes = distinct_numa_nodes(ctx.topology().core_to_numa());
    if (nodes < 2)
        GTEST_SKIP() << "Single NUMA node (" << nodes << ") — steal locality is "
                        "unmeasurable here; runs only on bare-metal multi-socket.";

    TaskScheduler sched = make_scheduler(ctx);

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

    const size_t local      = sched.get_local_steal_count();
    const size_t remote     = sched.get_remote_steal_count();
    const size_t steals     = local + remote;
    const double local_frac = steals ? double(local) / double(steals) : 1.0;
    const double thr        = ms > 0 ? (double(TOTAL) / (ms / 1000.0)) : 0.0;

    std::printf("\n=== Steal locality (%d NUMA nodes, %zu tasks) ===\n", nodes, TOTAL);
    std::printf("  Throughput   : %10.0f tasks/s  (%.2f ms)\n", thr, ms);
    std::printf("  Local steals : %zu\n", local);
    std::printf("  Remote steals: %zu\n", remote);
    std::printf("  Local frac   : %.3f\n", local_frac);

    EXPECT_EQ(executed.load(), TOTAL);

    // Locality is asserted only above 1000 steals; a balanced drain produces few.
    if (steals > 1000)
        EXPECT_GT(local_frac, 0.5)
            << "Most steals crossed NUMA nodes — hierarchical locality is not holding.";
}
