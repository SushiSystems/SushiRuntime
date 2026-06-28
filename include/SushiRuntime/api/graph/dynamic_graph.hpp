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
         * (Node::boundary_in_degree / boundary_successors) that the composer
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
         * Obtain one from Runtime::dynamic_graph(). Moveable, not copyable.
         */
        class DynamicGraph
        {
            public:
                /** @brief Caller-chosen identity of a region (e.g. a packed cell coordinate). */
                using RegionKey = std::uint64_t;

                DynamicGraph(DynamicGraph&&) = default;
                DynamicGraph& operator=(DynamicGraph&&) = default;
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

                /** @brief Number of live regions. */
                std::size_t region_count() const noexcept { return regions_.size(); }

                /** @brief Total tasks currently recorded across all live regions. */
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
                 * @brief Requests cancellation of an in-progress run, from any thread.
                 *
                 * Mirrors Graph::cancel(): the active step drains without launching
                 * new kernels and the driver stops before the next step.
                 */
                void cancel() { ctx_->get_execution_engine().cancel(); }

            private:
                friend class Runtime;

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

                // The boundary access summary of one region for one shared allocation.
                // Computed once when the region is built and consulted (read-only) by
                // the cross-region wiring of every base the region shares. Entry pins
                // receive incoming cross-region edges; exit pins are the sources for
                // the next region's edges. Splitting read from write keeps two regions
                // that only read the same buffer parallel.
                struct BaseAccess
                {
                    std::vector<Node*> entry_read;   // first touchers that only read (RAW targets)
                    std::vector<Node*> entry_write;  // first touchers that write (WAW/WAR targets)
                    std::vector<Node*> exit_write;    // last writers (RAW/WAW sources)
                    std::vector<Node*> exit_read;     // last reads not shadowed by a later write (WAR sources)
                    bool writes = false;
                    bool reads  = false;
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
                // (kept in intra_in_degree, the sentinel having no other predecessor).
                void link_sentinel(Node* n)
                {
                    if (n->sentinel_linked) return;
                    n->boundary_successors.push_back(sentinel_.get());
                    sentinel_->intra_in_degree += 1;
                    n->sentinel_linked = true;
                }

                // Removes a node's sentinel link (it gained a real successor or is
                // being torn down).
                void unlink_sentinel(Node* n)
                {
                    if (!n->sentinel_linked) return;
                    auto& v = n->boundary_successors;
                    for (auto it = v.begin(); it != v.end(); ++it)
                        if (*it == sentinel_.get()) { v.erase(it); break; }
                    sentinel_->intra_in_degree -= 1;
                    n->sentinel_linked = false;
                }

                // Adds one cross-region edge from->to for base b. A node that gains an
                // exported successor stops being a leaf, so it drops its sentinel link;
                // a node that gains a cross-region predecessor stops being a root.
                void add_cross_edge(Node* from, Node* to, Base b)
                {
                    if (from->sentinel_linked)
                        unlink_sentinel(from);
                    from->boundary_successors.push_back(to);

                    const int32_t prev = to->boundary_in_degree.fetch_add(1, std::memory_order_relaxed);
                    if (prev == 0 && to->intra_in_degree == 0)
                        roots_.erase(to);

                    base_edges_[b].push_back({from, to});
                }

                // Removes one previously recorded cross-region edge from->to. Reverses
                // both the root and the sentinel bookkeeping of add_cross_edge.
                void remove_cross_edge(Node* from, Node* to)
                {
                    auto& v = from->boundary_successors;
                    for (auto it = v.begin(); it != v.end(); ++it)
                        if (*it == to) { v.erase(it); break; }

                    const int32_t prev = to->boundary_in_degree.fetch_sub(1, std::memory_order_relaxed);
                    if (prev == 1 && to->intra_in_degree == 0)
                        roots_.insert(to);

                    // `from` may have just lost its last exported successor; if it is an
                    // intra-leaf with no remaining cross-region successor it becomes a
                    // true leaf again and must release the sentinel.
                    if (from->intra_leaf && !from->sentinel_linked && from->boundary_successors.empty())
                        link_sentinel(from);
                }

                // (Re)derives the cross-region edges for one shared allocation. Self
                // contained: it first un-wires the base's current edges, then walks
                // the regions touching it in ascending key order, threading the
                // producer (last writer) and the pending readers through so RAW/WAR/WAW
                // are honoured and read/read stays parallel. A base whose endpoints
                // include a torn-down node is un-wired earlier (while those nodes are
                // still alive); the undo here is then a no-op on an empty list.
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

                    std::vector<Node*> last_writer;   // exit writers of the most recent writing region
                    std::vector<Node*> pending_read;  // exit readers accumulated since that writer

                    for (RegionKey k : rit->second)   // std::set: ascending key = producer first
                    {
                        auto sit = states_.find(k);
                        if (sit == states_.end()) continue;
                        auto ait = sit->second.access.find(b);
                        if (ait == sit->second.access.end()) continue;
                        const BaseAccess& a = ait->second;

                        if (a.reads)
                            for (Node* w : last_writer)
                                for (Node* r : a.entry_read)
                                    add_cross_edge(w, r, b);   // RAW

                        if (a.writes)
                        {
                            for (Node* w : last_writer)
                                for (Node* d : a.entry_write)
                                    add_cross_edge(w, d, b);   // WAW
                            for (Node* rr : pending_read)
                                for (Node* d : a.entry_write)
                                    add_cross_edge(rr, d, b);  // WAR
                        }

                        if (a.writes)
                        {
                            last_writer = a.exit_write;       // new producer
                            pending_read = a.exit_read;       // its own unshadowed reads
                        }
                        else if (a.reads)
                        {
                            pending_read.insert(pending_read.end(),
                                                a.exit_read.begin(), a.exit_read.end());
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
                        spec(bg);

                    RegionState st;
                    st.nodes = bg.take_nodes();   // intra edges baked; insertion order

                    for (auto& np : st.nodes)
                    {
                        Node* n = np.get();
                        n->intra_in_degree = n->in_degree.load(std::memory_order_relaxed);
                        n->boundary_in_degree.store(0, std::memory_order_relaxed);
                        n->boundary_successors.clear();
                        n->sentinel_linked = false;
                        n->intra_leaf =
                            n->inline_successor_count.load(std::memory_order_relaxed) == 0 &&
                            n->successor_list.load(std::memory_order_relaxed) == nullptr;
                        n->plan_index = plan_index_counter_++;
                        if (n->intra_in_degree == 0)
                            st.intra_roots.push_back(n);
                    }

                    compute_access(st);
                    states_[k] = std::move(st);
                }

                // Fills a region's per-base boundary summary. For each allocation the
                // region touches it classifies the region's accessor nodes into entry
                // pins (no intra predecessor also touching the base) and exit pins (no
                // intra successor that writes the base), split by read vs write.
                void compute_access(RegionState& st)
                {
                    struct Acc { bool reads = false; bool writes = false; };
                    std::map<Base, std::unordered_map<Node*, Acc>> per_base;

                    for (auto& np : st.nodes)
                    {
                        Node* n = np.get();
                        for (const auto& r : n->io_reads_)
                            if (r.base) per_base[r.base][n].reads = true;
                        for (const auto& w : n->io_writes_)
                            if (w.base) per_base[w.base][n].writes = true;
                    }

                    for (auto& bm : per_base)
                    {
                        const Base b = bm.first;
                        auto& m = bm.second;   // node -> Acc for this base

                        std::unordered_set<Node*> has_base_pred;       // has an intra pred touching b
                        std::unordered_set<Node*> has_writing_succ;    // has an intra succ that writes b

                        for (auto& nm : m)
                        {
                            Node* n = nm.first;
                            for_each_intra_successor(n, [&](Node* s)
                            {
                                auto f = m.find(s);
                                if (f == m.end()) return;   // successor does not touch b
                                has_base_pred.insert(s);
                                if (f->second.writes) has_writing_succ.insert(n);
                            });
                        }

                        BaseAccess ba;
                        for (auto& nm : m)
                        {
                            Node* n = nm.first;
                            const Acc acc = nm.second;
                            ba.reads  |= acc.reads;
                            ba.writes |= acc.writes;

                            const bool entry = has_base_pred.find(n) == has_base_pred.end();
                            if (entry)
                            {
                                if (acc.writes)     ba.entry_write.push_back(n);
                                else if (acc.reads) ba.entry_read.push_back(n);
                            }
                            const bool shadowed = has_writing_succ.find(n) != has_writing_succ.end();
                            if (acc.writes && !shadowed) ba.exit_write.push_back(n);
                            if (acc.reads  && !shadowed) ba.exit_read.push_back(n);
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
                            if (np->intra_leaf) link_sentinel(np.get());
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
                            np->offload_attempts_.store(0, std::memory_order_relaxed);
#endif
                }

                template <class KeepGoing>
                Core::Result<Core::RunReport, Core::Error> drive(KeepGoing keep_going) noexcept
                {
                    if (regions_.empty())
                        return Core::make_error(Core::Error{Core::Errc::invalid_graph,
                                                            "DynamicGraph has no regions to run."});
                    try
                    {
                        return drive_impl(keep_going);
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
                Core::RunReport drive_impl(KeepGoing keep_going)
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

                    auto report = detail::build_run_report(*ctx_, before, after,
                                                           task_count * executed,
                                                           compile_ms, ms(t0, t1));
                    report.cancelled = engine.is_cancelled();
                    report.device_to_host_bytes = ctx_->device_to_host_bytes() - d2h_before;
                    report.host_to_device_bytes = ctx_->host_to_device_bytes() - h2d_before;

                    if (profiling)
                        engine.collect_profiling(plan_, report);
                    return report;
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

                // Current DAG entry points (intra_in_degree == 0 and boundary_in_degree
                // == 0), maintained incrementally as boundary edges and regions change.
                std::unordered_set<Node*> roots_;

                // The single completion sentinel, persistent across commits; its
                // baseline in-degree (kept in intra_in_degree) is the current leaf count.
                sushi_ptr<Node> sentinel_;

                // Pending structural changes applied at the next commit.
                std::set<RegionKey> dirty_regions_;
                std::set<RegionKey> dropped_;

                // Per-step lifecycle aggregated from every live region before each
                // run; the same machinery the static Graph uses, so both drivers run
                // the identical per-step hook order.
                detail::StepMachinery steps_;

                Execution::CompiledGraph plan_;
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
