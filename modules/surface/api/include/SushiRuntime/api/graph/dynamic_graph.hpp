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
#include <vector>

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
    namespace detail
    {
        /** @brief Grants a test a friend seam onto DynamicGraph::plan_/states_. */
        struct DynamicGraphNodeEventsTestAccess;
    }

    /**
     * @brief Composes regions that stream in and out into one plan, rebuilt only where it
     *        changed.
     *
     * A graph moves and does not copy; the source of a move is left with no region on the
     * same runtime and can be given regions and run again.
     * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
     */
    class DynamicGraph : public StepDriver<DynamicGraph>, private RegionComposer
    {
        public:
            /** @brief Caller-chosen identity of a region (e.g. a packed cell coordinate). */
            using RegionKey = std::uint64_t;

            /**
             * @brief Takes over another graph's regions, plan and pending changes.
             * @param other Left with no region on the same runtime; compose_count() is zero.
             * @pre No step started with run_async() on other is pending.
             */
            DynamicGraph(DynamicGraph&& other) noexcept
                : StepDriver<DynamicGraph>(std::move(other)),
                  RegionComposer(std::move(other)),
                  ctx_(other.ctx_)
            {
                take(other);
            }

            /**
             * @brief Replaces this graph's regions and plan with another's.
             * @param other Left with no region on the same runtime; compose_count() is zero.
             * @pre No step started with run_async() on either graph is pending.
             */
            DynamicGraph& operator=(DynamicGraph&& other) noexcept
            {
                if (this != &other)
                {
                    StepDriver<DynamicGraph>::operator=(std::move(other));
                    RegionComposer::operator=(std::move(other));
                    ctx_ = other.ctx_;
                    take(other);
                }
                return *this;
            }

            DynamicGraph(const DynamicGraph&) = delete;
            DynamicGraph& operator=(const DynamicGraph&) = delete;

            /**
             * @brief Waits for a step started with run_async() and still pending, then frees
             *        the regions and the plan.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            ~DynamicGraph()
            {
                // The plan and the regions are alive here and gone in a base destructor.
                end_pending_step();
            }

            /**
             * @brief Returns the region for key, creating it if absent.
             * @return A reference to the region, valid until the region is dropped.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            Region& region(RegionKey key)
            {
                auto it = regions_.find(key);
                if (it == regions_.end())
                {
                    if (changes_ == nullptr)
                        changes_ = std::make_unique<RegionChangeLog>();
                    it = regions_.emplace(key,
                        std::make_unique<Region>(ctx_, *changes_, key)).first;
                    // A region that did not exist is built at the next run.
                    dirty_regions_.insert(key);
                }
                return *it->second;
            }

            /**
             * @brief Returns whether a live region with @p key exists.
             * @param key Region identity to test.
             * @return True if the region is present.
             */
            [[nodiscard]] bool has_region(RegionKey key) const noexcept
            {
                return regions_.find(key) != regions_.end();
            }

            /**
             * @brief Returns the region for @p key without marking it for a rebuild.
             * @param key Region identity to look up.
             * @return The live region, or nullptr when no region has that key.
             */
            [[nodiscard]] const Region* find_region(RegionKey key) const noexcept
            {
                const auto it = regions_.find(key);
                return it == regions_.end() ? nullptr : it->second.get();
            }

            /**
             * @brief Removes the region for key from the live set; an absent key does nothing.
             *
             * The region's recording, scratch and per-step hooks stay alive until the next
             * run composes the plan without it, so a step started with run_async() and still
             * pending finishes on them.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void drop(RegionKey key)
            {
                const auto found = regions_.find(key);
                if (found == regions_.end())
                    return;
                retired_.push_back(std::move(found->second));
                regions_.erase(found);
                dirty_regions_.erase(key);
                dropped_.insert(key);
            }

            /**
             * @brief Reports how many regions the graph currently holds.
             * @return Number of live regions, dropped ones excluded.
             */
            [[nodiscard]] std::size_t region_count() const noexcept
            {
                return regions_.size();
            }

            /** @brief Returns how many rules the live regions hold together. */
            [[nodiscard]] std::size_t size() const noexcept
            {
                std::size_t total = 0;
                for (const auto& kv : regions_)
                    total += kv.second->size();
                return total;
            }

            /** @brief Returns how many nodes the live regions record together. */
            [[nodiscard]] std::size_t node_count() const noexcept
            {
                std::size_t total = 0;
                for (const auto& kv : regions_)
                    total += kv.second->node_count();
                return total;
            }

            /** @brief Stands for "this rule has no index yet". */
            static constexpr std::uint32_t NO_REGISTRATION =
                ::SushiRuntime::Internal::Graph::TaskGraph::NO_REGISTRATION;

            /**
             * @brief Returns the registration index a rule of a region carries in the plan.
             * @param rule Position in the region's add() order, below its size().
             * @return The index every node of the rule reports under, or NO_REGISTRATION for
             *         an absent region, a rule out of range or a region not yet composed.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            [[nodiscard]] std::uint32_t rule_registration(RegionKey key,
                                                          std::size_t rule) const noexcept
            {
                return registration_of(key, rule);
            }

            /**
             * @brief Returns how many times the plan has been composed.
             *
             * It rises by one at a run that follows a recording, a clear() or a drop(), and
             * holds across runs that only read the regions.
             */
            using RegionComposer::compose_count;

            using StepDriver<DynamicGraph>::is_profiling_requested;
            using StepDriver<DynamicGraph>::set_profiling;

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

            explicit DynamicGraph(std::shared_ptr<Internal::Execution::RuntimeContext> ctx)
                : RegionComposer(ctx->settings().profiling), ctx_(std::move(ctx))
            {
            }

            /** @brief Returns the context whose engine runs this graph. */
            Internal::Execution::RuntimeContext& run_context() noexcept { return *ctx_; }

            /**
             * @brief Records the profiling request; a changed one rebuilds every region.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void request_profiling(bool requested) noexcept
            {
                if (requested == RegionComposer::is_profiling_requested())
                    return;
                RegionComposer::set_profiling(requested);
                rebuild_every_region_ = true;
            }

            /** @brief Returns whether the next plan requests per-node device timing. */
            [[nodiscard]] bool profiling_request() const noexcept
            {
                return RegionComposer::is_profiling_requested();
            }

            /** @brief Returns a region's recorder without listing the region as written to. */
            [[nodiscard]] static const Recorder& recorded(const Region& region) noexcept
            {
                return region.recorder();
            }

            /** @brief Returns whether any region exists. */
            [[nodiscard]] bool has_work() const noexcept { return !regions_.empty(); }

            /** @brief Returns whether any region's recorded handle has been freed. */
            [[nodiscard]] bool has_dangling_resource() const noexcept
            {
                for (const auto& kv : regions_)
                    if (recorded(*kv.second).has_dangling_resource())
                        return true;
                return false;
            }

            /**
             * @brief Settles the regions written to since the last run and returns whether
             *        the plan must be composed before the next step.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            bool plan_is_stale()
            {
                settle_changes();
                return needs_commit();
            }

            /**
             * @brief Composes the plan from the changed regions, clears the pending sets and
             *        frees the dropped regions.
             * @pre No step started with run_async() is pending; the run entries refuse one.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void rebuild_plan()
            {
                if (rebuild_every_region_)
                {
                    for (const auto& kv : regions_)
                        dirty_regions_.insert(kv.first);
                    rebuild_every_region_ = false;
                }
                commit(dirty_regions_, dropped_,
                       [this](RegionKey key) -> const Recorder*
                       {
                           const auto it = regions_.find(key);
                           return it == regions_.end() ? nullptr : &recorded(*it->second);
                       },
                       ctx_->offload());
                dirty_regions_.clear();
                dropped_.clear();
                free_retired_regions();
            }

            /**
             * @brief Gathers every live region's per-step hooks, in region-key order.
             * @throws SushiException With invalid_graph when the plan has no root.
             */
            void prepare_run()
            {
                if (plan_.roots.empty())
                    throw SushiException(Error{Errc::invalid_graph,
                                                  "DynamicGraph has no runnable tasks."});
                refresh_updaters();
            }

            /** @brief Returns the plan a step runs. */
            ::SushiRuntime::Internal::Graph::CompiledGraph& run_plan() noexcept { return plan_; }

            /** @brief Returns the per-step hooks aggregated from the live regions. */
            [[nodiscard]] const detail::StepMachinery& run_steps() const noexcept
            {
                return steps_;
            }

            /** @brief Re-arms every node's live in-degree before a step. */
            void rearm_plan() { rearm(); }

            /** @brief Returns false: a composed plan always runs on the scheduler. */
            [[nodiscard]] bool runs_chained() const noexcept { return false; }

            /** @brief Returns whether a structural change or new tasks require a recompose. */
            [[nodiscard]] bool needs_commit() const
            {
                return !composed() || rebuild_every_region_ || !dirty_regions_.empty() ||
                       !dropped_.empty();
            }

            /** @brief Marks dirty each region written to whose recording really changed. */
            void settle_changes()
            {
                if (changes_ == nullptr)
                    return;
                for (RegionKey key : changes_->keys())
                {
                    const auto found = regions_.find(key);
                    if (found != regions_.end() &&
                        !is_current(key, recorded(*found->second)))
                        dirty_regions_.insert(key);
                }
                changes_->settle();
            }

            /**
             * @brief Moves other's regions and pending changes here and leaves other with none.
             * @param other Graph to empty; its context and profiling request are kept.
             */
            void take(DynamicGraph& other) noexcept
            {
                // The hooks list is replaced before the regions it names are freed.
                steps_ = std::exchange(other.steps_, detail::StepMachinery());
                retired_ = std::exchange(other.retired_, {});
                regions_ = std::exchange(other.regions_, {});
                changes_ = std::exchange(other.changes_, {});
                dirty_regions_ = std::exchange(other.dirty_regions_, {});
                dropped_ = std::exchange(other.dropped_, {});
                rebuild_every_region_ = std::exchange(other.rebuild_every_region_, false);
            }

            /**
             * @brief Rebuilds the aggregated per-step updater lists from every live region.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void refresh_updaters()
            {
                steps_.reset_updaters();
                for (auto& kv : regions_)
                    steps_.aggregate_from(recorded(*kv.second).steps());
            }

            /**
             * @brief Frees the regions dropped since the last commit.
             * @pre No step is pending and the plan holds no node of a dropped region.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            void free_retired_regions() noexcept
            {
                // steps_ names a retired region's hooks until the next refresh_updaters().
                steps_.reset_updaters();
                retired_.clear();
            }

            std::shared_ptr<Internal::Execution::RuntimeContext> ctx_;

            /**
             * @brief Holds the live regions by key; unique_ptr keeps each Region's address
             *        stable.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            std::map<RegionKey, std::unique_ptr<Region>> regions_;

            /**
             * @brief Holds the regions dropped since the last commit, in drop order.
             * @see modules/surface/api/include/SushiRuntime/api/graph/README.md
             */
            std::vector<std::unique_ptr<Region>> retired_;

            /** @brief Lists the regions written to since the last run; regions point at it. */
            std::unique_ptr<RegionChangeLog> changes_;

            /** @brief Regions to build or rebuild at the next commit. */
            std::set<RegionKey> dirty_regions_;

            /** @brief Regions removed since the last commit. */
            std::set<RegionKey> dropped_;

            /** @brief Aggregates every live region's per-step lifecycle before each run. */
            detail::StepMachinery steps_;

            /** @brief Set by a change of the profiling request, cleared by the next build. */
            bool rebuild_every_region_ = false;
    };



} // namespace SushiRuntime

// A DynamicGraph comes only from Runtime::dynamic_graph(), so the Runtime comes with it.
#include <SushiRuntime/api/runtime/runtime.hpp>
