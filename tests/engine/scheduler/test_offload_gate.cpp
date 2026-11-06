/**************************************************************************/
/* test_offload_gate.cpp                                                  */
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
 * @file test_offload_gate.cpp
 * @brief Tests OffloadGate: what it offers a coordinator, what it never offers, and what it counts.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>
#include <SushiRuntime/task/in_memory_offload_coordinator.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/recording_task.hpp>

#include <SushiRuntime/scheduler/offload_gate.hpp>
#include <SushiRuntime/scheduler/run_outcome.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Scheduler;

namespace
{
    /** @brief Builds a node that the graph marks offloadable. */
    sushi_ptr<Internal::Graph::Node> offloadable_node()
    {
        sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
        node->offload().offloadable = true;
        return node;
    }

} // namespace

/** @brief Tests that an offloadable node is offered to the installed coordinator. */
TEST(Small_OffloadGate, AnOffloadableNodeIsOfferedToTheCoordinator)
{
    Internal::Graph::InMemoryOffloadCoordinator coordinator;
    OffloadGate gate;
    gate.install(&coordinator);
    const sushi_ptr<Internal::Graph::Node> node = offloadable_node();
    RunOutcome outcome;

    static_cast<void>(gate.offer(*node, outcome));

    ASSERT_EQ(coordinator.offered().size(), 1u);
    EXPECT_EQ(coordinator.offered()[0], node.get());
}

/** @brief Tests that an offer the coordinator accepts is taken and counted. */
TEST(Small_OffloadGate, AnAcceptedOfferIsCounted)
{
    Internal::Graph::InMemoryOffloadCoordinator coordinator;
    coordinator.set_accepting(true);
    OffloadGate gate;
    gate.install(&coordinator);
    const sushi_ptr<Internal::Graph::Node> node = offloadable_node();
    RunOutcome outcome;

    EXPECT_TRUE(gate.offer(*node, outcome));
    EXPECT_EQ(gate.accepted_count(), 1u);
}

/** @brief Tests that an offer the coordinator refuses is not taken and not counted. */
TEST(Small_OffloadGate, ARefusedOfferIsNotCounted)
{
    Internal::Graph::InMemoryOffloadCoordinator coordinator;
    OffloadGate gate;
    gate.install(&coordinator);
    const sushi_ptr<Internal::Graph::Node> node = offloadable_node();
    RunOutcome outcome;

    EXPECT_FALSE(gate.offer(*node, outcome));
    EXPECT_EQ(gate.accepted_count(), 0u);
}

/** @brief Tests that the gate offers nothing while no coordinator is installed. */
TEST(Small_OffloadGate, TheGateOffersNothingWithNoCoordinator)
{
    OffloadGate gate;
    const sushi_ptr<Internal::Graph::Node> node = offloadable_node();
    RunOutcome outcome;

    EXPECT_FALSE(gate.offer(*node, outcome));
}

/** @brief Tests that the gate offers a task that is no graph node to nobody. */
TEST(Small_OffloadGate, TheGateOffersNothingThatIsNoNode)
{
    Internal::Graph::InMemoryOffloadCoordinator coordinator;
    coordinator.set_accepting(true);
    OffloadGate gate;
    gate.install(&coordinator);
    const sushi_ptr<RecordingTask> plain = make_sushi<RecordingTask>();
    RunOutcome outcome;

    EXPECT_FALSE(gate.offer(*plain, outcome));
    EXPECT_TRUE(coordinator.offered().empty());
}

/** @brief Tests that the gate offers a node that is not offloadable to nobody. */
TEST(Small_OffloadGate, TheGateOffersNothingThatIsNotOffloadable)
{
    Internal::Graph::InMemoryOffloadCoordinator coordinator;
    coordinator.set_accepting(true);
    OffloadGate gate;
    gate.install(&coordinator);
    const sushi_ptr<Internal::Graph::Node> local = make_sushi<Internal::Graph::Node>();
    RunOutcome outcome;

    EXPECT_FALSE(gate.offer(*local, outcome));
    EXPECT_TRUE(coordinator.offered().empty());
}

/** @brief Tests that the gate offers nothing while the run is aborting. */
TEST(Small_OffloadGate, TheGateOffersNothingToAnAbortingRun)
{
    Internal::Graph::InMemoryOffloadCoordinator coordinator;
    coordinator.set_accepting(true);
    OffloadGate gate;
    gate.install(&coordinator);
    const sushi_ptr<Internal::Graph::Node> node = offloadable_node();
    RunOutcome outcome;
    outcome.cancel();

    EXPECT_FALSE(gate.offer(*node, outcome));
    EXPECT_TRUE(coordinator.offered().empty());
}

/** @brief Tests that prepare() reaches an installed coordinator with the rules it was given. */
TEST(Small_OffloadGate, PrepareReachesTheCoordinatorWithTheRulesItWasGiven)
{
    std::vector<sushi_ptr<Internal::Graph::Node>> nodes{make_sushi<Internal::Graph::Node>()};
    const span<const sushi_ptr<Internal::Graph::Node>> view(nodes.data(), nodes.size());
    const std::shared_ptr<const PlacementRules> rules = std::make_shared<const PlacementRules>();
    Internal::Graph::InMemoryOffloadCoordinator coordinator;
    OffloadGate gate;
    gate.install(&coordinator);

    gate.prepare(view, rules);

    EXPECT_EQ(coordinator.prepare_count(), 1u);
    EXPECT_EQ(coordinator.last_rules(), rules) << "the gate passed on other rules than it was given";
}
