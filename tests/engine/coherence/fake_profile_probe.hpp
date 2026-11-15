/**************************************************************************/
/* fake_profile_probe.hpp                                               */
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
 * @file fake_profile_probe.hpp
 * @brief Declares FakeProfileProbe and the profile builders the coherence tests share.
 * @see tests/engine/coherence/README.md
 * @author Mustafa Garip
 */

#pragma once

#include <cstdint>
#include <map>
#include <string_view>

#include <SushiRuntime/coherence/device_profile_set.hpp>
#include <SushiRuntime/coherence/profile_probe.hpp>

namespace SushiRuntime::Internal::Coherence
{
    /** @brief Returns the host device key with the given ordinal. */
    [[nodiscard]] inline DeviceKey host_key(std::uint32_t ordinal)
    {
        DeviceKey key;
        key.ordinal = ordinal;
        return key;
    }

    /** @brief Returns a profile whose numbers are easy to check by hand. */
    [[nodiscard]] inline DeviceProfile round_profile(double speed)
    {
        DeviceProfile profile;
        profile.capacity_bytes = 1000;
        profile.reserve_bytes = 100;
        profile.memory_bandwidth = 1000.0 * speed;
        profile.base_throughput = 2000.0 * speed;
        profile.throughputs[OperationKinds::K_COMPUTE_BOUND] = 8000.0 * speed;
        profile.paths["pcie"] = PathProfile{500.0 * speed, 0.25};
        return profile;
    }

    /**
     * @brief Answers every probe with a time computed from fixed rates and a per-device speed.
     *
     * A device with no entry in speeds runs at speed 1. The probe counts its calls.
     */
    class FakeProfileProbe final : public ProfileProbe
    {
        public:
            /** @brief Holds the speed factor of each device. */
            std::map<DeviceKey, double> speeds;

            /** @brief Holds the seconds every path copy adds, whatever its size. */
            double latency = 0.5;

            /** @brief Holds the operations every probe kernel does. */
            std::uint64_t kernel_operations = 4000;

            /** @brief Makes every kernel report zero seconds when set. */
            bool report_no_time = false;

            /** @brief Counts the calls made to the probe. */
            int calls = 0;

            [[nodiscard]] Result<CapacitySample> measure_capacity(const DeviceKey&) override
            {
                ++calls;
                return CapacitySample{1000, 100};
            }

            [[nodiscard]] Result<CopySample> measure_device_copy(const DeviceKey& device,
                                                                 std::uint64_t bytes) override
            {
                ++calls;
                return CopySample{bytes, static_cast<double>(bytes) / (500.0 * speed_of(device))};
            }

            [[nodiscard]] Result<CopySample> measure_path_copy(const DeviceKey& source, const DeviceKey&,
                                                               std::string_view,
                                                               std::uint64_t bytes) override
            {
                ++calls;
                return CopySample{bytes, latency + static_cast<double>(bytes) / (100.0 * speed_of(source))};
            }

            [[nodiscard]] Result<CopySample> measure_host_copy(const DeviceKey& device, HostDirection direction,
                                                               std::uint64_t bytes) override
            {
                ++calls;
                const double rate = direction == HostDirection::ToDevice ? 200.0 : 100.0;
                return CopySample{bytes, latency + static_cast<double>(bytes) / (rate * speed_of(device))};
            }

            [[nodiscard]] Result<KernelSample> measure_kernel(const DeviceKey& device, OperationKind) override
            {
                ++calls;
                const double seconds = report_no_time ? 0.0 : 2.0 / speed_of(device);
                return KernelSample{kernel_operations, seconds};
            }

        private:
            /** @brief Returns the speed factor of device. */
            [[nodiscard]] double speed_of(const DeviceKey& device) const
            {
                const auto found = speeds.find(device);
                return found == speeds.end() ? 1.0 : found->second;
            }
    };
} // namespace SushiRuntime::Internal::Coherence
