/**************************************************************************/
/* test_offload_policy.cpp                                                */
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
 * @file test_offload_policy.cpp
 * @brief Tests LeastLoadedOffloadPolicy: the payload gate, distance, load, headroom and liveness.
 * @see tests/extension/cluster_load/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>

#include <SushiRuntime/cluster_load/offload_policy.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Holds a payload size above the default 64 KiB gate of the policy: 1 MiB. */
    constexpr std::uint64_t K_LARGE_PAYLOAD = 1u << 20;

    /** @brief Holds a free-memory figure no test payload exhausts: 1 GiB. */
    constexpr std::uint64_t K_AMPLE_MEMORY = 1ull << 30;

    /** @brief Returns the statistics of a live worker as a policy sees them. */
    WorkerLoad live_worker(int id, std::uint32_t inflight, std::uint64_t free_memory,
                            std::uint32_t round_trip_micros)
    {
        WorkerLoad worker;
        worker.worker_id = id;
        worker.alive = true;
        worker.inflight = inflight;
        worker.free_mem = free_memory;
        worker.rtt_micros = round_trip_micros;
        return worker;
    }

    /** @brief Asks the default policy to place a task of @p bytes on one of @p workers. */
    std::optional<int> pick(const std::vector<WorkerLoad>& workers, std::uint64_t bytes)
    {
        LeastLoadedOffloadPolicy policy;
        return policy.pick_worker(
            NodeOptions(), span<const WorkerLoad>(workers.data(), workers.size()), bytes);
    }
}

/** @brief Tests that a payload below the policy's byte gate stays local. */
TEST(Medium_OffloadPolicy, PolicyKeepsTinyPayloadLocal)
{
    const std::vector<WorkerLoad> workers{live_worker(1, 0, K_AMPLE_MEMORY, 50)};

    EXPECT_FALSE(pick(workers, 1024).has_value());
}

/** @brief Tests that the worker with the shorter round trip wins when load is equal. */
TEST(Medium_OffloadPolicy, PolicyPicksLowerRttWorker)
{
    const std::vector<WorkerLoad> workers{
        live_worker(1, 0, K_AMPLE_MEMORY, 50000),
        live_worker(2, 0, K_AMPLE_MEMORY, 100),
    };

    const std::optional<int> picked = pick(workers, K_LARGE_PAYLOAD);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 2);
}

/** @brief Tests that the worker with fewer tasks in flight wins when round trips are equal. */
TEST(Medium_OffloadPolicy, PolicyPrefersLessLoadedWhenEquallyNear)
{
    const std::vector<WorkerLoad> workers{
        live_worker(1, 5, K_AMPLE_MEMORY, 200),
        live_worker(2, 1, K_AMPLE_MEMORY, 200),
    };

    const std::optional<int> picked = pick(workers, K_LARGE_PAYLOAD);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 2);
}

/** @brief Tests that a worker with less free memory than the payload is not picked. */
TEST(Medium_OffloadPolicy, PolicyRejectsInsufficientHeadroom)
{
    const std::vector<WorkerLoad> workers{live_worker(1, 0, K_LARGE_PAYLOAD / 2, 100)};

    EXPECT_FALSE(pick(workers, K_LARGE_PAYLOAD).has_value());
}

/** @brief Tests that the policy picks nobody when no worker is alive. */
TEST(Medium_OffloadPolicy, PolicyFallsBackLocalWhenNoLiveWorker)
{
    WorkerLoad dead = live_worker(1, 0, K_AMPLE_MEMORY, 100);
    dead.alive = false;
    const std::vector<WorkerLoad> workers{dead};

    EXPECT_FALSE(pick(workers, K_LARGE_PAYLOAD).has_value());
}

/** @brief Tests that a free-memory figure of zero counts as unknown and keeps the worker. */
TEST(Medium_OffloadPolicy, PolicyTreatsZeroFreeMemAsUnknown)
{
    const std::vector<WorkerLoad> workers{live_worker(1, 0, 0, 100)};

    const std::optional<int> picked = pick(workers, K_LARGE_PAYLOAD);
    ASSERT_TRUE(picked.has_value());
    EXPECT_EQ(*picked, 1);
}

/** @brief Tests that a result naming more outputs than its payload holds is refused. */
