/**************************************************************************/
/* partition_assignment.hpp                                               */
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

/**
 * @file partition_assignment.hpp
 * @brief Declares PartitionAssignment, the device, parts and buffer homes a partitioner decided.
 * @see modules/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include <SushiRuntime/handle/device_key.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/machine/split_part.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Names the bytes [begin, end) of one buffer. */
        struct BufferRange
        {
            GlobalHandle handle;      /**< The buffer. */
            std::uint64_t begin = 0;  /**< First byte. */
            std::uint64_t end = 0;    /**< One past the last byte. */
        };

        /**
         * @brief Holds one part of a split node: its device, its range of the axis and the bytes it
         *        reads and writes.
         *
         * The parts of one node write disjoint ranges; their reads may overlap.
         */
        struct PlannedSplitPart
        {
            DeviceKey device;                 /**< Device the part runs on. */
            SplitPart part;                   /**< Range of the axis and place in slice order. */
            std::vector<BufferRange> reads;   /**< Bytes the part reads, in the node's read order. */
            std::vector<BufferRange> writes;  /**< Bytes the part writes, in the node's write order. */
        };

        /** @brief Holds the parts one split node runs as, in slice order. */
        struct PlannedSplit
        {
            std::size_t node = 0;                 /**< Index of the node in recording order. */
            std::vector<PlannedSplitPart> parts;  /**< Parts in slice order, at most one per device. */
        };

        /**
         * @brief Holds what a partitioner decided for one graph.
         *
         * The composer reads home_of() to allocate the first instance of a Residency::Planned buffer.
         */
        struct PartitionAssignment
        {
            std::vector<DeviceKey> node_devices;     /**< Device of each node, in recording order; a split node's first part's. */
            std::vector<PlannedSplit> splits;        /**< Split nodes in recording order; empty when no node is split. */
            std::map<GlobalHandle, DeviceKey> homes; /**< Home device of each planned buffer a node touches. */
            double step_time = 0.0;                  /**< Modelled seconds of one step. */
            bool floor_taken = false;                /**< Whether the floor plan replaced the scheduled one. */

            /** @brief Returns the home device of handle, or nothing when the assignment gives it none. */
            [[nodiscard]] std::optional<DeviceKey> home_of(const GlobalHandle& handle) const
            {
                const auto found = homes.find(handle);
                if (found == homes.end())
                    return std::nullopt;
                return found->second;
            }
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
