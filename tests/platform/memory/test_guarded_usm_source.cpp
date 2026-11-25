/**************************************************************************/
/* test_guarded_usm_source.cpp                                            */
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
 * @file test_guarded_usm_source.cpp
 * @brief Tests the guarded USMSource against the USMSource contract and its guard promises.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstring>
#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/device_access_scope.hpp>
#include <SushiRuntime/memory/guarded_device_pages.hpp>
#include <SushiRuntime/memory/guarded_usm_source.hpp>
#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>

#include "support/usm_source_contract.hpp"

using SushiRuntime::Internal::Memory::DeviceAccessScope;
using SushiRuntime::Internal::Memory::GuardedDevicePages;
using SushiRuntime::Internal::Memory::make_guarded_usm_source;
using SushiRuntime::Internal::Memory::make_host_usm_source;
using SushiRuntime::Internal::Memory::USMAllocator;
using SushiRuntime::Internal::Memory::USMKind;
using SushiRuntime::Internal::Memory::USMRequest;
using SushiRuntime::Internal::Memory::USMSource;

namespace
{
    /** @brief The bytes of the Device allocation each case makes. */
    constexpr std::size_t K_BYTES = 64;

    /** @brief Makes the source the contract runs on: the guarded source over a host source of one device. */
    struct GuardedSourceMaker
    {
        /** @brief Returns a guarded source over a host source of one device. */
        static std::unique_ptr<USMSource> make()
        {
            return make_guarded_usm_source(make_host_usm_source(), std::make_shared<GuardedDevicePages>());
        }
    };

    /** @brief Reads one byte through an address, as a stray host dereference does. */
    unsigned char read_byte(const void* address)
    {
        return *static_cast<const volatile unsigned char*>(address);
    }

    /** @brief Writes one byte through an address, as a stray host dereference does. */
    void write_byte(void* address)
    {
        *static_cast<volatile unsigned char*>(address) = 0x5A;
    }

    /** @brief Holds a guarded source over a host source of one device, and its pages. */
    class Small_GuardedUSMSource : public ::testing::Test
    {
        protected:
            /** @brief Returns a Device allocation of K_BYTES; the case frees it. */
            [[nodiscard]] void* allocate_device()
            {
                return source_->allocate(USMRequest{USMKind::Device, K_BYTES, 0, 0});
            }

            /** @brief The pages the source takes its Device memory from. */
            std::shared_ptr<GuardedDevicePages> pages_ = std::make_shared<GuardedDevicePages>();

            /** @brief The source under test. */
            std::unique_ptr<USMSource> source_ = make_guarded_usm_source(make_host_usm_source(), pages_);
    };

    /** @brief Names the fixture for the tests whose child process faults; a death test is medium. */
    using Medium_GuardedUSMSourceDeathTest = Small_GuardedUSMSource;
} // namespace

namespace TestSupport
{
    /** @brief Runs the USMSource contract on the guarded source. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Small_GuardedUSMSource, USMSourceContract, ::GuardedSourceMaker, );
} // namespace TestSupport

/** @brief Tests that a host read of a Device allocation outside runtime work kills the process. */
TEST_F(Medium_GuardedUSMSourceDeathTest, AHostReadOfADeviceAllocationFaults)
{
    void* const address = allocate_device();
    ASSERT_NE(address, nullptr);

    EXPECT_DEATH(static_cast<void>(read_byte(address)), "");

    source_->free(address, 0);
}

/** @brief Tests that a host write of a Device allocation outside runtime work kills the process. */
TEST_F(Medium_GuardedUSMSourceDeathTest, AHostWriteOfADeviceAllocationFaults)
{
    void* const address = allocate_device();
    ASSERT_NE(address, nullptr);

    EXPECT_DEATH(write_byte(address), "");

    source_->free(address, 0);
}

/** @brief Tests that a kernel's host form reaches a Device allocation while a scope holds the pages open. */
TEST_F(Small_GuardedUSMSource, AKernelInsideAnOpenScopeReachesADeviceAllocation)
{
    void* const address = allocate_device();
    ASSERT_NE(address, nullptr);
    unsigned char last = 0;

    {
        const DeviceAccessScope kernel(pages_.get());
        std::memset(address, 0x3C, K_BYTES);
        last = static_cast<unsigned char*>(address)[K_BYTES - 1];
    }

    EXPECT_EQ(last, 0x3C);
    source_->free(address, 0);
}

/** @brief Tests that USMAllocator::copy(), the call read_range() and write_range() make, round-trips a Device allocation. */
TEST(Small_GuardedUSMSourceCopy, ACopyThroughTheAllocatorRoundTripsADeviceAllocation)
{
    USMAllocator allocator(make_guarded_usm_source(make_host_usm_source(), std::make_shared<GuardedDevicePages>()));
    void* const device = allocator.allocate(USMRequest{USMKind::Device, K_BYTES, 0, 0});
    ASSERT_NE(device, nullptr);
    unsigned char written[K_BYTES];
    std::memset(written, 0x7E, K_BYTES);
    unsigned char back[K_BYTES] = {};

    allocator.copy(0, device, written, K_BYTES);
    allocator.copy(0, back, device, K_BYTES);

    EXPECT_EQ(std::memcmp(back, written, K_BYTES), 0);
    allocator.release(device, 0);
}

/** @brief Tests that a Shared request goes to the wrapped source and stays host addressable. */
TEST_F(Small_GuardedUSMSource, ASharedAllocationStaysHostAddressable)
{
    void* const address = source_->allocate(USMRequest{USMKind::Shared, K_BYTES, 0, 0});
    ASSERT_NE(address, nullptr);

    std::memset(address, 0x11, K_BYTES);

    EXPECT_EQ(static_cast<unsigned char*>(address)[K_BYTES - 1], 0x11);
    EXPECT_FALSE(pages_->holds(address));
    source_->free(address, 0);
}

/** @brief Tests that an address inside a Device allocation reports the Device kind. */
TEST_F(Small_GuardedUSMSource, ADeviceAllocationReportsTheDeviceKind)
{
    void* const address = allocate_device();
    ASSERT_NE(address, nullptr);

    EXPECT_EQ(source_->kind_of(static_cast<unsigned char*>(address) + K_BYTES - 1, 0), USMKind::Device);

    source_->free(address, 0);
}
