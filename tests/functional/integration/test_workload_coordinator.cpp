/**************************************************************************/
/* test_workload_coordinator.cpp                                        */
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

// Coverage for the WorkloadCoordinator heartbeat loop. Fakes for all three
// collaborators (telemetry, migrator, strategy) keep it deterministic and
// host-only: the loop must drive the strategy, start/stop cleanly, and swap
// strategies at runtime.

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <gtest/gtest.h>
#include <SushiRuntime/scheduler/workload_coordinator.hpp>
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>

using namespace SushiRuntime::Scheduler;

namespace
{
    /**
     * @brief Telemetry stub reporting a fixed, single-worker topology.
     *
     * Used by the workload coordinator to mock telemetry data without
     * needing actual runtime worker threads.
     */
    class FakeTelemetry : public ITelemetryProvider
    {
    public:
        size_t get_worker_count() const override       { return 1; }
        size_t get_queue_size(int) const override       { return 0; }
        size_t get_completion_rate(int) const override  { return 0; }
    };

    /**
     * @brief Migrator stub that records how many migrate calls it received.
     *
     * Used to verify that the workload coordinator issues migration commands
     * exactly as requested by the strategy.
     */
    class FakeMigrator : public ITaskMigrator
    {
    public:
        std::atomic<int> calls{0};
        void migrate_tasks(int, int, size_t) override { calls.fetch_add(1, std::memory_order_relaxed); }
    };

    /**
     * @brief Strategy that counts evaluations and optionally requests a migration.
     *
     * Tracks how many times the heartbeat loop invokes evaluate_and_rebalance
     * and forces migration commands for assertions.
     */
    class CountingStrategy : public IRebalanceStrategy
    {
    public:
        std::atomic<int>* evaluations;
        bool request_migration;

        CountingStrategy(std::atomic<int>* counter, bool migrate)
            : evaluations(counter), request_migration(migrate) {}

        void evaluate_and_rebalance(const ITelemetryProvider&, ITaskMigrator& migrator) override
        {
            evaluations->fetch_add(1, std::memory_order_relaxed);
            if (request_migration)
                migrator.migrate_tasks(0, 0, 1);
        }
    };

    /**
     * @brief Spin-wait until value reaches target or the budget elapses.
     *
     * Synchronizes concurrent test assertions against background coordinator threads.
     *
     * @param value The atomic integer to read.
     * @param target The target value to wait for.
     * @param budget The maximum duration to wait.
     * @return true if the target was reached, false on timeout.
     */
    bool wait_until_at_least(const std::atomic<int>& value, int target,
                             std::chrono::milliseconds budget)
    {
        auto deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (value.load(std::memory_order_relaxed) >= target)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return value.load(std::memory_order_relaxed) >= target;
    }
}

/**
 * @brief Tests that the coordinator repeatedly evaluates the strategy and stops cleanly.
 *
 * It starts the heartbeat loop, waits for multiple evaluations to happen,
 * then asserts that stop() successfully joins the background loop.
 */
TEST(Integration_WorkloadCoordinatorTest, HeartbeatInvokesStrategy)
{
    FakeTelemetry telemetry;
    FakeMigrator migrator;
    std::atomic<int> evaluations{0};

    WorkloadCoordinator coordinator(
        telemetry, migrator,
        std::make_unique<CountingStrategy>(&evaluations, false));

    coordinator.start();
    EXPECT_TRUE(wait_until_at_least(evaluations, 3, std::chrono::milliseconds(500)))
        << "Coordinator heartbeat did not invoke the strategy enough times.";
    coordinator.stop();

    int snapshot = evaluations.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(evaluations.load(), snapshot)
        << "Strategy was evaluated after stop() returned (loop not joined).";
}

/**
 * @brief Tests that a migration request from the strategy reaches the migrator.
 *
 * Configures the strategy to request a task migration and asserts that
 * the migrator receives the exact call.
 */
TEST(Integration_WorkloadCoordinatorTest, StrategyDrivesMigration)
{
    FakeTelemetry telemetry;
    FakeMigrator migrator;
    std::atomic<int> evaluations{0};

    WorkloadCoordinator coordinator(
        telemetry, migrator,
        std::make_unique<CountingStrategy>(&evaluations, true));

    coordinator.start();
    EXPECT_TRUE(wait_until_at_least(migrator.calls, 1, std::chrono::milliseconds(500)))
        << "Migration request from strategy never reached the migrator.";
    coordinator.stop();
}

/**
 * @brief Tests that set_strategy redirects subsequent evaluations to the new strategy.
 *
 * Verifies that swapping strategies at runtime causes the coordinator
 * to stop querying the old one and start driving the new one.
 */
TEST(Integration_WorkloadCoordinatorTest, SetStrategySwapsAtRuntime)
{
    FakeTelemetry telemetry;
    FakeMigrator migrator;
    std::atomic<int> first_evals{0};
    std::atomic<int> second_evals{0};

    WorkloadCoordinator coordinator(
        telemetry, migrator,
        std::make_unique<CountingStrategy>(&first_evals, false));

    coordinator.start();
    ASSERT_TRUE(wait_until_at_least(first_evals, 2, std::chrono::milliseconds(500)));

    coordinator.set_strategy(std::make_unique<CountingStrategy>(&second_evals, false));
    EXPECT_TRUE(wait_until_at_least(second_evals, 2, std::chrono::milliseconds(500)))
        << "New strategy was not driven after set_strategy().";

    coordinator.stop();
}

/**
 * @brief Tests that a second start() is a safe no-op.
 *
 * Calling start() while already running must not spawn duplicate threads
 * or cause deadlocks on shutdown.
 */
TEST(Integration_WorkloadCoordinatorTest, DoubleStartIsIdempotent)
{
    FakeTelemetry telemetry;
    FakeMigrator migrator;
    std::atomic<int> evaluations{0};

    WorkloadCoordinator coordinator(
        telemetry, migrator,
        std::make_unique<CountingStrategy>(&evaluations, false));

    coordinator.start();
    coordinator.start();

    EXPECT_TRUE(wait_until_at_least(evaluations, 1, std::chrono::milliseconds(500)));
    EXPECT_NO_THROW({ coordinator.stop(); });
}
