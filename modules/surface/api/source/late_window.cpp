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
 * @brief Implements the read of a late-bound node's controls.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/detail/README.md
 */

#include <SushiRuntime/api/detail/late_window.hpp>

#include <atomic>

#include <SushiRuntime/task/late_bindings.hpp>

#include "late_controls_access.hpp"

namespace SushiRuntime
{
    namespace Detail
    {
        LateWindow read_late_window(const LateControls* controls) noexcept
        {
            const Internal::Graph::LateBindings& bindings = *engine_controls(controls);
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
    } // namespace Detail
} // namespace SushiRuntime
