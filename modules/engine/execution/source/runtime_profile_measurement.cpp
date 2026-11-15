/**************************************************************************/
/* runtime_profile_measurement.cpp                                        */
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
 * @file runtime_profile_measurement.cpp
 * @brief Implements measure_runtime_profiles().
 * @see modules/engine/execution/README.md
 * @author Mustafa Garip
 */

#include "runtime_profile_measurement.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <SushiRuntime/device_profile/device_profile_probe.hpp>
#include <SushiRuntime/device_profile/probe_devices.hpp>
#include <SushiRuntime/device_profile/profile_measurement.hpp>
#include <SushiRuntime/task/handle_location.hpp>

#include "runtime_probe_kernels.hpp"

namespace SushiRuntime
{
    namespace Internal::Execution
    {
        namespace
        {
            /** @brief Holds the size of the copy that gives a link's latency. */
            constexpr std::uint64_t K_LATENCY_PROBE_BYTES = 1;

            /** @brief Holds the size of the copy that gives a link's bandwidth: 16 MiB. */
            constexpr std::uint64_t K_BANDWIDTH_PROBE_BYTES = std::uint64_t{16} << 20;

            /** @brief Returns the space of device index among spaces, or null when there is none. */
            [[nodiscard]] const Memory::MemorySpace* space_of(const std::vector<const Memory::MemorySpace*>& spaces,
                                                              std::size_t index)
            {
                const MemorySpaceID wanted = Graph::device_memory_space(index);
                for (const Memory::MemorySpace* space : spaces)
                    if (space != nullptr && space->id() == wanted)
                        return space;
                return nullptr;
            }

            /** @brief Finds a device of the runtime's set by its key. */
            class RuntimeProbeDevices final : public Coherence::ProbeDevices
            {
                public:
                    RuntimeProbeDevices(Device::DeviceQueueSet& devices,
                                        const std::vector<const Memory::MemorySpace*>& spaces) noexcept
                        : devices_(devices), spaces_(spaces)
                    {

                    }

                    [[nodiscard]] std::optional<Coherence::ProbeDevice> find(const DeviceKey& key) const override
                    {
                        const std::vector<DeviceDescription>& listed = devices_.devices();
                        for (std::size_t index = 0; index < listed.size(); ++index)
                            if (listed[index].key == key)
                                return Coherence::ProbeDevice{&devices_.queue(index), space_of(spaces_, index)};
                        return std::nullopt;
                    }

                private:
                    /// @brief The devices, by index.
                    Device::DeviceQueueSet& devices_;

                    /// @brief The runtime's memory spaces.
                    const std::vector<const Memory::MemorySpace*>& spaces_;
            };

            /** @brief Returns the targets of device index: the first path to each other device that has one. */
            [[nodiscard]] std::vector<Coherence::PathTarget>
            targets_of(std::size_t index, const std::vector<DeviceDescription>& listed,
                       const std::vector<const Memory::MemorySpace*>& spaces,
                       const std::vector<Coherence::TransferPath*>& paths)
            {
                std::vector<Coherence::PathTarget> targets;
                const Memory::MemorySpace* from = space_of(spaces, index);
                for (std::size_t other = 0; other < listed.size() && from != nullptr; ++other)
                {
                    const Memory::MemorySpace* to = space_of(spaces, other);
                    if (other == index || to == nullptr)
                        continue;
                    for (const Coherence::TransferPath* path : paths)
                        if (path->connects(*from, *to))
                        {
                            targets.push_back(Coherence::PathTarget{listed[other].key, path->id(), std::string(path->name())});
                            break;
                        }
                }
                return targets;
            }
        } // namespace

        Result<Coherence::DeviceProfileSet>
        measure_runtime_profiles(Device::DeviceQueueSet& devices, const std::vector<const Memory::MemorySpace*>& spaces,
                                 const std::vector<Coherence::TransferPath*>& paths)
        {
            const RuntimeProbeDevices probe_devices(devices, spaces);
            Coherence::DeviceProfileProbe probe(probe_devices, paths, runtime_probe_kernels());
            const std::vector<DeviceDescription>& listed = devices.devices();

            Coherence::DeviceProfileSet profiles;
            for (std::size_t index = 0; index < listed.size(); ++index)
            {
                Coherence::MeasurementPlan plan;
                plan.latency_probe_bytes = K_LATENCY_PROBE_BYTES;
                plan.bandwidth_probe_bytes = K_BANDWIDTH_PROBE_BYTES;
                plan.kinds = {OperationKinds::K_GENERIC, OperationKinds::K_MEMORY_BOUND};
                plan.paths = targets_of(index, listed, spaces, paths);
                plan.host_links = true;

                Coherence::ProfileMeasurement measurement(probe, std::move(plan));
                Result<Coherence::DeviceProfile> measured = measurement.measure(listed[index].key);
                if (!measured)
                    return make_error(std::move(measured.error()));
                profiles.set(listed[index].key, std::move(measured.value()));
            }
            return profiles;
        }
    } // namespace Internal::Execution
} // namespace SushiRuntime
