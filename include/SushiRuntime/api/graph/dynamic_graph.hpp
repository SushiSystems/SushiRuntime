/**************************************************************************/
/* dynamic_graph.hpp                                                      */
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
 * @file dynamic_graph.hpp
 * @brief Declares DynamicGraph, a graph composed of regions that stream in and out.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <algorithm>
#include <chrono>
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

#include <SushiRuntime/api/graph/region.hpp>
#include <SushiRuntime/api/graph/run_handle.hpp>
#include <SushiRuntime/api/runtime/runtime.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/graph/node.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /** @brief Grants a test a friend seam onto DynamicGraph::plan_/states_. */
            struct DynamicGraphNodeEventsTestAccess;
        }

        /**
         * @brief Composes regions that stream in and out into one plan, rebuilt only where it
         *        changed.
         * @see include/SushiRuntime/api/graph/README.md
         */
        class DynamicGraph
        {
            public:
                /** @brief Caller-chosen identity of a region (e.g. a packed cell coordinate). */
                using RegionKey = std::uint64_t;

                /**
                 * @brief Takes over another graph's regions and pending structure.
                 * @param other Graph to move from; left with no regions.
                 */
                DynamicGraph(DynamicGraph&& other) = default;

                /**
                 * @brief Replaces this graph's contents with another's.
                 * @param other Graph to move from; left with no regions.
                 * @return *this, now owning @p other's regions.
                 */
                DynamicGraph& operator=(DynamicGraph&& other) = default;

                DynamicGraph(const DynamicGraph&) = delete;
                DynamicGraph& operator=(const DynamicGraph&) = delete;

                /**
                 * @brief Returns the region for key, creating it if absent, and marks it for
                 *        rebuild.
                 * @return A reference to the region, valid until the region is dropped.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                Region& region(RegionKey key)
                {
                    auto it = regions_.find(key);
                    if (it == regions_.end())
                        it = regions_.emplace(key,
                            std::unique_ptr<Region>(new Region(ctx_))).first;
                    dirty_regions_.insert(key);
                    return *it->second;
                }

                /**
                 * @brief Returns whether a live region with @p key exists.
                 * @param key Region identity to test.
                 * @return True if the region is present.
                 */
                bool has_region(RegionKey key) const noexcept
                {
                    return regions_.find(key) != regions_.end();
                }

                /**
                 * @brief Returns the region for @p key without marking it for a rebuild.
                 * @param key Region identity to look up.
                 * @return The live region, or nullptr when no region has that key.
                 */
                const Region* find_region(RegionKey key) const noexcept
                {
                    const auto it = regions_.find(key);
                    return it == regions_.end() ? nullptr : it->second.get();
                }

                /**
                 * @brief Removes the region for key from the live set; an absent key does nothing.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void drop(RegionKey key)
                {
                    if (regions_.erase(key) != 0)
                    {
                        dirty_regions_.erase(key);
                        dropped_.insert(key);
                    }
                }

                /**
                 * @brief Reports how many regions the graph currently holds.
                 * @return Number of live regions, dropped ones excluded.
                 */
                std::size_t region_count() const noexcept { return regions_.size(); }

                /**
                 * @brief Requests or withdraws per-node device timing for this graph.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void set_profiling_requested(bool requested)
                {
                    if (requested == profiling_requested_)
                        return;
                    profiling_requested_ = requested;
                    plan_.profiling = requested;
                    for (const auto& kv : regions_)
                        dirty_regions_.insert(kv.first);
                }

                /**
                 * @brief Reports whether this graph currently requests profiling.
                 * @return The last value set by set_profiling_requested(), or the
                 *         runtime's RuntimeSettings::profiling default.
                 */
                bool profiling_requested() const noexcept { return profiling_requested_; }

                /**
                 * @brief Reports how much work the graph currently holds.
                 * @return Total tasks recorded across all live regions.
                 */
                std::size_t size() const noexcept
                {
                    std::size_t total = 0;
                    for (const auto& kv : regions_)
                        total += static_cast<const Graph&>(*kv.second).task_count_;
                    return total;
                }

                /**
                 * @brief Number of times the plan has been (re)composed.
                 *
                 * Holds at 1 across a multi-step run that mutates nothing, and rises
                 * by one per commit (a run that follows at least one mutation),
                 * regardless of how many regions exist.
                 * @return The composition count since this graph was created.
                 */
                std::size_t compile_count() const noexcept { return compiles_; }

                /**
                 * @brief Runs one step, blocking until it completes.
                 * @return A RunReport summarising the step.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport run() { return run(std::size_t{1}); }

                /**
                 * @brief Runs @p steps successive steps, blocking until done.
                 * @param steps Number of steps to run.
                 * @return A RunReport aggregating the whole run.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport run(std::size_t steps) { return unwrap(try_run(steps)); }

                /**
                 * @brief Runs one step into a report the caller owns, without allocating.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport& run(Core::RunReport& reuse)
                {
                    return run(std::size_t{1}, reuse);
                }

                /**
                 * @brief Runs @p steps successive steps into a report the caller owns.
                 * @param steps Number of steps to run.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A reference to @p reuse.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                Core::RunReport& run(std::size_t steps, Core::RunReport& reuse)
                {
                    auto driven = try_run(steps, reuse);
                    if (!driven)
                        throw Core::SushiException(std::move(driven.error()));
                    return reuse;
                }

                /**
                 * @brief Runs steps until a wall-clock @p budget elapses.
                 * @param budget Wall-clock limit; at least one step always runs.
                 * @return A RunReport aggregating the steps that ran.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                template <class Rep, class Period>
                Core::RunReport run_for(std::chrono::duration<Rep, Period> budget)
                {
                    const auto deadline = std::chrono::steady_clock::now() + budget;
                    return unwrap(drive([deadline](std::size_t k)
                    {
                        return k == 0 || std::chrono::steady_clock::now() < deadline;
                    }));
                }

                /**
                 * @brief Runs steps until @p done returns true.
                 * @param done Stop condition checked before each step.
                 * @return A RunReport aggregating the steps that ran.
                 * @throws Core::SushiException on any synchronous or device error.
                 */
                template <class Predicate>
                Core::RunReport run_until(Predicate done)
                {
                    return unwrap(drive([done](std::size_t) { return !done(); }));
                }

                /**
                 * @brief Non-throwing multi-step run.
                 * @param steps Number of steps to run.
                 * @return The RunReport on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport, Core::Error>
                try_run(std::size_t steps) noexcept
                {
                    return drive([steps](std::size_t k) { return k < steps; }, steps);
                }

                /**
                 * @brief Non-throwing single run into a report the caller owns.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to @p reuse on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport*, Core::Error>
                try_run(Core::RunReport& reuse) noexcept
                {
                    return try_run(std::size_t{1}, reuse);
                }

                /**
                 * @brief Non-throwing multi-step run into a report the caller owns.
                 * @param steps Number of steps to run.
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A pointer to @p reuse on success, or an Error on failure.
                 */
                [[nodiscard]] Core::Result<Core::RunReport*, Core::Error>
                try_run(std::size_t steps, Core::RunReport& reuse) noexcept
                {
                    return drive([steps](std::size_t k) { return k < steps; }, reuse, steps);
                }

                /**
                 * @brief Requests cancellation of an in-progress run, from any thread.
                 *
                 * Mirrors Graph::cancel(): the active step drains without launching
                 * new kernels and the driver stops before the next step.
                 */
                void cancel() { ctx_->get_execution_engine().cancel(); }

                /**
                 * @brief Starts one step on the worker pool and returns without waiting for it.
                 * @pre The returned handle does not outlive this graph.
                 * @throws Core::SushiException If nothing is runnable, a handle was freed, the
                 *        engine
                 *         cannot run asynchronously, or a run is in progress.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                RunHandle<DynamicGraph> run_async()
                {
                    if (regions_.empty())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "DynamicGraph has no regions to run."});
                    if (async_step_pending_)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                            "A step started with run_async() is still in flight; wait "
                            "on its handle before starting another."});
                    for (const auto& kv : regions_)
                        if (static_cast<const Graph&>(*kv.second).has_dangling_resource())
                            throw Core::SushiException(Core::Error{Core::Errc::dangling_resource,
                                "DynamicGraph region references a Buffer/State freed "
                                "before the run."});

                    auto& engine = ctx_->get_execution_engine();
                    if (!engine.supports_asynchronous_run())
                        throw Core::SushiException(Core::Error{Core::Errc::unsupported,
                            "The active execution engine does not support run_async()."});

                    using clock = std::chrono::steady_clock;
                    detail::AsyncRunState state;

                    if (needs_commit())
                    {
                        const auto compose_started = clock::now();
                        commit();
                        state.compile_ms = std::chrono::duration<double, std::milli>(
                            clock::now() - compose_started).count();
                    }

                    if (plan_.roots.empty())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                                                              "DynamicGraph has no runnable tasks."});

                    refresh_updaters();
                    steps_.resync();
                    engine.reset_cancellation();
                    if (profiling_requested_)
                        engine.reset_profiling(plan_);

                    state.before = engine.metrics();
                    state.device_to_host_before = ctx_->device_to_host_bytes();
                    state.host_to_device_before = ctx_->host_to_device_bytes();
                    state.task_count = size();

                    steps_.before_step();
                    rearm();
                    state.started = clock::now();
                    engine.submit(plan_);

                    async_step_pending_ = true;
                    return RunHandle<DynamicGraph>(this, state);
                }

            private:
                friend class Runtime;
                // RunHandle settles a step through the two private members below and nothing else.
                template <class Owner> friend class RunHandle;
                friend struct detail::DynamicGraphNodeEventsTestAccess;

                /** @brief The engine a pending asynchronous step is running on. */
                Execution::IExecutionEngine& async_engine()
                {
                    return ctx_->get_execution_engine();
                }

                /**
                 * @brief Releases the in-flight marker for a step that ended badly.
                 *
                 * See Graph::discard_async_step: a task exception must not leave the
                 * graph permanently refusing to run.
                 */
                void discard_async_step() noexcept { async_step_pending_ = false; }

                /**
                 * @brief Closes out an asynchronous step once the engine reports it done.
                 * @param report The report to fill; reset before it is written.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void finish_async_step(const detail::AsyncRunState& state,
                                       Core::RunReport& report)
                {
                    const auto finished = std::chrono::steady_clock::now();
                    auto& engine = ctx_->get_execution_engine();

                    async_step_pending_ = false;
                    steps_.check_faults();
                    steps_.after_step();

                    const auto after = engine.metrics();
                    detail::fill_run_report(
                        report, *ctx_, state.before, after, state.task_count, state.compile_ms,
                        std::chrono::duration<double, std::milli>(
                            finished - state.started).count());

                    report.cancelled = engine.is_cancelled();
                    report.device_to_host_bytes =
                        ctx_->device_to_host_bytes() - state.device_to_host_before;
                    report.host_to_device_bytes =
                        ctx_->host_to_device_bytes() - state.host_to_device_before;

                    if (plan_.profiling)
                        engine.collect_profiling(plan_, report);
                }

                using Node = ::SushiRuntime::Graph::Node;
                using Base = ::SushiRuntime::Core::ResourceId;  // allocation identity (void*)

                explicit DynamicGraph(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx)), sentinel_(make_sushi<Node>()),
                      profiling_requested_(ctx_->settings().profiling)
                {
                    plan_.profiling = profiling_requested_;
                }

                /** @brief Throws the carried error, or returns the report. */
                static Core::RunReport unwrap(Core::Result<Core::RunReport, Core::Error> r)
                {
                    if (!r)
                        throw Core::SushiException(std::move(r.error()));
                    return *r;
                }

                /**
                 * @brief Returns a region's Graph subobject through the private-base conversion.
                 */
                static Graph& as_graph(Region& r) noexcept { return static_cast<Graph&>(r); }

                /**
                 * @brief Holds one node's participation in a base's boundary with its exact byte
                 *        intervals.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                struct Pin
                {
                    Node* node = nullptr;
                    std::vector<Core::ResourceRegion> reads;
                    std::vector<Core::ResourceRegion> writes;
                };

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
                 * @brief Summarises one region's boundary accesses to one shared allocation.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                struct BaseAccess
                {
                    std::vector<Pin> entry;  // no intra predecessor overlapping this base
                    std::vector<Pin> exit;   // accesses not shadowed by an intra successor's write
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
                 * @brief Calls fn(succ) for each intra successor of a node; commit-time only,
                 *        single threaded.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                template <class Fn>
                static void for_each_intra_successor(Node* n, Fn&& fn)
                {
                    const int32_t count = n->inline_successor_count.load(std::memory_order_relaxed);
                    const int32_t max_idx = std::min<int32_t>(count, Node::MAX_INLINE_SUCCESSORS);
                    for (int32_t i = 0; i < max_idx; ++i)
                        if (n->inline_successors[i]) fn(n->inline_successors[i]);
                    for (auto* s = n->successor_list.load(std::memory_order_relaxed); s; s = s->next)
                        if (s->target) fn(s->target);
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
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void add_cross_edge(Node* from, Node* to, Base b)
                {
                    if (from->region.sentinel_linked)
                        unlink_sentinel(from);
                    from->region.boundary_successors.push_back(to);

                    const int32_t prev = to->region.boundary_in_degree.fetch_add(1, std::memory_order_relaxed);
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

                    const int32_t prev = to->region.boundary_in_degree.fetch_sub(1, std::memory_order_relaxed);
                    if (prev == 1 && to->region.intra_in_degree == 0)
                        roots_.insert(to);

                    // An intra-leaf with no cross-region successor left holds the sentinel again.
                    if (from->region.intra_leaf && !from->region.sentinel_linked && from->region.boundary_successors.empty())
                        link_sentinel(from);
                }

                // True when any interval in @p a intersects any interval in @p b.
                static bool overlaps_any(const std::vector<Core::ResourceRegion>& a,
                                         const std::vector<Core::ResourceRegion>& b)
                {
                    for (const auto& x : a)
                        for (const auto& y : b)
                            if (x.overlaps(y))
                                return true;
                    return false;
                }

                /** @brief Returns whether one interval in covers fully contains r. */
                static bool covered_by_any(const Core::ResourceRegion& r,
                                           const std::vector<Core::ResourceRegion>& covers)
                {
                    for (const auto& c : covers)
                        if (c.contains(r))
                            return true;
                    return false;
                }

                /**
                 * @brief Drops from pins every interval a later write fully covers, and emptied
                 *        pins with them.
                 * @see include/SushiRuntime/api/graph/README.md
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
                                return covered_by_any(r, covers);
                            }), regions.end());

                        if (regions.empty()) it = pins.erase(it);
                        else                 ++it;
                    }
                }

                /**
                 * @brief Re-derives the cross-region edges of one shared allocation from byte
                 *        intervals.
                 * @see include/SushiRuntime/api/graph/README.md
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
                 * @brief Builds or rebuilds one region's cached sub-graph from its recorded specs,
                 *        in O(region).
                 */
                void build_region(RegionKey k)
                {
                    Graph& g = as_graph(*regions_.at(k));

                    BackingGraph bg(*ctx_);
                    for (auto& spec : g.specs_)
                    {
                        spec.emit(bg);
                        bg.name_last_node(spec.name);
                    }

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

                    compute_access(st);
                    states_[k] = std::move(st);
                }

                /**
                 * @brief Fills a region's per-base boundary summary with entry and exit pins by
                 *        byte interval.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void compute_access(RegionState& st)
                {
                    struct Acc
                    {
                        std::vector<Core::ResourceRegion> reads;
                        std::vector<Core::ResourceRegion> writes;
                    };
                    std::map<Base, std::unordered_map<Node*, Acc>> per_base;

                    for (auto& np : st.nodes)
                    {
                        Node* n = np.get();
                        for (const auto& r : n->io_reads_)
                            if (r.base) per_base[r.base][n].reads.push_back(r);
                        for (const auto& w : n->io_writes_)
                            if (w.base) per_base[w.base][n].writes.push_back(w);
                    }

                    for (auto& bm : per_base)
                    {
                        const Base b = bm.first;
                        auto& m = bm.second;   // node -> Acc for this base

                        // Both maps are keyed by node and filled per base b.
                        std::unordered_set<Node*> has_overlapping_pred;
                        std::unordered_map<Node*, std::vector<Core::ResourceRegion>> shadowing;

                        for (auto& nm : m)
                        {
                            Node* n = nm.first;
                            const Acc& n_acc = nm.second;
                            for_each_intra_successor(n, [&](Node* s)
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

                        const std::vector<Core::ResourceRegion> nothing_shadows;

                        BaseAccess ba;
                        for (auto& nm : m)
                        {
                            Node* n = nm.first;
                            const Acc& acc = nm.second;

                            if (has_overlapping_pred.find(n) == has_overlapping_pred.end())
                                ba.entry.push_back(Pin{ n, acc.reads, acc.writes });

                            auto sit = shadowing.find(n);
                            const std::vector<Core::ResourceRegion>& covers =
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
                        st.access.emplace(b, std::move(ba));
                    }
                }

                /**
                 * @brief Removes a region's nodes from the roots, the sentinel and the bases it
                 *        touched.
                 * @pre The region's cross-region edges are already undone.
                 * @see include/SushiRuntime/api/graph/README.md
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

                /** @brief Returns whether a structural change or new tasks require a recompose. */
                bool needs_commit() const
                {
                    return !committed_once_ || !dirty_regions_.empty() || !dropped_.empty();
                }

                /**
                 * @brief Re-stitches the plan from the changed regions and their shared boundaries
                 *        only.
                 */
                void commit()
                {
                    // teardown lists regions to tear down; dirty_bases the bases to re-wire.
                    std::set<RegionKey> teardown(dirty_regions_.begin(), dirty_regions_.end());
                    teardown.insert(dropped_.begin(), dropped_.end());

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
                    for (RegionKey k : dirty_regions_)
                    {
                        if (regions_.find(k) == regions_.end()) continue;  // dropped, not re-created
                        build_region(k);
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

                    dirty_regions_.clear();
                    dropped_.clear();

                    // Only the profiler reads all_nodes, so it is gathered only when profiling.
                    plan_.roots.assign(roots_.begin(), roots_.end());
                    plan_.sentinel = sentinel_;
                    plan_.profiling = profiling_requested_;
                    plan_.all_nodes.clear();
                    if (profiling_requested_)
                        for (auto& kv : states_)
                            for (auto& np : kv.second.nodes)
                                plan_.all_nodes.push_back(np);

                    renumber_registration_indices();
                    if (profiling_requested_)
                        plan_.capture_profiling_state();

                    committed_once_ = true;
                    ++compiles_;
                }

                /** @brief Reassigns Node::registration_index densely by region key then add() order. */
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
                                it = local_to_global.emplace(n->registration_index, next_index++).first;
                            n->registration_index = it->second;
                        }
                    }
                }

                /**
                 * @brief Re-arms every node's live in-degree and refreshes the per-step updaters,
                 *        O(total).
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void rearm()
                {
                    for (auto& kv : states_)
                        for (auto& np : kv.second.nodes)
                            np->arm_in_degree();
                    sentinel_->arm_in_degree();
#ifdef SR_DISTRIBUTED
                    for (auto& kv : states_)
                        for (auto& np : kv.second.nodes)
                            if (auto* off = np->offload_state())
                                off->attempts.store(0, std::memory_order_relaxed);
#endif
                }

                template <class KeepGoing>
                Core::Result<Core::RunReport, Core::Error>
                drive(KeepGoing keep_going, std::size_t expected_invocations = 1) noexcept
                {
                    Core::RunReport report;
                    auto driven = drive(keep_going, report, expected_invocations);
                    if (!driven)
                        return Core::make_error(std::move(driven.error()));
                    return std::move(report);
                }

                /**
                 * @brief Drives the run loop into a caller-owned report; every run entry goes
                 *        through here.
                 * @param keep_going Called with the completed step count before each step; true
                 *        continues.
                 * @param report The report to fill; reset before the run starts.
                 * @return A pointer to report on success, or a mapped Error.
                 */
                template <class KeepGoing>
                Core::Result<Core::RunReport*, Core::Error>
                drive(KeepGoing keep_going, Core::RunReport& report,
                     std::size_t expected_invocations = 1) noexcept
                {
                    // Before any early return; see Graph::drive().
                    report.reset();
                    report.is_successful = false;

                    if (regions_.empty())
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                                                            "DynamicGraph has no regions to run."});
                    // rearm() must not run while the workers still execute a step.
                    if (async_step_pending_)
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                            "A step started with run_async() is still in flight; wait "
                            "on its handle before running this graph again."});
                    for (const auto& kv : regions_)
                        if (static_cast<const Graph&>(*kv.second).has_dangling_resource())
                            return Core::make_error(Core::Error{Core::Errc::dangling_resource,
                                "DynamicGraph region references a Buffer/State freed before the run."});
                    try
                    {
                        drive_impl(keep_going, report, expected_invocations);
                        return &report;
                    }
                    catch (const Core::SushiException& e)
                    {
                        return Core::make_error(e.error());
                    }
                    catch (const sycl::exception& e)
                    {
                        return Core::make_error(Core::Error{Core::Errc::kernel_failed, e.what()});
                    }
                    catch (const std::exception& e)
                    {
                        return Core::make_error(Core::Error{Core::Errc::compile_failed, e.what()});
                    }
                    catch (...)
                    {
                        return Core::make_error(Core::Error{Core::Errc::compile_failed,
                                                            "unknown exception"});
                    }
                }

                template <class KeepGoing>
                void drive_impl(KeepGoing keep_going, Core::RunReport& report,
                                std::size_t expected_invocations = 1)
                {
                    using clock = std::chrono::steady_clock;
                    auto ms = [](clock::time_point a, clock::time_point b)
                    {
                        return std::chrono::duration<double, std::milli>(b - a).count();
                    };

                    double compile_ms = 0.0;
                    if (needs_commit())
                    {
                        const auto c0 = clock::now();
                        commit();
                        compile_ms = ms(c0, clock::now());
                    }

                    if (plan_.roots.empty())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                                                              "DynamicGraph has no runnable tasks."});

                    // Updaters are aggregated from every live region in region-key order.
                    refresh_updaters();
                    steps_.resync();

                    auto& engine = ctx_->get_execution_engine();
                    engine.reset_cancellation();

                    const bool profiling = plan_.profiling;
                    if (profiling)
                        engine.reset_profiling(plan_, expected_invocations);

                    const auto before = engine.metrics();
                    const std::size_t d2h_before = ctx_->device_to_host_bytes();
                    const std::size_t h2d_before = ctx_->host_to_device_bytes();
                    const auto t0 = clock::now();

                    std::size_t task_count = size();
                    std::size_t executed = 0;
                    while (keep_going(executed) && !engine.is_cancelled())
                    {
                        steps_.before_step();

                        rearm();
                        engine.execute(plan_);
                        steps_.check_faults();

                        steps_.after_step();
                        ++executed;
                    }

                    const auto t1 = clock::now();
                    const auto after = engine.metrics();

                    detail::fill_run_report(report, *ctx_, before, after,
                                            task_count * executed,
                                            compile_ms, ms(t0, t1));
                    report.cancelled = engine.is_cancelled();
                    report.device_to_host_bytes = ctx_->device_to_host_bytes() - d2h_before;
                    report.host_to_device_bytes = ctx_->host_to_device_bytes() - h2d_before;

                    if (profiling)
                        engine.collect_profiling(plan_, report);
                }

                /**
                 * @brief Rebuilds the aggregated per-step updater lists from every live region.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                void refresh_updaters()
                {
                    steps_.reset_updaters();
                    for (auto& kv : regions_)
                        steps_.aggregate_from(as_graph(*kv.second).steps_);
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;

                /**
                 * @brief Holds the live regions by key; unique_ptr keeps each Region's address
                 *        stable.
                 * @see include/SushiRuntime/api/graph/README.md
                 */
                std::map<RegionKey, std::unique_ptr<Region>> regions_;

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

                // Pending structural changes applied at the next commit.
                std::set<RegionKey> dirty_regions_;
                std::set<RegionKey> dropped_;

                /** @brief Aggregates every live region's per-step lifecycle before each run. */
                detail::StepMachinery steps_;

                Execution::CompiledGraph plan_;

                /** @brief Tells whether a run_async() step has not been settled yet. */
                bool async_step_pending_ = false;
                std::size_t compiles_ = 0;
                std::size_t plan_index_counter_ = 0;
                bool committed_once_ = false;

                /** @brief Defaults from RuntimeSettings::profiling; mirrors plan_.profiling. */
                bool profiling_requested_ = false;
        };

        // Defined here because DynamicGraph is complete only from this point.
        inline DynamicGraph Runtime::dynamic_graph()
        {
            return DynamicGraph(ctx_);
        }

    } // namespace API

    using API::DynamicGraph;

} // namespace SushiRuntime
