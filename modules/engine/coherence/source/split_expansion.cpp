/**************************************************************************/
/* split_expansion.cpp                                                    */
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
 * @file split_expansion.cpp
 * @brief Defines expand_splits() and planned_splits().
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/split_expansion.hpp>

#include <cstdint>
#include <map>
#include <utility>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns value * index / extent rounded down, without overflow. */
            [[nodiscard]] std::uint64_t scaled(std::uint64_t value, std::uint64_t index, std::uint64_t extent)
            {
                const std::uint64_t whole = value / extent;
                std::uint64_t rest = value % extent;
                std::uint64_t quotient = 0;
                std::uint64_t remainder = 0;
                for (std::uint64_t bit = std::uint64_t{1} << 63; bit != 0; bit >>= 1)
                {
                    quotient = (quotient << 1) | (remainder >= extent - remainder ? 1u : 0u);
                    remainder = remainder >= extent - remainder ? remainder - (extent - remainder) : remainder * 2;
                    if ((index & bit) != 0)
                    {
                        if (rest >= extent - remainder)
                        {
                            quotient += 1;
                            remainder -= extent - rest;
                        }
                        else
                        {
                            remainder += rest;
                        }
                    }
                }
                return whole * index + quotient;
            }

            /** @brief Returns the share of value that part covers; the shares of one cut sum to value. */
            [[nodiscard]] std::uint64_t share_of(std::uint64_t value, const SplitPart& part, std::uint64_t extent)
            {
                return scaled(value, part.end, extent) - scaled(value, part.begin, extent);
            }

            /** @brief Returns whether node runs as parts. */
            [[nodiscard]] bool is_split(const std::vector<std::vector<SizedPart>>& parts, std::size_t node)
            {
                return node < parts.size() && parts[node].size() >= 2;
            }

            /** @brief Returns the extent the parts of node cover. */
            [[nodiscard]] std::uint64_t extent_of(const std::vector<SizedPart>& parts)
            {
                return parts.back().part.end;
            }
        } // namespace

        SplitExpansion expand_splits(const PartitionProblem& problem,
                                     const std::vector<std::vector<SizedPart>>& parts,
                                     const std::vector<DeviceKey>& devices)
        {
            SplitExpansion expansion;
            PartitionProblem& expanded = expansion.problem;
            expanded.buffers = problem.buffers;
            expanded.devices = problem.devices;

            std::vector<std::size_t> first(problem.nodes.size(), 0);
            // The slices each cut node wrote last into a buffer; a later reader reads them in its place.
            std::map<std::size_t, std::vector<std::size_t>> slices_of;
            const auto sliced_reads = [&slices_of](const std::vector<std::size_t>& reads)
            {
                std::vector<std::size_t> rewritten;
                for (const std::size_t buffer : reads)
                {
                    const auto found = slices_of.find(buffer);
                    if (found == slices_of.end())
                        rewritten.push_back(buffer);
                    else
                        rewritten.insert(rewritten.end(), found->second.begin(), found->second.end());
                }
                return rewritten;
            };
            for (std::size_t node = 0; node < problem.nodes.size(); ++node)
            {
                const PartitionNode& entry = problem.nodes[node];
                first[node] = expanded.nodes.size();
                if (!is_split(parts, node))
                {
                    PartitionNode whole = entry;
                    whole.reads = sliced_reads(entry.reads);
                    for (const std::size_t buffer : entry.writes)
                        slices_of.erase(buffer);
                    expanded.nodes.push_back(std::move(whole));
                    expansion.origin.push_back(node);
                    continue;
                }
                const std::uint64_t extent = extent_of(parts[node]);
                const std::vector<std::size_t> reads = sliced_reads(entry.reads);
                std::map<std::size_t, std::vector<std::size_t>> written;
                for (const SizedPart& sized : parts[node])
                {
                    PartitionNode part_node;
                    part_node.cost = entry.cost;
                    part_node.cost.operations = share_of(entry.cost.operations, sized.part, extent);
                    part_node.cost.read_bytes = share_of(entry.cost.read_bytes, sized.part, extent);
                    part_node.cost.written_bytes = share_of(entry.cost.written_bytes, sized.part, extent);
                    part_node.placed = devices[sized.device];
                    part_node.reads = reads;
                    for (const std::size_t buffer : entry.writes)
                    {
                        PartitionBuffer slice = problem.buffers[buffer];
                        slice.bytes = share_of(slice.bytes, sized.part, extent);
                        written[buffer].push_back(expanded.buffers.size());
                        part_node.writes.push_back(expanded.buffers.size());
                        expanded.buffers.push_back(slice);
                    }
                    expanded.nodes.push_back(std::move(part_node));
                    expansion.origin.push_back(node);
                }
                for (auto& entry_slices : written)
                    slices_of[entry_slices.first] = std::move(entry_slices.second);
            }

            for (const PartitionEdge& edge : problem.edges)
            {
                const std::size_t from_count = is_split(parts, edge.from) ? parts[edge.from].size() : 1;
                const std::size_t to_count = is_split(parts, edge.to) ? parts[edge.to].size() : 1;
                for (std::size_t from = 0; from < from_count; ++from)
                {
                    std::uint64_t bytes = edge.bytes;
                    if (from_count > 1)
                        bytes = share_of(edge.bytes, parts[edge.from][from].part, extent_of(parts[edge.from]));
                    for (std::size_t to = 0; to < to_count; ++to)
                        expanded.edges.push_back(PartitionEdge{first[edge.from] + from, first[edge.to] + to, bytes});
                }
            }
            return expansion;
        }

        std::vector<PlannedSplit> planned_splits(const PartitionProblem& problem,
                                                 const std::vector<std::vector<SizedPart>>& parts,
                                                 const std::vector<DeviceKey>& devices)
        {
            std::vector<PlannedSplit> splits;
            for (std::size_t node = 0; node < problem.nodes.size(); ++node)
            {
                if (!is_split(parts, node))
                    continue;
                const PartitionNode& entry = problem.nodes[node];
                const std::uint64_t extent = extent_of(parts[node]);
                PlannedSplit split;
                split.node = node;
                for (const SizedPart& sized : parts[node])
                {
                    PlannedSplitPart planned;
                    planned.device = devices[sized.device];
                    planned.part = sized.part;
                    for (const std::size_t buffer : entry.reads)
                        planned.reads.push_back(BufferRange{problem.buffers[buffer].handle, 0, problem.buffers[buffer].bytes});
                    for (const std::size_t buffer : entry.writes)
                    {
                        const PartitionBuffer& written = problem.buffers[buffer];
                        planned.writes.push_back(BufferRange{written.handle,
                                                             scaled(written.bytes, sized.part.begin, extent),
                                                             scaled(written.bytes, sized.part.end, extent)});
                    }
                    split.parts.push_back(std::move(planned));
                }
                splits.push_back(std::move(split));
            }
            return splits;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
