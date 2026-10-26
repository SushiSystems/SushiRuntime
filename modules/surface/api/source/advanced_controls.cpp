/**************************************************************************/
/* advanced_controls.cpp                                                  */
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
 * @file advanced_controls.cpp
 * @brief Implements AdvancedControls, the view of a runtime's devices and counters.
 * @author Mustafa Garip
 * @see modules/surface/api/include/SushiRuntime/api/runtime/README.md
 */

#include <SushiRuntime/api/runtime/advanced_controls.hpp>

#include <cstddef>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/context_access/context_access.hpp>
#include <SushiRuntime/device/device_queue_set.hpp>
#include <SushiRuntime/execution/execution_engine.hpp>
#include <SushiRuntime/execution/runtime_context.hpp>
#include <SushiRuntime/execution/runtime_settings.hpp>
#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/machine/worker_pool.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/observability/transfer_counters.hpp>

namespace SushiRuntime
{
    std::size_t AdvancedControls::device_count() const
    {
        return Internal::ContextAccess::context(context_).devices().count();
    }

    const std::vector<DeviceDescription>& AdvancedControls::devices() const
    {
        return Internal::ContextAccess::context(context_).devices().devices();
    }

    sycl::queue& AdvancedControls::queue(DeviceIndex device) const
    {
        return Internal::ContextAccess::context(context_).devices().queue(device.value());
    }

    bool AdvancedControls::is_rebalancer_enabled() const
    {
        return Internal::ContextAccess::context(context_).execution_engine().rebalancer_enabled();
    }

    WorkerPool AdvancedControls::worker_pool() const
    {
        return Internal::ContextAccess::context(context_).execution_engine().worker_pool();
    }

    std::size_t AdvancedControls::live_allocation_count() const
    {
        return Internal::ContextAccess::context(context_).memory().live_count();
    }

    std::size_t AdvancedControls::host_to_device_bytes() const noexcept
    {
        return Internal::ContextAccess::context(context_).transfers().host_to_device();
    }

    std::size_t AdvancedControls::device_to_host_bytes() const noexcept
    {
        return Internal::ContextAccess::context(context_).transfers().device_to_host();
    }
} // namespace SushiRuntime
