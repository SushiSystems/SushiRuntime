/**************************************************************************/
/* dynamic_graph.hpp                                                      */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

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
        /**
         * @brief A graph partitioned into regions that stream in and out cheaply.
         *
         * The mutable counterpart to the static Graph, built for an open world: work
         * is grouped into regions keyed by a caller id (a spatial cell, a streaming
         * chunk). region(key) records tasks with the static graph's add() surface;
         * drop(key) removes one. Mutations are applied at a step boundary, never
         * against the running DAG, so the lock-free scheduler is untouched.
         *
         * The composition is **incremental in the changed region and its boundary**,
         * not the world. Each region compiles to a cached sub-graph whose interior
         * (intra-region) edges are baked once and never touched again while the
         * region is unchanged. Dependencies that cross a region boundary — two
         * regions sharing a buffer — live in a separate layer on each node
         * (Node::region, a RegionLink) that the composer
         * maintains. When a region streams in or out only its own nodes are rebuilt
         * and only the boundary edges of the allocations it shares are re-derived;
         * the interior of every other region, and every boundary that did not
         * change, is left exactly as it was. Commit therefore costs
         * O(changed region + affected boundary pins), independent of the size of the
         * untouched world.
         *
         * Cross-region ordering follows ascending region key: for an allocation two
         * regions share, the lower-keyed region is the producer. Read/read sharing
         * stays parallel (no edge between two regions that only read the same
         * buffer); only true RAW/WAR/WAW hazards across regions are serialized.
         * Tasks within one region keep their add() order.
         *
         * Sharing is decided on **byte intervals, not allocations**. Two regions that
         * touch disjoint element ranges of one buffer gain no edge, which is what
         * makes one-region-per-simulation-island viable: islands are disjoint index
         * ranges of a shared entity column, not separate allocations, so an
         * allocation-granular boundary would serialize islands that never meet. Name
         * a slice with Buffer::region({offset, count}) and pass it to
         * Reads(...)/Writes(...) exactly as within one graph.
         *
         * Obtain one from Runtime::dynamic_graph(). Moveable, not copyable.
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
                 * @brief Returns the region for @p key, creating it if absent.
                 *
                 * The returned region records work with the same add() surface as a
                 * static graph. Accessing a region marks it to be (re)built at the
                 * next step boundary, since the caller accesses it to add work.
                 *
                 * @param key Identity of the region; stable across steps.
                 * @return A reference to the region, valid until the region is dropped.
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
                 * @brief Removes the region for @p key from the live set.
                 *
                 * Takes effect on the structure immediately (region()/has_region no
                 * longer see it); the boundary it leaves is re-stitched at the next
                 * run, so a region is never removed from a DAG that is mid-step.
                 * Dropping an absent region is a no-op.
                 *
                 * @param key Identity of the region to remove.
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
                 * @brief Runs one step into a report the caller owns.
                 *
                 * The allocation-free form of run(); see Graph::run(Core::RunReport&)
                 * for what "reuse" buys and where it stops.
                 *
                 * @param reuse The report to overwrite; its previous contents are lost.
                 * @return A reference to @p reuse.
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
                    return drive([steps](std::size_t k) { return k < steps; });
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
                    return drive([steps](std::size_t k) { return k < steps; }, reuse);
                }

                /**
                 * @brief Requests cancellation of an in-progress run, from any thread.
                 *
                 * Mirrors Graph::cancel(): the active step drains without launching
                 * new kernels and the driver stops before the next step.
                 */
                void cancel() { ctx_->get_execution_engine().cancel(); }

                /**
                 * @brief Starts one step and returns without waiting for it.
                 *
                 * Mirrors Graph::run_async(): the step goes to the worker pool and
                 * this returns, so the caller can extract the previous step's state
                 * while this one runs and then settle up through the handle. Any
                 * pending structural change (a region added or dropped) is committed
                 * before the step is submitted, exactly as a synchronous run does.
                 *
                 * The handle must not outlive this graph, and its destructor waits
                 * for the step rather than letting the plan be destroyed underneath
                 * the workers - see RunHandle.
                 *
                 * @return A handle for the step now in flight.
                 * @throws Core::SushiException If there are no regions or no runnable
                 *         tasks, a region references a freed Buffer/State, the engine
                 *         does not support asynchronous runs, or a run is already in
                 *         progress.
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
                    if (ctx_->profiling_enabled())
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
                // The handle run_async() returns settles the step through the two
                // private members below, which is the whole of its access.
                template <class Owner> friend class RunHandle;

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
                 * @brief Closes out an asynchronous step once the engine reports done.
                 *
                 * The counterpart of Graph::finish_async_step for the composed plan:
                 * every live region's States advance here, after the step genuinely
                 * finished, so an asynchronous step is observationally identical to a
                 * synchronous one.
                 *
                 * @param state  What run_async() recorded when it submitted the step.
                 * @param report The report to fill; reset before it is written.
                 */
                void finish_async_step(const detail::AsyncRunState& state,
                                       Core::RunReport& report)
                {
                    const auto finished = std::chrono::steady_clock::now();
                    auto& engine = ctx_->get_execution_engine();

                    async_step_pending_ = false;
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

                    if (ctx_->profiling_enabled())
                        engine.collect_profiling(plan_, report);
                }

                using Node = ::SushiRuntime::Graph::Node;
                using Base = ::SushiRuntime::Core::ResourceId;  // allocation identity (void*)

                explicit DynamicGraph(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx)), sentinel_(make_sushi<Node>())
                {
                }

                /** @brief Throws the carried error, or returns the report. */
                static Core::RunReport unwrap(Core::Result<Core::RunReport, Core::Error> r)
                {
                    if (!r)
                        throw Core::SushiException(std::move(r.error()));
                    return *r;
                }

                // Reaches a region's Graph subobject. DynamicGraph is a friend of both
                // Region (for the private-base conversion) and Graph (for its specs
                // and per-step updater vectors).
                static Graph& as_graph(Region& r) noexcept { return static_cast<Graph&>(r); }

                // One node's participation in a base's boundary, with the exact byte
                // intervals it touches. Carrying the intervals — rather than just the
                // node — is what lets two regions writing disjoint slices of one
                // allocation stay parallel, which is the whole point of mapping one
                // region to one simulation island: islands are index ranges of a
                // shared column, not separate allocations.
                struct Pin
                {
                    Node* node = nullptr;
                    std::vector<Core::ResourceRegion> reads;
                    std::vector<Core::ResourceRegion> writes;
                };

                // A boundary pin reduced to one hazard's intervals, threaded through
                // rewire_base as it walks the regions sharing a base.
                struct BoundaryPin
                {
                    Node* node = nullptr;
                    std::vector<Core::ResourceRegion> regions;
                };

                // The boundary access summary of one region for one shared allocation.
                // Computed once when the region is built and consulted (read-only) by
                // the cross-region wiring of every base the region shares. Entry pins
                // receive incoming cross-region edges; exit pins are the sources for
                // the next region's edges. Each pin carries both its read and its
                // write intervals, so the wiring can apply the right hazard rule
                // (RAW/WAW/WAR) to the right bytes — and two regions that only read
                // the same buffer still never gain an edge.
                struct BaseAccess
                {
                    std::vector<Pin> entry;  // no intra predecessor overlapping this base
                    std::vector<Pin> exit;   // accesses not shadowed by an intra successor's write
                };

                // The cached compiled sub-graph of one region: its nodes (intra edges
                // baked), its intra-roots, and its per-shared-base boundary summary.
                struct RegionState
                {
                    std::vector<sushi_ptr<Node>> nodes;
                    std::vector<Node*>           intra_roots;
                    std::map<Base, BaseAccess>   access;
                };

                // One recorded cross-region edge, kept per base so a base can be
                // un-wired in isolation when it becomes dirty.
                struct BoundaryEdge { Node* from; Node* to; };

                // Walks a node's intra successors (inline buffer then lock-free
                // spillover) and calls fn(succ) for each. Used only at commit, single
                // threaded, so a relaxed load of the spillover head is fine.
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

                // Links a leaf node to the completion sentinel: appends the sentinel
                // to its boundary successors and counts it on the sentinel's baseline
                // (kept in region.intra_in_degree, the sentinel having no other predecessor).
                void link_sentinel(Node* n)
                {
                    if (n->region.sentinel_linked) return;
                    n->region.boundary_successors.push_back(sentinel_.get());
                    sentinel_->region.intra_in_degree += 1;
                    n->region.sentinel_linked = true;
                }

                // Removes a node's sentinel link (it gained a real successor or is
                // being torn down).
                void unlink_sentinel(Node* n)
                {
                    if (!n->region.sentinel_linked) return;
                    auto& v = n->region.boundary_successors;
                    for (auto it = v.begin(); it != v.end(); ++it)
                        if (*it == sentinel_.get()) { v.erase(it); break; }
                    sentinel_->region.intra_in_degree -= 1;
                    n->region.sentinel_linked = false;
                }

                // Adds one cross-region edge from->to for base b. A node that gains an
                // exported successor stops being a leaf, so it drops its sentinel link;
                // a node that gains a cross-region predecessor stops being a root.
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

                // Removes one previously recorded cross-region edge from->to. Reverses
                // both the root and the sentinel bookkeeping of add_cross_edge.
                void remove_cross_edge(Node* from, Node* to)
                {
                    auto& v = from->region.boundary_successors;
                    for (auto it = v.begin(); it != v.end(); ++it)
                        if (*it == to) { v.erase(it); break; }

                    const int32_t prev = to->region.boundary_in_degree.fetch_sub(1, std::memory_order_relaxed);
                    if (prev == 1 && to->region.intra_in_degree == 0)
                        roots_.insert(to);

                    // `from` may have just lost its last exported successor; if it is an
                    // intra-leaf with no remaining cross-region successor it becomes a
                    // true leaf again and must release the sentinel.
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

                // True when one interval in @p covers fully contains @p r, so every
                // byte of @p r is rewritten and any access to it is transitively
                // ordered behind that writer.
                static bool covered_by_any(const Core::ResourceRegion& r,
                                           const std::vector<Core::ResourceRegion>& covers)
                {
                    for (const auto& c : covers)
                        if (c.contains(r))
                            return true;
                    return false;
                }

                // Drops from @p pins every interval a later write fully covers, and
                // the pin itself once nothing of it is left live. Retiring only fully
                // covered intervals is deliberately conservative: a partially
                // overwritten pin stays live, which can only add a cross edge, never
                // drop one. The whole boundary layer errs that way — an extra edge
                // costs parallelism, a missing one is a data race.
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

                // (Re)derives the cross-region edges for one shared allocation. Self
                // contained: it first un-wires the base's current edges, then walks
                // the regions touching it in ascending key order, threading the live
                // writers and the pending readers through so RAW/WAR/WAW are honoured
                // and read/read stays parallel. Every hazard test compares byte
                // intervals, so two regions touching disjoint slices of one allocation
                // gain no edge at all. A base whose endpoints include a torn-down node
                // is un-wired earlier (while those nodes are still alive); the undo
                // here is then a no-op on an empty list.
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

                    // Writes no later region has fully overwritten yet, and reads no
                    // later region has overwritten yet. Both accumulate rather than
                    // being replaced wholesale, because a region that writes one slice
                    // does not shadow an earlier writer of a different slice.
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
                            // RAW and WAW share a source (an earlier write) and differ
                            // only in what the consumer does with those bytes, so one
                            // edge per (producer, consumer) pair covers both.
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

                // Builds (or rebuilds) the cached sub-graph of one region from its
                // recorded specs: bakes intra edges, captures the intra in-degree and
                // leaf flag of every node, gathers intra-roots, and computes the
                // per-shared-base boundary summary. O(region).
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

                // Fills a region's per-base boundary summary. For each allocation the
                // region touches it classifies the region's accessor nodes into entry
                // pins (no intra predecessor touching an OVERLAPPING range of the
                // base) and exit pins (whatever an intra successor's write does not
                // fully cover), keeping each pin's exact byte intervals.
                //
                // Both classifications test overlap rather than base equality, and
                // both err toward keeping a pin: a node wrongly excluded from entry
                // would lose an incoming edge, and one wrongly treated as shadowed
                // would lose an outgoing edge — either is a data race, while an extra
                // pin only costs an edge.
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

                        // Nodes with an intra predecessor overlapping them on b, and
                        // per node the intra successors' writes that may shadow it.
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

                // Removes a region's contribution to the shared structures: drops its
                // nodes from the root set and the sentinel, and unregisters it from the
                // bases it touched (marking each dirty). Its cross-region edges must
                // already have been undone. Frees the region's node objects.
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

                // True when a structural change or a region gaining tasks means the
                // plan must be recomposed.
                bool needs_commit() const
                {
                    return !committed_once_ || !dirty_regions_.empty() || !dropped_.empty();
                }

                // Re-stitches the plan touching only the changed regions and the
                // boundaries of the allocations they share. See the class brief for
                // the cost argument.
                void commit()
                {
                    // The set of regions whose old state must be torn down, and the
                    // set of bases that therefore need re-wiring.
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

                    // Undo every cross-region edge on a dirty base while all endpoints
                    // are still alive, so freeing torn-down nodes leaves no dangling
                    // pointer in any other node's boundary list.
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

                    // Rebuild the regions that are still present; register them on the
                    // bases they touch (marking those dirty) and seed roots/sentinel.
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

                    // Publish the plan the engine runs: roots to inject and the shared
                    // sentinel to wait on. all_nodes is only read by the profiler, so
                    // it is gathered solely when profiling is on.
                    plan_.roots.assign(roots_.begin(), roots_.end());
                    plan_.sentinel = sentinel_;
                    plan_.all_nodes.clear();
                    if (ctx_->profiling_enabled())
                        for (auto& kv : states_)
                            for (auto& np : kv.second.nodes)
                                plan_.all_nodes.push_back(np);

                    committed_once_ = true;
                    ++compiles_;
                }

                // Re-arms every node's live in-degree from the two-layer baseline and
                // refreshes the per-step State / late-binding updaters. O(total) per
                // step, which is inherent to replaying the whole plan.
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
                Core::Result<Core::RunReport, Core::Error> drive(KeepGoing keep_going) noexcept
                {
                    Core::RunReport report;
                    auto driven = drive(keep_going, report);
                    if (!driven)
                        return Core::make_error(std::move(driven.error()));
                    return std::move(report);
                }

                /**
                 * @brief The same driver, writing into a caller-owned report.
                 *
                 * Every run entry funnels through here, so the reuse overloads cannot
                 * drift from the allocating ones. See Graph::drive().
                 *
                 * @param keep_going Called with the count of completed steps before
                 *                   each step; the loop continues while it is true.
                 * @param report     The report to fill; reset before the run starts.
                 * @return A pointer to @p report on success, or a mapped Error.
                 */
                template <class KeepGoing>
                Core::Result<Core::RunReport*, Core::Error>
                drive(KeepGoing keep_going, Core::RunReport& report) noexcept
                {
                    // Before any early return; see Graph::drive().
                    report.reset();
                    report.is_successful = false;

                    if (regions_.empty())
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                                                            "DynamicGraph has no regions to run."});
                    // Before anything touches the plan: rearm() rewrites every
                    // node's in-degree, which would corrupt a step the workers are
                    // still executing. See Graph::drive().
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
                        drive_impl(keep_going, report);
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
                void drive_impl(KeepGoing keep_going, Core::RunReport& report)
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

                    // Aggregate every live region's per-step updaters (State advance,
                    // late-binding refresh, distributed live-IO) in region-key order.
                    refresh_updaters();
                    steps_.resync();

                    auto& engine = ctx_->get_execution_engine();
                    engine.reset_cancellation();

                    const bool profiling = ctx_->profiling_enabled();
                    if (profiling)
                        engine.reset_profiling(plan_);

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

                // Rebuilds the aggregated per-step updater lists from every live
                // region. Cheap (proportional to the number of stateful tasks, which
                // are iterated every step anyway) and kept here so a region added or
                // dropped is reflected before the next run.
                void refresh_updaters()
                {
                    steps_.reset_updaters();
                    for (auto& kv : regions_)
                        steps_.aggregate_from(as_graph(*kv.second).steps_);
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;

                // Live regions (the recorders), ordered by key for deterministic
                // composition. unique_ptr keeps Region addresses stable and lets the
                // composer use Region's private constructor.
                std::map<RegionKey, std::unique_ptr<Region>> regions_;

                // Cached compiled sub-graph per region, the lifetime owner of the
                // nodes the plan runs.
                std::map<RegionKey, RegionState> states_;

                // Which regions touch each allocation, and the cross-region edges
                // recorded per allocation so a single base can be un-wired in isolation.
                std::map<Base, std::set<RegionKey>>      base_regions_;
                std::map<Base, std::vector<BoundaryEdge>> base_edges_;

                // Current DAG entry points (region.intra_in_degree == 0 and
                // region.boundary_in_degree == 0), maintained incrementally as
                // boundary edges and regions change.
                std::unordered_set<Node*> roots_;

                // The single completion sentinel, persistent across commits; its
                // baseline in-degree (kept in region.intra_in_degree) is the current leaf count.
                sushi_ptr<Node> sentinel_;

                // Pending structural changes applied at the next commit.
                std::set<RegionKey> dirty_regions_;
                std::set<RegionKey> dropped_;

                // Per-step lifecycle aggregated from every live region before each
                // run; the same machinery the static Graph uses, so both drivers run
                // the identical per-step hook order.
                detail::StepMachinery steps_;

                Execution::CompiledGraph plan_;

                // True between run_async() and the settling of its handle; see
                // Graph::async_step_pending_.
                bool async_step_pending_ = false;
                std::size_t compiles_ = 0;
                std::size_t plan_index_counter_ = 0;
                bool committed_once_ = false;
        };

        // Defined here, where DynamicGraph is complete; declared on Runtime in
        // runtime.hpp next to graph().
        inline DynamicGraph Runtime::dynamic_graph()
        {
            return DynamicGraph(ctx_);
        }

    } // namespace API

    using API::DynamicGraph;

} // namespace SushiRuntime
