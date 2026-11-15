/**************************************************************************/
/* problem_hash.cpp                                                       */
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
 * @file problem_hash.cpp
 * @brief Implements problem_hash().
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partitioning/problem_hash.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <SushiRuntime/machine/split_part.hpp>
#include <SushiRuntime/machine/split_regions.hpp>
#include <SushiRuntime/partitioning/fnv_hash.hpp>
#include <SushiRuntime/region/buffer_region.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Folds whether key holds a device, then the device. */
            void fold_optional_key(FNVHash& hash, const std::optional<DeviceKey>& key) noexcept
            {
                hash.fold(key ? 1U : 0U);
                if (key)
                    fold_device_key(hash, *key);
            }

            /** @brief Folds the count of indices, then each index. */
            void fold_indices(FNVHash& hash, const std::vector<std::size_t>& indices) noexcept
            {
                hash.fold(indices.size());
                for (const std::size_t index : indices)
                    hash.fold(index);
            }

            /** @brief Folds whether ranges are stated, then each range. */
            void fold_ranges(FNVHash& hash, const std::optional<std::vector<BufferRange>>& ranges) noexcept
            {
                hash.fold(ranges ? 1U : 0U);
                if (!ranges)
                    return;
                hash.fold(ranges->size());
                for (const BufferRange& range : *ranges)
                {
                    hash.fold(range.handle.to_high_word());
                    hash.fold(range.handle.to_low_word());
                    hash.fold(range.begin);
                    hash.fold(range.end);
                }
            }

            /** @brief Folds the count of regions, then each region's handle, offset and length. */
            void fold_regions(FNVHash& hash, const std::vector<BufferRegion>& regions) noexcept
            {
                hash.fold(regions.size());
                for (const BufferRegion& region : regions)
                {
                    hash.fold(region.base.to_high_word());
                    hash.fold(region.base.to_low_word());
                    hash.fold(static_cast<std::uint64_t>(region.offset));
                    hash.fold(static_cast<std::uint64_t>(region.length));
                }
            }

            /** @brief Returns extent * index / count rounded down, without overflow while count stays small. */
            [[nodiscard]] std::uint64_t cut_at(std::uint64_t extent, std::uint64_t index, std::uint64_t count) noexcept
            {
                return extent / count * index + extent % count * index / count;
            }

            /**
             * @brief Folds the regions split returns for the whole axis and for its equal cut into parts parts.
             *
             * The parts a partitioner asks for depend on the cost model, which the hash does not see;
             * these parts + 1 probes are a fixed set in which a change of layout rule shows.
             */
            void fold_split_regions(FNVHash& hash, const PartitionSplit& split, std::size_t parts)
            {
                hash.fold(split.regions ? 1U : 0U);
                if (!split.regions || split.extent == 0)
                    return;
                const std::uint64_t count = std::max<std::uint64_t>(1, std::min<std::uint64_t>(parts, split.extent));
                const auto fold_part = [&](const SplitPart& part)
                {
                    const SplitRegions regions = split.regions(part);
                    fold_regions(hash, regions.reads);
                    fold_regions(hash, regions.writes);
                };
                fold_part(SplitPart{split.axis, 0, split.extent, 0, 1});
                if (count < 2)
                    return;
                for (std::uint64_t index = 0; index < count; ++index)
                {
                    fold_part(SplitPart{split.axis, cut_at(split.extent, index, count),
                                        cut_at(split.extent, index + 1, count), static_cast<std::size_t>(index),
                                        static_cast<std::size_t>(count)});
                }
            }

            /** @brief Folds every field of node; parts is the probe cut of its split. */
            void fold_node(FNVHash& hash, const PartitionNode& node, std::size_t parts)
            {
                hash.fold(node.cost.operations);
                hash.fold(node.cost.read_bytes);
                hash.fold(node.cost.written_bytes);
                hash.fold(node.cost.kind.value);
                hash.fold(static_cast<std::uint64_t>(node.cost.dtype));
                fold_optional_key(hash, node.placed);
                fold_indices(hash, node.reads);
                fold_indices(hash, node.writes);
                hash.fold(node.split ? 1U : 0U);
                if (node.split)
                {
                    hash.fold(node.split->axis);
                    hash.fold(node.split->extent);
                    fold_split_regions(hash, *node.split, parts);
                }
                fold_ranges(hash, node.read_ranges);
                fold_ranges(hash, node.write_ranges);
            }
        } // namespace

        std::uint64_t problem_hash(const PartitionProblem& problem)
        {
            FNVHash hash;
            hash.fold(problem.nodes.size());
            for (const PartitionNode& node : problem.nodes)
                fold_node(hash, node, problem.devices.size());
            hash.fold(problem.buffers.size());
            for (const PartitionBuffer& buffer : problem.buffers)
            {
                hash.fold(buffer.handle.to_high_word());
                hash.fold(buffer.handle.to_low_word());
                hash.fold(buffer.bytes);
                fold_optional_key(hash, buffer.device);
            }
            hash.fold(problem.edges.size());
            for (const PartitionEdge& edge : problem.edges)
            {
                hash.fold(edge.from);
                hash.fold(edge.to);
                hash.fold(edge.bytes);
            }
            hash.fold(problem.devices.size());
            for (const PartitionDevice& device : problem.devices)
            {
                fold_device_key(hash, device.key);
                hash.fold(device.pool);
                hash.fold(device.copy_path.value());
            }
            return hash.value();
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
