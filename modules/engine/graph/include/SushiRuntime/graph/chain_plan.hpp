/**************************************************************************/
/* chain_plan.hpp                                                         */
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
 * @file chain_plan.hpp
 * @brief Declares PendingEvent and ChainPlan, the chain record a compiled plan carries.
 * @author Mustafa Garip
 * @see modules/engine/graph/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/task/node_widths.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        struct Node;

        /**
         * @brief Holds one profiled submission until ChainRun::finish() can read its event.
         *
         * Stored in ChainPlan::pending_events, not in the ChainRun that fills it.
         */
        struct PendingEvent
        {
            Internal::Graph::Node* node;          /**< Node the event belongs to. */
            std::uint32_t step;         /**< Step index the node was submitted in. */
            sycl::event event;          /**< The submission's event. */
            Internal::Graph::NodeWidths widths{}; /**< Node's widths, snapshotted at submit. */
            std::uint32_t submit_thread = 0;      /**< Profiler thread index of the submitting thread; 0 when unknown. */
            std::vector<sycl::event> handed_over{}; /**< The events the node's body handed over, when it handed over any. */
            std::uint64_t submit_steady_ns = 0;     /**< Steady reading taken before the node's submit call began. */
        };

        /**
         * @brief Records whether a compiled plan can run as an in-order chain, and how.
         *
         * A plan is chainable when every node submits device work, every node's
         * data lives on one device, and no node reads on the host at dispatch a
         * value a predecessor wrote on the device (spec
         * docs/design/GRAPH_REPLAY/README.md §C).
         */
        struct ChainPlan
        {
            bool chainable = false;               /**< Whether the chain path may run this plan. */
            const char* not_chainable = nullptr;  /**< Why the plan is not chainable, a string literal; null when it is. */
            std::size_t queue_index = 0;          /**< Device whose in-order queue the chain submits to. */
            std::vector<Internal::Graph::Node*> order;      /**< Every node except the sentinel, in a topological order. */

            /**
             * @brief Profiled submissions awaiting finish(), which clears the list.
             *
             * The list keeps its capacity across runs.
             */
            std::vector<PendingEvent> pending_events;
        };

    } // namespace Graph
} // namespace SushiRuntime
