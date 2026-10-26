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
/* Licensed under the PolyForm Noncommercial License 1.0.0 (the           */
/* "License"); you may not use this file except in compliance with the    */
/* License. You may obtain a copy of the License at                       */
/*                                                                        */
/*     https://polyformproject.org/licenses/noncommercial/1.0.0           */
/*                                                                        */
/* Noncommercial use is free. Commercial use requires a separate licence  */
/* from Sushi Systems; see COMMERCIAL.md. The software is provided        */
/* "as is", without warranty of any kind.                                 */
/**************************************************************************/

/**
 * @file test_device_track_name.cpp
 * @brief Tests the functions that name the tracks of the device source.
 * @see tests/surface/profiling/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/profiling/device_track_name.hpp>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

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

/** @brief Tests that the host track names the node and is not a device track of any index. */
TEST(Small_DeviceTrackName, TheHostTrackNamesTheNode)
{
    EXPECT_EQ(host_track_name("node"), "node/host");
    EXPECT_NE(host_track_name("node"), device_track_name("node", 0, "host"));
}

/** @brief Tests that the runtime overload builds the same string from the runtime's own node and device. */
TEST(Medium_DeviceTrackName, TheRuntimeOverloadAgreesWithThePartsOverload)
{
    auto runtime = Runtime::create().value();
    const auto& devices = runtime.advanced().devices();
    ASSERT_FALSE(devices.empty());

    for (std::uint32_t index = 0; index < devices.size(); ++index)
        EXPECT_EQ(device_track_name(runtime, index),
                  device_track_name(runtime.topology().name, index, devices[index].name));
}

/** @brief Tests that an index the runtime does not have names a device called "device". */
TEST(Medium_DeviceTrackName, AnIndexTheRuntimeLacksNamesAGenericDevice)
{
    auto runtime = Runtime::create().value();

    EXPECT_EQ(device_track_name(runtime, 99), device_track_name(runtime.topology().name, 99, "device"));
}
