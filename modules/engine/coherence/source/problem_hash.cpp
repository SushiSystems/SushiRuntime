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
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/coherence/problem_hash.hpp>

#include <cstddef>
#include <optional>
#include <vector>

#include <SushiRuntime/coherence/fnv_hash.hpp>

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

            /** @brief Folds every field of node. */
            void fold_node(FNVHash& hash, const PartitionNode& node) noexcept
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
                }
            }
        } // namespace

        std::uint64_t problem_hash(const PartitionProblem& problem) noexcept
        {
            FNVHash hash;
            hash.fold(problem.nodes.size());
            for (const PartitionNode& node : problem.nodes)
                fold_node(hash, node);
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
                hash.fold(device.copy_path.size());
                for (const char letter : device.copy_path)
                    hash.fold(static_cast<unsigned char>(letter));
            }
            return hash.value();
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
