/**************************************************************************/
/* test_host_staged_path.cpp                                              */
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
 * @file test_host_staged_path.cpp
 * @brief Tests that HostStagedPath connects the spaces no other path connects, copies matching bytes in chunks and reports its name.
 * @see tests/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/memory/host_memory_space.hpp>
#include <SushiRuntime/transfer/file_path.hpp>
#include <SushiRuntime/transfer/host_staged_path.hpp>

#include "support/fake_transfer_path.hpp"
#include "support/transfer_path_doubles.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Memory::DeviceMemorySpace;
using SushiRuntime::Internal::Memory::DeviceMemorySpaceDescription;
using SushiRuntime::Internal::Memory::HostMemorySpace;
using SushiRuntime::Internal::Memory::HostMemorySpaceDescription;

namespace
{
    const MemorySpaceID K_HOST(0);
    const MemorySpaceID K_DEVICE_0(1);
    const MemorySpaceID K_DEVICE_1(2);
    constexpr std::size_t K_SLOT_BYTES = 64;
    constexpr std::size_t K_SLOT_COUNT = 2;

    /** @brief Holds a host space and two devices that share no context, over a ring of small slots. */
    class Medium_HostStagedPath : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                if (!cpu_queues_.available())
                    GTEST_SKIP() << "SYCL offers no CPU device.";
            }

            /** @brief Returns bytes that differ from one position to the next. */
            static std::vector<std::uint8_t> pattern(std::size_t bytes)
            {
                std::vector<std::uint8_t> data(bytes);
                std::iota(data.begin(), data.end(), static_cast<std::uint8_t>(7));
                return data;
            }

            FixedMemoryProbe probe_;
            HostMemorySpace host_{HostMemorySpaceDescription{K_HOST, 0, 0, {}}, probe_};
            DeviceMemorySpace device_0_{DeviceMemorySpaceDescription{K_DEVICE_0, 1, {}}, probe_};
            DeviceMemorySpace device_1_{DeviceMemorySpaceDescription{K_DEVICE_1, 2, {}}, probe_};
            CPUSpaceQueues cpu_queues_;
            QueuesWithoutSpace queues_{cpu_queues_, K_HOST};
            HeapPinnedHostMemory memory_;
            StagingRing ring_{memory_, StagingRingShape{K_SLOT_BYTES, K_SLOT_COUNT}};
            RecordingTransferObserver observer_;
            HostStagedPath subject_{queues_, ring_, &observer_};
    };

    TEST_F(Medium_HostStagedPath, ConnectsAnyTwoDevicesAndTheHostWithADevice)
    {
        EXPECT_TRUE(subject_.connects(device_0_, device_1_));
        EXPECT_TRUE(subject_.connects(host_, device_0_));
        EXPECT_TRUE(subject_.connects(device_0_, host_));
        EXPECT_FALSE(subject_.connects(host_, host_));
        EXPECT_EQ(subject_.name(), "host_staged");
        EXPECT_EQ(subject_.id(), HostStagedPath::K_ID);
        EXPECT_NE(HostStagedPath::K_ID, FilePath::K_ID);
    }

    TEST_F(Medium_HostStagedPath, CopiesBetweenTwoSpacesWithNoDirectPathAndMatchesTheBytes)
    {
        const std::size_t sizes[] = {K_SLOT_BYTES / 2, K_SLOT_BYTES, K_SLOT_BYTES + 1, K_SLOT_BYTES * K_SLOT_COUNT,
                                     K_SLOT_BYTES * 5 + 3};
        for (const std::size_t bytes : sizes)
        {
            const std::vector<std::uint8_t> from = pattern(bytes);
            std::vector<std::uint8_t> to(bytes, 0);

            subject_.copy(TransferRequest{K_DEVICE_0, K_DEVICE_1, from.data(), to.data(), bytes}, {}).value().wait();

            EXPECT_EQ(to, from) << "bytes: " << bytes;
        }
    }

    TEST_F(Medium_HostStagedPath, ReusesTheRingAcrossCopiesAndAllocatesItOnce)
    {
        const std::vector<std::uint8_t> from = pattern(K_SLOT_BYTES * 9);
        std::vector<std::uint8_t> to(from.size(), 0);

        subject_.copy(TransferRequest{K_DEVICE_0, K_DEVICE_1, from.data(), to.data(), from.size()}, {}).value().wait();
        subject_.copy(TransferRequest{K_DEVICE_1, K_DEVICE_0, to.data(), to.data(), 0}, {}).value().wait();

        EXPECT_EQ(to, from);
        EXPECT_EQ(memory_.allocated(), static_cast<int>(K_SLOT_COUNT));
    }

    TEST_F(Medium_HostStagedPath, ReportsHostStagedOncePerCopy)
    {
        const std::vector<std::uint8_t> from = pattern(K_SLOT_BYTES * 3);
        std::vector<std::uint8_t> to(from.size(), 0);

        subject_.copy(TransferRequest{K_DEVICE_0, K_DEVICE_1, from.data(), to.data(), from.size()}, {}).value().wait();

        ASSERT_EQ(observer_.heard().size(), 1U);
        EXPECT_EQ(observer_.heard()[0].path, "host_staged");
        EXPECT_EQ(observer_.heard()[0].id, HostStagedPath::K_ID);
        EXPECT_EQ(observer_.heard()[0].source, K_DEVICE_0);
        EXPECT_EQ(observer_.heard()[0].destination, K_DEVICE_1);
        EXPECT_EQ(observer_.heard()[0].bytes, from.size());
    }

    TEST_F(Medium_HostStagedPath, CopiesToAndFromTheHostWithOneMemcpyAndNoSlot)
    {
        const std::vector<std::uint8_t> from = pattern(K_SLOT_BYTES * 4 + 5);
        std::vector<std::uint8_t> on_host(from.size(), 0);
        std::vector<std::uint8_t> back(from.size(), 0);

        subject_.copy(TransferRequest{K_DEVICE_0, K_HOST, from.data(), on_host.data(), from.size()}, {}).value().wait();
        subject_.copy(TransferRequest{K_HOST, K_DEVICE_1, on_host.data(), back.data(), from.size()}, {}).value().wait();

        EXPECT_EQ(on_host, from);
        EXPECT_EQ(back, from);
        EXPECT_EQ(memory_.allocated(), 0);
        EXPECT_EQ(observer_.heard().size(), 2U);
    }

    TEST_F(Medium_HostStagedPath, RefusesACopyWhenNeitherSpaceHasAQueue)
    {
        const QueuesWithoutSpace none(queues_, K_DEVICE_0);
        const QueuesWithoutSpace neither(none, K_DEVICE_1);
        HostStagedPath path(neither, ring_);
        std::uint8_t byte = 0;

        const Result<sycl::event> refused = path.copy(TransferRequest{K_DEVICE_0, K_DEVICE_1, &byte, &byte, 1}, {});

        ASSERT_FALSE(refused);
        EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
    }
} // namespace
