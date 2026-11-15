/**************************************************************************/
/* runtime_partition_parts.cpp                                            */
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
 * @file runtime_partition_parts.cpp
 * @brief Implements make_space_device_resolver() and make_partition_parts().
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#include "runtime_partition_parts.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/machine/device_description.hpp>
#include <SushiRuntime/machine/device_kind.hpp>
#include <SushiRuntime/partitioning/floor_partitioner.hpp>
#include <SushiRuntime/partitioning/list_scheduling_partitioner.hpp>
#include <SushiRuntime/task/handle_location.hpp>

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /** @brief Returns the space of device index, or null when the runtime has none. */
            [[nodiscard]] const Memory::MemorySpace* space_of(const std::vector<const Memory::MemorySpace*>& spaces,
                                                              std::size_t index)
            {
                const std::size_t id = Graph::device_memory_space(index).value();
                return id < spaces.size() ? spaces[id] : nullptr;
            }

            /** @brief Returns the identity of the first path that connects device index to another device, or none. */
            [[nodiscard]] TransferPathID copy_path_of(std::size_t index, std::size_t count,
                                                   const std::vector<const Memory::MemorySpace*>& spaces,
                                                   const std::vector<Coherence::TransferPath*>& paths)
            {
                const Memory::MemorySpace* from = space_of(spaces, index);
                for (std::size_t other = 0; other < count && from != nullptr; ++other)
                {
                    const Memory::MemorySpace* to = space_of(spaces, other);
                    if (other == index || to == nullptr)
                        continue;
                    for (const Coherence::TransferPath* path : paths)
                    {
                        if (path->connects(*from, *to))
                            return path->id();
                    }
                }
                return TransferPathID();
            }
        } // namespace

        Coherence::SpaceDeviceResolver make_space_device_resolver(const Device::DeviceQueueSet& devices)
        {
            const Device::DeviceQueueSet* const set = &devices;
            return [set](MemorySpaceID space) -> std::optional<DeviceKey>
            {
                const std::optional<std::size_t> device = Graph::device_of_space(space);
                if (!device || *device >= set->devices().size())
                    return std::nullopt;
                return set->devices()[*device].key;
            };
        }

        Coherence::PartitionSessionsParts
        make_partition_parts(const Device::DeviceQueueSet& devices, const std::vector<const Memory::MemorySpace*>& spaces,
                             const std::vector<Coherence::TransferPath*>& paths,
                             std::shared_ptr<const Coherence::CostModel> model,
                             std::shared_ptr<Coherence::TransferPathTally> copies)
        {
            Coherence::PartitionSessionsParts parts;
            const std::vector<DeviceDescription>& listed = devices.devices();
            for (std::size_t index = 0; index < listed.size(); ++index)
            {
                const bool system_memory = listed[index].kind == DeviceKind::CPU || listed[index].integrated;
                const auto pool = system_memory ? std::uint32_t{0} : static_cast<std::uint32_t>(index + 1);
                parts.devices.push_back(Coherence::PartitionDevice{listed[index].key, pool,
                                                                   copy_path_of(index, listed.size(), spaces, paths)});
            }
            parts.partitioners.scheduler = [](std::shared_ptr<const Coherence::CostModel> costs)
            {
                return std::unique_ptr<Coherence::Partitioner>(
                    std::make_unique<Coherence::ListSchedulingPartitioner>(std::move(costs)));
            };
            parts.partitioners.floor = [](std::shared_ptr<const Coherence::CostModel> costs)
            {
                return std::unique_ptr<Coherence::Partitioner>(
                    std::make_unique<Coherence::FloorPartitioner>(std::move(costs)));
            };
            parts.profiles = std::make_shared<Coherence::ProfileStore>(std::move(model));
            parts.copies = std::move(copies);
            parts.device_of = make_space_device_resolver(devices);
            return parts;
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
