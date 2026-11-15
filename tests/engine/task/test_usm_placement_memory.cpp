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
#include "support/handle_table.hpp"
#include "support/made_up_handle.hpp"

using SushiRuntime::Internal::Graph::USMPlacementMemory;

/** @brief Holds an allocator over two host devices that share one context; a test places each handle on a device. */
class Small_USMPlacementMemoryContract : public ::testing::Test
{
    protected:
        Small_USMPlacementMemoryContract()
            : allocator_(SushiRuntime::Internal::Memory::make_host_usm_source(2)),
              memory_(allocator_, handles_.resolver())
        {

        }

        /// @brief Allocates nothing.
        SushiRuntime::Internal::Memory::USMAllocator allocator_;
        /// @brief Gives each address of a test a handle that resolves to it.
        TestSupport::HandleTable handles_;
        /// @brief The memory under test.
        USMPlacementMemory memory_;
};

/** @brief Tests that a retired handle has no device. */
TEST_F(Small_USMPlacementMemoryContract, ARetiredHandleHasNoDevice)
{
    int cell = 0;
    const SushiRuntime::GlobalHandle handle = handles_.place(&cell, 1);
    ASSERT_EQ(handles_.retire(&cell), SushiRuntime::HandleStatus::Ok);

    TestSupport::expect_unrecorded_handle_has_no_device(memory_, handle);
}

/** @brief Tests that a handle that resolves to no instance has no device. */
TEST_F(Small_USMPlacementMemoryContract, AHandleWithNoInstanceHasNoDevice)
{
    TestSupport::expect_unrecorded_handle_has_no_device(memory_, TestSupport::made_up_handle(1));
}

/** @brief Tests that a handle names the device its instance lies on. */
TEST_F(Small_USMPlacementMemoryContract, AHandleNamesTheDeviceOfItsInstance)
{
    int cell = 0;

    TestSupport::expect_recorded_handle_names_device(memory_, handles_.place(&cell, 1), 1);
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
