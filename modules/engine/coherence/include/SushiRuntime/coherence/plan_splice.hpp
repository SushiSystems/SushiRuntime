/**************************************************************************/
/* plan_splice.hpp                                                        */
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
 * @file plan_splice.hpp
 * @brief Declares splice_plan, which adds the planned nodes of a copy plan to a built graph.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/coherence/coherence_planner.hpp>
#include <SushiRuntime/coherence/planned_node_factory.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/handle/memory_space_id.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Returns the space a node runs on, or nothing for a node the planner leaves alone. */
        using SpaceOfNode = std::function<std::optional<MemorySpaceID>(const Graph::Node&)>;

        /** @brief Returns the space of a device index; a split node's parts name their devices by index. */
        using SpaceOfDevice = std::function<MemorySpaceID(std::size_t)>;

        /**
         * @brief Plans the coherence of nodes and adds the planned nodes and their edges.
         *
         * A step follows its node's predecessors and precedes later users of its instances (README).
         * @param nodes Nodes whose edges are baked; gains the planned nodes.
         * @param space_of_device Space of a cut node's part's device; empty plans every node whole.
         * @return The spliced plan, or the error the planner or the factory gave; nodes is then unchanged.
         */
        [[nodiscard]] SUSHIRUNTIME_API Result<CopyPlan> splice_plan(std::vector<sushi_ptr<Graph::Node>>& nodes,
                                               const SpaceOfNode& space_of, CoherencePlanner& planner,
                                               PlannedNodeFactory& factory,
                                               const SpaceOfDevice& space_of_device = {});
    } // namespace Internal::Coherence
} // namespace SushiRuntime
