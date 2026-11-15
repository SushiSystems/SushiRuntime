/**************************************************************************/
/* test_device_memory_space.cpp                                           */
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
 * @file test_device_memory_space.cpp
 * @brief Tests that a device memory space reports the reserve its probe measured.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/memory/device_memory_space.hpp>

#include "fake_memory_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    TEST(Small_DeviceMemorySpace, ReportsTheMeasuredReserve)
    {
        const FakeMemoryProbe probe(1000, 700);
        const DeviceMemorySpace space(
            DeviceMemorySpaceDescription{MemorySpaceID(1), 1, {MemorySpaceID(3), MemorySpaceID(0)}},
            probe);

        EXPECT_EQ(probe.probe_count(), 1);
        EXPECT_EQ(space.id(), MemorySpaceID(1));
        EXPECT_EQ(space.kind_name(), "device");
        EXPECT_EQ(space.capacity_bytes(), 1000U);
        EXPECT_EQ(space.reserve_bytes(), 300U);
        EXPECT_EQ(space.shared_pool(), 1U);
        ASSERT_EQ(space.direct_targets().size(), 2U);
        EXPECT_EQ(space.direct_targets()[0], MemorySpaceID(0));
        EXPECT_EQ(space.direct_targets()[1], MemorySpaceID(3));
    }

    TEST(Small_DeviceMemorySpace, FreeAboveTotalLeavesNoReserve)
    {
        const FakeMemoryProbe probe(100, 150);
        const DeviceMemorySpace space(DeviceMemorySpaceDescription{MemorySpaceID(2), 2, {}}, probe);
        EXPECT_EQ(space.reserve_bytes(), 0U);
    }
} // namespace
