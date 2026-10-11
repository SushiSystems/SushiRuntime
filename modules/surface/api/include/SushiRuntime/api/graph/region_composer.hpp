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

#include <SushiRuntime/api/graph/boundary_access.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/memory/resource_region.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/offload_coordinator.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief Owns the compiled sub-graph of every region and stitches them into one plan.
         *
         * A move leaves the source with no region and no plan, ready to compose again.
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        class RegionComposer
        {
            public:
                /** @brief Caller-chosen identity of a region (e.g. a packed cell coordinate). */
                using RegionKey = std::uint64_t;

                /**
                 * @brief Rebuilds the changed regions, re-wires them and renumbers every node.
                 * A build that throws changes nothing; after a later throw the next commit
                 * rebuilds every live region.
                 * @param recorder_of Called as recorder_of(key); null when the region is gone.
                 * @pre A commit that threw is repeated with the dirty and dropped keys it had.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                template <class RecorderOf>
                void commit(const std::set<RegionKey>& dirty, const std::set<RegionKey>& dropped,
                            RecorderOf&& recorder_of,
                            ::SushiRuntime::Graph::OffloadCoordinator* offload)
                {
                    // A composer that was moved from holds no sentinel until it composes again.
                    if (sentinel_ == nullptr)
                        sentinel_ = make_sushi<Node>();

                    const bool everything = recompose_everything_;
                    std::set<RegionKey> rebuilt(dirty.begin(), dirty.end());
                    if (everything)
                        for (const auto& kv : states_)
                            rebuilt.insert(kv.first);

                    std::map<RegionKey, RegionState> built;
                    for (RegionKey k : rebuilt)
                    {
                        const Recorder* recorder = recorder_of(k);
                        if (recorder != nullptr)
                            built.emplace(k, build_region(*recorder, offload));
                    }

                    std::set<RegionKey> retired(rebuilt);
                    retired.insert(dropped.begin(), dropped.end());
                    std::set<Base> dirty_bases = bases_of(retired);
                    sushi_ptr<Node> fresh_sentinel;
                    if (everything)
                        fresh_sentinel = make_sushi<Node>();

                    // Nothing above changed a member a region's ordering depends on.
                    RecomposeGuard guard(*this);
                    if (everything)
                        discard_every_region(std::move(fresh_sentinel));
                    else
                        retire_regions(retired, dirty_bases);

                    for (auto& kv : built)
                        install_region(kv.first, std::move(kv.second), dirty_bases);
                    for (Base b : dirty_bases)
                        rewire_base(b);
                    publish_plan();

                    guard.dismiss();
                    recompose_everything_ = false;
                    committed_once_ = true;
                    ++compiles_;
                }

                /**
                 * @brief Re-arms the live in-degree of every node and of the sentinel, in
                 *        O(total nodes).
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
                [[nodiscard]] std::size_t compose_count() const noexcept { return compiles_; }

                /**
                 * @brief Returns whether a composed plan stands.
                 * @return False before the first commit and after a commit that threw midway.
                 */
                [[nodiscard]] bool composed() const noexcept { return committed_once_; }

                /**
                 * @brief Returns whether a region's cached sub-graph matches its recorder.
                 * @return False for a region never built, or built at another generation.
                 */
                [[nodiscard]] bool is_current(RegionKey key, const Recorder& recorder) const
                {
                    const auto found = states_.find(key);
                    return found != states_.end() &&
                           found->second.generation == recorder.generation();
                }

                /**
                 * @brief Returns the registration index a rule of a region carries in the plan.
                 * @param rule Position in the region's recording order, one per rule.
                 * @return The index, or TaskGraph::NO_REGISTRATION before the region is built.
                 */
                [[nodiscard]] std::uint32_t registration_of(RegionKey key, std::size_t rule) const
                {
                    const auto found = states_.find(key);
                    if (found == states_.end() || rule >= found->second.rule_nodes.size() ||
                        found->second.rule_nodes[rule] == nullptr)
                        return ::SushiRuntime::Graph::TaskGraph::NO_REGISTRATION;
                    return found->second.rule_nodes[rule]->registration_index;
                }

            protected:
                /**
                 * @brief Starts with no region and a fresh completion sentinel.
                 * @param profiling Whether the first plan requests per-node device timing.
                 */
                explicit RegionComposer(bool profiling)
                    : sentinel_(make_sushi<Node>()), profiling_requested_(profiling)
                {
                }

                /**
                 * @brief Takes over another composer's regions and plan.
                 * @param other Left with no region, no plan and a compose count of zero.
                 */
                RegionComposer(RegionComposer&& other) noexcept
                    : profiling_requested_(other.profiling_requested_)
                {
                    take(other);
                }

                /**
                 * @brief Replaces this composer's regions and plan with another's.
                 * @param other Left with no region, no plan and a compose count of zero.
                 */
                RegionComposer& operator=(RegionComposer&& other) noexcept
                {
                    if (this != &other)
                        take(other);
                    return *this;
                }

                RegionComposer(const RegionComposer&) = delete;
                RegionComposer& operator=(const RegionComposer&) = delete;
                ~RegionComposer() = default;

                /**
                 * @brief Requests or withdraws per-node device timing.
                 *
                 * The next commit stamps the request into the plan; the same value changes
                 * nothing.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void set_profiling(bool requested) noexcept
                {
                    if (requested == profiling_requested_)
                        return;
                    profiling_requested_ = requested;
                }

                /**
                 * @brief Returns whether the next plan requests per-node device timing.
                 * @return The last value given to set_profiling(), or the value the composer
                 *         was constructed with.
                 */
                [[nodiscard]] bool is_profiling_requested() const noexcept
                {
                    return profiling_requested_;
                }

                /** @brief The stitched plan; the owning graph and its test seams read it. */
                ::SushiRuntime::Graph::CompiledGraph plan_;

            private:
                using Node = ::SushiRuntime::Graph::Node;
                using Base = ::SushiRuntime::Memory::ResourceID;  // allocation identity (void*)
                using Pin = detail::Pin;
                using BaseAccess = detail::BaseAccess;

                /**
                 * @brief Holds a boundary pin reduced to one hazard's intervals, used by
                 *        rewire_base.
                 */
                struct BoundaryPin
                {
                    Node* node = nullptr;
                    std::vector<Memory::ResourceRegion> regions;
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
                    std::vector<const Node*>     rule_nodes;     /**< Last node of each rule. */
                    std::uint64_t                generation = 0; /**< Recorder generation built. */
                };

                /**
                 * @brief Records one cross-region edge, kept per base so a base can be un-wired
                 *        alone.
                 */
                struct BoundaryEdge { Node* from; Node* to; };

                /**
                 * @brief Marks a composer for a rebuild of every region unless dismissed.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                class RecomposeGuard final
                {
                    public:
                        /** @brief Watches composer until dismiss() or destruction. */
                        explicit RecomposeGuard(RegionComposer& composer) noexcept
                            : composer_(&composer)
                        {
                        }

                        /** @brief Invalidates the plan of a composer that was not dismissed. */
                        ~RecomposeGuard()
                        {
                            if (composer_ != nullptr)
                                composer_->invalidate_plan();
                        }

                        RecomposeGuard(const RecomposeGuard&) = delete;
                        RecomposeGuard& operator=(const RecomposeGuard&) = delete;
                        RecomposeGuard(RecomposeGuard&&) = delete;
                        RecomposeGuard& operator=(RecomposeGuard&&) = delete;

                        /** @brief Leaves the composer as it is. */
                        void dismiss() noexcept { composer_ = nullptr; }

                    private:
                        /** @brief Composer to invalidate, or null once dismissed. */
                        RegionComposer* composer_;
                };

                /**
                 * @brief Moves other's regions, plan and counters here and leaves other with none.
                 * @param other Composer to empty; its profiling request is copied, not cleared, and
                 *        commit() gives it a sentinel when it composes.
                 */
                void take(RegionComposer& other) noexcept
                {
                    plan_ = std::exchange(other.plan_, {});
                    states_ = std::exchange(other.states_, {});
                    base_regions_ = std::exchange(other.base_regions_, {});
                    base_edges_ = std::exchange(other.base_edges_, {});
                    roots_ = std::exchange(other.roots_, {});
                    sentinel_ = std::exchange(other.sentinel_, {});
                    compiles_ = std::exchange(other.compiles_, 0);
                    plan_index_counter_ = std::exchange(other.plan_index_counter_, 0);
                    committed_once_ = std::exchange(other.committed_once_, false);
                    recompose_everything_ = std::exchange(other.recompose_everything_, false);
                    profiling_requested_ = other.profiling_requested_;
                }

                /**
                 * @brief Makes the next commit rebuild every live region and leaves no plan to run.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void invalidate_plan() noexcept
                {
                    recompose_everything_ = true;
                    committed_once_ = false;
                    // A root may name a node a half-done commit freed.
                    plan_.roots.clear();
                }

                /** @brief Returns the allocations the built regions among keys touch. */
                [[nodiscard]] std::set<Base> bases_of(const std::set<RegionKey>& keys) const
                {
                    std::set<Base> bases;
                    for (RegionKey k : keys)
                    {
                        const auto found = states_.find(k);
                        if (found == states_.end())
                            continue;
                        for (const auto& ba : found->second.access)
                            bases.insert(ba.first);
                    }
                    return bases;
                }

                /**
                 * @brief Un-wires the allocations of the retired regions and tears those
                 *        regions down.
                 * @param bases Every allocation a retired region touches.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void retire_regions(const std::set<RegionKey>& retired, const std::set<Base>& bases)
                {
                    // A base's cross edges are undone while every endpoint is alive.
                    for (Base b : bases)
                    {
                        auto eit = base_edges_.find(b);
                        if (eit == base_edges_.end())
                            continue;
                        for (auto& e : eit->second)
                            remove_cross_edge(e.from, e.to);
                        eit->second.clear();
                    }
                    for (RegionKey k : retired)
                        teardown_region(k);
                }

                /**
                 * @brief Frees every region, edge and root and takes a fresh sentinel.
                 * @param sentinel Sentinel no node links to yet.
                 */
                void discard_every_region(sushi_ptr<Node> sentinel) noexcept
                {
                    states_.clear();
                    base_regions_.clear();
                    base_edges_.clear();
                    roots_.clear();
                    sentinel_ = std::move(sentinel);
                }

                /**
                 * @brief Takes a built region into the composer: its bases, roots and leaves.
                 * @param dirty_bases Gains every allocation the region touches.
                 * @pre No state is held for k.
                 */
                void install_region(RegionKey k, RegionState&& built, std::set<Base>& dirty_bases)
                {
                    RegionState& st = states_[k];
                    st = std::move(built);
                    for (auto& ba : st.access)
                    {
                        base_regions_[ba.first].insert(k);
                        dirty_bases.insert(ba.first);
                    }
                    // A fresh node carries no boundary in-degree yet.
                    for (Node* r : st.intra_roots)
                        roots_.insert(r);
                    for (auto& np : st.nodes)
                        if (np->region.intra_leaf) link_sentinel(np.get());
                }

                /**
                 * @brief Writes the roots, the sentinel and the registration indices into the plan.
                 *
                 * A plan that requests profiling also lists every node for the profiler.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void publish_plan()
                {
                    const bool profiling = profiling_requested_;
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
                }

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
                                           const std::vector<Memory::ResourceRegion>& covers)
                {
                    for (auto it = pins.begin(); it != pins.end(); )
                    {
                        auto& regions = it->regions;
                        regions.erase(std::remove_if(regions.begin(), regions.end(),
                            [&covers](const Memory::ResourceRegion& r)
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

                        std::vector<Memory::ResourceRegion> covering;
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
                 * @brief Builds one region's sub-graph from its recorder, in O(region).
                 * @return The state to install; no region held by the composer is changed.
                 * @throws Whatever the replay of the recorder throws.
                 */
                [[nodiscard]] RegionState build_region(
                    const Recorder& recorder, ::SushiRuntime::Graph::OffloadCoordinator* offload)
                {
                    ::SushiRuntime::Graph::TaskGraph bg(offload);
                    RegionState st;
                    st.generation = recorder.generation();
                    st.rule_nodes.assign(recorder.rule_count(), nullptr);
                    recorder.replay(bg, [&st, &bg](std::size_t rule, std::size_t)
                    {
                        st.rule_nodes[rule] = bg.last_node_added();
                    });

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
                    return st;
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

                /** @brief Set by a commit that threw after it changed a region. */
                bool recompose_everything_ = false;

                /** @brief Starts as RuntimeSettings::profiling; stamped into the plan at commit. */
                bool profiling_requested_ = false;
        };

    } // namespace API
} // namespace SushiRuntime
