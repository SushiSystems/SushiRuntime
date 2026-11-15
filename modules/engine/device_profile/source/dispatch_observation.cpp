/**************************************************************************/
/* dispatch_observation.cpp                                               */
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
 * @file dispatch_observation.cpp
 * @brief Implements observe_dispatches().
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/device_profile/dispatch_observation.hpp>

#include <cstddef>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Returns the key of the device that ran record, or nothing when the table names none. */
            [[nodiscard]] std::optional<DeviceKey> device_of(const NodeEventRecord& record, const DeviceKeyTable& devices)
            {
                if (record.device_index == K_HOST_DEVICE_INDEX)
                    return devices.host;
                if (static_cast<std::size_t>(record.device_index) >= devices.by_index.size())
                    return std::nullopt;
                return devices.by_index[record.device_index];
            }
        } // namespace

        std::vector<DispatchObservation> observe_dispatches(const std::vector<NodeEventRecord>& records,
                                                            const NodeTable& table, const DeviceKeyTable& devices)
        {
            constexpr double K_SECONDS_PER_NANOSECOND = 1.0e-9;
            std::vector<DispatchObservation> observations;
            observations.reserve(records.size());
            for (const NodeEventRecord& record : records)
            {
                const NodeTableRow* row = table.find(record.node_index);
                if (row == nullptr || !row->cost.has_value() || row->cost->operations == 0)
                    continue;
                if (record.end_ns <= record.start_ns)
                    continue;
                const std::optional<DeviceKey> device = device_of(record, devices);
                if (!device.has_value())
                    continue;
                observations.push_back(DispatchObservation{
                    *device, *row->cost, static_cast<double>(record.end_ns - record.start_ns) * K_SECONDS_PER_NANOSECOND});
            }
            return observations;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime
