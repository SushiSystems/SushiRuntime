/**************************************************************************/
/* device_memory_probe.cpp                                                */
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
 * @file device_memory_probe.cpp
 * @brief Implements DeviceMemoryProbe.
 * @see modules/platform/device/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/device/device_memory_probe.hpp>

namespace SushiRuntime
{
    namespace Internal::Device
    {
        DeviceMemoryProbe::DeviceMemoryProbe(const DeviceQueueSet& devices, std::size_t index) noexcept
            : devices_(&devices), index_(index)
        {

        }

        Memory::ProbedMemory DeviceMemoryProbe::probe() const
        {
            Memory::ProbedMemory measured;
            measured.total_bytes = static_cast<std::size_t>(devices_->devices()[index_].memory_bytes);
            measured.free_bytes = static_cast<std::size_t>(devices_->free_memory(index_));
            return measured;
        }
    } // namespace Internal::Device
} // namespace SushiRuntime
