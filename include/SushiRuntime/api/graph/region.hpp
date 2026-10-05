/****************************************************************/
/* region.hpp                                                   */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file region.hpp
 * @brief Declares Region, one mutable partition of a DynamicGraph.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <memory>
#include <SushiRuntime/api/graph/graph.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Records one mutable partition of a DynamicGraph through Graph's add() surface.
         * @see include/SushiRuntime/api/graph/README.md
         */
        class Region : private Graph
        {
            public:
                // Only Graph's task-building surface is re-exposed; the run() family stays hidden.
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
