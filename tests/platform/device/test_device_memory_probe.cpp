/**************************************************************************/
/* test_device_memory_probe.cpp                                           */
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
 * @file test_device_memory_probe.cpp
 * @brief Tests that DeviceMemoryProbe reads one device of a DeviceQueueSet.
 * @see tests/platform/device/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/device/device_memory_probe.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Device;

namespace
{
    TEST(Small_DeviceMemoryProbe, ReadsTheMemoryOfItsDeviceFromAHostSet)
    {
        std::vector<DeviceDescription> given(2);
        given[0].name = "host a";
        given[0].memory_bytes = std::uint64_t{1} << 20;
        given[1].name = "host b";
        given[1].memory_bytes = std::uint64_t{2} << 20;
        const DeviceQueueSet devices(given);

        const Internal::Memory::ProbedMemory measured = DeviceMemoryProbe(devices, 1).probe();

        EXPECT_EQ(measured.total_bytes, std::size_t{2} << 20);
        EXPECT_EQ(measured.free_bytes, std::size_t{2} << 20);
    }
} // namespace
