/**************************************************************************/
/* dynamic_graph_plan_access.hpp                                          */
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
 * @file dynamic_graph_plan_access.hpp
 * @brief Declares the test seam onto the composed plan of a DynamicGraph and its nodes.
 * @see tests/surface/api/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/task/node.hpp>

#include "plan_access.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        /** @brief Declares the test-only friend access to the plan of a DynamicGraph. */
        struct DynamicGraphNodeEventsTestAccess
        {
            static const SushiRuntime::Internal::Graph::CompiledGraph&
            plan(const DynamicGraph& g) noexcept
            {
                return *engine_plan(g.compiled_plan());
            }
        };
    }
}

namespace SushiRuntime
{
    namespace Testing
    {
        /**
         * @brief Returns the one node of a profiled plan that carries a name.
         * @return The node, or nullptr with a test failure when none or several carry it.
         * @pre The graph requested profiling, so plan.all_nodes is filled.
         */
        inline const Internal::Graph::Node* find_plan_node(const Internal::Graph::CompiledGraph& plan, const char* name)
        {
            const Internal::Graph::Node* found = nullptr;
            for (const auto& n : plan.all_nodes)
            {
                if (n->metadata.name != nullptr && std::string(n->metadata.name) == name)
                {
                    EXPECT_EQ(found, nullptr) << "Node name '" << name << "' is not unique.";
                    found = n.get();
                }
            }
            EXPECT_NE(found, nullptr) << "No compiled node named '" << name << "'.";
            return found;
        }

        /** @brief Returns whether a cross-region edge leads from one node to another. */
        inline bool has_boundary_edge(const Internal::Graph::Node& from, const Internal::Graph::Node& to)
        {
            const auto& successors = from.region.boundary_successors;
            return std::find(successors.begin(), successors.end(), &to) != successors.end();
        }

        /** @brief Returns how many cross-region edges end at a node. */
        inline int boundary_in_degree(const Internal::Graph::Node& node)
        {
            return node.region.boundary_in_degree.load(std::memory_order_relaxed);
        }
    } // namespace Testing
} // namespace SushiRuntime
