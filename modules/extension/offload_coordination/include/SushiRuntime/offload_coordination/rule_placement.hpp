/**************************************************************************/
/* rule_placement.hpp                                                     */
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
 * @file rule_placement.hpp
 * @brief Declares RulePlacement, which places a node by the rule that won for it.
 * @author Mustafa Garip
 * @see modules/extension/offload_coordination/README.md
 */

#pragma once

#include <chrono>
#include <cstddef>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/cluster_load/placement_decision.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rule.hpp>

#include <SushiRuntime/offload_coordination/held_nodes.hpp>
#include <SushiRuntime/offload_coordination/worker_directory.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph { struct Node; }

    namespace Distributed
    {
        /**
         * @brief Decides where a node goes from its winning rule, in place of the OffloadPolicy.
         *
         * A rule whose machine part names the master keeps the node local. Otherwise the
         * directory picks a worker the target names and the device on it; an absent target
         * follows the rule's if_absent, and a wait is measured from the node's first offer.
         * It owns no lock; the directory and the held table take their own.
         */
        class SUSHIRUNTIME_API RulePlacement final
        {
            public:
                /**
                 * @brief Builds the placement over bricks that outlive it.
                 * @param master The facts of the machine the master runs on; copied.
                 */
                RulePlacement(WorkerDirectory& directory, HeldNodes& held, NodeFacts master);

                RulePlacement(const RulePlacement&) = delete;
                RulePlacement& operator=(const RulePlacement&) = delete;
                RulePlacement(RulePlacement&&) = delete;
                RulePlacement& operator=(RulePlacement&&) = delete;

                /** @brief Returns whether the machine part of a rule's target names the master. */
                [[nodiscard]] bool names_master(const PlacementRule& rule) const noexcept;

                /**
                 * @brief Returns where a node goes under its winning rule.
                 *
                 * A worker that matches but has no room holds the node with no end, until a
                 * slot frees. On RunOnWorker the worker's inflight count rose.
                 * @param position The rule's position in its set, for the error of Fail.
                 */
                [[nodiscard]] PlacementDecision decide(const PlacementRule& rule,
                                                       std::size_t position,
                                                       const Internal::Graph::Node& node,
                                                       std::chrono::steady_clock::time_point now);

            private:
                /** @brief Applies if_absent to a node whose target no alive worker matches. */
                [[nodiscard]] PlacementDecision follow_if_absent(
                    const PlacementRule& rule, std::size_t position,
                    const Internal::Graph::Node& node, std::chrono::steady_clock::time_point now);

                /// @brief Finds the worker a target names and counts its load.
                WorkerDirectory& directory_;

                /// @brief Keeps the first offer of each waiting node.
                HeldNodes& held_;

                /// @brief The master's own facts, for a rule that names this machine.
                NodeFacts master_;
        };

    } // namespace Distributed
} // namespace SushiRuntime
