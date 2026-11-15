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
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/task/placed_split.hpp>

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

            /** @brief Returns the regions that name ranges. */
            [[nodiscard]] std::vector<BufferRegion> regions_of(const std::vector<BufferRange>& ranges)
            {
                std::vector<BufferRegion> regions;
                regions.reserve(ranges.size());
                for (const BufferRange& range : ranges)
                    regions.emplace_back(range.handle, static_cast<std::size_t>(range.begin),
                                         static_cast<std::size_t>(range.end - range.begin));
                return regions;
            }

            /**
             * @brief Returns the placed form of split, each part's device named by its index among devices.
             * @return InvalidArgument when a part runs on a device the problem does not hold.
             */
            [[nodiscard]] Result<std::shared_ptr<const Graph::PlacedSplit>>
            place_split(const PlannedSplit& split, const std::vector<PartitionDevice>& devices)
            {
                auto placed = std::make_shared<Graph::PlacedSplit>();
                for (const PlannedSplitPart& part : split.parts)
                {
                    const std::optional<std::size_t> device = index_of(devices, part.device);
                    if (!device)
                        return make_error(Error{ErrorCode::InvalidArgument,
                                                "the assignment runs a part of node " + std::to_string(split.node) +
                                                    " on " + to_text(part.device) +
                                                    ", which is not a device of the problem."});
                    placed->parts.push_back(Graph::SplitDispatchPart{*device, part.part});
                    placed->access.push_back(Graph::PlacedSplitAccess{regions_of(part.reads), regions_of(part.writes)});
                }
                return std::shared_ptr<const Graph::PlacedSplit>(std::move(placed));
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
            std::vector<std::shared_ptr<const Graph::PlacedSplit>> splits(problem.nodes.size());
            for (const PlannedSplit& split : assignment.splits)
            {
                if (split.node >= problem.nodes.size() || split.parts.size() < 2)
                    return make_error(Error{ErrorCode::InvalidArgument,
                                            "the assignment splits node " + std::to_string(split.node) + " into " +
                                                std::to_string(split.parts.size()) +
                                                " parts; a split names a node of the problem and two parts or more."});
                Result<std::shared_ptr<const Graph::PlacedSplit>> placed = place_split(split, problem.devices);
                if (!placed)
                    return make_error(std::move(placed.error()));
                splits[split.node] = std::move(placed.value());
            }
            for (std::size_t node = 0; node < problem.nodes.size(); ++node)
            {
                if (targets[node])
                    graph.nodes[node]->place_on(*targets[node]);
                graph.nodes[node]->placed_split = std::move(splits[node]);
            }
            return {};
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
