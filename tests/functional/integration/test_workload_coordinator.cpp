/**************************************************************************/
/* test_workload_coordinator.cpp                                        */
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
 * @file test_workload_coordinator.cpp
 * @brief Integration coverage for the WorkloadCoordinator heartbeat loop.
 *
 * The coordinator drives an IRebalanceStrategy on a background thread against
 * an ITelemetryProvider / ITaskMigrator pair. We wire fakes for all three
 * collaborators so the test is deterministic and host-only: it asserts the
 * loop actually invokes the strategy, that start/stop is clean (no hang, no
 * double-start), and that set_strategy swaps the active strategy at runtime.
 */

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
    /** @brief Telemetry stub reporting a fixed, single-worker topology. */
    class FakeTelemetry : public ITelemetryProvider
    {
    public:
        size_t get_worker_count() const override          { return 1; }
        size_t get_queue_size(int) const override         { return 0; }
        size_t get_completion_rate(int) const override    { return 0; }
    };

    /** @brief Migrator stub that records how many migrate calls it received. */
    class FakeMigrator : public ITaskMigrator
    {
    public:
        std::atomic<int> calls{0};
        void migrate_tasks(int, int, size_t) override { calls.fetch_add(1, std::memory_order_relaxed); }
    };

    /** @brief Strategy that counts evaluations; optionally requests a migration. */
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

    /** @brief Spin-wait until @p value reaches @p target or @p budget elapses. */
    bool wait_until_at_least(const std::atomic<int>& value, int target,
                             std::chrono::milliseconds budget)
    {
        auto deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (value.load(std::memory_order_relaxed) >= target) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return value.load(std::memory_order_relaxed) >= target;
    }
}

/**
 * @test HeartbeatInvokesStrategy
 * @brief Once started, the coordinator must repeatedly evaluate the strategy
 * (the ~5ms heartbeat), and stop() must cleanly join the loop.
 */
TEST(Integration_WorkloadCoordinatorTest, HeartbeatInvokesStrategy)
{
    FakeTelemetry telemetry;
    FakeMigrator migrator;
    std::atomic<int> evaluations{0};

    WorkloadCoordinator coordinator(
        telemetry, migrator,
        std::make_unique<CountingStrategy>(&evaluations, /*migrate=*/false));

    coordinator.start();
    EXPECT_TRUE(wait_until_at_least(evaluations, 3, std::chrono::milliseconds(500)))
        << "Coordinator heartbeat did not invoke the strategy enough times.";
    coordinator.stop();

    int snapshot = evaluations.load();
    // After stop() returns, the loop has joined; no further evaluations occur.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(evaluations.load(), snapshot)
        << "Strategy was evaluated after stop() returned (loop not joined).";
}

/**
 * @test StrategyDrivesMigration
 * @brief A strategy that asks for migration must reach the migrator through the
 * coordinator's loop.
 */
TEST(Integration_WorkloadCoordinatorTest, StrategyDrivesMigration)
{
    FakeTelemetry telemetry;
    FakeMigrator migrator;
    std::atomic<int> evaluations{0};

    WorkloadCoordinator coordinator(
        telemetry, migrator,
        std::make_unique<CountingStrategy>(&evaluations, /*migrate=*/true));

    coordinator.start();
    EXPECT_TRUE(wait_until_at_least(migrator.calls, 1, std::chrono::milliseconds(500)))
        << "Migration request from strategy never reached the migrator.";
    coordinator.stop();
}

/**
 * @test SetStrategySwapsAtRuntime
 * @brief set_strategy must redirect subsequent evaluations to the new strategy.
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
 * @test DoubleStartIsIdempotent
 * @brief Calling start() twice must not spawn a second thread or leave the
 * coordinator unable to stop cleanly.
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
    coordinator.start(); // second call must be a no-op

    EXPECT_TRUE(wait_until_at_least(evaluations, 1, std::chrono::milliseconds(500)));
    EXPECT_NO_THROW({ coordinator.stop(); });
}
