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
#include <unordered_set>
#include <utility>
#include <vector>

#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /** @brief Names the engine node a boundary pin refers to. */
            using GraphNode = ::SushiRuntime::Graph::Node;

            /**
             * @brief Holds one node's participation in a base's boundary with its exact byte
             *        intervals.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            struct Pin
            {
                GraphNode* node = nullptr;
                std::vector<Memory::ResourceRegion> reads;
                std::vector<Memory::ResourceRegion> writes;
            };

            /**
             * @brief Summarises one region's boundary accesses to one shared allocation.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            struct BaseAccess
            {
                std::vector<Pin> entry;  // no intra predecessor overlapping this base
                std::vector<Pin> exit;   // accesses not shadowed by an intra successor's write
            };

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

            // True when any interval in @p a intersects any interval in @p b.
            inline bool overlaps_any(const std::vector<Memory::ResourceRegion>& a,
                                     const std::vector<Memory::ResourceRegion>& b)
            {
                for (const auto& x : a)
                    for (const auto& y : b)
                        if (x.overlaps(y))
                            return true;
                return false;
            }

            /** @brief Returns whether one interval in covers fully contains r. */
            inline bool covered_by_any(const Memory::ResourceRegion& r,
                                       const std::vector<Memory::ResourceRegion>& covers)
            {
                for (const auto& c : covers)
                    if (c.contains(r))
                        return true;
                return false;
            }

            /**
             * @brief Returns a region's per-base boundary summary: entry and exit pins by
             *        byte interval.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            inline std::map<Memory::ResourceId, BaseAccess> compute_boundary_access(
                const std::vector<sushi_ptr<GraphNode>>& nodes)
            {
                std::map<Memory::ResourceId, BaseAccess> access;
                struct Acc
                {
                    std::vector<Memory::ResourceRegion> reads;
                    std::vector<Memory::ResourceRegion> writes;
                };
                std::map<Memory::ResourceId, std::unordered_map<GraphNode*, Acc>> per_base;

                for (const auto& np : nodes)
                {
                    GraphNode* n = np.get();
                    for (const auto& r : n->io_reads_)
                        if (r.base) per_base[r.base][n].reads.push_back(r);
                    for (const auto& w : n->io_writes_)
                        if (w.base) per_base[w.base][n].writes.push_back(w);
                }

                for (auto& bm : per_base)
                {
                    const Memory::ResourceId b = bm.first;
                    auto& m = bm.second;   // node -> Acc for this base

                    // Both maps are keyed by node and filled per base b.
                    std::unordered_set<GraphNode*> has_overlapping_pred;
                    std::unordered_map<GraphNode*, std::vector<Memory::ResourceRegion>> shadowing;

                    for (auto& nm : m)
                    {
                        GraphNode* n = nm.first;
                        const Acc& n_acc = nm.second;
                        for_each_intra_successor(n, [&](GraphNode* s)
                        {
                            auto f = m.find(s);
                            if (f == m.end()) return;   // successor does not touch b
                            const Acc& s_acc = f->second;

                            if (overlaps_any(n_acc.reads,  s_acc.reads)  ||
                                overlaps_any(n_acc.reads,  s_acc.writes) ||
                                overlaps_any(n_acc.writes, s_acc.reads)  ||
                                overlaps_any(n_acc.writes, s_acc.writes))
                                has_overlapping_pred.insert(s);

                            if (!s_acc.writes.empty())
                            {
                                auto& v = shadowing[n];
                                v.insert(v.end(), s_acc.writes.begin(), s_acc.writes.end());
                            }
                        });
                    }

                    const std::vector<Memory::ResourceRegion> nothing_shadows;

                    BaseAccess ba;
                    for (auto& nm : m)
                    {
                        GraphNode* n = nm.first;
                        const Acc& acc = nm.second;

                        if (has_overlapping_pred.find(n) == has_overlapping_pred.end())
                            ba.entry.push_back(Pin{ n, acc.reads, acc.writes });

                        auto sit = shadowing.find(n);
                        const std::vector<Memory::ResourceRegion>& covers =
                            (sit == shadowing.end()) ? nothing_shadows : sit->second;

                        Pin ex;
                        ex.node = n;
                        for (const auto& w : acc.writes)
                            if (!covered_by_any(w, covers)) ex.writes.push_back(w);
                        for (const auto& r : acc.reads)
                            if (!covered_by_any(r, covers)) ex.reads.push_back(r);

                        if (!ex.writes.empty() || !ex.reads.empty())
                            ba.exit.push_back(std::move(ex));
                    }
                    access.emplace(b, std::move(ba));
                }
                return access;
            }
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
