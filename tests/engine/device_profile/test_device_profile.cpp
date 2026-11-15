/**************************************************************************/
/* test_device_profile.cpp                                              */
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
 * @file test_device_profile.cpp
 * @brief Tests that a DeviceProfile falls back to its base throughput and finds its paths by name.
 * @see tests/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/device_profile/device_profile.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_DeviceProfile, AKindWithNoEntryTakesTheBaseThroughput)
    {
        DeviceProfile profile;
        profile.base_throughput = 10.0;
        profile.throughputs["gemm"_kind] = 40.0;

        EXPECT_EQ(profile.throughput_for("gemm"_kind), 40.0);
        EXPECT_EQ(profile.throughput_for("scan"_kind), 10.0);
        EXPECT_EQ(profile.throughput_for(OperationKinds::K_GENERIC), 10.0);
    }

    TEST(Small_DeviceProfile, APathIsFoundByNameAndAnotherIsNot)
    {
        DeviceProfile profile;
        profile.set_path("pcie", PathProfile{5.0, 1.0, {}});

        ASSERT_NE(profile.find_path(TransferPathID::named("pcie")), nullptr);
        EXPECT_EQ(profile.find_path(TransferPathID::named("pcie"))->bandwidth, 5.0);
        EXPECT_EQ(profile.find_path(TransferPathID::named("nvlink")), nullptr);
    }

    TEST(Small_DeviceProfile, SetPathKeysTheEntryByTheIdentityOfItsNameAndKeepsTheName)
    {
        DeviceProfile profile;
        profile.set_path("pcie", PathProfile{5.0, 1.0, {}});
        profile.set_path("pcie", PathProfile{7.0, 2.0, {}});
        profile.set_path("nvlink", PathProfile{9.0, 3.0, {}});

        ASSERT_EQ(profile.paths.size(), 2U);
        EXPECT_EQ(profile.find_path(TransferPathID::named("pcie"))->bandwidth, 7.0);
        EXPECT_EQ(profile.find_path(TransferPathID::named("pcie"))->name, "pcie");
        EXPECT_EQ(profile.find_path(TransferPathID::named("nvlink"))->name, "nvlink");
    }
} // namespace
