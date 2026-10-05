/****************************************************************/
/* chain_plan.hpp                                               */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <sycl/sycl.hpp>
#include <SushiRuntime/core/export.hpp>
#include <SushiRuntime/graph/node.hpp>

namespace SushiRuntime
{
    namespace Graph
    {
        struct Node;
    } // namespace Graph

    namespace Execution
    {
        struct CompiledGraph;
        class RuntimeContext;

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

        /**
         * @brief Classifies a compiled plan for the in-order chain.
         *
         * Any doubt leaves the plan on the scheduler: native host work, an
         * offload candidate, a device value read at dispatch, a node with no
         * USM region, a region this context did not allocate, or two devices.
         * @param graph A plan after capture_base_state().
         * @param ctx   The context whose USM registry owns the plan's regions.
         * @return The classification; not chainable on any doubt.
         */
        SUSHIRUNTIME_API ChainPlan classify_chain(const CompiledGraph& graph, RuntimeContext& ctx);

    } // namespace Execution
} // namespace SushiRuntime
