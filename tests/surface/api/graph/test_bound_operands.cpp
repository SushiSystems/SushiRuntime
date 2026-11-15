/**************************************************************************/
/* test_bound_operands.cpp                                                 */
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
 * @file test_bound_operands.cpp
 * @brief Tests bound_operands(): a task body bound by handle reads the instances its node bound on its device.
 * @see tests/surface/api/graph/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/api/graph/bound_operands.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>

using namespace SushiRuntime;
using Internal::Graph::KernelPointerScope;
using Internal::Graph::KernelPointers;

namespace
{
    /** @brief The number of elements of each instance. */
    constexpr std::size_t K_COUNT = 8;

    /** @brief Mints a buffer handle in @p directory homed on device 0 at @p home. */
    GlobalHandle homed_on_device_zero(Internal::Memory::ResidencyDirectory& directory, void* home)
    {
        const MintOutcome outcome = directory.mint(HandleKind::Buffer, HandleFlags::None);
        EXPECT_EQ(outcome.status, HandleStatus::Ok);
        const MemorySpaceID space = Internal::Graph::device_memory_space(0);
        EXPECT_EQ(directory.describe(outcome.handle, K_COUNT * sizeof(int),
                                     Internal::Memory::ElementLayout{sizeof(int), alignof(int)}, space),
                  HandleStatus::Ok);
        EXPECT_EQ(directory.set_instance(outcome.handle, space, home), HandleStatus::Ok);
        return outcome.handle;
    }

    /** @brief Builds the pointer table of @p handles, each home read from @p directory. */
    KernelPointers table_by_handle(const Internal::Memory::ResidencyDirectory& directory,
                                   const std::vector<GlobalHandle>& handles)
    {
        std::vector<Internal::Graph::KernelPointerSlot> slots;
        for (const GlobalHandle handle : handles)
            slots.push_back(Internal::Graph::make_kernel_pointer_slot(directory, handle, 2));
        return KernelPointers(std::move(slots), directory);
    }

    /** @brief Stands for one kernel a task body submits: out[i] = in[i] + 1 over the bound operands. */
    void add_one(const BoundOperands& operands)
    {
        const int* source = operands.as<const int>(0);
        int* target = operands.as<int>(1);
        for (std::size_t index = 0; index < K_COUNT; ++index)
            target[index] = source[index] + 1;
    }

    /** @brief Tests that outside a task body there are no bound operands. */
    TEST(Small_BoundOperands, OutsideABodyTheViewIsEmpty)
    {
        EXPECT_TRUE(bound_operands().is_empty());
    }

    /**
     * @brief Tests that a task on device 1 reads the instance copied there, in every kernel it submits.
     *
     * The input's home on device 0 holds stale bytes, the copy on device 1 the right ones.
     */
    TEST(Small_BoundOperands, ATaskOnDeviceOneReadsTheCopiedInstanceInEveryKernel)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        std::vector<int> home(K_COUNT, -1);
        std::vector<int> copied(K_COUNT, 6);
        std::vector<int> out_home(K_COUNT, 0);
        std::vector<int> out_copied(K_COUNT, 0);
        const GlobalHandle input = homed_on_device_zero(directory, home.data());
        const GlobalHandle output = homed_on_device_zero(directory, out_home.data());
        ASSERT_EQ(directory.set_instance(input, Internal::Graph::device_memory_space(1), copied.data()),
                  HandleStatus::Ok);
        ASSERT_EQ(directory.set_instance(output, Internal::Graph::device_memory_space(1), out_copied.data()),
                  HandleStatus::Ok);
        KernelPointers pointers = table_by_handle(directory, {input, output});

        pointers.bind(1);
        {
            const KernelPointerScope scope(pointers);
            add_one(bound_operands());
            add_one(bound_operands());
        }

        EXPECT_EQ(out_copied, std::vector<int>(K_COUNT, 7));
        EXPECT_EQ(out_home, std::vector<int>(K_COUNT, 0)) << "the home instance is not touched";
    }
} // namespace