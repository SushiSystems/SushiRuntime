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

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <SushiRuntime/api/region.hpp>
#include <SushiRuntime/api/runtime.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/graph/node.hpp>

namespace SushiRuntime
{
    namespace API
    {
        /**
         * @brief A graph partitioned into regions that can change between steps.
         *
         * The mutable counterpart to the static Graph. Work is grouped into regions
         * keyed by a caller-chosen id (a spatial cell, an island, a streaming
         * chunk); region(key) hands back a Region to record tasks on, and drop(key)
         * removes one. Mutations are applied at a step boundary, never against the
         * running DAG, so the lock-free scheduler invariants are untouched.
         *
         * The composer is what makes streaming cheap. Each region's nodes are built
         * once and cached; a region that did not change is never rebuilt. At commit
         * the cached nodes from every live region are adopted into one backing graph
         * and compiled in a single dependency pass — the tracker shards on the
         * owning allocation, so intra- and cross-region edges fall out together with
         * no double counting. The combined plan is then replayed across steps like
         * the static graph's, with every region's per-step State/late-binding
         * updaters driven in the same order.
         *
         * Cross-region ordering follows ascending region key. Where the static
         * graph orders two tasks that touch the same buffer by their add() order,
         * the dynamic graph adopts region nodes in key order, so the lower-keyed
         * region is the producer: when a hard dataflow dependency crosses regions,
         * key the producer below the consumer. Tasks within one region keep their
         * add() order. Independent regions (the common open-world case) need no
         * such care.
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
                 * A fresh region starts empty; record work on it with the same
                 * add() surface as a static graph. Creating a region marks the graph
                 * for recomposition at the next step boundary.
                 *
                 * @param key Identity of the region; stable across steps.
                 * @return A reference to the region, valid until the region is dropped.
                 */
                Region& region(RegionKey key)
                {
                    auto it = regions_.find(key);
                    if (it == regions_.end())
                    {
                        it = regions_.emplace(key,
                            std::unique_ptr<Region>(new Region(ctx_))).first;
                        structural_dirty_ = true;
                    }
                    return *it->second;
                }

                /**
                 * @brief Returns whether a region with @p key currently exists.
                 * @param key Region identity to test.
                 * @return True if the region is present and not pending a drop.
                 */
                bool has_region(RegionKey key) const noexcept
                {
                    return regions_.find(key) != regions_.end();
                }

                /**
                 * @brief Removes the region for @p key from the live set.
                 *
                 * Takes effect immediately on the graph's structure (region() and
                 * has_region() no longer see it); the combined plan it leaves is
                 * recomposed at the next run, so a region is never removed from a DAG
                 * that is mid-step. Dropping a region that does not exist is a no-op.
                 *
                 * @param key Identity of the region to remove.
                 */
                void drop(RegionKey key)
                {
                    if (regions_.erase(key) != 0)
                    {
                        region_nodes_.erase(key);
                        structural_dirty_ = true;
                    }
                }

                /** @brief Number of live regions (excludes pending drops). */
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
                 * @brief Number of times the combined plan has been (re)composed.
                 *
                 * The amortisation guarantee: a multi-step run that does not mutate
                 * any region holds this at 1, and mutating one region of many
                 * recomposes once without rebuilding the unchanged regions' nodes.
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
                 *
                 * Pending mutations are applied and the plan recomposed (only if
                 * something changed) before the first step; the combined plan is then
                 * replayed for each step, advancing every region's States.
                 *
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

                explicit DynamicGraph(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx))
                {
                }

                /** @brief Throws the carried error, or returns the report. */
                static Core::RunReport unwrap(Core::Result<Core::RunReport, Core::Error> r)
                {
                    if (!r)
                        throw Core::SushiException(std::move(r.error()));
                    return *r;
                }

                // Reaches a region's Graph subobject. DynamicGraph is a friend of
                // both Region (for the private-base conversion) and Graph (for its
                // recorded specs and updater vectors).
                static Graph& as_graph(Region& r) noexcept { return static_cast<Graph&>(r); }

                // True when a structural change (region added/dropped) or any region
                // gaining tasks means the combined plan must be rebuilt.
                bool needs_commit() const
                {
                    if (structural_dirty_ || !committed_once_)
                        return true;
                    for (const auto& kv : regions_)
                        if (static_cast<const Graph&>(*kv.second).dirty_)
                            return true;
                    return false;
                }

