/**************************************************************************/
/* region_composer.hpp                                                    */
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
 * @file region_composer.hpp
 * @brief Declares RegionComposer, which stitches the plan of a DynamicGraph from its regions.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SushiRuntime/api/graph/backing_graph.hpp>
#include <SushiRuntime/api/graph/boundary_access.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Owns the compiled sub-graph of every region and stitches them into one plan.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        class RegionComposer
        {
            public:
                /** @brief Caller-chosen identity of a region (e.g. a packed cell coordinate). */
                using RegionKey = std::uint64_t;

                /**
                 * @brief Re-stitches the plan from the changed regions and their shared boundaries
                 *        only.
                 * @param dirty       Regions whose recording changed; rebuilt when still live.
                 * @param dropped     Regions removed since the last commit.
                 * @param recorder_of Called as recorder_of(key); returns the live region's
                 *        const Recorder*, or null when the region is gone.
                 */
                template <class RecorderOf>
                void commit(const std::set<RegionKey>& dirty, const std::set<RegionKey>& dropped,
                            RecorderOf&& recorder_of,
                            ::SushiRuntime::Graph::OffloadCoordinator* offload, bool profiling)
                {
                    // teardown lists regions to tear down; dirty_bases the bases to re-wire.
                    std::set<RegionKey> teardown(dirty.begin(), dirty.end());
                    teardown.insert(dropped.begin(), dropped.end());

                    std::set<Base> dirty_bases;
                    for (RegionKey k : teardown)
                    {
                        auto sit = states_.find(k);
                        if (sit != states_.end())
                            for (auto& ba : sit->second.access)
                                dirty_bases.insert(ba.first);
                    }

                    // A dirty base's cross edges are undone while every endpoint is alive.
                    for (Base b : dirty_bases)
                    {
                        auto eit = base_edges_.find(b);
                        if (eit == base_edges_.end()) continue;
                        for (auto& e : eit->second)
                            remove_cross_edge(e.from, e.to);
                        eit->second.clear();
                    }

                    // Tear down old state (drops + rebuilt regions).
                    for (RegionKey k : teardown)
                        teardown_region(k);

                    // Rebuilt regions mark their bases dirty and seed roots and sentinel.
                    for (RegionKey k : dirty)
                    {
                        const Recorder* recorder = recorder_of(k);
                        if (recorder == nullptr) continue;  // dropped, not re-created
                        build_region(k, *recorder, offload);
                        RegionState& st = states_.at(k);
                        for (auto& ba : st.access)
                        {
                            base_regions_[ba.first].insert(k);
                            dirty_bases.insert(ba.first);
                        }
                        for (Node* r : st.intra_roots)
                            roots_.insert(r);   // fresh nodes carry no boundary in-degree yet
                        for (auto& np : st.nodes)
                            if (np->region.intra_leaf) link_sentinel(np.get());
                    }

                    // Re-derive the boundary edges of every dirty allocation.
                    for (Base b : dirty_bases)
                        rewire_base(b);

                    // Only the profiler reads all_nodes, so it is gathered only when profiling.
                    plan_.roots.assign(roots_.begin(), roots_.end());
                    plan_.sentinel = sentinel_;
                    plan_.profiling = profiling;
                    plan_.all_nodes.clear();
                    if (profiling)
                        for (auto& kv : states_)
                            for (auto& np : kv.second.nodes)
                                plan_.all_nodes.push_back(np);

                    renumber_registration_indices();
                    if (profiling)
                        plan_.capture_profiling_state();

                    committed_once_ = true;
                    ++compiles_;
                }

                /**
                 * @brief Re-arms every node's live in-degree and refreshes the per-step updaters,
                 *        O(total).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void rearm()
                {
                    for (auto& kv : states_)
                        for (auto& np : kv.second.nodes)
                            np->arm_in_degree();
                    sentinel_->arm_in_degree();
                }

                /** @brief Returns how many times the plan has been composed. */
                std::size_t compose_count() const noexcept { return compiles_; }

                /** @brief Returns whether the plan has been composed at least once. */
                bool composed() const noexcept { return committed_once_; }

            protected:
                /** @brief Starts with no region and a fresh completion sentinel. */
                RegionComposer() : sentinel_(make_sushi<Node>())
                {
                }

                RegionComposer(RegionComposer&&) = default;
                RegionComposer& operator=(RegionComposer&&) = default;
                RegionComposer(const RegionComposer&) = delete;
                RegionComposer& operator=(const RegionComposer&) = delete;
                ~RegionComposer() = default;

                /** @brief The stitched plan; the owning graph and its test seams read it. */
                ::SushiRuntime::Graph::CompiledGraph plan_;

            private:
                using Node = ::SushiRuntime::Graph::Node;
                using Base = ::SushiRuntime::Core::ResourceId;  // allocation identity (void*)
                using Pin = detail::Pin;
                using BaseAccess = detail::BaseAccess;

                /**
                 * @brief Holds a boundary pin reduced to one hazard's intervals, used by
                 *        rewire_base.
                 */
                struct BoundaryPin
                {
                    Node* node = nullptr;
                    std::vector<Core::ResourceRegion> regions;
                };

                /**
                 * @brief Caches one region's compiled sub-graph: nodes, intra-roots and boundary
                 *        summary.
                 */
                struct RegionState
                {
                    std::vector<sushi_ptr<Node>> nodes;
                    std::vector<Node*>           intra_roots;
                    std::map<Base, BaseAccess>   access;
                };

                /**
                 * @brief Records one cross-region edge, kept per base so a base can be un-wired
                 *        alone.
                 */
                struct BoundaryEdge { Node* from; Node* to; };

                /**
                 * @brief Links a leaf node to the completion sentinel and counts it on the
                 *        sentinel's baseline.
                 */
                void link_sentinel(Node* n)
                {
                    if (n->region.sentinel_linked) return;
                    n->region.boundary_successors.push_back(sentinel_.get());
                    sentinel_->region.intra_in_degree += 1;
                    n->region.sentinel_linked = true;
                }

                /** @brief Removes a node's sentinel link. */
                void unlink_sentinel(Node* n)
                {
                    if (!n->region.sentinel_linked) return;
                    auto& v = n->region.boundary_successors;
                    for (auto it = v.begin(); it != v.end(); ++it)
                        if (*it == sentinel_.get()) { v.erase(it); break; }
                    sentinel_->region.intra_in_degree -= 1;
                    n->region.sentinel_linked = false;
                }

                /**
                 * @brief Adds one cross-region edge from->to for base b and updates root and leaf
                 *        bookkeeping.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void add_cross_edge(Node* from, Node* to, Base b)
                {
                    if (from->region.sentinel_linked)
                        unlink_sentinel(from);
                    from->region.boundary_successors.push_back(to);

                    const int32_t prev =
                        to->region.boundary_in_degree.fetch_add(1, std::memory_order_relaxed);
                    if (prev == 0 && to->region.intra_in_degree == 0)
                        roots_.erase(to);

                    base_edges_[b].push_back({from, to});
                }

                /**
                 * @brief Removes one recorded cross-region edge, reversing add_cross_edge's
                 *        bookkeeping.
                 */
                void remove_cross_edge(Node* from, Node* to)
                {
                    auto& v = from->region.boundary_successors;
                    for (auto it = v.begin(); it != v.end(); ++it)
                        if (*it == to) { v.erase(it); break; }

                    const int32_t prev =
                        to->region.boundary_in_degree.fetch_sub(1, std::memory_order_relaxed);
                    if (prev == 1 && to->region.intra_in_degree == 0)
                        roots_.insert(to);

                    // An intra-leaf with no cross-region successor left holds the sentinel again.
                    if (from->region.intra_leaf && !from->region.sentinel_linked &&
                        from->region.boundary_successors.empty())
                        link_sentinel(from);
                }

                /**
                 * @brief Drops from pins every interval a later write fully covers, and emptied
                 *        pins with them.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                static void retire_covered(std::vector<BoundaryPin>& pins,
                                           const std::vector<Core::ResourceRegion>& covers)
                {
                    for (auto it = pins.begin(); it != pins.end(); )
                    {
                        auto& regions = it->regions;
                        regions.erase(std::remove_if(regions.begin(), regions.end(),
                            [&covers](const Core::ResourceRegion& r)
                            {
                                return detail::covered_by_any(r, covers);
                            }), regions.end());

                        if (regions.empty()) it = pins.erase(it);
                        else                 ++it;
                    }
                }

                /**
                 * @brief Re-derives the cross-region edges of one shared allocation from byte
                 *        intervals.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void rewire_base(Base b)
                {
                    auto eit = base_edges_.find(b);
                    if (eit != base_edges_.end())
                    {
                        for (auto& e : eit->second)
                            remove_cross_edge(e.from, e.to);
                        eit->second.clear();
                    }

                    auto rit = base_regions_.find(b);
                    if (rit == base_regions_.end() || rit->second.size() < 2)
                        return;  // not shared: nothing crosses a boundary

                    // Both lists accumulate; a slice write shadows no writer of another slice.
                    std::vector<BoundaryPin> live_writers;
                    std::vector<BoundaryPin> pending_reads;

                    for (RegionKey k : rit->second)   // std::set: ascending key = producer first
                    {
                        auto sit = states_.find(k);
                        if (sit == states_.end()) continue;
                        auto ait = sit->second.access.find(b);
                        if (ait == sit->second.access.end()) continue;
                        const BaseAccess& a = ait->second;

                        for (const Pin& in : a.entry)
                        {
                            // One edge per producer and consumer pair covers both RAW and WAW.
                            for (const BoundaryPin& w : live_writers)
                                if (detail::overlaps_any(w.regions, in.reads) ||
                                    detail::overlaps_any(w.regions, in.writes))
                                    add_cross_edge(w.node, in.node, b);

                            if (!in.writes.empty())
                                for (const BoundaryPin& r : pending_reads)
                                    if (detail::overlaps_any(r.regions, in.writes))
                                        add_cross_edge(r.node, in.node, b);   // WAR
                        }

                        std::vector<Core::ResourceRegion> covering;
                        for (const Pin& e : a.exit)
                            covering.insert(covering.end(), e.writes.begin(), e.writes.end());

                        if (!covering.empty())
                        {
                            retire_covered(live_writers, covering);
                            retire_covered(pending_reads, covering);
                        }

                        for (const Pin& e : a.exit)
                        {
                            if (!e.writes.empty()) live_writers.push_back({ e.node, e.writes });
                            if (!e.reads.empty())  pending_reads.push_back({ e.node, e.reads });
                        }
                    }
                }

                /**
                 * @brief Builds or rebuilds one region's cached sub-graph from its recorder,
                 *        in O(region).
                 */
                void build_region(RegionKey k, const Recorder& recorder,
                                  ::SushiRuntime::Graph::OffloadCoordinator* offload)
                {
                    BackingGraph bg(offload);
                    recorder.replay(bg);

                    RegionState st;
                    st.nodes = bg.take_nodes();   // intra edges baked; insertion order

                    for (auto& np : st.nodes)
                    {
                        Node* n = np.get();
                        n->region.intra_in_degree = n->in_degree.load(std::memory_order_relaxed);
                        n->region.boundary_in_degree.store(0, std::memory_order_relaxed);
                        n->region.boundary_successors.clear();
                        n->region.sentinel_linked = false;
                        n->region.intra_leaf =
                            n->inline_successor_count.load(std::memory_order_relaxed) == 0 &&
                            n->successor_list.load(std::memory_order_relaxed) == nullptr;
                        n->plan_index = plan_index_counter_++;
                        if (n->region.intra_in_degree == 0)
                            st.intra_roots.push_back(n);
                    }

                    st.access = detail::compute_boundary_access(st.nodes);
                    states_[k] = std::move(st);
                }

                /**
                 * @brief Removes a region's nodes from the roots, the sentinel and the bases it
                 *        touched.
                 * @pre The region's cross-region edges are already undone.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void teardown_region(RegionKey k)
                {
                    auto sit = states_.find(k);
                    if (sit == states_.end()) return;

                    for (auto& np : sit->second.nodes)
                    {
                        Node* n = np.get();
                        roots_.erase(n);
                        unlink_sentinel(n);
                    }
                    for (auto& ba : sit->second.access)
                    {
                        auto rit = base_regions_.find(ba.first);
                        if (rit != base_regions_.end())
                        {
                            rit->second.erase(k);
                            if (rit->second.empty()) base_regions_.erase(rit);
                        }
                    }
                    states_.erase(sit);
                }

                /**
                 * @brief Reassigns Node::registration_index densely by region key then add() order.
                 */
                void renumber_registration_indices()
                {
                    std::uint32_t next_index = 0;
                    for (auto& kv : states_)   // std::map<RegionKey, ...>: ascending key
                    {
                        // Grouping by value maps one add() call to exactly one global index.
                        std::unordered_map<std::uint32_t, std::uint32_t> local_to_global;
                        for (auto& np : kv.second.nodes)
                        {
                            Node* n = np.get();
                            auto it = local_to_global.find(n->registration_index);
                            if (it == local_to_global.end())
                                it = local_to_global.emplace(n->registration_index,
                                                             next_index++).first;
                            n->registration_index = it->second;
                        }
                    }
                }

                /**
                 * @brief Caches the compiled sub-graph of each region and owns the nodes the plan
                 *        runs.
                 */
                std::map<RegionKey, RegionState> states_;

                /** @brief Maps each allocation to the regions that touch it. */
                std::map<Base, std::set<RegionKey>>      base_regions_;
                std::map<Base, std::vector<BoundaryEdge>> base_edges_;

                /**
                 * @brief Holds the current DAG entry points: nodes whose two in-degrees are both
                 *        zero.
                 */
                std::unordered_set<Node*> roots_;

                /**
                 * @brief Holds the single completion sentinel; its baseline in-degree is the leaf
                 *        count.
                 */
                sushi_ptr<Node> sentinel_;

                std::size_t compiles_ = 0;
                std::size_t plan_index_counter_ = 0;
                bool committed_once_ = false;
        };

    } // namespace API
} // namespace SushiRuntime
