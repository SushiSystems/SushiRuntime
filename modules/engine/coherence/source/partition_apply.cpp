/**************************************************************************/
/* partition_apply.cpp                                                    */
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
 * @file partition_apply.cpp
 * @brief Implements apply_partition().
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/partition_apply.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <SushiRuntime/core/error.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the index of key among devices, or nothing when it is not there. */
            [[nodiscard]] std::optional<std::size_t> index_of(const std::vector<PartitionDevice>& devices,
                                                              const DeviceKey& key)
            {
                for (std::size_t index = 0; index < devices.size(); ++index)
                {
                    if (devices[index].key == key)
                        return index;
                }
                return std::nullopt;
            }
        } // namespace

        Result<void> apply_partition(const GraphPartitionProblem& graph, const PartitionAssignment& assignment)
        {
            const PartitionProblem& problem = graph.problem;
            if (assignment.node_devices.size() != problem.nodes.size() || graph.nodes.size() != problem.nodes.size())
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "the assignment names " + std::to_string(assignment.node_devices.size()) +
                                            " nodes and the problem holds " + std::to_string(problem.nodes.size()) +
                                            "."});

            // Every index is found before any node changes, so a failed call leaves the graph as it was.
            std::vector<std::optional<std::size_t>> targets(problem.nodes.size());
            for (std::size_t node = 0; node < problem.nodes.size(); ++node)
            {
                if (problem.nodes[node].placed)
                    continue;
                targets[node] = index_of(problem.devices, assignment.node_devices[node]);
                if (!targets[node])
                    return make_error(Error{ErrorCode::InvalidArgument,
                                            "the assignment puts node " + std::to_string(node) +
                                                " on " + to_text(assignment.node_devices[node]) +
                                                ", which is not a device of the problem."});
            }
            for (std::size_t node = 0; node < problem.nodes.size(); ++node)
            {
                if (targets[node])
                    graph.nodes[node]->place_on(*targets[node]);
            }
            return {};
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
