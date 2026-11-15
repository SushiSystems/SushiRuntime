/**************************************************************************/
/* test_same_context_path.cpp                                             */
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
 * @file test_same_context_path.cpp
 * @brief Tests that SameContextPath connects spaces of one context and copies the bytes.
 * @see tests/engine/transfer/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/memory/device_memory_space.hpp>
#include <SushiRuntime/transfer/same_context_path.hpp>

#include "support/fake_transfer_path.hpp"
#include "support/transfer_path_doubles.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;
using SushiRuntime::Internal::Memory::DeviceMemorySpace;
using SushiRuntime::Internal::Memory::DeviceMemorySpaceDescription;

namespace
{
    const MemorySpaceID K_DEVICE_0(1);
    const MemorySpaceID K_DEVICE_1(2);
    const MemorySpaceID K_DEVICE_2(3);

    /** @brief Holds device 0 and 1 in one context and device 2 in another. */
    class Medium_SameContextPath : public ::testing::Test
    {
        protected:
            void SetUp() override
            {
                if (!queues_.available())
                    GTEST_SKIP() << "SYCL offers no CPU device.";
            }

            FixedMemoryProbe probe_;
            DeviceMemorySpace device_0_{DeviceMemorySpaceDescription{K_DEVICE_0, 1, {K_DEVICE_1}}, probe_};
            DeviceMemorySpace device_1_{DeviceMemorySpaceDescription{K_DEVICE_1, 2, {K_DEVICE_0}}, probe_};
            DeviceMemorySpace device_2_{DeviceMemorySpaceDescription{K_DEVICE_2, 3, {}}, probe_};
            CPUSpaceQueues queues_;
            RecordingTransferObserver observer_;
    };

    TEST_F(Medium_SameContextPath, ConnectsTwoSpacesOfOneContextAndNoOthers)
    {
        const SameContextPath subject(queues_);

        EXPECT_TRUE(subject.connects(device_0_, device_1_));
        EXPECT_TRUE(subject.connects(device_1_, device_0_));
        EXPECT_FALSE(subject.connects(device_0_, device_2_));
        EXPECT_FALSE(subject.connects(device_2_, device_0_));
    }

    TEST_F(Medium_SameContextPath, CopiesTheBytesAndReportsItsNameForTheCopy)
    {
        SameContextPath subject(queues_, &observer_);
        const std::array<unsigned char, 6> from{1, 2, 3, 4, 5, 6};
        std::array<unsigned char, 6> to{};

        subject.copy(TransferRequest{K_DEVICE_0, K_DEVICE_1, from.data(), to.data(), to.size()}, {}).value().wait();

        EXPECT_EQ(to, from);
        ASSERT_EQ(observer_.heard().size(), 1U);
        EXPECT_EQ(observer_.heard()[0].path, "same_context");
        EXPECT_EQ(observer_.heard()[0].id, SameContextPath::K_ID);
        EXPECT_EQ(subject.id(), SameContextPath::K_ID);
        EXPECT_EQ(observer_.heard()[0].source, K_DEVICE_0);
        EXPECT_EQ(observer_.heard()[0].destination, K_DEVICE_1);
        EXPECT_EQ(observer_.heard()[0].bytes, to.size());
    }

    TEST_F(Medium_SameContextPath, CopiesWithoutAnObserver)
    {
        SameContextPath subject(queues_);
        const std::array<unsigned char, 3> from{9, 8, 7};
        std::array<unsigned char, 3> to{};

        subject.copy(TransferRequest{K_DEVICE_0, K_DEVICE_1, from.data(), to.data(), to.size()}, {}).value().wait();

        EXPECT_EQ(to, from);
    }
} // namespace
