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
 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/graph/recorder.hpp>
#include <SushiRuntime/api/graph/region.hpp>
#include <SushiRuntime/api/graph/region_composer.hpp>
#include <SushiRuntime/api/graph/run_handle.hpp>
#include <SushiRuntime/api/graph/step_driver.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/graph/compiled_graph.hpp>

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
         * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
         */
        class DynamicGraph : public StepDriver<DynamicGraph>, private RegionComposer
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
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
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
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
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
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
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
                        total += kv.second->size();
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
                std::size_t compile_count() const noexcept { return compose_count(); }

            private:
                friend class Runtime;
                // The driver reaches the plan, the hooks and the checks below, and nothing else.
                friend class StepDriver<DynamicGraph>;
                friend struct detail::DynamicGraphNodeEventsTestAccess;

                /** @brief Error text of a run started with no region. */
                static constexpr const char* K_NOTHING_TO_RUN =
                    "DynamicGraph has no regions to run.";

                /** @brief Error text of a run whose recorded handle was freed. */
                static constexpr const char* K_DANGLING_RESOURCE =
                    "DynamicGraph region references a Buffer/State freed before the run.";

                explicit DynamicGraph(std::shared_ptr<Execution::RuntimeContext> ctx)
                    : ctx_(std::move(ctx)), profiling_requested_(ctx_->settings().profiling)
                {
                    plan_.profiling = profiling_requested_;
                }

                /** @brief Returns the context whose engine runs this graph. */
                Execution::RuntimeContext& run_context() noexcept { return *ctx_; }

                /** @brief Returns whether any region exists. */
                bool has_work() const noexcept { return !regions_.empty(); }

                /** @brief Returns whether any region's recorded handle has been freed. */
                bool has_dangling_resource() const noexcept
                {
                    for (const auto& kv : regions_)
                        if (kv.second->recorder().has_dangling_resource())
                            return true;
                    return false;
                }

                /** @brief Returns whether the plan must be composed before the next step. */
                bool plan_is_stale() const { return needs_commit(); }

                /** @brief Composes the plan from the changed regions and clears the pending set. */
                void rebuild_plan()
                {
                    commit(dirty_regions_, dropped_,
                           [this](RegionKey key) -> const Recorder*
                           {
                               const auto it = regions_.find(key);
                               return it == regions_.end() ? nullptr : &it->second->recorder();
                           },
                           ctx_->offload(), profiling_requested_);
                    dirty_regions_.clear();
                    dropped_.clear();
                }

                /**
                 * @brief Gathers every live region's per-step hooks, in region-key order.
                 * @throws Core::SushiException With invalid_graph when the plan has no root.
                 */
                void prepare_run()
                {
                    if (plan_.roots.empty())
                        throw Core::SushiException(Core::Error{Core::Errc::invalid_graph,
                                                      "DynamicGraph has no runnable tasks."});
                    refresh_updaters();
                }

                /** @brief Returns the plan a step runs. */
                ::SushiRuntime::Graph::CompiledGraph& run_plan() noexcept { return plan_; }

                /** @brief Returns the per-step hooks aggregated from the live regions. */
                const detail::StepMachinery& run_steps() const noexcept { return steps_; }

                /** @brief Re-arms every node's live in-degree before a step. */
                void rearm_plan() { rearm(); }

                /** @brief Returns false: a composed plan always runs on the scheduler. */
                bool runs_chained() const noexcept { return false; }

                /** @brief Returns whether a structural change or new tasks require a recompose. */
                bool needs_commit() const
                {
                    return !composed() || !dirty_regions_.empty() || !dropped_.empty();
                }

                /**
                 * @brief Rebuilds the aggregated per-step updater lists from every live region.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                void refresh_updaters()
                {
                    steps_.reset_updaters();
                    for (auto& kv : regions_)
                        steps_.aggregate_from(kv.second->recorder().steps());
                }

                std::shared_ptr<Execution::RuntimeContext> ctx_;

                /**
                 * @brief Holds the live regions by key; unique_ptr keeps each Region's address
                 *        stable.
                 * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
                 */
                std::map<RegionKey, std::unique_ptr<Region>> regions_;

                // Pending structural changes applied at the next commit.
                std::set<RegionKey> dirty_regions_;
                std::set<RegionKey> dropped_;

                /** @brief Aggregates every live region's per-step lifecycle before each run. */
                detail::StepMachinery steps_;

                /** @brief Defaults from RuntimeSettings::profiling; mirrors plan_.profiling. */
                bool profiling_requested_ = false;
        };

    } // namespace API

    using API::DynamicGraph;

} // namespace SushiRuntime

// A DynamicGraph comes only from Runtime::dynamic_graph(), so the Runtime comes with it.
#include <SushiRuntime/api/runtime/runtime.hpp>
