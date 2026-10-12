/**************************************************************************/
/* emit_dynamic.cpp                                                       */
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
 * @file emit_dynamic.cpp
 * @brief Implements the creation of a late-bound node's controls from a Dynamic.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#include <SushiRuntime/api/detail/emit_dynamic.hpp>

#include <memory>
#include <vector>

#include <SushiRuntime/api/vocabulary/dynamic.hpp>
#include <SushiRuntime/region/buffer_region.hpp>
#include <SushiRuntime/region/liveness.hpp>
#include <SushiRuntime/task/late_bindings.hpp>

namespace SushiRuntime
{
    namespace Detail
    {
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
    } // namespace Detail
} // namespace SushiRuntime
