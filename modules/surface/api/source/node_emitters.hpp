/**************************************************************************/
/* node_emitters.hpp                                                      */
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
 * @file node_emitters.hpp
 * @brief Declares the functions that add a described node to the engine's graph.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/detail/node_stamp.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/residency_view.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Adds one described node to a backing graph, by the function of its kind.
         * @param registration Index to stamp on the node; none draws a fresh one.
         * @return The node added; the graph owns it.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        SUSHIRUNTIME_API Internal::Graph::Node&
        add_described_node(Internal::Graph::TaskGraph& backing_graph,
                           const NodeDescription& description,
                           std::optional<std::uint32_t> registration);

        /**
         * @brief Gives a node the table of its kernel's pointers, which each dispatch binds on
         *        the node's device; a node with no pointer gets none.
         * @param handles The directory the pointers resolve through; outlives the node.
         * @param device_count Devices of the runtime.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        SUSHIRUNTIME_API void attach_kernel_pointers(Internal::Graph::Node& node,
                                                     const NodeDescription& description,
                                                     const Internal::Memory::ResidencyView& handles,
                                                     std::size_t device_count);

        /**
         * @brief Writes on a node what the caller stated and leaves the rest as it is.
         * @param node A node of a graph that has not been compiled.
         */
        SUSHIRUNTIME_API void apply_stamp(const NodeStamp& stamp,
                                          Internal::Graph::Node& node) noexcept;
    } // namespace Detail
} // namespace SushiRuntime
