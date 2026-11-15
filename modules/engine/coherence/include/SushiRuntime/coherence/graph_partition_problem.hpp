/**************************************************************************/
/* graph_partition_problem.hpp                                            */
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
 * @file graph_partition_problem.hpp
 * @brief Declares make_graph_partition_problem(), the PartitionProblem of a recorded graph.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <set>
#include <vector>

#include <SushiRuntime/coherence/partition_problem.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/memory/residency_view.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/placement_memory.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds the problem of a graph and the graph node each of its nodes stands for. */
        struct GraphPartitionProblem
        {
            PartitionProblem problem;          /**< What the partitioner reads. */
            std::vector<Graph::Node*> nodes;   /**< Graph node of each problem node, in recording order. */
            std::size_t planned_buffers = 0;   /**< Buffers of the problem with no stated device. */
        };

        /**
         * @brief Builds the problem of nodes over devices, device i standing for device index i.
         *
         * Borrowed and HostOnly buffers are left out; an owned buffer keeps its device, any other is
         * planned. A stated or FixedToHome-and-owned node stays where placement_of() puts it.
         * @param assigned Nodes an earlier partition placed; they count as unstated. nodes outlive the result.
         * @return The problem, or the placement error of a fixed node.
         */
        [[nodiscard]] Result<GraphPartitionProblem>
        make_graph_partition_problem(const std::vector<sushi_ptr<Graph::Node>>& nodes,
                                     const Graph::PlacementMemory& memory, const Memory::ResidencyView& directory,
                                     const std::vector<PartitionDevice>& devices,
                                     const std::set<const Graph::Node*>& assigned = {});
    } // namespace Internal::Coherence
} // namespace SushiRuntime
