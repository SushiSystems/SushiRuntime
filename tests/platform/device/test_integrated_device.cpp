/**************************************************************************/
/* test_integrated_device.cpp                                             */
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
 * @file test_integrated_device.cpp
 * @brief Tests that an integrated device shares the host pool and a discrete one keeps its own.
 * @see tests/platform/device/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/device/device_memory_probe.hpp>
#include <SushiRuntime/device/integrated_device.hpp>
#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/host_memory_space.hpp>
#include <SushiRuntime/memory/memory_pool_capacity.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Device;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    constexpr std::uint32_t K_HOST_POOL = 0;
    constexpr std::size_t K_HOST_BYTES = std::size_t{8} << 20;
    constexpr std::size_t K_DEVICE_BYTES = std::size_t{16} << 20;

    /** @brief A probe that reports a fixed total, all of it free. */
    class FixedProbe final : public MemoryProbe
    {
        public:
            explicit FixedProbe(std::size_t total) noexcept : total_(total)
            {

            }

            [[nodiscard]] ProbedMemory probe() const override
            {
                return ProbedMemory{total_, total_};
            }

        private:
            std::size_t total_;
    };

    std::vector<DeviceDescription> inventory(bool integrated)
    {
        std::vector<DeviceDescription> given(1);
        given[0].name = "device";
        given[0].memory_bytes = K_DEVICE_BYTES;
        given[0].integrated = integrated;
        return given;
    }

    TEST(Small_IntegratedDevice, AnIntegratedDeviceNamesTheHostPool)
    {
        EXPECT_EQ(shared_pool_of(inventory(true)[0], 5, K_HOST_POOL), K_HOST_POOL);
    }

    TEST(Small_IntegratedDevice, ADiscreteDeviceKeepsItsOwnPool)
    {
        EXPECT_EQ(shared_pool_of(inventory(false)[0], 5, K_HOST_POOL), 5U);
    }

    TEST(Small_IntegratedDevice, AnIntegratedDeviceIsCountedOnceWithTheHost)
    {
        const DeviceQueueSet devices(inventory(true));
        const FixedProbe host_probe(K_HOST_BYTES);
        const HostMemorySpace host(HostMemorySpaceDescription{MemorySpaceID{0}, K_HOST_POOL, 0, {}}, host_probe);
        DeviceMemorySpaceDescription description;
        description.id = MemorySpaceID{1};
        description.shared_pool = shared_pool_of(devices.devices()[0], 1, K_HOST_POOL);
        const DeviceMemoryProbe probe(devices, 0, K_HOST_BYTES);
        const DeviceMemorySpace device(std::move(description), probe);

        EXPECT_EQ(device.capacity_bytes(), K_HOST_BYTES);
        EXPECT_EQ(usable_pool_bytes({&host, &device}), K_HOST_BYTES);
    }

    TEST(Small_IntegratedDevice, ADiscreteDeviceAddsItsOwnPool)
    {
        const DeviceQueueSet devices(inventory(false));
        const FixedProbe host_probe(K_HOST_BYTES);
        const HostMemorySpace host(HostMemorySpaceDescription{MemorySpaceID{0}, K_HOST_POOL, 0, {}}, host_probe);
        DeviceMemorySpaceDescription description;
        description.id = MemorySpaceID{1};
        description.shared_pool = shared_pool_of(devices.devices()[0], 1, K_HOST_POOL);
        const DeviceMemoryProbe probe(devices, 0);
        const DeviceMemorySpace device(std::move(description), probe);

        EXPECT_EQ(usable_pool_bytes({&host, &device}), K_HOST_BYTES + K_DEVICE_BYTES);
    }
} // namespace