                // Rebuilds only the regions whose tasks changed, then composes every
                // live region's cached nodes into one plan in a single wiring pass.
                void commit()
                {
                    // Rebuild node objects only for regions that changed (or are new).
                    // Unchanged regions keep their cached, already-built kernels.
                    for (auto& kv : regions_)
                    {
                        Graph& g = as_graph(*kv.second);
                        const bool missing = region_nodes_.find(kv.first) == region_nodes_.end();
                        if (g.dirty_ || missing)
                        {
                            BackingGraph bg(*ctx_);
                            for (auto& spec : g.specs_)
                                spec(bg);
                            region_nodes_[kv.first] = bg.take_nodes();
                            g.dirty_ = false;
                        }
                    }

                    // Aggregate every live region's per-step updaters in region-key
                    // order so each State advances exactly once and late bindings
                    // refresh, just as in the static graph's single-region case.
                    view_resyncs_.clear();
                    step_advancers_.clear();
                    binding_updaters_.clear();
                    dist_io_updaters_.clear();
                    task_count_ = 0;

                    for (auto& kv : regions_)
                    {
                        Graph& g = as_graph(*kv.second);
                        append(view_resyncs_, g.view_resyncs_);
                        append(step_advancers_, g.step_advancers_);
                        append(binding_updaters_, g.binding_updaters_);
                        append(dist_io_updaters_, g.dist_io_updaters_);
                        task_count_ += g.task_count_;
                    }

                    // Compose: adopt the cached nodes (shared ownership) into one
                    // backing graph and compile in a single dependency pass.
                    BackingGraph world(*ctx_);
                    for (auto& kv : region_nodes_)
                        for (auto& node : kv.second)
                            world.adopt(node);

                    plan_ = world.compile();
                    plan_.capture_base_state();

                    structural_dirty_ = false;
                    committed_once_   = true;
                    ++compiles_;
                }

                static void append(std::vector<std::function<void()>>& dst,
                                   const std::vector<std::function<void()>>& src)
                {
                    dst.insert(dst.end(), src.begin(), src.end());
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

                    if (task_count_ == 0)
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                                                              "DynamicGraph has no tasks to run."});

                    for (auto& resync : view_resyncs_)
                        resync();

                    auto& engine = ctx_->get_execution_engine();
                    engine.reset_cancellation();

                    const bool profiling = ctx_->profiling_enabled();
                    if (profiling)
                        engine.reset_profiling(plan_);

                    const auto before = engine.metrics();
                    const std::size_t d2h_before = ctx_->device_to_host_bytes();
                    const std::size_t h2d_before = ctx_->host_to_device_bytes();
                    const auto t0 = clock::now();

                    std::size_t executed = 0;
                    while (keep_going(executed) && !engine.is_cancelled())
                    {
                        for (auto& update_io : dist_io_updaters_)
                            update_io();
                        for (auto& update_binding : binding_updaters_)
                            update_binding();

                        plan_.rearm();
                        engine.execute(plan_);

                        for (auto& advance : step_advancers_)
                            advance();
                        ++executed;
                    }

                    const auto t1 = clock::now();
                    const auto after = engine.metrics();

                    const std::size_t total_tasks = task_count_ * executed;
                    auto report = detail::build_run_report(*ctx_, before, after, total_tasks,
                                                           compile_ms, ms(t0, t1));
                    report.cancelled = engine.is_cancelled();
                    report.device_to_host_bytes = ctx_->device_to_host_bytes() - d2h_before;
                    report.host_to_device_bytes = ctx_->host_to_device_bytes() - h2d_before;

                    if (profiling)
                        engine.collect_profiling(plan_, report);
                    return report;
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;

                // Live regions, ordered by key so composition and updater aggregation
                // are deterministic. unique_ptr keeps Region addresses stable across
                // insertions and lets DynamicGraph use Region's private constructor.
                std::map<RegionKey, std::unique_ptr<Region>> regions_;

                // Cached built nodes per region, shared with the combined plan. A
                // region is only rebuilt into this map when its tasks change.
                std::map<RegionKey, std::vector<sushi_ptr<::SushiRuntime::Graph::Node>>> region_nodes_;

                // Per-step updaters aggregated from every live region at commit.
                std::vector<std::function<void()>> view_resyncs_;
                std::vector<std::function<void()>> step_advancers_;
                std::vector<std::function<void()>> binding_updaters_;
                std::vector<std::function<void()>> dist_io_updaters_;

                Execution::CompiledGraph plan_;
                std::size_t task_count_ = 0;
                std::size_t compiles_ = 0;
                bool structural_dirty_ = false;
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
