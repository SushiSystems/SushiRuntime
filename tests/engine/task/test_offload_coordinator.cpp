/**************************************************************************/
/* test_offload_coordinator.cpp                                           */
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
 * @file test_offload_coordinator.cpp
 * @brief Tests the OffloadCoordinator contract through its in-memory implementation.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>
#include <SushiRuntime/task/in_memory_offload_coordinator.hpp>
#include <SushiRuntime/task/in_memory_offload_sink.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

/** @brief Tests that bind() through the port reaches the implementation with the sink. */
TEST(Small_OffloadCoordinator, BindReachesTheImplementationWithTheSink)
{
    InMemoryOffloadCoordinator implementation;
    OffloadCoordinator& coordinator = implementation;
    InMemoryOffloadSink sink;

    coordinator.bind(sink);

    EXPECT_EQ(implementation.bound_sink(), &sink);
}

/** @brief Tests that prepare() through the port reaches the implementation with the rules. */
TEST(Small_OffloadCoordinator, PrepareReachesTheImplementationWithTheRules)
{
    InMemoryOffloadCoordinator implementation;
    OffloadCoordinator& coordinator = implementation;
    std::vector<sushi_ptr<Node>> nodes{make_sushi<Node>(), make_sushi<Node>()};
    const std::shared_ptr<const PlacementRules> rules = std::make_shared<const PlacementRules>();

    coordinator.prepare(span<const sushi_ptr<Node>>(nodes.data(), nodes.size()), rules);

    EXPECT_EQ(implementation.prepare_count(), 1u);
    EXPECT_EQ(implementation.last_rules(), rules);
}

/** @brief Tests that try_offload() through the port reaches the implementation with the node. */
TEST(Small_OffloadCoordinator, TryOffloadReachesTheImplementationWithTheNode)
{
    InMemoryOffloadCoordinator implementation;
    OffloadCoordinator& coordinator = implementation;
    Node node;

    EXPECT_FALSE(coordinator.try_offload(node));

    EXPECT_EQ(implementation.offered(), std::vector<Node*>{&node});
}

/** @brief Tests that on_local_completion() through the port reaches the implementation with the node. */
TEST(Small_OffloadCoordinator, OnLocalCompletionReachesTheImplementationWithTheNode)
{
    InMemoryOffloadCoordinator implementation;
    OffloadCoordinator& coordinator = implementation;
    Node node;

    coordinator.on_local_completion(node);

    EXPECT_EQ(implementation.local_completions(), std::vector<Node*>{&node});
}

/** @brief Tests that stop() through the port reaches the implementation. */
TEST(Small_OffloadCoordinator, StopReachesTheImplementation)
{
    InMemoryOffloadCoordinator implementation;
    OffloadCoordinator& coordinator = implementation;

    coordinator.stop();

    EXPECT_TRUE(implementation.stopped());
}
