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
 * @brief Tests KernelPointers, its slots and its scope: a node's pointers bound on its device.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>
#include <SushiRuntime/task/node.hpp>

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

        ASSERT_EQ(pointers.operands().size(), 1u);
        EXPECT_EQ(pointers.operands().untyped(0), &copy);
        EXPECT_EQ(pointers.resolved_binds(), 1u);
        EXPECT_EQ(pointers.home_binds(), 0u);
    }

    /** @brief Tests that bind_home() returns a table bound to another device to the home addresses. */
    TEST(Small_KernelPointers, BindHomeReturnsEverySlotToItsHomeAddress)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        int copy = 2;
        const GlobalHandle handle = minted(directory);
        place(directory, handle, &home, 0);
        place(directory, handle, &copy, 1);
        KernelPointers pointers({KernelPointerSlot{handle, &home, 0}}, directory);
        pointers.bind(1);

        pointers.bind_home();

        ASSERT_EQ(pointers.operands().size(), 1u);
        EXPECT_EQ(pointers.operands().untyped(0), &home);
        EXPECT_EQ(pointers.home_binds(), 1u);
    }

    /** @brief Tests that a node with no cached device binds its operands at their homes, on a dispatch and on a host run. */
    TEST(Small_KernelPointers, ANodeWithNoCachedDeviceBindsItsOperandsAtTheirHomes)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        int copy = 2;
        const GlobalHandle handle = minted(directory);
        place(directory, handle, &home, 0);
        place(directory, handle, &copy, 1);
        Node node;
        node.kernel_pointers_ = std::make_unique<KernelPointers>(
            std::vector<KernelPointerSlot>{KernelPointerSlot{handle, &home, 0}}, directory);
        node.kernel_pointers_->bind(1);
        ASSERT_LT(node.cached_queue_index_, 0);

        EXPECT_EQ(node.bound_pointers().operands().untyped(0), &home);

        node.kernel_pointers_->bind(1);
        const void* seen = nullptr;
        node.host_form = [&seen]()
        {
            const KernelPointers* const current = KernelPointerScope::current();
            seen = current != nullptr ? current->operands().untyped(0) : nullptr;
        };
        ASSERT_TRUE(node.run_on_host());
        EXPECT_EQ(seen, &home) << "the host run's body saw no operand scope, or the device copy";
    }

    /** @brief Tests that a host-accessible home stays visible after a host write left only the host space valid. */
    TEST(Small_KernelPointers, AHostAccessibleHomeIsVisibleWhateverTheMapSaysOfItsSpace)
    {
        HandleHome written_on_host;
        written_on_host.home = device_memory_space(1);
        written_on_host.resident = false;
        written_on_host.memory = HomeMemory::HostAccessible;
        HandleHome device_only = written_on_host;
        device_only.memory = HomeMemory::DeviceOnly;

        EXPECT_TRUE(home_is_host_visible(written_on_host, GlobalHandle::null()));
        EXPECT_FALSE(home_is_host_visible(device_only, GlobalHandle::null()))
            << "device memory whose map is stale is not read in place";
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

        EXPECT_EQ(pointers.operands().untyped(0), &home);
        EXPECT_EQ(pointers.home_binds(), 2u);
        EXPECT_EQ(pointers.resolved_binds(), 0u);
    }

    /** @brief Tests that a host-visible buffer with no instance on the node's device keeps its home address. */
    TEST(Small_KernelPointers, AHostVisibleBufferWithNoInstanceThereKeepsItsHomeAddress)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        const GlobalHandle handle = minted(directory);
        KernelPointers pointers({KernelPointerSlot{handle, &home, std::nullopt, true}}, directory);

        pointers.bind(1);

        EXPECT_EQ(pointers.operands().untyped(0), &home);
        EXPECT_EQ(pointers.resolved_binds(), 1u);
    }

    /** @brief Tests that a bind finding no instance of a device-only buffer stops, naming the handle and the device. A death test starts a child process. */
    TEST(Medium_KernelPointersDeathTest, ABindWithNoInstanceOnTheDeviceFailsItsVerify)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        const GlobalHandle handle = minted(directory);
        place(directory, handle, &home, 0);
        KernelPointers pointers({KernelPointerSlot{handle, &home, 0, false}}, directory);

        EXPECT_DEATH(pointers.bind(1),
                     "handle slot " + std::to_string(handle.slot()) + " generation " +
                         std::to_string(handle.generation()) + " has no instance on device 1");
    }

    /** @brief Records a described buffer homed on @p home with an instance there of @p kind. */
    GlobalHandle homed(Internal::Memory::ResidencyDirectory& directory, GlobalHandle handle,
                       MemorySpaceID home, void* address, std::optional<Internal::Memory::USMKind> kind)
    {
        EXPECT_EQ(directory.describe(handle, sizeof(int), Internal::Memory::ElementLayout{sizeof(int), alignof(int)}, home),
                  HandleStatus::Ok);
        EXPECT_EQ(directory.set_instance(handle, home, address, kind), HandleStatus::Ok);
        return handle;
    }

    /** @brief Tests that a slot made over host USM is host visible and knows its home device. */
    TEST(Small_KernelPointerSlot, HostUSMIsHostVisible)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        const GlobalHandle handle = homed(directory, minted(directory), device_memory_space(0), &home,
                                          Internal::Memory::USMKind::Host);

        const KernelPointerSlot slot = make_kernel_pointer_slot(directory, handle, &home, 2);

        EXPECT_TRUE(slot.host_visible);
        EXPECT_EQ(slot.home_device, std::optional<std::size_t>(0));
    }

    /** @brief Tests that a slot made from a handle alone takes its home address from the directory. */
    TEST(Small_KernelPointerSlot, ASlotMadeByHandleReadsItsHomeAddressFromTheDirectory)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        const GlobalHandle handle = homed(directory, minted(directory), device_memory_space(0), &home,
                                          Internal::Memory::USMKind::Device);

        const KernelPointerSlot slot = make_kernel_pointer_slot(directory, handle, 2);

        EXPECT_EQ(slot.home, static_cast<void*>(&home));
        EXPECT_EQ(slot.home_device, std::optional<std::size_t>(0));
    }

    /** @brief Tests that a slot made from the handle of a planned buffer with no home instance has no home address. */
    TEST(Small_KernelPointerSlot, ASlotMadeByHandleOfAPlannedBufferHasNoHomeAddress)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        const GlobalHandle handle = minted(directory);
        ASSERT_EQ(directory.describe(handle, sizeof(int), Internal::Memory::ElementLayout{sizeof(int), alignof(int)},
                                     host_memory_space()),
                  HandleStatus::Ok);

        const KernelPointerSlot slot = make_kernel_pointer_slot(directory, handle, 2);

        EXPECT_EQ(slot.home, nullptr);
        EXPECT_EQ(slot.home_device, std::nullopt);
    }

    /** @brief Tests that a slot made from the directory carries the element layout of its buffer. */
    TEST(Small_KernelPointerSlot, ASlotCarriesTheElementLayoutOfItsBuffer)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        const GlobalHandle handle = homed(directory, minted(directory), device_memory_space(0), &home,
                                          Internal::Memory::USMKind::Device);

        const KernelPointerSlot slot = make_kernel_pointer_slot(directory, handle, &home, 2);

        EXPECT_EQ(slot.layout, (ElementLayout{sizeof(int), alignof(int)}));
    }

    /** @brief Tests that a slot made over shared USM is host visible. */
    TEST(Small_KernelPointerSlot, SharedUSMIsHostVisible)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        const GlobalHandle handle = homed(directory, minted(directory), device_memory_space(0), &home,
                                          Internal::Memory::USMKind::Shared);

        EXPECT_TRUE(make_kernel_pointer_slot(directory, handle, &home, 2).host_visible);
    }

    /** @brief Tests that a slot made over device USM is not host visible. */
    TEST(Small_KernelPointerSlot, DeviceUSMIsNotHostVisible)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        const GlobalHandle handle = homed(directory, minted(directory), device_memory_space(0), &home,
                                          Internal::Memory::USMKind::Device);

        EXPECT_FALSE(make_kernel_pointer_slot(directory, handle, &home, 2).host_visible);
    }

    /** @brief Tests that borrowed memory of unknown kind is host visible and runtime memory of unknown kind is not. */
    TEST(Small_KernelPointerSlot, OnlyBorrowedMemoryOfUnknownKindIsHostVisible)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int borrowed_cell = 1;
        int owned_cell = 2;
        const MintOutcome borrowed_mint = directory.mint(HandleKind::Buffer, HandleFlags::Borrowed);
        ASSERT_EQ(borrowed_mint.status, HandleStatus::Ok);
        const GlobalHandle borrowed = homed(directory, borrowed_mint.handle, device_memory_space(0),
                                            &borrowed_cell, std::nullopt);
        const GlobalHandle owned = homed(directory, minted(directory), device_memory_space(0),
                                         &owned_cell, std::nullopt);

        EXPECT_TRUE(make_kernel_pointer_slot(directory, borrowed, &borrowed_cell, 2).host_visible);
        EXPECT_FALSE(make_kernel_pointer_slot(directory, owned, &owned_cell, 2).host_visible);
    }

    /** @brief Tests that a planned buffer, homed on the host with no instance yet, is not host visible. */
    TEST(Small_KernelPointerSlot, APlannedBufferWithNoHomeInstanceIsNotHostVisible)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        const GlobalHandle handle = minted(directory);
        ASSERT_EQ(directory.describe(handle, sizeof(int), Internal::Memory::ElementLayout{sizeof(int), alignof(int)},
                                     host_memory_space()),
                  HandleStatus::Ok);

        const KernelPointerSlot slot = make_kernel_pointer_slot(directory, handle, nullptr, 2);

        EXPECT_FALSE(slot.host_visible);
        EXPECT_EQ(slot.home_device, std::nullopt);
    }

    /** @brief Tests that the view of a bound table refuses an element type that is not its buffer's. */
    TEST(Small_KernelPointers, TheViewOfABoundTableRefusesAnElementTypeOfAnotherSize)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        int home = 1;
        const GlobalHandle handle = homed(directory, minted(directory), device_memory_space(0), &home,
                                          Internal::Memory::USMKind::Device);
        KernelPointers pointers({make_kernel_pointer_slot(directory, handle, &home, 2)}, directory);
        pointers.bind(0);

        const BoundOperands view = pointers.operands();

        EXPECT_EQ(view.at<int>(0).address, &home);
        EXPECT_EQ(view.at<double>(0).status, OperandStatus::ElementMismatch);
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
