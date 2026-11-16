/**************************************************************************/
/* shared_memory_devices.hpp                                              */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file shared_memory_devices.hpp
 * @brief Declares shared_memory_devices(), the devices of a runtime that take Residency::Shared buffers.
 * @see tests/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstddef>
#include <vector>

#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>

namespace TestSupport
{
/**
 * @brief Returns the devices of a runtime whose queue offers shared USM, in index order.
 * @param runtime A runtime on SYCL devices.
 * @return The indices of the devices a Residency::Shared buffer can live on.
 */
[[nodiscard]] inline std::vector<SushiRuntime::DeviceIndex> shared_memory_devices(const SushiRuntime::Runtime& runtime)
{
    const SushiRuntime::AdvancedControls view = runtime.advanced();
    std::vector<SushiRuntime::DeviceIndex> devices;
    for (std::size_t index = 0; index < view.devices().size(); ++index)
    {
        const SushiRuntime::DeviceIndex device{index};
        if (view.queue(device).get_device().has(sycl::aspect::usm_shared_allocations))
            devices.push_back(device);
    }
    return devices;
}
} // namespace TestSupport
