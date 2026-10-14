/**************************************************************************/
/* step_machinery.cpp                                                     */
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
 * @file step_machinery.cpp
 * @brief Implements the member of StepMachinery that reaches a node's late bindings.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#include <SushiRuntime/api/detail/step_machinery.hpp>

#include <atomic>
#include <memory>
#include <utility>

#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/task/late_bindings.hpp>

#include <SushiRuntime/engine_handles/late_controls_access.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        void StepMachinery::register_binding_updater(
            std::shared_ptr<LateControls> controls, Dynamic dyn)
        {
            if (!DynamicAccess::when(dyn) && !DynamicAccess::count(dyn) &&
                !DynamicAccess::base(dyn))
                return;
            binding_updaters_.push_back([lb = engine_controls(controls), dyn = std::move(dyn)]()
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
    } // namespace Detail
} // namespace SushiRuntime
