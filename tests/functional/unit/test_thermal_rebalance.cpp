/**************************************************************************/
/* test_thermal_rebalance.cpp                                           */
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

// Drives the real ThermalProxyStrategy with fake telemetry/migrator. Its rule:
// migrate work away from a throttled worker (queue backing up while completion
// crawls) toward the least-loaded one — but leave a long queue that is still
// draining fast alone. get_completion_rate() is cumulative; on the first
// evaluate() the delta equals the reported total.

#include <atomic>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>
#include <SushiRuntime/scheduler/thermal_proxy_strategy.hpp>

using namespace SushiRuntime::Scheduler;

namespace
{
    // Telemetry stub with per-worker queue depths and cumulative completion totals.
    class FakeTelemetry : public ITelemetryProvider
    {
    public:
        std::vector<size_t> queue;       // current queue depth per worker
        std::vector<size_t> completed;   // cumulative completed tasks per worker

        size_t get_worker_count() const override         { return queue.size(); }
        size_t get_queue_size(int w) const override       { return queue[static_cast<size_t>(w)]; }
        size_t get_completion_rate(int w) const override  { return completed[static_cast<size_t>(w)]; }
    };

    // Migrator stub recording the most recent (and total) migrate calls.
    class RecordingMigrator : public ITaskMigrator
    {
    public:
        std::atomic<int> calls{0};
        int from = -1;
        int to = -1;
        size_t count = 0;

        void migrate_tasks(int f, int t, size_t c) override
        {
            from  = f;
            to    = t;
            count = c;
            calls.fetch_add(1, std::memory_order_relaxed);
        }
    };
}

// Worker 0 has a deep queue but a zero completion delta (throttled); worker 1 is
// idle. The strategy must move half the imbalance from 0 to 1.
TEST(Unit_ThermalRebalanceTest, RescuesThrottledWorker)
{
    FakeTelemetry telemetry;
    telemetry.queue     = {100, 0};
    telemetry.completed = {0,   0}; // worker 0 delta = 0 -> throttled

    RecordingMigrator migrator;
    ThermalProxyStrategy strategy;

    strategy.evaluate_and_rebalance(telemetry, migrator);

    ASSERT_EQ(migrator.calls.load(), 1) << "A throttled, backed-up worker must trigger one migration.";
    EXPECT_EQ(migrator.from, 0);
    EXPECT_EQ(migrator.to, 1);
    EXPECT_EQ(migrator.count, 50u) << "Should move half the (max-min) queue imbalance.";
}

// A long queue that is draining fast is not a bottleneck — leave it alone.
TEST(Unit_ThermalRebalanceTest, IgnoresFastWorkerWithLongQueue)
{
    FakeTelemetry telemetry;
    telemetry.queue     = {100,  0};
    telemetry.completed = {1000, 0}; // worker 0 delta = 1000 -> draining fast

    RecordingMigrator migrator;
    ThermalProxyStrategy strategy;

    strategy.evaluate_and_rebalance(telemetry, migrator);

    EXPECT_EQ(migrator.calls.load(), 0)
        << "A fast-draining worker is not throttled; its long queue must not trigger migration.";
}

// Shallow, comparable queues below the threshold produce no migration.
TEST(Unit_ThermalRebalanceTest, NoMigrationWhenBalanced)
{
    FakeTelemetry telemetry;
    telemetry.queue     = {5,  5};
    telemetry.completed = {50, 60};

    RecordingMigrator migrator;
    ThermalProxyStrategy strategy;

    strategy.evaluate_and_rebalance(telemetry, migrator);

    EXPECT_EQ(migrator.calls.load(), 0) << "Balanced, shallow queues must not migrate.";
}

// With fewer than two workers there is nowhere to migrate to.
TEST(Unit_ThermalRebalanceTest, SingleWorkerIsNoOp)
{
    FakeTelemetry telemetry;
    telemetry.queue     = {1000};
    telemetry.completed = {0};

    RecordingMigrator migrator;
    ThermalProxyStrategy strategy;

    strategy.evaluate_and_rebalance(telemetry, migrator);

    EXPECT_EQ(migrator.calls.load(), 0) << "A single-worker topology has no migration target.";
}
