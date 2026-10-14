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
 * @file test_offload_gate.cpp
 * @brief Tests OffloadGate: what it offers a coordinator, what it never offers, and what it counts.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/span.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>
#include <SushiRuntime/task/offload_sink.hpp>
#include <SushiRuntime/task/task.hpp>

#include <SushiRuntime/scheduler/offload_gate.hpp>
#include <SushiRuntime/scheduler/run_outcome.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Scheduler;

namespace
{
    /** @brief Accepts every offer when told to, and counts the offers. */
    class AcceptingCoordinator final : public Internal::Graph::OffloadCoordinator
    {
        public:
            void bind(Internal::Graph::OffloadSink&) override {}
            void prepare(span<const sushi_ptr<Internal::Graph::Node>> nodes) override { prepared = nodes.size(); }
            bool try_offload(Internal::Graph::Node&) override { ++offers; return accepts; }
            void on_local_completion(Internal::Graph::Node&) noexcept override {}
            void stop() noexcept override {}

            bool accepts = true;        /**< What try_offload() answers. */
            int offers = 0;             /**< The calls of try_offload(). */
            std::size_t prepared = 0;   /**< The nodes prepare() was given. */
    };

    /** @brief A task that is no graph node. */
    class PlainTask final : public Task
    {
        public:
            std::optional<sushi_ptr<Task>> split() override { return std::nullopt; }
            bool is_splittable() const override { return false; }
    };
}

/** @brief Tests that an offloadable node is offered and counted once accepted. */
TEST(Unit_OffloadGate, AnOffloadableNodeIsOfferedAndCounted)
{
    AcceptingCoordinator coordinator;
    OffloadGate gate;
    gate.install(&coordinator);
    const sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
    node->offload().offloadable = true;
    RunOutcome outcome;

    EXPECT_TRUE(gate.offer(*node, outcome));
    EXPECT_EQ(gate.accepted_count(), 1u);
    coordinator.accepts = false;
    EXPECT_FALSE(gate.offer(*node, outcome));
    EXPECT_EQ(gate.accepted_count(), 1u) << "a refused offer was counted";
    EXPECT_EQ(coordinator.offers, 2);
}

/** @brief Tests the four cases the gate never offers: no coordinator, no node, not offloadable, aborting. */
TEST(Unit_OffloadGate, TheGateOffersNothingItMustNot)
{
    AcceptingCoordinator coordinator;
    OffloadGate gate;
    RunOutcome outcome;
    const sushi_ptr<Internal::Graph::Node> node = make_sushi<Internal::Graph::Node>();
    node->offload().offloadable = true;
    EXPECT_FALSE(gate.offer(*node, outcome)) << "offered with no coordinator";

    gate.install(&coordinator);
    const sushi_ptr<PlainTask> plain = make_sushi<PlainTask>();
    EXPECT_FALSE(gate.offer(*plain, outcome));
    const sushi_ptr<Internal::Graph::Node> local = make_sushi<Internal::Graph::Node>();
    EXPECT_FALSE(gate.offer(*local, outcome));
    outcome.cancel();
    EXPECT_FALSE(gate.offer(*node, outcome));
    EXPECT_EQ(coordinator.offers, 0);
}

/** @brief Tests that prepare() reaches the coordinator, and does nothing without one. */
TEST(Unit_OffloadGate, PrepareReachesTheCoordinator)
{
    std::vector<sushi_ptr<Internal::Graph::Node>> nodes{make_sushi<Internal::Graph::Node>()};
    const span<const sushi_ptr<Internal::Graph::Node>> view(nodes.data(), nodes.size());
    OffloadGate gate;
    gate.prepare(view);
    AcceptingCoordinator coordinator;
    gate.install(&coordinator);
    gate.prepare(view);
    EXPECT_EQ(coordinator.prepared, 1u);
}
