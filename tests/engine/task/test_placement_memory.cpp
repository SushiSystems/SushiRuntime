/**************************************************************************/
/* test_placement_memory.cpp                                              */
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
 * @file test_placement_memory.cpp
 * @brief Runs the PlacementMemory contract against InMemoryPlacementMemory.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/task/in_memory_placement_memory.hpp>

#include "support/placement_memory_contract.hpp"
#include "support/made_up_handle.hpp"

using SushiRuntime::Internal::Graph::InMemoryPlacementMemory;

/** @brief Tests that a handle nobody recorded has no device. */
TEST(Small_PlacementMemoryContract, AnUnrecordedHandleHasNoDevice)
{
    const InMemoryPlacementMemory memory(std::vector<std::size_t>{0});
    TestSupport::expect_unrecorded_handle_has_no_device(memory, TestSupport::made_up_handle(1));
}

/** @brief Tests that a recorded handle names its device. */
TEST(Small_PlacementMemoryContract, ARecordedHandleNamesItsDevice)
{
    InMemoryPlacementMemory memory(std::vector<std::size_t>{0, 0});
    memory.record(TestSupport::made_up_handle(1), 1);
    TestSupport::expect_recorded_handle_names_device(memory, TestSupport::made_up_handle(1), 1);
}

/** @brief Tests that the device count is the number of devices the memory was built with. */
TEST(Small_PlacementMemoryContract, TheDeviceCountIsTheNumberOfDevicesItWasBuiltWith)
{
    const InMemoryPlacementMemory memory(std::vector<std::size_t>{0, 0, 1});
    TestSupport::expect_device_count(memory, 3);
}

/** @brief Tests that two devices in one context share it. */
TEST(Small_PlacementMemoryContract, TwoDevicesInOneContextShareIt)
{
    const InMemoryPlacementMemory memory(std::vector<std::size_t>{0, 0});
    TestSupport::expect_devices_share_a_context(memory, 0, 1);
}
