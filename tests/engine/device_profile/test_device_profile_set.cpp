/**************************************************************************/
/* test_device_profile_set.cpp                                          */
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
 * @file test_device_profile_set.cpp
 * @brief Tests that a DeviceProfileSet answers has_profile() by key and orders its profiles by DeviceKey.
 * @see tests/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/device_profile/device_profile_set.hpp>

#include "support/fake_profile_probe.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    TEST(Small_DeviceProfileSet, ADeviceHasAProfileOnlyAfterItIsSet)
    {
        DeviceProfileSet profiles;
        EXPECT_FALSE(profiles.has_profile(host_key(1)));

        EXPECT_TRUE(profiles.set(host_key(1), round_profile(1.0)));

        EXPECT_TRUE(profiles.has_profile(host_key(1)));
        EXPECT_FALSE(profiles.has_profile(host_key(2)));
        EXPECT_EQ(profiles.find(host_key(2)), nullptr);
    }

    TEST(Small_DeviceProfileSet, SettingAgainReplacesTheProfile)
    {
        DeviceProfileSet profiles;
        profiles.set(host_key(1), round_profile(1.0));

        EXPECT_FALSE(profiles.set(host_key(1), round_profile(3.0)));

        EXPECT_EQ(profiles.find(host_key(1))->base_throughput, 6000.0);
    }

    TEST(Small_DeviceProfileSet, TheProfilesComeInDeviceKeyOrder)
    {
        DeviceProfileSet profiles;
        profiles.set(host_key(5), round_profile(1.0));
        profiles.set(host_key(3), round_profile(1.0));

        EXPECT_EQ(profiles.profiles().begin()->first, host_key(3));
    }
} // namespace
