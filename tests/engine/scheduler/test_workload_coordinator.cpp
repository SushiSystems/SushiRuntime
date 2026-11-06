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
 * @file test_workload_coordinator.cpp
 * @brief Tests the WorkloadCoordinator heartbeat on in-memory adapters and a clock the test advances.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/in_memory_telemetry.hpp>
#include <SushiRuntime/scheduler/manual_heartbeat_clock.hpp>
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>
#include <SushiRuntime/scheduler/recording_rebalance_strategy.hpp>
#include <SushiRuntime/scheduler/recording_task_migrator.hpp>
#include <SushiRuntime/scheduler/workload_coordinator.hpp>

using namespace SushiRuntime::Internal::Scheduler;

namespace
{
    /** @brief Strategy that asks the migrator to move one task off worker 0 on every evaluation. */
    class MigratingStrategy final : public RebalanceStrategy
    {
        public:
            void evaluate_and_rebalance(const TelemetryProvider&, TaskMigrator& migrator) override
            {
                migrator.migrate_tasks(0, 1);
            }
    };

    /** @brief Telemetry of one idle worker. */
    InMemoryTelemetry one_idle_worker()
    {
        return InMemoryTelemetry({0}, {0});
    }
} // namespace

/** @brief Tests that one beat evaluates the strategy once. */
TEST(Small_WorkloadCoordinator, OneBeatEvaluatesTheStrategyOnce)
{
    const InMemoryTelemetry telemetry = one_idle_worker();
    RecordingTaskMigrator migrator;
    auto strategy = std::make_unique<RecordingRebalanceStrategy>();
    const RecordingRebalanceStrategy& evaluations = *strategy;
    WorkloadCoordinator coordinator(telemetry, migrator, std::move(strategy));

    coordinator.beat();

    EXPECT_EQ(evaluations.evaluations(), 1u);
}

/** @brief Tests that a migration the strategy asks for during a beat reaches the migrator. */
TEST(Small_WorkloadCoordinator, AMigrationTheStrategyRequestsReachesTheMigrator)
{
    const InMemoryTelemetry telemetry = one_idle_worker();
    RecordingTaskMigrator migrator;
    WorkloadCoordinator coordinator(telemetry, migrator, std::make_unique<MigratingStrategy>());

    coordinator.beat();

    EXPECT_EQ(migrator.requests(), (std::vector<MigrationRequest>{MigrationRequest{0, 1}}));
}

/** @brief Tests that set_strategy hands the next beat to the new strategy and not the old one. */
TEST(Small_WorkloadCoordinator, SetStrategyHandsTheNextBeatToTheNewStrategy)
{
    const InMemoryTelemetry telemetry = one_idle_worker();
    RecordingTaskMigrator migrator;
    auto first = std::make_unique<RecordingRebalanceStrategy>();
    const RecordingRebalanceStrategy& first_evaluations = *first;
    auto second = std::make_unique<RecordingRebalanceStrategy>();
    const RecordingRebalanceStrategy& second_evaluations = *second;
    WorkloadCoordinator coordinator(telemetry, migrator, std::move(first));
    coordinator.beat();

    coordinator.set_strategy(std::move(second));
    coordinator.beat();

    EXPECT_EQ(first_evaluations.evaluations(), 1u);
    EXPECT_EQ(second_evaluations.evaluations(), 1u);
}

/** @brief Tests that a beat with no strategy set does nothing. */
TEST(Small_WorkloadCoordinator, ABeatWithNoStrategyMigratesNothing)
{
    const InMemoryTelemetry telemetry = one_idle_worker();
    RecordingTaskMigrator migrator;
    WorkloadCoordinator coordinator(telemetry, migrator, std::make_unique<MigratingStrategy>());
    coordinator.set_strategy(nullptr);

    coordinator.beat();

    EXPECT_TRUE(migrator.requests().empty());
}

/** @brief Tests that the started thread evaluates once, then once more for each beat the clock grants. */
TEST(Medium_WorkloadCoordinator, TheThreadEvaluatesOnceForEachBeatTheClockGrants)
{
    const InMemoryTelemetry telemetry = one_idle_worker();
    RecordingTaskMigrator migrator;
    ManualHeartbeatClock clock;
    auto strategy = std::make_unique<RecordingRebalanceStrategy>();
    const RecordingRebalanceStrategy& evaluations = *strategy;
    WorkloadCoordinator coordinator(telemetry, migrator, std::move(strategy), clock);

    coordinator.start();
    clock.wait_for_pauses(1);
    clock.advance(1);
    clock.wait_for_pauses(2);

    EXPECT_EQ(evaluations.evaluations(), 2u);
    clock.release_all();
    coordinator.stop();
}

/** @brief Tests that no evaluation follows the return of stop(), however far the clock advances. */
TEST(Medium_WorkloadCoordinator, NoEvaluationFollowsStop)
{
    const InMemoryTelemetry telemetry = one_idle_worker();
    RecordingTaskMigrator migrator;
    ManualHeartbeatClock clock;
    auto strategy = std::make_unique<RecordingRebalanceStrategy>();
    const RecordingRebalanceStrategy& evaluations = *strategy;
    WorkloadCoordinator coordinator(telemetry, migrator, std::move(strategy), clock);
    coordinator.start();
    clock.wait_for_pauses(1);
    clock.release_all();
    coordinator.stop();
    const std::size_t at_stop = evaluations.evaluations();

    clock.advance(3);

    EXPECT_EQ(evaluations.evaluations(), at_stop);
}

/** @brief Tests that a second start() leaves one heartbeat thread, not two. */
TEST(Medium_WorkloadCoordinator, ASecondStartLeavesOneHeartbeatThread)
{
    const InMemoryTelemetry telemetry = one_idle_worker();
    RecordingTaskMigrator migrator;
    ManualHeartbeatClock clock;
    auto strategy = std::make_unique<RecordingRebalanceStrategy>();
    const RecordingRebalanceStrategy& evaluations = *strategy;
    WorkloadCoordinator coordinator(telemetry, migrator, std::move(strategy), clock);

    coordinator.start();
    coordinator.start();
    clock.wait_for_pauses(1);
    clock.advance(1);
    clock.wait_for_pauses(2);

    EXPECT_EQ(evaluations.evaluations(), 2u);
    clock.release_all();
    coordinator.stop();
}
