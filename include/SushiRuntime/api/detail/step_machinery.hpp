/****************************************************************/
/* step_machinery.hpp                                           */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file step_machinery.hpp
 * @brief Declares StepMachinery, the per-step hooks a replayed plan runs around each step.
 * @author Mustafa Garip
 * @see include/SushiRuntime/api/detail/README.md
 */

#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SushiRuntime/api/data/state.hpp>
#include <SushiRuntime/api/detail/device_fault.hpp>
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
             * @brief Owns the per-step hook lists a replayed plan runs around each step, in one
             *        fixed order.
             * @see include/SushiRuntime/api/detail/README.md
             */
            class StepMachinery
            {
                public:
                    /**
                     * @brief Stores a per-step hook without heap allocation, sized from the largest
                     *        hook.
                     * @see include/SushiRuntime/api/detail/README.md
                     */
                    using StepHook = Core::Delegate<void(),
                        sizeof(Dynamic) +
                        sizeof(std::shared_ptr<::SushiRuntime::Graph::LateBindings>)>;

                    /**
                     * @brief Returns the shared view for a State, creating it and its resync on
                     *        first use.
                     * @see include/SushiRuntime/api/detail/README.md
                     */
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

                    /**
                     * @brief Registers the once-per-step advance for a written State, at most once
                     *        per State.
                     * @see include/SushiRuntime/api/detail/README.md
                     */
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

                    /**
                     * @brief Wires a Dynamic's providers to a node's bindings through a per-step
                     *        updater.
                     * @see include/SushiRuntime/api/detail/README.md
                     */
                    void register_binding_updater(
                        std::shared_ptr<::SushiRuntime::Graph::LateBindings> lb, Dynamic dyn)
                    {
                        if (!dyn.when_pred && !dyn.count_fn && !dyn.base_fn)
                            return;
                        binding_updaters_.push_back([lb = std::move(lb), dyn = std::move(dyn)]()
                        {
                            if (dyn.when_pred)
                                lb->enabled.store(dyn.when_pred(), std::memory_order_relaxed);
                            if (dyn.count_fn)
                                lb->count.store(dyn.count_fn(), std::memory_order_relaxed);
                            if (dyn.base_fn)
                                lb->base.store(dyn.base_fn(), std::memory_order_relaxed);
                        });
                    }

                    /**
                     * @brief Registers a device fault record every finished step checks.
                     * @param record DEVICE_FAULT_WORDS host-USM words the owning graph keeps alive.
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
                     * @brief Points every State view at its current and next buffers before a run's
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
                     * @see include/SushiRuntime/api/detail/README.md
                     */
                    void reset_updaters()
                    {
                        aggregated_.clear();
                    }

                    /**
                     * @brief References another machinery's per-step hooks without copying them.
                     * @pre other outlives every run driven through this one.
                     * @see include/SushiRuntime/api/detail/README.md
                     */
                    void aggregate_from(const StepMachinery& other)
                    {
                        aggregated_.push_back(&other);
                    }

                private:
                    /**
                     * @brief Holds one type-erased StateView per State, shared by every kernel that
                     *        touches it.
                     */
                    std::unordered_map<const void*, std::shared_ptr<void>> state_views_;
                    // States that already have an advancer (one writer per State).
                    std::unordered_set<const void*> advanced_states_;
                    // Points each view at its state's current/next; run before each run.
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
        } // namespace detail
    } // namespace API
} // namespace SushiRuntime
