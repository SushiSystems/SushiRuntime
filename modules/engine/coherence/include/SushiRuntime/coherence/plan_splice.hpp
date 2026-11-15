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

        /**
         * @brief Plans the coherence of nodes and adds the planned nodes and their edges.
         *
         * A step takes its node's predecessors and precedes it and every later user of its
         * instances; a step at the graph end follows every leaf. The README lists the edges.
         * @param nodes Nodes whose edges are baked; gains the planned nodes.
         * @return Nothing, or the error the planner or the factory gave; nodes is then unchanged.
         */
        [[nodiscard]] Result<void> splice_plan(std::vector<sushi_ptr<Graph::Node>>& nodes,
                                               const SpaceOfNode& space_of, CoherencePlanner& planner,
                                               PlannedNodeFactory& factory);
    } // namespace Internal::Coherence
} // namespace SushiRuntime
