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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file node_emitters.hpp
 * @brief Declares the functions that add a described node to the engine's graph.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <cstdint>
#include <optional>

#include <SushiRuntime/api/detail/node_description.hpp>
#include <SushiRuntime/api/detail/node_stamp.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
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
         * @brief Writes on a node what the caller stated and leaves the rest as it is.
         * @param node A node of a graph that has not been compiled.
         */
        SUSHIRUNTIME_API void apply_stamp(const NodeStamp& stamp,
                                          Internal::Graph::Node& node) noexcept;
    } // namespace Detail
} // namespace SushiRuntime
