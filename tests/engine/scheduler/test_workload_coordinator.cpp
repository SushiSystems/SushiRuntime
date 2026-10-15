/**************************************************************************/
/* test_workload_coordinator.cpp                                          */
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
 * @file test_workload_coordinator.cpp
 * @brief Tests the WorkloadCoordinator heartbeat loop with fake telemetry, migrator and strategy.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/workload_coordinator.hpp>
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>

using namespace SushiRuntime::Internal::Scheduler;

namespace
{
    /**
     * @brief Telemetry stub reporting a fixed, single-worker topology.
     *
     * Used by the workload coordinator to mock telemetry data without
     * needing actual runtime worker threads.
     */
    class FakeTelemetry : public TelemetryProvider
    {
    public:
        size_t worker_count() const override       { return 1; }
        size_t queue_size(int) const override       { return 0; }
        size_t completed_task_count(int) const override  { return 0; }
    };

    /**
     * @brief Migrator stub that records how many migrate calls it received.
     *
     * Used to verify that the workload coordinator issues migration commands
     * exactly as requested by the strategy.
     */
    class FakeMigrator : public TaskMigrator
    {
    public:
        std::atomic<int> calls{0};
        void migrate_tasks(int, size_t) override { calls.fetch_add(1, std::memory_order_relaxed); }
    };

    /**
     * @brief Strategy that counts evaluations and optionally requests a migration.
     *
     * Tracks how many times the heartbeat loop invokes evaluate_and_rebalance
     * and forces migration commands for assertions.
     */
    class CountingStrategy : public RebalanceStrategy
    {
    public:
        std::atomic<int>* evaluations;
        bool request_migration;

        CountingStrategy(std::atomic<int>* counter, bool migrate)
            : evaluations(counter), request_migration(migrate) {}

        void evaluate_and_rebalance(const TelemetryProvider&, TaskMigrator& migrator) override
        {
            evaluations->fetch_add(1, std::memory_order_relaxed);
            if (request_migration)
                migrator.migrate_tasks(0, 1);
        }
    };

    /**
     * @brief Spins until the value reaches the target or the budget elapses and returns whether it
     *        was reached.
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
TEST(Medium_WorkloadCoordinatorTest, HeartbeatInvokesStrategy)
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
TEST(Medium_WorkloadCoordinatorTest, StrategyDrivesMigration)
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
TEST(Medium_WorkloadCoordinatorTest, SetStrategySwapsAtRuntime)
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
TEST(Medium_WorkloadCoordinatorTest, DoubleStartIsIdempotent)
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
