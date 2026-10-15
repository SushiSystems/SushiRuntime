/**************************************************************************/
/* test_rebalancer_interfaces.cpp                                         */
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
 * @file test_rebalancer_interfaces.cpp
 * @brief Tests the rebalancer contracts: a strategy reads telemetry and moves work through a migrator.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Scheduler;

namespace
{
    /** @brief Reports fixed queue sizes for two workers. */
    class FixedTelemetry final : public TelemetryProvider
    {
        public:
            std::size_t worker_count() const override { return 2; }
            std::size_t queue_size(int worker_id) const override { return worker_id == 0 ? 10 : 2; }
            std::size_t completed_task_count(int) const override { return 0; }
    };

    /** @brief Records each migration it is asked for. */
    class RecordingMigrator final : public TaskMigrator
    {
        public:
            void migrate_tasks(int from_worker_id, std::size_t count) override
            {
                moves.push_back({from_worker_id, count});
            }

            /** @brief Holds one migration. */
            struct Move
            {
                int from;          /**< The worker the tasks leave. */
                std::size_t count; /**< How many leave. */
            };

            /** @brief Holds the migrations in order. */
            std::vector<Move> moves;
    };

    /** @brief Moves half the difference from the fullest worker to even the two queues. */
    class EvenOutStrategy final : public RebalanceStrategy
    {
        public:
            void evaluate_and_rebalance(const TelemetryProvider& telemetry,
                                        TaskMigrator& migrator) override
            {
                const std::size_t first = telemetry.queue_size(0);
                const std::size_t second = telemetry.queue_size(1);
                if (first > second)
                    migrator.migrate_tasks(0, (first - second) / 2);
            }
    };
}

/** @brief Tests that a strategy reads the telemetry it is given and asks the migrator once. */
TEST(Small_RebalancerInterfaces, AStrategyMovesWorkThroughTheMigrator)
{
    const FixedTelemetry telemetry;
    RecordingMigrator migrator;
    EvenOutStrategy strategy;
    RebalanceStrategy& seam = strategy;
    seam.evaluate_and_rebalance(telemetry, migrator);
    ASSERT_EQ(migrator.moves.size(), 1u);
    EXPECT_EQ(migrator.moves[0].from, 0);
    EXPECT_EQ(migrator.moves[0].count, 4u);
    EXPECT_EQ(telemetry.worker_count(), 2u);
}
