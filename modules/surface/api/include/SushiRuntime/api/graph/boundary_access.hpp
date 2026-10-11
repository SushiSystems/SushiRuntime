/**************************************************************************/
/* boundary_access.hpp                                                    */
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
 * @file boundary_access.hpp
 * @brief Declares the summary of how one region's nodes touch each shared allocation.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace detail
    {
        /** @brief Names the engine node a boundary pin refers to. */
        using GraphNode = ::SushiRuntime::Internal::Graph::Node;

        /**
         * @brief Holds one node's participation in a base's boundary with its exact byte
         *        intervals.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        struct Pin
        {
            GraphNode* node = nullptr;
            std::vector<Internal::Memory::ResourceRegion> reads;
            std::vector<Internal::Memory::ResourceRegion> writes;
        };

        /**
         * @brief Summarises one region's boundary accesses to one shared allocation.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        struct BaseAccess
        {
            std::vector<Pin> entry; /**< Intervals no direct intra predecessor covers. */
            std::vector<Pin> exit;  /**< Intervals no direct intra successor's write covers. */
        };

        /** @brief Holds the byte intervals one node reads and writes on one allocation. */
        struct NodeAccess
        {
            std::vector<Internal::Memory::ResourceRegion> reads;
            std::vector<Internal::Memory::ResourceRegion> writes;
        };

        /** @brief Lists the accesses of a node's direct intra neighbours on one allocation. */
        using NeighbourAccesses = std::vector<const NodeAccess*>;

        /**
         * @brief Calls fn(succ) for each intra successor of a node; commit-time only,
         *        single threaded.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        template <class Fn>
        inline void for_each_intra_successor(GraphNode* n, Fn&& fn)
        {
            const int32_t count = n->inline_successor_count.load(std::memory_order_relaxed);
            const int32_t max_idx = std::min<int32_t>(count, GraphNode::MAX_INLINE_SUCCESSORS);
            for (int32_t i = 0; i < max_idx; ++i)
                if (n->inline_successors[i]) fn(n->inline_successors[i]);
            for (auto* s = n->successor_list.load(std::memory_order_relaxed); s; s = s->next)
                if (s->target) fn(s->target);
        }

        /** @brief Returns whether an interval in a intersects an interval in b. */
        inline bool overlaps_any(const std::vector<Internal::Memory::ResourceRegion>& a,
                                 const std::vector<Internal::Memory::ResourceRegion>& b)
        {
            for (const auto& x : a)
                for (const auto& y : b)
                    if (x.overlaps(y))
                        return true;
            return false;
        }

        /** @brief Returns whether one interval in covers fully contains r. */
        inline bool covered_by_any(const Internal::Memory::ResourceRegion& r,
                                   const std::vector<Internal::Memory::ResourceRegion>& covers)
        {
            for (const auto& c : covers)
                if (c.contains(r))
                    return true;
            return false;
        }

        /** @brief Returns whether a write of one neighbour fully contains r. */
        inline bool covered_by_write(const Internal::Memory::ResourceRegion& r,
                                     const NeighbourAccesses& neighbours)
        {
            for (const NodeAccess* neighbour : neighbours)
                if (covered_by_any(r, neighbour->writes))
                    return true;
            return false;
        }

        /** @brief Returns whether a read or a write of one neighbour fully contains r. */
        inline bool covered_by_access(const Internal::Memory::ResourceRegion& r,
                                      const NeighbourAccesses& neighbours)
        {
            for (const NodeAccess* neighbour : neighbours)
                if (covered_by_any(r, neighbour->reads) || covered_by_any(r, neighbour->writes))
                    return true;
            return false;
        }

        /**
         * @brief Returns the intervals of a node that its direct intra predecessors do not
         *        order behind earlier regions.
         * @param predecessors Accesses of the node's direct intra predecessors on the base.
         * @return A pin with no interval when every access of the node is covered.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        inline Pin entry_pin(GraphNode* node, const NodeAccess& own,
                             const NeighbourAccesses& predecessors)
        {
            Pin pin;
            pin.node = node;
            for (const auto& r : own.reads)
                if (!covered_by_access(r, predecessors)) pin.reads.push_back(r);
            for (const auto& w : own.writes)
                if (!covered_by_write(w, predecessors)) pin.writes.push_back(w);
            return pin;
        }

        /**
         * @brief Returns the intervals of a node that no direct intra successor's write
         *        fully contains.
         * @param successors Accesses of the node's direct intra successors on the base.
         * @return A pin with no interval when every access of the node is shadowed.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        inline Pin exit_pin(GraphNode* node, const NodeAccess& own,
                            const NeighbourAccesses& successors)
        {
            Pin pin;
            pin.node = node;
            for (const auto& r : own.reads)
                if (!covered_by_write(r, successors)) pin.reads.push_back(r);
            for (const auto& w : own.writes)
                if (!covered_by_write(w, successors)) pin.writes.push_back(w);
            return pin;
        }

        /**
         * @brief Returns a region's per-base boundary summary: entry and exit pins by
         *        byte interval.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        inline std::map<Internal::Memory::ResourceID, BaseAccess> compute_boundary_access(
            const std::vector<sushi_ptr<GraphNode>>& nodes)
        {
            std::map<Internal::Memory::ResourceID, std::unordered_map<GraphNode*, NodeAccess>> per_base;
            for (const auto& np : nodes)
            {
                GraphNode* n = np.get();
                for (const auto& r : n->io_reads_)
                    if (r.base) per_base[r.base][n].reads.push_back(r);
                for (const auto& w : n->io_writes_)
                    if (w.base) per_base[w.base][n].writes.push_back(w);
            }

            std::map<Internal::Memory::ResourceID, BaseAccess> access;
            const NeighbourAccesses none;
            for (const auto& base_and_accessors : per_base)
            {
                const auto& accessors = base_and_accessors.second;

                // An entry of accessors keeps its address: the map is not changed below.
                std::unordered_map<GraphNode*, NeighbourAccesses> predecessors;
                std::unordered_map<GraphNode*, NeighbourAccesses> successors;
                for (const auto& accessor : accessors)
                {
                    GraphNode* n = accessor.first;
                    const NodeAccess* n_access = &accessor.second;
                    for_each_intra_successor(n, [&](GraphNode* s)
                    {
                        const auto found = accessors.find(s);
                        if (found == accessors.end())
                            return;
                        predecessors[s].push_back(n_access);
                        successors[n].push_back(&found->second);
                    });
                }

                BaseAccess summary;
                for (const auto& accessor : accessors)
                {
                    GraphNode* n = accessor.first;
                    const auto before = predecessors.find(n);
                    const auto after = successors.find(n);

                    Pin entry = entry_pin(n, accessor.second,
                                          before == predecessors.end() ? none : before->second);
                    if (!entry.reads.empty() || !entry.writes.empty())
                        summary.entry.push_back(std::move(entry));

                    Pin exit = exit_pin(n, accessor.second,
                                        after == successors.end() ? none : after->second);
                    if (!exit.reads.empty() || !exit.writes.empty())
                        summary.exit.push_back(std::move(exit));
                }
                access.emplace(base_and_accessors.first, std::move(summary));
            }
            return access;
        }
    } // namespace detail
} // namespace SushiRuntime
