/****************************************************************/
/* region.hpp                                                   */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

#pragma once

#include <memory>
#include <SushiRuntime/api/graph/graph.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief One mutable partition of a DynamicGraph, recorded like a Graph.
         *
         * A region is the unit of mutation for the dynamic graph: a streaming open
         * world groups its work into spatial cells, and a cell is added or dropped
         * as it streams in or out without recompiling the rest of the world. To
         * avoid a second way of recording tasks, Region is implemented in terms of
         * the static Graph by private inheritance and re-exposes only the task-
         * building surface (add(), size()). It does not run itself — a region is
         * driven by its owning DynamicGraph, which composes every live region's
         * nodes into one plan and steps them together — so Graph's run() family is
         * intentionally hidden. Obtain one from DynamicGraph::region(); it is not
         * copyable or movable and lives in the DynamicGraph that owns it.
         */
        class Region : private Graph
        {
            public:
                // The whole fluent task-building surface, inherited unchanged from
                // Graph. Every add() overload (per-element, N-D, State, capture,
                // tracked, dynamic, offload), the untracked and host forms, the
                // reductions, the prefix scan, the counting sort and size() come through verbatim, so a region records
                // work exactly as a static graph does. Only the run() family is
                // hidden — a region is stepped by the DynamicGraph that owns it.
                using Graph::add;
                using Graph::add_untracked;
                using Graph::add_host;
                using Graph::add_reduce;
                using Graph::add_segmented_reduce;
                using Graph::add_prefix_scan;
                using Graph::add_counting_sort;
                using Graph::size;
                using Graph::name_last;

            private:
                friend class DynamicGraph;

                explicit Region(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : Graph(std::move(ctx))
                {
                }
        };

    } // namespace API
} // namespace SushiRuntime
