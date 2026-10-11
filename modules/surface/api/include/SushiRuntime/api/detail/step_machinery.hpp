/**************************************************************************/
/* step_machinery.hpp                                                     */
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
 * @file step_machinery.hpp
 * @brief Declares StepMachinery, the per-step hooks a replayed plan runs around each step.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/core/delegate.hpp>
#include <SushiRuntime/task/node.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        /**
         * @brief Owns the per-step hook lists a replayed plan runs around each step, in one
         *        fixed order.
         * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
         */
        class StepMachinery
        {
            public:
                /**
                 * @brief Stores a per-step hook without heap allocation, sized from the largest
                 *        hook.
                 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
                 */
                using StepHook = Delegate<void(),
                    sizeof(Dynamic) +
                    sizeof(std::shared_ptr<::SushiRuntime::Internal::Graph::LateBindings>)>;

                /**
                 * @brief Returns the view a State owns and registers its resync on first use.
                 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
                 */
                template <typename T>
                std::shared_ptr<StateView<T>> view_for(const State<T>& state)
                {
                    std::shared_ptr<StateView<T>> view = HandleAccess::view(state);
                    if (resynced_views_.insert(view.get()).second)
                        view_resyncs_.push_back([view]() { HandleAccess::resync(*view); });
                    return view;
                }

                /**
                 * @brief Registers the once-per-step advance of a written State, at most once
                 *        per view.
                 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
                 */
                template <typename T>
                void register_advance(std::shared_ptr<StateView<T>> view)
                {
                    if (!advanced_views_.insert(view.get()).second)
                        return;
                    step_advancers_.push_back([view = std::move(view)]()
                    {
                        HandleAccess::advance(*view);
                    });
                }

                /**
                 * @brief Wires a Dynamic's providers to a node's bindings through a per-step
                 *        updater.
                 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
                 */
                void register_binding_updater(
                    std::shared_ptr<::SushiRuntime::Internal::Graph::LateBindings> lb, Dynamic dyn)
                {
                    if (!DynamicAccess::when(dyn) && !DynamicAccess::count(dyn) &&
                        !DynamicAccess::base(dyn))
                        return;
                    binding_updaters_.push_back([lb = std::move(lb), dyn = std::move(dyn)]()
                    {
                        const Dynamic::WhenProvider& when = DynamicAccess::when(dyn);
                        const Dynamic::CountProvider& count = DynamicAccess::count(dyn);
                        const Dynamic::BaseProvider& base = DynamicAccess::base(dyn);
                        if (when)
                            lb->enabled.store(when(), std::memory_order_relaxed);
                        if (count)
                            lb->count.store(count(), std::memory_order_relaxed);
                        if (base)
                            lb->base.store(base(), std::memory_order_relaxed);
                    });
                }

                /**
                 * @brief Registers a device fault record every finished step checks.
                 * @param record K_DEVICE_FAULT_WORDS host-USM words the owning graph keeps alive.
                 */
                void register_fault_record(std::uint32_t* record)
                {
                    fault_records_.push_back(record);
                }

                /**
                 * @brief Throws the first fault a finished step's kernels recorded.
                 * @pre Every kernel of the step has completed.
                 */
                void check_faults() const
                {
                    for (std::uint32_t* record : fault_records_)
                        throw_if_device_fault(record);
                    for (const StepMachinery* s : aggregated_)
                        s->check_faults();
                }

                /**
                 * @brief Records a re-point hook for a double-buffered offload node, run before
                 *        each step.
                 */
                void add_dist_io_updater(StepHook updater)
                {
                    dist_io_updaters_.push_back(std::move(updater));
                }

                /**
                 * @brief Rewrites every State view from the State that owns it, before a run's
                 *        first step.
                 */
                void resync() const
                {
                    for (auto& r : view_resyncs_)
                        r();
                    for (const StepMachinery* s : aggregated_)
                        s->resync();
                }

                /**
                 * @brief Runs one step's pre-execute hooks: distributed live IO first, then
                 *        late-bound controls.
                 */
                void before_step() const
                {
                    for (auto& update_io : dist_io_updaters_)
                        update_io();
                    for (auto& update_binding : binding_updaters_)
                        update_binding();
                    for (const StepMachinery* s : aggregated_)
                        s->before_step();
                }

                /**
                 * @brief Advances every written State after a step so its next field becomes
                 *        current.
                 */
                void after_step() const
                {
                    for (auto& advance : step_advancers_)
                        advance();
                    for (const StepMachinery* s : aggregated_)
                        s->after_step();
                }

                /**
                 * @brief Clears this machinery's aggregation of other machineries, leaving its
                 *        own hooks.
                 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
                 */
                void reset_updaters()
                {
                    aggregated_.clear();
                }

                /**
                 * @brief References another machinery's per-step hooks without copying them.
                 * @pre other outlives every run driven through this one.
                 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
                 */
                void aggregate_from(const StepMachinery& other)
                {
                    aggregated_.push_back(&other);
                }

            private:
                /** @brief The views that already have a resync; the hooks keep them alive. */
                std::unordered_set<const void*> resynced_views_;
                /** @brief The views that already have an advancer: one writer per State. */
                std::unordered_set<const void*> advanced_views_;
                /** @brief Holds one resync per recorded State view, run before each run. */
                std::vector<StepHook> view_resyncs_;
                /**
                 * @brief Holds one advancer per State update rule, run once per step boundary.
                 */
                std::vector<StepHook> step_advancers_;
                /**
                 * @brief Holds one updater per late-bound task; empty unless a Dynamic add()
                 *        was used.
                 */
                std::vector<StepHook> binding_updaters_;
                /**
                 * @brief Holds one live-IO re-point hook per double-buffered offload rule, run
                 *        before each step.
                 */
                std::vector<StepHook> dist_io_updaters_;
                /** @brief Device fault records of this machinery's late-bound nodes. */
                std::vector<std::uint32_t*> fault_records_;
                /**
                 * @brief Lists the other machineries this one drives; empty outside a composer.
                 */
                std::vector<const StepMachinery*> aggregated_;
        };
    } // namespace Detail
} // namespace SushiRuntime
