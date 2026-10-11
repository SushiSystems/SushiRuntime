/**************************************************************************/
/* late_window.cpp                                                        */
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
 * @file late_window.cpp
 * @brief Implements the creation, update and read of a late-bound node's controls.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#include <SushiRuntime/api/detail/late_window.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <SushiRuntime/api/detail/emit_dynamic.hpp>
#include <SushiRuntime/api/detail/step_machinery.hpp>
#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/region/liveness.hpp>
#include <SushiRuntime/task/late_bindings.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
        LateWindow read_late_window(const Internal::Graph::LateBindings& bindings) noexcept
        {
            LateWindow window;
            window.count = bindings.count.load(std::memory_order_relaxed);
            window.base = bindings.base.load(std::memory_order_relaxed);
            window.rest_of_capacity =
                (window.count == Internal::Graph::LateBindings::K_USE_BAKED);
            window.count_slot = (bindings.device_count != nullptr)
                ? bindings.device_count + bindings.device_count_index : nullptr;
            window.base_slot = (bindings.device_base != nullptr)
                ? bindings.device_base + bindings.device_base_index : nullptr;
            return window;
        }

        std::shared_ptr<Internal::Graph::LateBindings>
        make_late_bindings(const Dynamic& dyn, std::vector<BufferRegion>& reads,
                           std::vector<LivenessObserver>& lives)
        {
            auto lb = std::make_shared<Internal::Graph::LateBindings>();
            const DeviceSlot& count = DynamicAccess::count_slot(dyn);
            if (count.words != nullptr)
            {
                lb->device_count = count.words;
                lb->device_count_index = count.index;
                register_device_slot(count, reads, lives);
            }
            const DeviceSlot& base = DynamicAccess::base_slot(dyn);
            if (base.words != nullptr)
            {
                lb->device_base = base.words;
                lb->device_base_index = base.index;
                register_device_slot(base, reads, lives);
            }
            return lb;
        }

        void StepMachinery::register_binding_updater(
            std::shared_ptr<Internal::Graph::LateBindings> lb, Dynamic dyn)
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
    } // namespace Detail
} // namespace SushiRuntime
