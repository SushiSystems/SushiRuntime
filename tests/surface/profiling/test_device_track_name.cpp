/**************************************************************************/
/* test_device_track_name.cpp                                             */
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
 * @file test_device_track_name.cpp
 * @brief Tests the functions that name the tracks of the device source.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/profiling/device_track_name.hpp>
#include <SushiRuntime/sushiruntime.hpp>

#include "support/host_runtime.hpp"

using namespace SushiRuntime;

/** @brief Fixture of the tests that need a host runtime. */
class Small_DeviceTrackNameOfARuntime : public TestSupport::HostRuntimeTest
{
};

/** @brief Tests that the track names the node, the device index and the device. */
TEST(Small_DeviceTrackName, NamesTheNodeTheIndexAndTheDevice)
{
    EXPECT_EQ(device_track_name("workstation", 1, "NVIDIA GeForce RTX 3080 Ti"),
              "workstation/1/NVIDIA GeForce RTX 3080 Ti");
    EXPECT_EQ(device_track_name("workstation", 0, "cpu"), "workstation/0/cpu");
}

/** @brief Tests that two devices of one name on one node get different tracks. */
TEST(Small_DeviceTrackName, TwoDevicesOfOneNameGetDifferentTracks)
{
    EXPECT_NE(device_track_name("node", 0, "gpu"), device_track_name("node", 1, "gpu"));
}

/** @brief Tests that two nodes with the same device get different tracks. */
TEST(Small_DeviceTrackName, TwoNodesWithTheSameDeviceGetDifferentTracks)
{
    EXPECT_NE(device_track_name("alpha", 0, "gpu"), device_track_name("beta", 0, "gpu"));
}

/** @brief Tests that the host track names the node. */
TEST(Small_DeviceTrackName, TheHostTrackNamesTheNode)
{
    EXPECT_EQ(host_track_name("node"), "node/host");
}

/** @brief Tests that the host track is not the track of a device called host, whatever its index. */
TEST(Small_DeviceTrackName, TheHostTrackDiffersFromADeviceTrack)
{
    EXPECT_NE(host_track_name("node"), device_track_name("node", 0, "host"));
}

namespace
{
    /** @brief Checks that the runtime overload names device @p index as the parts overload names it. */
    void expect_overloads_agree(const Runtime& runtime, std::uint32_t index)
    {
        SCOPED_TRACE(index);
        const auto& devices = runtime.advanced().devices();
        EXPECT_EQ(device_track_name(runtime, index),
                  device_track_name(runtime.topology().name, index, devices[index].name));
    }
} // namespace

/** @brief Tests that the runtime overload builds the same string from the runtime's own node and device. */
TEST_F(Small_DeviceTrackNameOfARuntime, TheRuntimeOverloadAgreesWithThePartsOverload)
{
    const auto& devices = runtime().advanced().devices();
    ASSERT_FALSE(devices.empty());

    expect_overloads_agree(runtime(), 0);
    expect_overloads_agree(runtime(), static_cast<std::uint32_t>(devices.size() - 1));
}

/** @brief Tests that an index the runtime does not have names a device called "device". */
TEST_F(Small_DeviceTrackNameOfARuntime, AnIndexTheRuntimeLacksNamesAGenericDevice)
{
    EXPECT_EQ(device_track_name(runtime(), 99), device_track_name(runtime().topology().name, 99, "device"));
}

/** @brief Tests that a device the list holds is named by its own name. */
TEST(Small_DeviceTrackName, ADeviceTheListHoldsIsNamedByItsName)
{
    DeviceDescription device;
    device.name = "cpu";

    EXPECT_EQ(device_track_name("node", 0, std::vector<DeviceDescription>{device}), "node/0/cpu");
}

/** @brief Tests that an index the list lacks names a device called "device". */
TEST(Small_DeviceTrackName, AnIndexTheListLacksNamesAGenericDevice)
{
    DeviceDescription device;
    device.name = "cpu";

    EXPECT_EQ(device_track_name("node", 1, std::vector<DeviceDescription>{device}), "node/1/device");
    EXPECT_EQ(device_track_name("node", 0, std::vector<DeviceDescription>{}), "node/0/device");
}
