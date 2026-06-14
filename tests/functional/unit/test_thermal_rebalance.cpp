/**************************************************************************/
/* test_thermal_rebalance.cpp                                           */
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
 * @file test_thermal_rebalance.cpp
 * @brief Unit coverage for the REAL ThermalProxyStrategy decision logic.
 *
 * The WorkloadCoordinator tests drive the heartbeat loop with a fake strategy;
 * here we do the opposite and drive the real ThermalProxyStrategy directly with
 * fake telemetry/migrator so its decision rule is asserted deterministically.
 *
 * The rule it must encode: migrate work AWAY from a worker only when that worker
 * is *throttled* — its queue is backing up (large) yet its completion delta is
 * crawling (a slow / thermally-limited core) — and toward the least-loaded
 * worker. Crucially, a worker with a long queue that is still draining fast is
 * NOT a bottleneck and must be left alone; that distinction is the whole point
 * of the "thermal proxy" heuristic.
 *
 * Telemetry note: get_completion_rate() reports a CUMULATIVE completion total;
 * the strategy derives the per-tick delta itself against its own history. On the
 * first (and only) evaluate() here the prior history is zero, so the delta equals
 * the reported total.
 */

#include <atomic>
#include <vector>
#include <gtest/gtest.h>
#include <SushiRuntime/scheduler/rebalancer_interfaces.hpp>
#include <SushiRuntime/scheduler/thermal_proxy_strategy.hpp>

using namespace SushiRuntime::Scheduler;

namespace
{
    /** @brief Telemetry stub with per-worker queue sizes and cumulative totals. */
    class FakeTelemetry : public ITelemetryProvider
    {
    public:
        std::vector<size_t> queue;       // current queue depth per worker
        std::vector<size_t> completed;   // cumulative completed tasks per worker

        size_t get_worker_count() const override { return queue.size(); }
        size_t get_queue_size(int w) const override { return queue[static_cast<size_t>(w)]; }
        size_t get_completion_rate(int w) const override { return completed[static_cast<size_t>(w)]; }
    };

    /** @brief Migrator stub recording the most recent (and total) migrate calls. */
    class RecordingMigrator : public ITaskMigrator
    {
    public:
        std::atomic<int> calls{0};
        int from = -1;
        int to = -1;
        size_t count = 0;

        void migrate_tasks(int f, int t, size_t c) override
        {
            from = f; to = t; count = c;
            calls.fetch_add(1, std::memory_order_relaxed);
        }
    };
}

/**
 * @test RescuesThrottledWorker
 * @brief Worker 0 has a deep queue (100) but a near-zero completion delta — the
 * throttled-core signature — while worker 1 is idle. The strategy must migrate
 * half the imbalance from 0 to 1.
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
 * @test IgnoresFastWorkerWithLongQueue
 * @brief A long queue alone is not a bottleneck. Worker 0 has a deep queue (100)
 * but is completing fast (high delta), so it is keeping up and must be left
 * untouched — no migration.
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
 * @test NoMigrationWhenBalanced
 * @brief Shallow, comparable queues below the imbalance threshold must produce
 * no migration regardless of completion rate.
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
 * @test SingleWorkerIsNoOp
 * @brief With fewer than two workers there is nowhere to migrate to; the
 * strategy must return without touching the migrator.
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
