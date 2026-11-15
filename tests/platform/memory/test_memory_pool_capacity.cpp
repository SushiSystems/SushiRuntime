/**************************************************************************/
/* test_memory_pool_capacity.cpp                                          */
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
 * @file test_memory_pool_capacity.cpp
 * @brief Tests that usable_pool_bytes counts a pool shared by an integrated GPU once.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/host_memory_space.hpp>
#include <SushiRuntime/memory/memory_pool_capacity.hpp>

#include "fake_memory_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Memory;

namespace
{
    TEST(Small_MemoryPoolCapacity, SharedPoolOfAnIntegratedGPUCountedOnce)
    {
        const FakeMemoryProbe host_probe(1000, 800);
        const FakeMemoryProbe discrete_probe(500, 450);
        const HostMemorySpace host(HostMemorySpaceDescription{MemorySpaceID(0), 0, 0, {}}, host_probe);
        const DeviceMemorySpace integrated(DeviceMemorySpaceDescription{MemorySpaceID(1), 0, {}},
                                           host_probe);
        const DeviceMemorySpace discrete(DeviceMemorySpaceDescription{MemorySpaceID(2), 1, {}},
                                         discrete_probe);

        EXPECT_EQ(usable_pool_bytes({&host, &integrated, &discrete}), 800U + 450U);
        EXPECT_EQ(usable_pool_bytes({&integrated, &host}), 800U);
    }

    TEST(Small_MemoryPoolCapacity, NoSpacesHaveNoBytes)
    {
        EXPECT_EQ(usable_pool_bytes({}), 0U);
    }
} // namespace
