/**************************************************************************/
/* partition_problem.hpp                                                  */
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
 * @file partition_problem.hpp
 * @brief Declares PartitionProblem, the nodes, buffers, edges and devices a partitioner reads.
 * @see modules/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <SushiRuntime/core/node_cost.hpp>
#include <SushiRuntime/handle/device_key.hpp>
#include <SushiRuntime/handle/global_handle.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        /** @brief Holds one buffer of the graph: its handle, its declared size and, unless planned, its device. */
        struct PartitionBuffer
        {
            GlobalHandle handle;              /**< The buffer's handle. */
            std::uint64_t bytes = 0;          /**< Declared size, or the declared upper bound of a buffer that grows. */
            std::optional<DeviceKey> device;  /**< Device of a buffer with a stated residency; empty for Residency::Planned. */
        };

        /**
         * @brief Holds one node of the graph: its declared cost, its placement and the buffers it touches.
         *
         * A node that writes a buffer with a stated device is fixed to that device, as a placed node is.
         */
        struct PartitionNode
        {
            NodeCost cost;                     /**< Cost of one dispatch. */
            std::optional<DeviceKey> placed;   /**< Device the consumer placed the node on, if any. */
            std::vector<std::size_t> reads;    /**< Indices into PartitionProblem::buffers the node reads. */
            std::vector<std::size_t> writes;   /**< Indices into PartitionProblem::buffers the node writes. */
        };

        /** @brief Holds one dependency: the reader waits on the writer and receives bytes from it. */
        struct PartitionEdge
        {
            std::size_t from = 0;     /**< Index of the earlier node; less than to. */
            std::size_t to = 0;       /**< Index of the later node. */
            std::uint64_t bytes = 0;  /**< Bytes that cross when the two nodes run in different pools. */
        };

        /** @brief Holds one device the partitioner may use: its key, its memory pool and its copy path. */
        struct PartitionDevice
        {
            DeviceKey key;           /**< The device. */
            std::uint32_t pool = 0;  /**< Memory pool the device draws from; devices that share memory share it. */
            std::string copy_path;   /**< Name of the path a copy that leaves the device takes, as its profile names it. */
        };

        /**
         * @brief Holds everything a partitioner reads, and nothing else.
         *
         * Nodes are in recording order, which is a topological order of the edges.
         */
        struct PartitionProblem
        {
            std::vector<PartitionNode> nodes;      /**< Nodes in recording order. */
            std::vector<PartitionBuffer> buffers;  /**< Buffers the nodes name by index. */
            std::vector<PartitionEdge> edges;      /**< Dependencies between nodes. */
            std::vector<PartitionDevice> devices;  /**< Devices of the machine, in any order. */
        };
    } // namespace Internal::Coherence
} // namespace SushiRuntime
