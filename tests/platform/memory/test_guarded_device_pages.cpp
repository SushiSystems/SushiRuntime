/**************************************************************************/
/* test_guarded_device_pages.cpp                                          */
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
 * @file test_guarded_device_pages.cpp
 * @brief Tests the window GuardedDevicePages opens and closes over its allocations, and a CPU device's kernel on them.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/memory/device_access_scope.hpp>
#include <SushiRuntime/memory/guarded_device_pages.hpp>

using SushiRuntime::Internal::Memory::DeviceAccessScope;
using SushiRuntime::Internal::Memory::GuardedDevicePages;

namespace
{
    /** @brief The bytes of the allocation each case makes. */
    constexpr std::size_t K_BYTES = 32;

    /** @brief An alignment of 1 GiB, above the page size of every supported system. */
    constexpr std::size_t K_ABOVE_ANY_PAGE = std::size_t{1} << 30;

    /** @brief Reads one byte through an address. */
    unsigned char read_byte(const void* address)
    {
        return *static_cast<const volatile unsigned char*>(address);
    }

    /** @brief Writes one byte through an address. */
    void write_byte(void* address, unsigned char value)
    {
        *static_cast<volatile unsigned char*>(address) = value;
    }

    /** @brief The elements the CPU device's kernel writes. */
    constexpr std::size_t K_ELEMENTS = 1024;

    /** @brief Returns a queue on a CPU device, or none when the machine has no CPU device. */
    std::optional<sycl::queue> cpu_queue()
    {
        try
        {
            return sycl::queue(sycl::cpu_selector_v);
        }
        catch (const sycl::exception&)
        {
            return std::nullopt;
        }
    }
} // namespace

/** @brief Tests that after two opens and one close the pages are still reachable. */
TEST(Small_GuardedDevicePages, ANestedCloseLeavesThePagesOpen)
{
    GuardedDevicePages pages;
    void* const address = pages.allocate(K_BYTES, 0);
    ASSERT_NE(address, nullptr);
    pages.open();
    pages.open();

    pages.close();
    write_byte(address, 0x42);

    EXPECT_EQ(read_byte(address), 0x42);
    EXPECT_TRUE(pages.is_open());
    pages.close();
    pages.release(address);
}

/** @brief Tests that an allocation made while the window is open faults once the window closes. A death test starts a child process. */
TEST(Medium_GuardedDevicePagesDeathTest, AnAllocationMadeInsideAWindowClosesWithIt)
{
    GuardedDevicePages pages;
    pages.open();
    void* const address = pages.allocate(K_BYTES, 0);
    ASSERT_NE(address, nullptr);
    write_byte(address, 0x24);

    pages.close();

    EXPECT_DEATH(static_cast<void>(read_byte(address)), "");
    pages.release(address);
}

/** @brief Tests that an alignment above the page size is refused with null. */
TEST(Small_GuardedDevicePages, RefusesAnAlignmentAboveThePageSize)
{
    GuardedDevicePages pages;

    EXPECT_EQ(pages.allocate(K_BYTES, K_ABOVE_ANY_PAGE), nullptr);
}

/** @brief Tests that a CPU device's kernel writes guarded pages and its queue copies them while the gate is open. */
TEST(Medium_GuardedDevicePagesOnCPUDevice, AKernelAndACopyReachThePagesWhileTheGateIsOpen)
{
    std::optional<sycl::queue> queue = cpu_queue();
    if (!queue)
        GTEST_SKIP() << "the machine has no SYCL CPU device";
    GuardedDevicePages pages;
    auto* const cells = static_cast<int*>(pages.allocate(K_ELEMENTS * sizeof(int), alignof(int)));
    ASSERT_NE(cells, nullptr);
    std::vector<int> copied(K_ELEMENTS, -1);

    {
        const DeviceAccessScope window(&pages);
        queue->submit([cells](sycl::handler& handler)
        {
            handler.parallel_for(sycl::range<1>(K_ELEMENTS), [cells](sycl::id<1> index)
            {
                cells[index[0]] = static_cast<int>(index[0]) * 3;
            });
        }).wait_and_throw();
        queue->memcpy(copied.data(), cells, K_ELEMENTS * sizeof(int)).wait_and_throw();
    }

    for (std::size_t index = 0; index < K_ELEMENTS; ++index)
        ASSERT_EQ(copied[index], static_cast<int>(index) * 3) << "at element " << index;
    pages.release(cells);
}
