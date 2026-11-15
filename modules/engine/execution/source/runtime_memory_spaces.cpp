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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include <SushiRuntime/device/device_memory_probe.hpp>
#include <SushiRuntime/device/integrated_device.hpp>
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
            const Memory::MemorySpace& host_space = *spaces.owned.front();
            const std::size_t host_usable = host_space.capacity_bytes()
                                            - std::min(host_space.reserve_bytes(), host_space.capacity_bytes());

            const std::size_t count = devices.count();
            for (std::size_t device = 0; device < count; ++device)
            {
                Memory::DeviceMemorySpaceDescription description;
                description.id = Graph::device_memory_space(device);
                const DeviceDescription& facts = devices.devices()[device];
                description.shared_pool = Device::shared_pool_of(facts, description.id.value(), host.value());
                for (std::size_t other = 0; other < count; ++other)
                    if (other != device && devices.shares_context(device, other))
                        description.direct_targets.push_back(Graph::device_memory_space(other));

                const std::size_t limit = facts.integrated ? host_usable : std::numeric_limits<std::size_t>::max();
                const Device::DeviceMemoryProbe probe(devices, device, limit);
                spaces.owned.push_back(std::make_unique<Memory::DeviceMemorySpace>(std::move(description), probe));
            }

            for (const auto& space : spaces.owned)
                spaces.views.push_back(space.get());
            return spaces;
        }

        const Memory::FileMemorySpace& add_file_space_to(RuntimeMemorySpaces& spaces, Memory::FileRange range)
        {
            if (!spaces.vacant_file_ids.empty())
            {
                const std::uint32_t index = *spaces.vacant_file_ids.begin();
                spaces.vacant_file_ids.erase(spaces.vacant_file_ids.begin());
                auto space = std::make_unique<Memory::FileMemorySpace>(MemorySpaceID(index), std::move(range));
                const Memory::FileMemorySpace& added = *space;
                spaces.owned[index] = std::move(space);
                spaces.views[index] = &added;
                return added;
            }
            const MemorySpaceID id(static_cast<std::uint32_t>(spaces.views.size()));
            auto space = std::make_unique<Memory::FileMemorySpace>(id, std::move(range));
            const Memory::FileMemorySpace& added = *space;
            spaces.owned.push_back(std::move(space));
            spaces.views.push_back(&added);
            return added;
        }

        bool remove_file_space_from(RuntimeMemorySpaces& spaces, MemorySpaceID id)
        {
            const std::uint32_t index = id.value();
            if (index >= spaces.owned.size() || spaces.vacant_file_ids.count(index) != 0 ||
                dynamic_cast<const Memory::FileMemorySpace*>(spaces.owned[index].get()) == nullptr)
                return false;
            // An empty range keeps the id an index while no buffer can be held there.
            auto vacant = std::make_unique<Memory::FileMemorySpace>(id, Memory::FileRange{});
            spaces.views[index] = vacant.get();
            spaces.owned[index] = std::move(vacant);
            spaces.vacant_file_ids.insert(index);
            return true;
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
