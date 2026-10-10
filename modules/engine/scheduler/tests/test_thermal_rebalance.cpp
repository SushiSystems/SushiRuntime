/**************************************************************************/
/* test_thermal_rebalance.cpp                                             */
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
 * @file test_thermal_rebalance.cpp
 * @brief Tests ThermalProxyStrategy against fake telemetry and a fake migrator.
 * @see modules/engine/scheduler/tests/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>
#include <SushiRuntime/scheduler/thermal_proxy_strategy.hpp>

using namespace SushiRuntime::Scheduler;

namespace
{
    /**
     * @brief A stub telemetry provider for testing thermal rebalancing.
     *
     * Supplies preconfigured per-worker queue depths and cumulative completion rates to simulate various system states.
     */
    class FakeTelemetry : public ITelemetryProvider
    {
    public:
        std::vector<size_t> queue;       // current queue depth per worker
        std::vector<size_t> completed;   // cumulative completed tasks per worker

        size_t get_worker_count() const override         { return queue.size(); }
        size_t get_queue_size(int w) const override       { return queue[static_cast<size_t>(w)]; }
        size_t get_completion_rate(int w) const override  { return completed[static_cast<size_t>(w)]; }
    };

    /**
     * @brief A stub task migrator that records migration requests.
     *
     * Captures the source, destination, and task count of the most recent migration to verify rebalancer decisions.
     */
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

/**
 * @brief Verifies that the strategy migrates tasks away from a throttled worker.
 *
 * Sets up a scenario where one worker is backed up but stalled, while another is idle, expecting half the imbalance to be migrated.
 */
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

/**
 * @brief Verifies that a long but fast-draining queue is not mistaken for a throttled worker.
 *
 * Provides a high completion delta for a worker with a deep queue, ensuring the strategy does not trigger migration.
 */
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

/**
 * @brief Verifies that balanced and shallow queues do not trigger unnecessary migrations.
 *
 * Simulates comparable queues well below the imbalance threshold, expecting zero migrations.
 */
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

/**
 * @brief Verifies that a single-worker topology correctly acts as a no-op during rebalancing.
 *
 * Provides only one worker and ensures that no migrations are attempted.
 */
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
