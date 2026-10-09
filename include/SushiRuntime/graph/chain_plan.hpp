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
 * @file chain_plan.hpp
 * @brief Declares PendingEvent and ChainPlan, the chain record a compiled plan carries.
 * @author Mustafa Garip
 * @see include/SushiRuntime/graph/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/task/node_widths.hpp>

namespace SushiRuntime
{
    namespace Graph
    {
        struct Node;

        /**
         * @brief Holds one profiled submission until ChainRun::finish() can read its event.
         *
         * Lives on ChainPlan rather than on a ChainRun so the buffer survives past
         * the one-run-per-tick ChainRun object: capacity is reserved once and
         * reused by every later run instead of being reallocated every tick.
         */
        struct PendingEvent
        {
            Graph::Node* node;          /**< Node the event belongs to. */
            std::uint32_t step;         /**< Step index the node was submitted in. */
            sycl::event event;          /**< The submission's event. */
            Graph::NodeWidths widths{}; /**< Node's widths, snapshotted at submit. */
        };

        /**
         * @brief Records whether a compiled plan can run as an in-order chain, and how.
         *
         * A plan is chainable when every node submits device work, every node's
         * data lives on one device, and no node reads on the host at dispatch a
         * value a predecessor wrote on the device (spec
         * docs/design/GRAPH_REPLAY.md §C).
         */
        struct ChainPlan
        {
            bool chainable = false;               /**< Whether the chain path may run this plan. */
            std::size_t queue_index = 0;          /**< Device whose in-order queue the chain submits to. */
            std::vector<Graph::Node*> order;      /**< Every node except the sentinel, in a topological order. */

            /**
             * @brief Profiled submissions awaiting finish(), reused run after run.
             *
             * Owned here instead of by ChainRun so a plan run every tick through a
             * fresh ChainRun does not pay a fresh heap allocation for it each time.
             */
            std::vector<PendingEvent> pending_events;
        };

    } // namespace Graph
} // namespace SushiRuntime
