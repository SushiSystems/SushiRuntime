/**************************************************************************/
/* node_operation.hpp                                                     */
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
 * @file node_operation.hpp
 * @brief Declares run_node, the integer operation every generated node performs.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "graph_case.hpp"

namespace SushiRuntime
{
    namespace Internal::Coherence::Equivalence
    {
        /** @brief Returns the first byte of a buffer on the space the node runs on. */
        using BufferAddress = std::function<unsigned char*(std::size_t buffer)>;

        /** @brief Returns a well-mixed 64-bit value of value; integer only, so the bytes are exact. */
        [[nodiscard]] constexpr std::uint64_t mix(std::uint64_t value) noexcept
        {
            value += 0x9E3779B97F4A7C15ULL;
            value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
            value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
            return value ^ (value >> 31);
        }

        /**
         * @brief Reads the node's ranges, then writes its ranges from what it read.
         *
         * The digest folds the salt and every byte read, in order. Byte k of write w is the top byte
         * of the mix of the digest, w and k. A stale byte read changes the digest and every byte
         * written after it.
         * @return The digest of the bytes read.
         */
        inline std::uint64_t run_node(const GraphNode& node, const BufferAddress& address)
        {
            std::uint64_t digest = mix(node.salt);
            for (const ByteAccess& read : node.reads)
            {
                const unsigned char* const bytes = address(read.buffer) + read.offset;
                for (std::size_t index = 0; index < read.length; ++index)
                    digest = mix(digest ^ bytes[index]);
            }
            for (std::size_t which = 0; which < node.writes.size(); ++which)
            {
                const ByteAccess& write = node.writes[which];
                unsigned char* const bytes = address(write.buffer) + write.offset;
                for (std::size_t index = 0; index < write.length; ++index)
                    bytes[index] = static_cast<unsigned char>(
                        mix(digest + (static_cast<std::uint64_t>(which) << 40) + index) >> 56);
            }
            return digest;
        }
    } // namespace Internal::Coherence::Equivalence
} // namespace SushiRuntime
