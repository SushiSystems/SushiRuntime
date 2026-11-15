/**************************************************************************/
/* runtime_memory_spaces.cpp                                              */
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
 * @file runtime_memory_spaces.cpp
 * @brief Implements build_memory_spaces.
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#include "runtime_memory_spaces.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

#include <SushiRuntime/device/device_memory_probe.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/host_memory_space.hpp>
#include <SushiRuntime/task/handle_location.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        RuntimeMemorySpaces build_memory_spaces(const Device::DeviceQueueSet& devices,
                                                const Memory::MemoryProbe& host_probe)
        {
            RuntimeMemorySpaces spaces;
            const MemorySpaceID host = Graph::host_memory_space();
            spaces.owned.push_back(std::make_unique<Memory::HostMemorySpace>(
                Memory::HostMemorySpaceDescription{host, host.value(), 0, {}}, host_probe));

            const std::size_t count = devices.count();
            for (std::size_t device = 0; device < count; ++device)
            {
                Memory::DeviceMemorySpaceDescription description;
                description.id = Graph::device_memory_space(device);
                description.shared_pool = description.id.value();
                for (std::size_t other = 0; other < count; ++other)
                    if (other != device && devices.shares_context(device, other))
                        description.direct_targets.push_back(Graph::device_memory_space(other));

                const Device::DeviceMemoryProbe probe(devices, device);
                spaces.owned.push_back(std::make_unique<Memory::DeviceMemorySpace>(std::move(description), probe));
            }

            for (const auto& space : spaces.owned)
                spaces.views.push_back(space.get());
            return spaces;
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
