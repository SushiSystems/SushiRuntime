/**************************************************************************/
/* graph_generator.hpp                                                    */
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
 * @file graph_generator.hpp
 * @brief Declares generate_graph, which builds one random graph from a seed.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "graph_case.hpp"
#include "node_operation.hpp"

namespace SushiRuntime
{
    namespace Internal::Coherence::Equivalence
    {
        /** @brief A SplitMix64 stream; the same seed gives the same numbers on every platform. */
        class SeededRandom final
        {
            public:
                /** @brief Starts the stream at seed. */
                explicit SeededRandom(std::uint64_t seed) noexcept : state_(seed) {}

                /** @brief Returns the next 64 bits. */
                [[nodiscard]] std::uint64_t next() noexcept
                {
                    state_ += 0x9E3779B97F4A7C15ULL;
                    return mix(state_);
                }

                /** @brief Returns a number in [0, bound); bound must be above zero. */
                [[nodiscard]] std::size_t below(std::size_t bound) noexcept
                {
                    return static_cast<std::size_t>(next() % bound);
                }

            private:
                /// @brief Position in the stream.
                std::uint64_t state_;
        };

        namespace Detail
        {
            /** @brief Returns a range of buffer; one in four covers the whole buffer. */
            inline ByteAccess random_range(SeededRandom& random, std::size_t buffer, std::size_t size)
            {
                if (random.below(4) == 0)
                    return ByteAccess{buffer, 0, size};
                const std::size_t offset = random.below(size);
                return ByteAccess{buffer, offset, 1 + random.below(size - offset)};
            }

            /** @brief Returns node on a device, with up to three reads and up to two writes. */
            inline GraphNode random_node(SeededRandom& random, std::size_t device_count,
                                         const std::vector<std::vector<unsigned char>>& buffers)
            {
                GraphNode node;
                node.device = random.below(device_count);
                node.salt = random.next();
                const std::size_t reads = random.below(4);
                for (std::size_t index = 0; index < reads; ++index)
                {
                    const std::size_t buffer = random.below(buffers.size());
                    node.reads.push_back(random_range(random, buffer, buffers[buffer].size()));
                }
                std::size_t writes = random.below(3);
                if (reads == 0 && writes == 0)
                    writes = 1;
                for (std::size_t index = 0; index < writes; ++index)
                {
                    const std::size_t buffer = random.below(buffers.size());
                    const bool taken = std::any_of(node.writes.begin(), node.writes.end(),
                                                   [buffer](const ByteAccess& write) { return write.buffer == buffer; });
                    if (!taken)
                        node.writes.push_back(random_range(random, buffer, buffers[buffer].size()));
                }
                return node;
            }
        } // namespace Detail

        /**
         * @brief Builds a graph of two to four devices, two to six buffers of 16 to 255 bytes and
         *        six to twenty-four nodes, from seed.
         */
        [[nodiscard]] inline GraphCase generate_graph(std::uint64_t seed)
        {
            SeededRandom random(seed);
            GraphCase graph;
            graph.device_count = 2 + random.below(3);
            const std::size_t buffer_count = 2 + random.below(5);
            for (std::size_t buffer = 0; buffer < buffer_count; ++buffer)
            {
                std::vector<unsigned char> bytes(16 + random.below(240));
                for (unsigned char& byte : bytes)
                    byte = static_cast<unsigned char>(random.next() >> 56);
                graph.initial.push_back(std::move(bytes));
            }
            const std::size_t node_count = 6 + random.below(19);
            for (std::size_t node = 0; node < node_count; ++node)
                graph.nodes.push_back(Detail::random_node(random, graph.device_count, graph.initial));
            return graph;
        }
    } // namespace Internal::Coherence::Equivalence
} // namespace SushiRuntime
