/**************************************************************************/
/* region_composer.cpp                                                    */
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
 * @file region_composer.cpp
 * @brief Implements RegionComposer, which stitches the sub-graph of every region into one plan.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
 */

#include <SushiRuntime/api/graph/plumbing/region_composer.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/sushi_ptr.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>
#include <SushiRuntime/graph/task_graph.hpp>
#include <SushiRuntime/machine/node_facts.hpp>
#include <SushiRuntime/placement/placement_rules.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/task/node.hpp>

#include <SushiRuntime/region_boundary/boundary_access.hpp>
#include <SushiRuntime/engine_handles/plan_access.hpp>
#include "recorder_replay.hpp"
#include "rule_stamp.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Owns the compiled sub-graph of every region and the plan stitched from them.
         * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
         */
        class RegionComposerState final
        {
            public:
                /** @brief Caller-chosen identity of a region (e.g. a packed cell coordinate). */
                using RegionKey = std::uint64_t;

                /**
                 * @brief Rebuilds the changed regions, re-wires them and renumbers every node.
                 * @param recorder_of Called as recorder_of(key); null when the region is gone.
                 * @param profiling   Whether the plan requests per-node device timing.
                 * @pre A commit that threw is repeated with the dirty and dropped keys it had.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                [[nodiscard]] Result<void>
                commit(const std::set<RegionKey>& dirty, const std::set<RegionKey>& dropped,
                       const std::function<const Recorder*(RegionKey)>& recorder_of,
                       bool profiling, const Internal::Execution::RuntimeContext& runtime,
                       const NodeFacts* here)
                {
                    const bool everything = recompose_everything_;
                    std::set<RegionKey> rebuilt(dirty.begin(), dirty.end());
                    if (everything)
                        for (const auto& kv : states_)
                            rebuilt.insert(kv.first);

                    std::shared_ptr<const PlacementRules> rules = runtime.placement_rules();
                    const RuleStamp stamp(rules.get(), here, runtime.offload() == nullptr);
                    std::map<RegionKey, RegionState> built;
                    for (RegionKey k : rebuilt)
                    {
                        const Recorder* recorder = recorder_of(k);
                        if (recorder == nullptr)
                            continue;
                        RegionState state = build_region(*recorder);
                        for (auto& np : state.nodes)
                        {
                            Result<void> stamped = stamp.stamp(*np);
                            if (!stamped)
                                return make_error(std::move(stamped.error()));
                        }
                        built.emplace(k, std::move(state));
                    }
                    profiling_requested_ = profiling;

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
                    rules_ = std::move(rules);
                    rules_generation_ = runtime.placement_generation();
                    ++compiles_;
                    return {};
                }

                /** @brief Returns whether a rule change followed the last commit. */
                [[nodiscard]] bool rules_changed(std::uint64_t generation) const noexcept
                {
                    return committed_once_ && generation != rules_generation_;
                }

                /**
                 * @brief Re-arms the live in-degree of every node and of the sentinel, in
                 *        O(total nodes).
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
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
                 * @return The index, or TaskGraph::K_NO_REGISTRATION before the region is built.
                 */
                [[nodiscard]] std::uint32_t registration_of(RegionKey key, std::size_t rule) const
                {
                    const auto found = states_.find(key);
                    if (found == states_.end() || rule >= found->second.rule_nodes.size() ||
                        found->second.rule_nodes[rule] == nullptr)
                        return Internal::Graph::TaskGraph::K_NO_REGISTRATION;
                    return found->second.rule_nodes[rule]->registration_index;
                }

                /** @brief Starts with no region and a fresh completion sentinel. */
                RegionComposerState() : sentinel_(make_sushi<Node>())
                {
                }

                RegionComposerState(const RegionComposerState&) = delete;
                RegionComposerState& operator=(const RegionComposerState&) = delete;

                /** @brief The stitched plan; the owning graph and its test seams read it. */
                Internal::Graph::CompiledGraph plan_;

                using Node = Internal::Graph::Node;
                using Base = ::SushiRuntime::ResourceID;  // allocation identity (void*)

                /**
                 * @brief Holds a boundary pin reduced to one hazard's intervals, used by
                 *        rewire_base.
                 */
                struct BoundaryPin
                {
                    Node* node = nullptr;
                    std::vector<BufferRegion> regions;
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
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                class RecomposeGuard final
                {
                    public:
                        /** @brief Watches composer until dismiss() or destruction. */
                        explicit RecomposeGuard(RegionComposerState& composer) noexcept
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
                        RegionComposerState* composer_;
                };

                /**
                 * @brief Makes the next commit rebuild every live region and leaves no plan to run.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
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
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
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
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
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
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
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
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
                 */
                static void retire_covered(std::vector<BoundaryPin>& pins,
                                           const std::vector<BufferRegion>& covers)
                {
                    for (auto it = pins.begin(); it != pins.end(); )
                    {
                        auto& regions = it->regions;
                        regions.erase(std::remove_if(regions.begin(), regions.end(),
                            [&covers](const BufferRegion& r)
                            {
                                return covered_by_any(r, covers);
                            }), regions.end());

                        if (regions.empty()) it = pins.erase(it);
                        else                 ++it;
                    }
                }

                /**
                 * @brief Re-derives the cross-region edges of one shared allocation from byte
                 *        intervals.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
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
                                if (overlaps_any(w.regions, in.reads) ||
                                    overlaps_any(w.regions, in.writes))
                                    add_cross_edge(w.node, in.node, b);

                            if (!in.writes.empty())
                                for (const BoundaryPin& r : pending_reads)
                                    if (overlaps_any(r.regions, in.writes))
                                        add_cross_edge(r.node, in.node, b);   // WAR
                        }

                        std::vector<BufferRegion> covering;
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
                [[nodiscard]] RegionState build_region(const Recorder& recorder)
                {
                    Internal::Graph::TaskGraph backing_graph;
                    RegionState st;
                    st.generation = recorder.generation();
                    st.rule_nodes.assign(recorder.rule_count(), nullptr);
                    RecorderReplay::replay(recorder, backing_graph,
                                           [&st, &backing_graph](std::size_t rule, std::size_t)
                    {
                        st.rule_nodes[rule] = backing_graph.last_node_added();
                    });

                    st.nodes = backing_graph.take_nodes();   // intra edges baked; insertion order

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

                    st.access = compute_boundary_access(st.nodes);
                    return st;
                }

                /**
                 * @brief Removes a region's nodes from the roots, the sentinel and the bases it
                 *        touched.
                 * @pre The region's cross-region edges are already undone.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/plumbing/README.md
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

                /** @brief The request of the commit in progress; stamped into the plan. */
                bool profiling_requested_ = false;

                /** @brief The rule set the last commit stamped with; the plan's nodes index it. */
                std::shared_ptr<const PlacementRules> rules_;

                /** @brief The context's placement generation at the last commit. */
                std::uint64_t rules_generation_ = 0;
        };

        RegionComposer::RegionComposer(bool profiling) noexcept : profiling_requested_(profiling)
        {
        }

        RegionComposer::RegionComposer(RegionComposer&& other) noexcept
            : state_(std::move(other.state_)), profiling_requested_(other.profiling_requested_)
        {
        }

        RegionComposer& RegionComposer::operator=(RegionComposer&& other) noexcept
        {
            if (this != &other)
            {
                state_ = std::move(other.state_);
                profiling_requested_ = other.profiling_requested_;
            }
            return *this;
        }

        RegionComposer::~RegionComposer() = default;

        Result<void> RegionComposer::commit(
            const std::set<RegionKey>& dirty, const std::set<RegionKey>& dropped,
            const std::function<const Recorder*(RegionKey)>& recorder_of,
            const ContextHandle& context, const NodeFacts* here)
        {
            // A composer that never composed, or was moved from, holds no state yet.
            if (state_ == nullptr)
                state_ = std::make_unique<Detail::RegionComposerState>();
            return state_->commit(dirty, dropped, recorder_of, profiling_requested_,
                                  Internal::ContextAccess::context(context), here);
        }

        bool RegionComposer::rules_changed(const ContextHandle& context) const noexcept
        {
            return state_ != nullptr &&
                   state_->rules_changed(
                       Internal::ContextAccess::context(context).placement_generation());
        }

        void RegionComposer::rearm() noexcept
        {
            state_->rearm();
        }

        std::size_t RegionComposer::compose_count() const noexcept
        {
            return state_ != nullptr ? state_->compose_count() : 0;
        }

        bool RegionComposer::composed() const noexcept
        {
            return state_ != nullptr && state_->composed();
        }

        bool RegionComposer::is_current(RegionKey key, const Recorder& recorder) const
        {
            return state_ != nullptr && state_->is_current(key, recorder);
        }

        std::uint32_t RegionComposer::registration_of(RegionKey key,
                                                      std::size_t rule) const noexcept
        {
            return state_ != nullptr ? state_->registration_of(key, rule)
                                     : Detail::K_NO_REGISTRATION;
        }

        bool RegionComposer::has_roots() const noexcept
        {
            return state_ != nullptr && !state_->plan_.roots.empty();
        }

        Plan* RegionComposer::compiled_plan() noexcept
        {
            return surface_plan(&state_->plan_);
        }

        const Plan* RegionComposer::compiled_plan() const noexcept
        {
            return surface_plan(&state_->plan_);
        }
    } // namespace Detail
} // namespace SushiRuntime
