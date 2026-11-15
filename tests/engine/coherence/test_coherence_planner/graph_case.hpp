/**************************************************************************/
/* graph_case.hpp                                                         */
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
 * @file graph_case.hpp
 * @brief Declares GraphCase and Outcome, the values the randomized coherence test passes around.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace SushiRuntime
{
    namespace Internal::Coherence::Equivalence
    {
        /** @brief Names one byte range of one buffer. */
        struct ByteAccess
        {
            std::size_t buffer = 0; /**< Index of the buffer in the case. */
            std::size_t offset = 0; /**< First byte of the range. */
            std::size_t length = 0; /**< Bytes in the range. */
        };

        /** @brief Holds one node of a generated graph: where it runs and what it touches. */
        struct GraphNode
        {
            std::size_t device = 0;          /**< Index of the device the node runs on. */
            std::uint64_t salt = 0;          /**< Seeds the node's operation, so two nodes differ. */
            std::vector<ByteAccess> reads;   /**< Ranges the node reads. */
            std::vector<ByteAccess> writes;  /**< Ranges the node writes, on distinct buffers. */
        };

        /** @brief Holds one generated graph; the order of its nodes is a topological order. */
        struct GraphCase
        {
            std::size_t device_count = 0;                      /**< Devices the nodes are placed on. */
            std::vector<std::vector<unsigned char>> initial;   /**< Start bytes of each buffer. */
            std::vector<GraphNode> nodes;                      /**< The nodes, in execution order. */
        };

        /** @brief Holds what one run of a graph leaves behind. */
        struct Outcome
        {
            std::vector<std::vector<unsigned char>> buffers; /**< Bytes of each buffer at the end. */
            std::vector<std::uint64_t> digests;              /**< What each node read, in node order. */

            /** @brief Returns whether both runs left the same bytes and the same reads. */
            [[nodiscard]] bool operator==(const Outcome& other) const noexcept
            {
                return buffers == other.buffers && digests == other.digests;
            }
        };
    } // namespace Internal::Coherence::Equivalence
} // namespace SushiRuntime
