/**************************************************************************/
/* region_link.hpp                                                        */
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
 * @file region_link.hpp
 * @brief Declares RegionLink, a node's cross-region dependency layer.
 * @author Mustafa Garip
 * @see modules/engine/graph/README.md
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace SushiRuntime
{
    namespace Graph
    {
        struct Node;

        /**
         * @brief Holds the cross-region (boundary) dependency layer for the dynamic graph.
         * @see modules/engine/graph/README.md
         */
        struct RegionLink
        {
            /**
             * @brief Predecessor count from edges internal to the owning region.
             *
             * Captured once when the region is compiled — it equals the live
             * in_degree right after the region's own dependency pass — and never
             * touched again while the region is unchanged.
             */
            int32_t intra_in_degree = 0;

            /**
             * @brief Predecessor count from cross-region (boundary) edges.
             *
             * Maintained incrementally by the composer as boundary edges are added
             * and removed; summed with intra_in_degree to re-arm the node each step.
             */
            std::atomic<int32_t> boundary_in_degree{0};

            /**
             * @brief Cross-region successors released in addition to the intra ones.
             *
             * The scheduler's trigger_successors walks this list alongside the
             * inline/spillover intra successors, decrementing each target's
             * in_degree. Mutated only at a step boundary (no worker running) and
             * read-only during a step, so a plain vector needs no synchronisation.
             */
            std::vector<Node*> boundary_successors;

            /**
             * @brief True when this node has no successor internal to its region.
             *
             * An intra-leaf is a candidate to link to the completion sentinel, but
             * only while it also has no cross-region successor. Set once when the
             * region is compiled.
             */
            bool intra_leaf = false;

            /**
             * @brief Whether this node currently releases the completion sentinel.
             *
             * The composer keeps the sentinel wired to exactly the current leaves
             * (no intra and no cross-region successor). Flipped as boundary edges
             * make a leaf gain or lose an exported successor.
             */
            bool sentinel_linked = false;

            /**
             * @brief Clears only the cross-region boundary layer.
             *
             * Drops the boundary predecessor count and the boundary successor list,
             * leaving the frozen intra-region edges untouched. The composer calls
             * this on a pin before re-deriving the boundary edges of a base that
             * became dirty. Safe at a step boundary only.
             */
            void clear() noexcept
            {
                boundary_in_degree.store(0, std::memory_order_relaxed);
                boundary_successors.clear();
            }
        };

    } // namespace Graph
} // namespace SushiRuntime
