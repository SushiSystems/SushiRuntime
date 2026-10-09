/**************************************************************************/
/* region.hpp                                                             */
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
 * @file region.hpp
 * @brief Declares Region, one mutable partition of a DynamicGraph.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
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
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        class Region : private Graph
        {
            public:
                // Only Graph's task-building surface is re-exposed; the run() family stays hidden.
                using Graph::add;
                using Graph::add_untracked;
                using Graph::add_emitter;
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
