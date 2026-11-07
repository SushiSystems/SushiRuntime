/**************************************************************************/
/* test_usm_placement_memory.cpp                                          */
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
 * @file test_usm_placement_memory.cpp
 * @brief Runs the PlacementMemory contract against USMPlacementMemory over a USMAllocator.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/task/usm_placement_memory.hpp>

#include "support/placement_memory_contract.hpp"

using SushiRuntime::Internal::Graph::USMPlacementMemory;

/** @brief Holds an allocator over two host devices that share one context; a test records ownership with adopt(). */
class Small_USMPlacementMemoryContract : public ::testing::Test
{
    protected:
        Small_USMPlacementMemoryContract()
            : allocator_(std::make_unique<SushiRuntime::Internal::Memory::HostUSMSource>(2)),
              memory_(allocator_)
        {

        }

        /// @brief Allocates nothing.
        SushiRuntime::Internal::Memory::USMAllocator allocator_;
        /// @brief The memory under test.
        USMPlacementMemory memory_;
};

/** @brief Tests that an address nobody recorded has no device. */
TEST_F(Small_USMPlacementMemoryContract, AnUnrecordedAddressHasNoDevice)
{
    int cell = 0;
    TestSupport::expect_unrecorded_address_has_no_device(memory_, &cell);
}

/** @brief Tests that an adopted address names its device. */
TEST_F(Small_USMPlacementMemoryContract, AnAdoptedAddressNamesItsDevice)
{
    int cell = 0;
    allocator_.adopt(&cell, 1, sizeof(cell), std::nullopt);

    TestSupport::expect_recorded_address_names_device(memory_, &cell, 1);

    allocator_.forget(&cell);
}

/** @brief Tests that the device count is the number of entries of the allocator. */
TEST_F(Small_USMPlacementMemoryContract, TheDeviceCountIsTheNumberOfEntriesOfTheAllocator)
{
    TestSupport::expect_device_count(memory_, 2);
}

/** @brief Tests that two host devices share one context. */
TEST_F(Small_USMPlacementMemoryContract, TwoHostDevicesShareOneContext)
{
    TestSupport::expect_devices_share_a_context(memory_, 0, 1);
}
