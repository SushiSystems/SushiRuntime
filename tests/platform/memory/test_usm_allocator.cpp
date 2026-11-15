/**************************************************************************/
/* test_usm_allocator.cpp                                                 */
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
 * @file test_usm_allocator.cpp
 * @brief Tests USMAllocator's allocation kinds, release path and copy.
 * @see tests/platform/memory/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiLog/log/log_level.hpp>

#include <SushiRuntime/device_probe/device_probe.hpp>
#include <SushiRuntime/memory/host_usm_source.hpp>
#include <SushiRuntime/memory/sycl_queues.hpp>
#include <SushiRuntime/memory/sycl_usm_source.hpp>
#include <SushiRuntime/memory/usm_allocator.hpp>
#include <SushiRuntime/memory/usm_request.hpp>
#include <SushiRuntime/test_runner/log_capture.hpp>

#include "support/runtime_environment.hpp"

using SushiRuntime::Internal::Memory::make_host_usm_source;
using SushiRuntime::Internal::Memory::USMAllocator;
using SushiRuntime::Internal::Memory::USMKind;
using SushiRuntime::Internal::Memory::USMRequest;

namespace
{
    /** @brief Holds the length of every allocation the tests make, in bytes. */
    constexpr std::size_t K_BYTES = 256;

    /** @brief Holds the byte the tests write through an allocation. */
    constexpr unsigned char K_PATTERN = 0x5A;

    /** @brief Holds the number of devices of the host source the small tests allocate from. */
    constexpr std::size_t K_HOST_DEVICES = 2;

    /** @brief Returns the name a failure message prints for a kind. */
    const char* name_of(USMKind kind)
    {
        switch (kind)
        {
            case USMKind::Host:   return "Host";
            case USMKind::Shared: return "Shared";
            case USMKind::Device: return "Device";
        }
        return "unknown";
    }

    /** @brief Returns whether a device can allocate USM of a kind. */
    bool supports(const sycl::device& device, USMKind kind)
    {
        switch (kind)
        {
            case USMKind::Host:   return device.has(sycl::aspect::usm_host_allocations);
            case USMKind::Shared: return device.has(sycl::aspect::usm_shared_allocations);
            case USMKind::Device: return device.has(sycl::aspect::usm_device_allocations);
        }
        return false;
    }

    /** @brief Returns the bytes of a buffer as a vector, so one comparison covers all of them. */
    std::vector<unsigned char> bytes_of(const unsigned char* data, std::size_t count)
    {
        return std::vector<unsigned char>(data, data + count);
    }

    /** @brief Builds one queue per SYCL device and an allocator over them. */
    class UsmAllocatorFixture : public TestSupport::RuntimeTest
    {
        protected:
            /** @brief Skips when SYCL reports no device; otherwise builds the allocator. */
            void SetUp() override
            {
                if (!SushiRuntime::Test::has_any_sycl_device())
                    GTEST_SKIP() << "SYCL reports no device.";

                for (const sycl::device& device : sycl::device::get_devices())
                    queues_.emplace_back(device);
                allocator_ = std::make_unique<USMAllocator>(
                    SushiRuntime::Internal::Memory::make_sycl_usm_source(
                        SushiRuntime::Internal::Memory::make_sycl_queues(queues_)));
            }

            /** @brief Returns the first device that can allocate a kind, or nothing. */
            [[nodiscard]] std::optional<std::size_t> device_supporting(USMKind kind) const
            {
                for (std::size_t index = 0; index < queues_.size(); ++index)
                {
                    if (supports(queues_[index].get_device(), kind))
                        return index;
                }
                return std::nullopt;
            }

            /** @brief One queue per device; index i is the allocator's device i. */
            std::vector<sycl::queue> queues_;

            /** @brief The allocator under test, built over queues_. */
            std::unique_ptr<USMAllocator> allocator_;
    };

    /** @brief Fixture of the small tests: an allocator over a host source of two devices. */
    class HostAllocatorFixture : public ::testing::Test
    {
        protected:
            /** @brief Allocates K_BYTES of shared USM on the last device and checks it came. */
            [[nodiscard]] void* allocate_on_last_device()
            {
                void* const address = allocator_.allocate(
                    USMRequest{USMKind::Shared, K_BYTES, 0, K_HOST_DEVICES - 1});
                EXPECT_NE(address, nullptr);
                return address;
            }

            /** @brief The allocator under test, over a host source of K_HOST_DEVICES devices. */
            USMAllocator allocator_{make_host_usm_source(K_HOST_DEVICES)};
    };
} // namespace

/** @brief Fixture of the medium tests that read one allocator over every SYCL device. */
class Medium_UsmAllocator : public UsmAllocatorFixture
{
};

