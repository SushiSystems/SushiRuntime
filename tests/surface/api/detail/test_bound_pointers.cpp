/**************************************************************************/
/* test_bound_pointers.cpp                                                */
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
 * @file test_bound_pointers.cpp
 * @brief Tests bound_pointers(): a capture launch receives the instances its node bound on its device.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/api/detail/bound_pointers.hpp>
#include <SushiRuntime/api/detail/emit_captured.hpp>
#include <SushiRuntime/handle/global_handle.hpp>
#include <SushiRuntime/memory/residency_directory.hpp>
#include <SushiRuntime/task/handle_location.hpp>
#include <SushiRuntime/task/kernel_pointers.hpp>

using namespace SushiRuntime;
using Internal::Graph::KernelPointerScope;
using Internal::Graph::KernelPointerSlot;
using Internal::Graph::KernelPointers;

namespace
{
    /** @brief The number of elements of each instance. */
    constexpr std::size_t K_COUNT = 8;

    /** @brief Mints a buffer handle in @p directory. */
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
        EXPECT_EQ(directory.set_instance(handle, Internal::Graph::device_memory_space(device), address),
                  HandleStatus::Ok);
    }

    /** @brief Tests that with no node dispatching, the recorded pointers are handed on. */
    TEST(Small_BoundPointers, WithNoBoundTableTheRecordedPointersAreKept)
    {
        int cell = 0;
        const std::tuple<int*> recorded(&cell);

        EXPECT_EQ(Detail::bound_pointers(recorded), recorded);
    }

    /**
     * @brief Tests that a node dispatched on device 1 reads the instance copied there.
     *
     * The input's home instance on device 0 holds stale bytes, the planned copy on device 1 the
     * right ones; the output lives on device 1. The kernel must read the copy.
     */
    TEST(Small_BoundPointers, ANodeOnDeviceOneReadsTheCopiedInstance)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        std::vector<int> home(K_COUNT, -1);
        std::vector<int> copied(K_COUNT);
        std::vector<int> out(K_COUNT, 0);
        for (std::size_t index = 0; index < K_COUNT; ++index)
            copied[index] = static_cast<int>(index) * 3;
        const GlobalHandle input = minted(directory);
        const GlobalHandle output = minted(directory);
        place(directory, input, home.data(), 0);
        place(directory, input, copied.data(), 1);
        place(directory, output, out.data(), 1);

        const Detail::NodeDescription description = Detail::captured_node(
            {}, {}, K_COUNT, std::make_tuple(static_cast<const int*>(home.data()), out.data()),
            std::vector<GlobalHandle>{input, output},
            [](std::size_t index, const int* source, int* target) { target[index] = source[index] + 1; });
        KernelPointers pointers({KernelPointerSlot{input, home.data(), 0},
                                 KernelPointerSlot{output, out.data(), 1}},
                                directory);

        pointers.bind(1);
        {
            const KernelPointerScope scope(pointers);
            description.host_form();
        }

        for (std::size_t index = 0; index < K_COUNT; ++index)
            EXPECT_EQ(out[index], static_cast<int>(index) * 3 + 1) << "element " << index;
        EXPECT_EQ(pointers.resolved_binds(), 1u);
    }

    /** @brief Tests that a node on the home device of every pointer reads the home instances with no lookup. */
    TEST(Small_BoundPointers, ANodeOnTheHomeDeviceReadsTheHomeInstances)
    {
        Internal::Memory::ResidencyDirectory directory(0);
        std::vector<int> home(K_COUNT, 4);
        std::vector<int> copied(K_COUNT, -1);
        std::vector<int> out(K_COUNT, 0);
        const GlobalHandle input = minted(directory);
        const GlobalHandle output = minted(directory);
        place(directory, input, home.data(), 0);
        place(directory, input, copied.data(), 1);
        place(directory, output, out.data(), 0);

        const Detail::NodeDescription description = Detail::captured_node(
            {}, {}, K_COUNT, std::make_tuple(static_cast<const int*>(home.data()), out.data()),
            std::vector<GlobalHandle>{input, output},
            [](std::size_t index, const int* source, int* target) { target[index] = source[index] + 1; });
        KernelPointers pointers({KernelPointerSlot{input, home.data(), 0},
                                 KernelPointerSlot{output, out.data(), 0}},
                                directory);

        pointers.bind(0);
        {
            const KernelPointerScope scope(pointers);
            description.host_form();
        }

        EXPECT_EQ(out, std::vector<int>(K_COUNT, 5));
        EXPECT_EQ(pointers.home_binds(), 1u);
        EXPECT_EQ(pointers.resolved_binds(), 0u);
    }
} // namespace
