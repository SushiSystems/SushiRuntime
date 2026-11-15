/**************************************************************************/
/* test_kernel_pointers.cpp                                               */
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
 * @file test_kernel_pointers.cpp
 * @brief Tests KernelPointers, the per-node pointer table bound on the node's device, and its scope.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Mints a buffer handle in @p directory with no instance. */
    GlobalHandle minted(Internal::Memory::ResidencyDirectory& directory)
    {
        const MintOutcome outcome = directory.mint(HandleKind::Buffer, HandleFlags::None);
        EXPECT_EQ(outcome.status, HandleStatus::Ok);
        return outcome.handle;
    }

    /** @brief Records an instance of @p handle at @p address on @p device. */
    void place(Internal::Memory::ResidencyDirectory& directory, GlobalHandle handle, void* address,
               std::size_t device)
    {
        EXPECT_EQ(directory.set_instance(handle, device_memory_space(device), address), HandleStatus::Ok);
    }

    /** @brief Tests that a node on device 1 receives the copied instance, not the home address. */
    TEST(Small_KernelPointers, ANodeOnAnotherDeviceReceivesTheInstanceThere)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        int copy = 2;
        const GlobalHandle handle = minted(directory);
        place(directory, handle, &home, 0);
        place(directory, handle, &copy, 1);
        KernelPointers pointers({KernelPointerSlot{handle, &home, 0}}, directory);

        pointers.bind(1);

        ASSERT_EQ(pointers.size(), 1u);
        EXPECT_EQ(pointers.table()[0], &copy);
        EXPECT_EQ(pointers.resolved_binds(), 1u);
        EXPECT_EQ(pointers.home_binds(), 0u);
    }

    /** @brief Tests that a node on the home device keeps the home address and reads no directory. */
    TEST(Small_KernelPointers, ANodeOnTheHomeDeviceTakesTheFastPath)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        int copy = 2;
        const GlobalHandle handle = minted(directory);
        place(directory, handle, &home, 0);
        place(directory, handle, &copy, 1);
        KernelPointers pointers({KernelPointerSlot{handle, &home, 0}}, directory);

        pointers.bind(0);
        pointers.bind(0);

        EXPECT_EQ(pointers.table()[0], &home);
        EXPECT_EQ(pointers.home_binds(), 2u);
        EXPECT_EQ(pointers.resolved_binds(), 0u);
    }

    /** @brief Tests that a buffer with no instance on the node's device keeps its home address. */
    TEST(Small_KernelPointers, ABufferWithNoInstanceThereKeepsItsHomeAddress)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        const GlobalHandle handle = minted(directory);
        KernelPointers pointers({KernelPointerSlot{handle, &home, std::nullopt}}, directory);

        pointers.bind(1);

        EXPECT_EQ(pointers.table()[0], &home);
        EXPECT_EQ(pointers.resolved_binds(), 1u);
    }

    /** @brief Tests that a scope hands its table to current() and restores the one before it. */
    TEST(Small_KernelPointers, AScopeOpensItsTableAndRestoresThePreviousOne)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        KernelPointers outer({}, directory);
        KernelPointers inner({}, directory);

        EXPECT_EQ(KernelPointerScope::current(), nullptr);
        {
            const KernelPointerScope outer_scope(outer);
            {
                const KernelPointerScope inner_scope(inner);
                EXPECT_EQ(KernelPointerScope::current(), &inner);
            }
            EXPECT_EQ(KernelPointerScope::current(), &outer);
        }
        EXPECT_EQ(KernelPointerScope::current(), nullptr);
    }
} // namespace
