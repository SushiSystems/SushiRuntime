/**************************************************************************/
/* test_in_memory_offload_coordinator.cpp                                 */
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
 * @file test_in_memory_offload_coordinator.cpp
 * @brief Tests the InMemoryOffloadCoordinator adapter against the OffloadCoordinator contract.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/in_memory_offload_coordinator.hpp>
#include <SushiRuntime/task/in_memory_offload_sink.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /**
     * @brief Builds a rules pointer whose address is set by its owner; the rules are never read.
     * @param owner The object whose address the rules pointer carries.
     * @return A rules pointer that shares ownership with the owner.
     */
    std::shared_ptr<const PlacementRules> rules_named_by(const std::shared_ptr<int>& owner)
    {
        // The address is only compared, never read, so the incomplete type needs no link.
        return std::shared_ptr<const PlacementRules>(
            owner, reinterpret_cast<const PlacementRules*>(owner.get()));
    }
}

/** @brief Tests that a coordinator refuses a node by default and still records the offer. */
TEST(Small_InMemoryOffloadCoordinator, RefusesAndRecordsANodeByDefault)
{
    InMemoryOffloadCoordinator coordinator;
    OffloadCoordinator& port = coordinator;
    Node node;
    EXPECT_FALSE(port.try_offload(node));
    EXPECT_EQ(coordinator.offered(), std::vector<Node*>{&node});
    EXPECT_TRUE(coordinator.accepted().empty());
}

/** @brief Tests that a coordinator with acceptance set accepts a node and lists it as accepted. */
TEST(Small_InMemoryOffloadCoordinator, AcceptsAndListsANodeOnceAcceptingIsSet)
{
    InMemoryOffloadCoordinator coordinator;
    OffloadCoordinator& port = coordinator;
    Node node;
    coordinator.set_accepting(true);
    EXPECT_TRUE(port.try_offload(node));
    EXPECT_EQ(coordinator.accepted(), std::vector<Node*>{&node});
}

/** @brief Tests that bind() keeps the address of the sink it was given. */
TEST(Small_InMemoryOffloadCoordinator, KeepsTheSinkItIsBoundTo)
{
    InMemoryOffloadCoordinator coordinator;
    OffloadCoordinator& port = coordinator;
    InMemoryOffloadSink sink;
    port.bind(sink);
    EXPECT_EQ(coordinator.bound_sink(), &sink);
}

/** @brief Tests that every prepare() call is counted. */
TEST(Small_InMemoryOffloadCoordinator, CountsEachPrepareCall)
{
    InMemoryOffloadCoordinator coordinator;
    OffloadCoordinator& port = coordinator;
    std::vector<sushi_ptr<Node>> nodes{make_sushi<Node>()};
    const span<const sushi_ptr<Node>> view(nodes.data(), nodes.size());
    port.prepare(view, nullptr);
    port.prepare(view, nullptr);
    EXPECT_EQ(coordinator.prepare_count(), 2u);
}

/** @brief Tests that the rules of the latest prepare() call replace the rules held before. */
TEST(Small_InMemoryOffloadCoordinator, KeepsTheRulesOfTheLatestPrepareCall)
{
    InMemoryOffloadCoordinator coordinator;
    OffloadCoordinator& port = coordinator;
    const std::shared_ptr<int> first_owner = std::make_shared<int>(1);
    const std::shared_ptr<int> second_owner = std::make_shared<int>(2);
    const std::shared_ptr<const PlacementRules> second_rules = rules_named_by(second_owner);
    port.prepare(span<const sushi_ptr<Node>>(), rules_named_by(first_owner));
    port.prepare(span<const sushi_ptr<Node>>(), second_rules);
    EXPECT_EQ(coordinator.last_rules(), second_rules);
}

/** @brief Tests that every node the pool finished itself is recorded in call order. */
TEST(Small_InMemoryOffloadCoordinator, RecordsEachLocalCompletionInCallOrder)
{
    InMemoryOffloadCoordinator coordinator;
    OffloadCoordinator& port = coordinator;
    Node first;
    Node second;
    port.on_local_completion(first);
    port.on_local_completion(second);
    EXPECT_EQ(coordinator.local_completions(), (std::vector<Node*>{&first, &second}));
}

/** @brief Tests that stop() is recorded once the scheduler calls it. */
TEST(Small_InMemoryOffloadCoordinator, RecordsThatStopRan)
{
    InMemoryOffloadCoordinator coordinator;
    OffloadCoordinator& port = coordinator;
    port.stop();
    EXPECT_TRUE(coordinator.stopped());
}
