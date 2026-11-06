/**************************************************************************/
/* test_thermal_proxy_strategy.cpp                                        */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file test_thermal_proxy_strategy.cpp
 * @brief Tests the thermal proxy strategy of the rebalancer.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/in_memory_telemetry.hpp>
#include <SushiRuntime/scheduler/recording_task_migrator.hpp>
#include <SushiRuntime/scheduler/thermal_proxy_strategy.hpp>

using namespace SushiRuntime::Internal::Scheduler;

namespace
{
    /** @brief Runs one evaluation of a fresh strategy over the given queue depths and completed totals. */
    std::vector<MigrationRequest> migrations_for(std::vector<size_t> queues, std::vector<size_t> completed)
    {
        const InMemoryTelemetry telemetry(std::move(queues), std::move(completed));
        RecordingTaskMigrator migrator;
        ThermalProxyStrategy strategy;

        strategy.evaluate_and_rebalance(telemetry, migrator);

        return migrator.requests();
    }
} // namespace

/** @brief Tests that a worker with a deep queue that completes at most 5 tasks is migrated half the gap to the shortest queue. */
TEST(Small_ThermalRebalanceTest, RescuesThrottledWorker)
{
    // Worker 0 completed 0 tasks since the last look, so it is throttled; its queue is 100 deep against 0.
    const std::vector<MigrationRequest> requests = migrations_for({100, 0}, {0, 0});

    ASSERT_EQ(requests.size(), 1u) << "A throttled, backed-up worker must trigger one migration.";
    EXPECT_EQ(requests[0], (MigrationRequest{0, 50u})) << "Should move half the (max-min) queue imbalance.";
}

/** @brief Tests that a deep queue that drains faster than 5 tasks is not mistaken for a throttled worker. */
TEST(Small_ThermalRebalanceTest, IgnoresFastWorkerWithLongQueue)
{
    // Worker 0 completed 1000 tasks since the last look, so it is draining fast.
    EXPECT_TRUE(migrations_for({100, 0}, {1000, 0}).empty())
        << "A fast-draining worker is not throttled; its long queue must not trigger migration.";
}

/** @brief Tests that balanced or shallow queues trigger no migration. */
TEST(Small_ThermalRebalanceTest, NoMigrationWhenBalanced)
{
    EXPECT_TRUE(migrations_for({5, 5}, {50, 60}).empty()) << "Balanced, shallow queues must not migrate.";
}

/** @brief Tests that a single worker triggers no migration. */
TEST(Small_ThermalRebalanceTest, SingleWorkerIsNoOp)
{
    EXPECT_TRUE(migrations_for({1000}, {0}).empty()) << "A single-worker topology has no migration target.";
}

/** @brief Tests that a migration moves at least 10 tasks, even when half the gap is smaller. */
TEST(Small_ThermalRebalanceTest, AMigrationMovesAtLeastTenTasks)
{
    // Queues of 25 and 20 are 5 apart, so half the gap is 2; the floor is 10.
    const std::vector<MigrationRequest> requests = migrations_for({25, 20}, {0, 0});

    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0], (MigrationRequest{0, 10u}));
}

/** @brief Tests that the completion rate is the change since the previous evaluation, not the running total. */
TEST(Small_ThermalRebalanceTest, TheCompletionRateIsTheChangeSinceThePreviousEvaluation)
{
    const InMemoryTelemetry first_look({100, 0}, {1000, 0});
    const InMemoryTelemetry second_look({100, 0}, {1000, 0});
    RecordingTaskMigrator migrator;
    ThermalProxyStrategy strategy;

    // The first look sees 1000 completions since the start: draining fast.
    strategy.evaluate_and_rebalance(first_look, migrator);
    ASSERT_TRUE(migrator.requests().empty());
    // The second look sees the same total, so nothing completed since the first.
    strategy.evaluate_and_rebalance(second_look, migrator);

    ASSERT_EQ(migrator.requests().size(), 1u);
    EXPECT_EQ(migrator.requests()[0], (MigrationRequest{0, 50u}));
}
