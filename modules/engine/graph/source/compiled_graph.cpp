/**************************************************************************/
/* compiled_graph.cpp                                                     */
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
 * @file compiled_graph.cpp
 * @brief Captures a compiled plan's baseline state and restores it between runs.
 * @author Mustafa Garip
 */

#include <SushiRuntime/graph/compiled_graph.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/task.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        /**
         * @brief Captures the base in-degree state of all nodes in the graph.
         *
         * Stores the initial dependencies before any execution begins so that
         * they can be restored upon re-arming. Defined here since touching
         * Node::in_degree requires the complete Node type.
         */
        void CompiledGraph::capture_base_state()
        {
            base_in_degree.clear();
            base_in_degree.reserve(all_nodes.size());
            for (std::size_t i = 0; i < all_nodes.size(); ++i)
            {
                auto& n = all_nodes[i];
                // This pass also stamps each node's plan identity (modules/engine/execution/README.md).
                n->plan_index = i;
                base_in_degree.push_back(n->in_degree.load(std::memory_order_relaxed));
            }
            sentinel_in_degree = sentinel
                ? sentinel->in_degree.load(std::memory_order_relaxed) : 0;

            capture_profiling_state();
        }

        /**
         * @brief Sizes profiling_multiplicities and profiling_host_native from all_nodes.
         */
        void CompiledGraph::capture_profiling_state()
        {
            std::uint32_t max_index = 0;
            for (auto& n : all_nodes)
                if (!n->is_sentinel) max_index = std::max(max_index, n->registration_index);
            const std::size_t index_count = all_nodes.empty() ? 0 : static_cast<std::size_t>(max_index) + 1;

            profiling_multiplicities.assign(index_count, 0);
            profiling_host_native.assign(index_count, false);
            for (auto& n : all_nodes)
            {
                if (n->is_sentinel) continue;
                ++profiling_multiplicities[n->registration_index];
                if (n->get_execution_mode() == SushiRuntime::Internal::Scheduler::Task::ExecutionMode::HOST &&
                    !n->submits_device_work())
                    profiling_host_native[n->registration_index] = true;
            }
        }

        /**
         * @brief Restores the captured dependency state to allow re-execution.
         *
         * Resets the in-degree of all nodes (including the sentinel) to their
         * base states.
         */
        void CompiledGraph::rearm() noexcept
        {
            const std::size_t count = all_nodes.size();
            for (std::size_t i = 0; i < count && i < base_in_degree.size(); ++i)
                all_nodes[i]->in_degree.store(base_in_degree[i],
                                              std::memory_order_relaxed);
            if (sentinel)
                sentinel->in_degree.store(sentinel_in_degree,
                                          std::memory_order_relaxed);
        }

    } // namespace Graph
} // namespace SushiRuntime
