/**************************************************************************/
/* step_machinery.hpp                                                     */
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

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/core/delegate.hpp>
#include <SushiRuntime/graph/node.hpp>

namespace SushiRuntime
{
    namespace API
    {
        namespace detail
        {
            /**
             * @brief The per-step lifecycle a replayed plan runs around each step.
             *
             * Both the static Graph and the DynamicGraph composer drive the same
             * compile-once / replay-many loop, and both need the identical four
             * per-step hook lists run in the identical order. This object owns those
             * lists (plus the State double-buffer bookkeeping that produces them) so
             * the ordering invariant — re-point distributed live IO, refresh
             * late-bound controls, execute, then advance every State — lives in one
             * place instead of being copied into each driver.
             *
             * The building side (view_for / register_advance / register_binding_updater)
             * is used by Graph's add() overloads as they record tasks. The driving
             * side (resync / before_step / after_step) is used by both drivers. The
             * aggregation side (reset_updaters / aggregate_from) lets DynamicGraph
             * gather every live region's hooks into one before a run.
             */
            class StepMachinery
            {
                public:
                    // A per-step hook stored without heap allocation. Capacity covers
                    // the largest hook: a binding updater that captures a whole Dynamic
                    // (two ~64-byte provider slots) plus a LateBindings shared pointer.
                    using StepHook = Core::Delegate<void(), 176>;

                    // Returns the shared view for a State, creating it (and registering
                    // its resync) on first use. Every kernel touching a State captures
                    // the same view, so one advancer keeps all references current — the
                    // basis for correct coupled (multi-field) updates.
                    template <typename T>
                    std::shared_ptr<StateView<T>> view_for(State<T>& s)
                    {
                        const void* key = &s;
                        auto it = state_views_.find(key);
                        if (it != state_views_.end())
                            return std::static_pointer_cast<StateView<T>>(it->second);

                        auto view = std::make_shared<StateView<T>>();
                        state_views_.emplace(key, view);
                        State<T>* sp = &s;
                        view_resyncs_.push_back([sp, view]()
                        {
                            view->cur = sp->current();
                            view->next = sp->next();
                        });
                        return view;
                    }

                    // Registers the once-per-step advance for a written State. Deduped so
                    // a State advances exactly once per step (one update rule per field).
                    template <typename T>
                    void register_advance(State<T>& s, std::shared_ptr<StateView<T>> view)
                    {
                        const void* key = &s;
                        if (!advanced_states_.insert(key).second)
                            return;
                        State<T>* sp = &s;
                        step_advancers_.push_back([sp, view]()
                        {
                            sp->advance();
                            view->cur = sp->current();
                            view->next = sp->next();
                        });
                    }

                    // Wires a Dynamic's providers to a node's bindings: pushes a per-step
                    // updater that polls when()/sized() on the driver thread and stores
                    // the results into the shared block the kernel and scheduler read.
                    // Skipped when the Dynamic carries no providers (the defaults — always
                    // enabled, full range — already hold), so an unused tag costs nothing.
                    void register_binding_updater(
                        std::shared_ptr<::SushiRuntime::Graph::LateBindings> lb, Dynamic dyn)
                    {
                        if (!dyn.when_pred && !dyn.count_fn)
                            return;
                        binding_updaters_.push_back([lb = std::move(lb), dyn = std::move(dyn)]()
                        {
                            if (dyn.when_pred)
                                lb->enabled.store(dyn.when_pred(), std::memory_order_relaxed);
                            if (dyn.count_fn)
                                lb->count.store(dyn.count_fn(), std::memory_order_relaxed);
                        });
                    }

                    // Records a re-point hook for a double-buffered offload node, run
                    // before each step so the coordinator resolves the step's handles.
                    void add_dist_io_updater(StepHook updater)
                    {
                        dist_io_updaters_.push_back(std::move(updater));
                    }

                    // Points every State view at its state's current/next buffers. Run
                    // before the first step of a run (a fresh build or a continuing replay).
                    void resync() const
                    {
                        for (auto& r : view_resyncs_)
                            r();
                        for (const StepMachinery* s : aggregated_)
                            s->resync();
                    }

                    // Runs the pre-execute hooks for one step: re-point any distributed
                    // live IO, then refresh any late-bound size/enablement controls.
                    void before_step() const
                    {
                        for (auto& update_io : dist_io_updaters_)
                            update_io();
                        for (auto& update_binding : binding_updaters_)
                            update_binding();
                        for (const StepMachinery* s : aggregated_)
                            s->before_step();
                    }

                    // Advances every written State after a step so its just-written next
                    // field becomes current for the following step and for the caller.
                    void after_step() const
                    {
                        for (auto& advance : step_advancers_)
                            advance();
                        for (const StepMachinery* s : aggregated_)
                            s->after_step();
                    }

                    // Clears this machinery's aggregation of other machineries. A
                    // composer calls this then re-aggregates the live regions before a
                    // run; a region's own hooks (and the State bookkeeping that owns the
                    // views) are untouched, since the region still owns them.
                    void reset_updaters()
                    {
                        aggregated_.clear();
                    }

                    // References another machinery's per-step hooks from this one, so a
                    // composer drives every live region's hooks without copying them
                    // (the hooks are move-only). The referenced machinery must outlive
                    // every run driven through this one — it is the region's own,
                    // owned for the region's lifetime.
                    void aggregate_from(const StepMachinery& other)
                    {
                        aggregated_.push_back(&other);
                    }

                private:
                    // One shared StateView per State, captured by every kernel that
                    // touches it. Type-erased; view_for() casts back to the typed view.
                    std::unordered_map<const void*, std::shared_ptr<void>> state_views_;
                    // States that already have an advancer (one writer per State).
                    std::unordered_set<const void*> advanced_states_;
                    // Points each view at its state's current/next; run before each run.
                    std::vector<StepHook> view_resyncs_;
                    // One per State update rule: advances the state and re-points its
                    // view at the swapped buffers, run once per step boundary by run(n).
                    std::vector<StepHook> step_advancers_;
                    // One per dynamic (late-bound) task: refreshes its size/enable
                    // controls before each step. Empty unless a Dynamic add() was used.
                    std::vector<StepHook> binding_updaters_;
                    // One per double-buffered offload rule: re-points the node's live IO
                    // pointer lists at the step's current/next allocations before each
                    // step. Empty unless a distributed State step was added.
                    std::vector<StepHook> dist_io_updaters_;
                    // Other machineries this one drives in addition to its own hooks. A
                    // composer aggregates each live region's machinery here; empty for a
                    // region's or a static graph's own machinery.
                    std::vector<const StepMachinery*> aggregated_;
            };
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
