/**************************************************************************/
/* device_key_assigner.cpp                                                */
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
 * @file device_key_assigner.cpp
 * @brief Defines DeviceKeyAssigner.
 * @author Mustafa Garip
 * @see modules/platform/topology/README.md
 */

#include <SushiRuntime/topology/device_key_assigner.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>

#include <SushiRuntime/topology/ordinal_device_key_source.hpp>
#include <SushiRuntime/topology/pci_device_key_source.hpp>
#include <SushiRuntime/topology/uuid_device_key_source.hpp>

namespace SushiRuntime
{
    namespace Internal::Topology
    {
        namespace
        {
            /** @brief Returns the key of the first source that names the identity, ordinal 0. */
            DeviceKey first_key(const std::vector<std::unique_ptr<DeviceKeySource>>& sources,
                                const DeviceIdentity& identity)
            {
                for (const std::unique_ptr<DeviceKeySource>& source : sources)
                {
                    std::optional<DeviceKey> key = source->key_of(identity);
                    if (key.has_value())
                        return *key;
                }

                DeviceKey key;
                key.backend = identity.backend;
                return key;
            }
        } // namespace

        DeviceKeyAssigner::DeviceKeyAssigner(std::vector<std::unique_ptr<DeviceKeySource>> sources)
            : sources_(std::move(sources))
        {
        }

        DeviceKeyAssigner DeviceKeyAssigner::standard()
        {
            std::vector<std::unique_ptr<DeviceKeySource>> sources;
            sources.push_back(std::make_unique<CUDAUUIDDeviceKeySource>());
            sources.push_back(std::make_unique<HIPUUIDDeviceKeySource>());
            sources.push_back(std::make_unique<LevelZeroUUIDDeviceKeySource>());
            sources.push_back(std::make_unique<PCIDeviceKeySource>());
            sources.push_back(std::make_unique<OrdinalDeviceKeySource>());
            return DeviceKeyAssigner(std::move(sources));
        }

        std::vector<DeviceKey>
        DeviceKeyAssigner::assign(const std::vector<DeviceIdentity>& identities) const
        {
            std::vector<DeviceKey> keys;
            keys.reserve(identities.size());
            for (const DeviceIdentity& identity : identities)
                keys.push_back(first_key(sources_, identity));

            std::vector<std::size_t> order(identities.size());
            for (std::size_t position = 0; position < order.size(); ++position)
                order[position] = position;

            const auto precedes = [&](std::size_t left, std::size_t right)
            {
                if (keys[left] != keys[right])
                    return keys[left] < keys[right];
                const DeviceIdentity& a = identities[left];
                const DeviceIdentity& b = identities[right];
                return std::tie(a.vendor, a.name, a.global_mem_size, a.max_compute_units, left) <
                       std::tie(b.vendor, b.name, b.global_mem_size, b.max_compute_units, right);
            };
            std::sort(order.begin(), order.end(), precedes);

            std::map<DeviceKey, std::uint32_t> next_ordinal;
            for (const std::size_t position : order)
                keys[position].ordinal = next_ordinal[keys[position]]++;
            return keys;
        }
    } // namespace Internal::Topology
} // namespace SushiRuntime
