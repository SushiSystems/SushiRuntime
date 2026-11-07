/**************************************************************************/
/* test_in_memory_offload_policy.cpp                                      */
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
 * @file test_in_memory_offload_policy.cpp
 * @brief Tests the InMemoryOffloadPolicy adapter against the OffloadPolicy contract.
 * @see tests/extension/cluster_load/README.md
 * @author Mustafa Garip
 */

#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/api/graph/node_options.hpp>

#include <SushiRuntime/cluster_load/in_memory_offload_policy.hpp>
#include <SushiRuntime/cluster_load/offload_policy.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

namespace
{
    /** @brief Returns a worker load that carries only an id. */
    WorkerLoad worker_with_id(int id)
    {
        WorkerLoad worker;
        worker.worker_id = id;
        return worker;
    }

    /** @brief Asks a policy through its port for a worker among @p workers. */
    std::optional<int> pick_from(OffloadPolicy& policy, const std::vector<WorkerLoad>& workers)
    {
        return policy.pick_worker(NodeOptions(),
                                  span<const WorkerLoad>(workers.data(), workers.size()), 0);
    }
} // namespace

/** @brief Tests that the first offered worker is the one picked. */
TEST(Small_InMemoryOffloadPolicy, PicksTheFirstWorkerItIsOffered)
{
    InMemoryOffloadPolicy policy;
    const std::vector<WorkerLoad> workers{worker_with_id(7), worker_with_id(3)};
    EXPECT_EQ(pick_from(policy, workers), std::optional<int>(7));
}

/** @brief Tests that an empty offer picks no worker. */
TEST(Small_InMemoryOffloadPolicy, PicksNothingFromAnEmptyOffer)
{
    InMemoryOffloadPolicy policy;
    EXPECT_EQ(pick_from(policy, {}), std::nullopt);
}

/** @brief Tests that every call is counted, an empty offer included. */
TEST(Small_InMemoryOffloadPolicy, CountsEveryCall)
{
    InMemoryOffloadPolicy policy;
    ASSERT_EQ(policy.calls(), 0);
    static_cast<void>(pick_from(policy, {worker_with_id(1)}));
    static_cast<void>(pick_from(policy, {}));
    EXPECT_EQ(policy.calls(), 2);
}
