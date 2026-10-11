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

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
#include <SushiRuntime/api/detail/handle_access.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/core/delegate.hpp>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/export.hpp>

namespace SushiRuntime
{
    namespace Internal::Graph
    {
        struct LateBindings;
    } // namespace Internal::Graph

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
                    sizeof(std::shared_ptr<Internal::Graph::LateBindings>)>;

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
                SUSHIRUNTIME_API void register_binding_updater(
                    std::shared_ptr<Internal::Graph::LateBindings> lb, Dynamic dyn);

                /**
                 * @brief Registers a fault record every finished step checks.
                 * @param record K_DEVICE_FAULT_WORDS words the owning graph keeps alive.
                 */
                void register_fault_record(std::uint32_t* record)
                {
                    fault_records_.push_back(record);
                }

                /**
                 * @brief Returns the first fault a finished step recorded, and clears it.
                 * @return An InvalidGraph error, or nothing when no record holds a fault.
                 * @pre Every kernel of the step has completed.
                 */
                [[nodiscard]] std::optional<Error> take_fault() const
                {
                    for (std::uint32_t* record : fault_records_)
                        if (std::optional<Error> fault = take_device_fault(record))
                            return fault;
                    for (const StepMachinery* s : aggregated_)
                        if (std::optional<Error> fault = s->take_fault())
                            return fault;
                    return std::nullopt;
                }

                /**
                 * @brief Records the hook that re-points the live fields of a State rule that
                 *        names an operation, run before each step.
                 */
                void add_live_field_updater(StepHook updater)
                {
                    live_field_updaters_.push_back(std::move(updater));
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
                 * @brief Runs one step's pre-execute hooks: the live fields first, then the
                 *        late-bound controls.
                 */
                void before_step() const
                {
                    for (auto& update_fields : live_field_updaters_)
                        update_fields();
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
                 * @brief Holds one live-field hook per State rule that names an operation, run
                 *        before each step.
                 */
                std::vector<StepHook> live_field_updaters_;
                /** @brief Device fault records of this machinery's late-bound nodes. */
                std::vector<std::uint32_t*> fault_records_;
                /**
                 * @brief Lists the other machineries this one drives; empty outside a composer.
                 */
                std::vector<const StepMachinery*> aggregated_;
        };
    } // namespace Detail
} // namespace SushiRuntime