/** @brief Fixture of the medium tests that run once per kind. */
class Medium_UsmAllocatorKind : public UsmAllocatorFixture, public ::testing::WithParamInterface<USMKind>
{
};

/** @brief Fixture of the small tests that read one allocator over a host source. */
class Small_UsmAllocator : public HostAllocatorFixture
{
};

TEST_P(Medium_UsmAllocatorKind, AllocatesIsUsableAndIsReleased)
{
    const USMKind kind = GetParam();
    const std::optional<std::size_t> device = device_supporting(kind);
    if (!device.has_value())
        GTEST_SKIP() << "No SYCL device supports this kind.";

    void* const address = allocator_->allocate(USMRequest{kind, K_BYTES, 0, *device});
    ASSERT_NE(address, nullptr);

    if (kind == USMKind::Device)
    {
        // Device USM has no host address to write through; the device writes it.
        queues_[*device].memset(address, K_PATTERN, K_BYTES).wait_and_throw();
    }
    else
    {
        std::memset(address, K_PATTERN, K_BYTES);
        EXPECT_EQ(static_cast<unsigned char*>(address)[K_BYTES - 1], K_PATTERN);
    }

    SushiRuntime::TestRunner::LogCapture capture;
    allocator_->release(address, *device);
    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Error), 0u);
}

INSTANTIATE_TEST_SUITE_P(Kinds, Medium_UsmAllocatorKind,
                         ::testing::Values(USMKind::Host, USMKind::Shared, USMKind::Device),
                         [](const ::testing::TestParamInfo<USMKind>& info) { return std::string(name_of(info.param)); });

TEST_F(Small_UsmAllocator, TheFirstReleaseLogsNothing)
{
    void* const address = allocate_on_last_device();

    SushiRuntime::TestRunner::LogCapture capture;
    allocator_.release(address, K_HOST_DEVICES - 1);

    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Error), 0u);
}

TEST_F(Small_UsmAllocator, ANullReleaseLogsNothing)
{
    SushiRuntime::TestRunner::LogCapture capture;
    allocator_.release(nullptr, K_HOST_DEVICES);

    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Error), 0u);
}

TEST_F(Small_UsmAllocator, AReleaseOnADeviceTheAllocatorDoesNotHoldLogsAndFreesNothing)
{
    const auto memory = std::make_unique<unsigned char[]>(K_BYTES);
    std::memset(memory.get(), K_PATTERN, K_BYTES);

    {
        SushiRuntime::TestRunner::LogCapture capture;
        allocator_.release(memory.get(), K_HOST_DEVICES);
        EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Error), 1u);
    }

    EXPECT_EQ(bytes_of(memory.get(), K_BYTES), std::vector<unsigned char>(K_BYTES, K_PATTERN));
}

TEST_F(Small_UsmAllocator, CopyMovesTheBytesOnTheNamedDevice)
{
    void* const address = allocate_on_last_device();
    std::vector<unsigned char> source(K_BYTES, K_PATTERN);

    allocator_.copy(K_HOST_DEVICES - 1, address, source.data(), K_BYTES);

    EXPECT_EQ(bytes_of(static_cast<unsigned char*>(address), K_BYTES), source);
    allocator_.release(address, K_HOST_DEVICES - 1);
}

/** @brief Names the fixture for the tests that stop the process; see README.md. */
using Medium_UsmAllocatorDeathTest = Medium_UsmAllocator;

TEST_F(Medium_UsmAllocatorDeathTest, ABadDeviceIndexStopsWithTheIndexAndTheCount)
{
    const std::size_t count = queues_.size();
    const std::size_t bad_index = count + 7;
    int external = 0;
    const std::string pattern = "USM device index " + std::to_string(bad_index) +
                                " is out of range: the allocator holds " +
                                std::to_string(count) + " device";

    const USMRequest request{USMKind::Shared, 64, 0, bad_index};
    EXPECT_DEATH(static_cast<void>(allocator_->allocate(request)), pattern);
    EXPECT_DEATH(allocator_->copy(bad_index, &external, &external, sizeof(external)), pattern);
}

TEST(Small_UsmAllocatorFailure, LogsTheRefusedRequest)
{
    USMAllocator allocator(make_host_usm_source());
    const std::size_t past_the_memory = std::numeric_limits<std::size_t>::max();

    SushiRuntime::TestRunner::LogCapture capture;
    void* const address =
        allocator.allocate(USMRequest{USMKind::Shared, past_the_memory, 64, 0});

    EXPECT_EQ(address, nullptr);
    EXPECT_EQ(capture.count_at(SushiLog::LogLevel::Warn), 1u);
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, std::to_string(past_the_memory)));
    EXPECT_TRUE(capture.has_message(SushiLog::LogLevel::Warn, "alignment 64"));
    EXPECT_EQ(capture.count_in("memory"), 1u);
}
