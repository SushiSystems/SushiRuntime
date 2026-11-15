/**************************************************************************/
/* graph_partition_problem.cpp                                            */
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
 * @file graph_partition_problem.cpp
 * @brief Implements make_graph_partition_problem().
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partition_session/graph_partition_problem.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include <SushiRuntime/coherence/coherence_planner.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>
#include <SushiRuntime/task/node_placement.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Numbers the buffers of a problem by handle, adding each on first sight. */
            class BufferIndex
            {
                public:
                    /** @brief Builds an index that adds to problem. */
                    BufferIndex(GraphPartitionProblem& built, const Graph::PlacementMemory& memory,
                                const Memory::ResidencyReader& directory, const std::vector<PartitionDevice>& devices)
                        : built_(&built), memory_(&memory), directory_(&directory), devices_(&devices)
                    {

                    }

                    /** @brief Returns the index of handle's buffer, or nothing for a buffer the problem leaves out. */
                    [[nodiscard]] std::optional<std::size_t> index_of(GlobalHandle handle)
                    {
                        if (handle.is_null() || has_flags(handle.flags(), HandleFlags::Borrowed) ||
                            has_flags(handle.flags(), HandleFlags::HostOnly))
                            return std::nullopt;
                        const auto found = indices_.find(handle);
                        if (found != indices_.end())
                            return found->second;

                        PartitionBuffer buffer;
                        buffer.handle = handle;
                        buffer.bytes = directory_->inspect(handle).size;
                        const std::optional<std::size_t> owner = memory_->owning_device(handle);
                        if (owner && *owner < devices_->size())
                            buffer.device = (*devices_)[*owner].key;
                        else
                            ++built_->planned_buffers;
                        const std::size_t index = built_->problem.buffers.size();
                        built_->problem.buffers.push_back(std::move(buffer));
                        indices_.emplace(handle, index);
                        return index;
                    }

                    /** @brief Returns the bytes region names of buffer index. */
                    [[nodiscard]] std::uint64_t region_bytes(std::size_t index, const BufferRegion& region) const
                    {
                        const auto range =
                            byte_range(region, static_cast<std::size_t>(built_->problem.buffers[index].bytes));
                        return range.second - range.first;
                    }

                private:
                    /// @brief The problem buffers are added to.
                    GraphPartitionProblem* built_;

                    /// @brief Names the device that owns a buffer.
                    const Graph::PlacementMemory* memory_;

                    /// @brief Gives each buffer's size.
                    const Memory::ResidencyReader* directory_;

                    /// @brief Devices by index.
                    const std::vector<PartitionDevice>* devices_;

                    /// @brief Index of each buffer added so far.
                    std::map<GlobalHandle, std::size_t> indices_;
            };

            /** @brief Returns whether the partitioner may move node; a node of assigned counts as unstated. */
            [[nodiscard]] bool is_movable(const Graph::Node& node, const Graph::PlacementMemory& memory,
                                          const std::set<const Graph::Node*>& assigned)
            {
                if (node.stated_placement() && assigned.count(&node) == 0)
                    return false;
                return node.kernel_addressing == Graph::KernelAddressing::Rebindable ||
                       !Graph::anchor_device(node, memory).has_value();
            }
        } // namespace

        Result<GraphPartitionProblem>
        make_graph_partition_problem(const std::vector<sushi_ptr<Graph::Node>>& nodes,
                                     const Graph::PlacementMemory& memory, const Memory::ResidencyReader& directory,
                                     const std::vector<PartitionDevice>& devices,
                                     const std::set<const Graph::Node*>& assigned)
        {
            GraphPartitionProblem built;
            built.problem.devices = devices;
            BufferIndex buffers(built, memory, directory, devices);
            std::map<std::size_t, std::size_t> last_writer;
            std::map<std::pair<std::size_t, std::size_t>, std::uint64_t> edge_bytes;

            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                Graph::Node& node = *nodes[index];
                PartitionNode entry;
                entry.cost = node.metadata.cost.value_or(NodeCost{});
                if (node.declared_split != nullptr)
                    entry.split = PartitionSplit{node.declared_split->axis, node.declared_split->extent,
                                                 node.declared_split->regions};
                for (const BufferRegion& region : node.io_reads_)
                {
                    const std::optional<std::size_t> buffer = buffers.index_of(region.base);
                    if (!buffer)
                        continue;
                    entry.reads.push_back(*buffer);
                    const auto writer = last_writer.find(*buffer);
                    if (writer != last_writer.end() && writer->second != index)
                        edge_bytes[{writer->second, index}] += buffers.region_bytes(*buffer, region);
                }
                for (const BufferRegion& region : node.io_writes_)
                {
                    const std::optional<std::size_t> buffer = buffers.index_of(region.base);
                    if (!buffer)
                        continue;
                    entry.writes.push_back(*buffer);
                    last_writer[*buffer] = index;
                }

                if (!is_movable(node, memory, assigned))
                {
                    const Result<std::size_t> placed = Graph::placement_of(node, memory);
                    if (!placed)
                        return make_error(placed.error());
                    if (placed.value() >= devices.size())
                        return make_error(Error{ErrorCode::InvalidArgument,
                                                "node " + std::to_string(index) + " is placed on device " +
                                                    std::to_string(placed.value()) + ", past the " +
                                                    std::to_string(devices.size()) + " devices of the runtime."});
                    entry.placed = devices[placed.value()].key;
                }
                built.problem.nodes.push_back(std::move(entry));
                built.nodes.push_back(&node);
            }

            // The map orders the edges by writer, then reader, so the problem is the same in every run.
            for (const auto& edge : edge_bytes)
                built.problem.edges.push_back(PartitionEdge{edge.first.first, edge.first.second, edge.second});
            return built;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
