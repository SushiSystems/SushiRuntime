/**************************************************************************/
/* test_uuid_device_key_source.cpp                                        */
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
 * @file test_uuid_device_key_source.cpp
 * @brief Tests the DeviceKeySource of each backend that reports a device UUID.
 * @see tests/platform/topology/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/topology/uuid_device_key_source.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Topology;

namespace
{
    /** @brief Returns the UUID whose bytes count up from @p first. */
    HardwareID uuid_from(std::uint8_t first)
    {
        HardwareID uuid{};
        for (std::size_t index = 0; index < uuid.size(); ++index)
            uuid[index] = static_cast<std::uint8_t>(first + index);
        return uuid;
    }

    /** @brief Returns the identity of a device on a backend, with a UUID or without one. */
    DeviceIdentity identity_on(DeviceBackend backend, std::optional<HardwareID> uuid)
    {
        DeviceIdentity identity;
        identity.backend = backend;
        identity.uuid = uuid;
        return identity;
    }
} // namespace

/** @brief Tests that the CUDA source names a CUDA device by its UUID. */
TEST(Small_UUIDDeviceKeySource, TheCUDASourceNamesACUDADeviceByItsUUID)
{
    const std::optional<DeviceKey> key =
        CUDAUUIDDeviceKeySource().key_of(identity_on(DeviceBackend::CUDA, uuid_from(1)));

    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->backend, DeviceBackend::CUDA);
    EXPECT_EQ(key->id_kind, HardwareIDKind::UUID);
    EXPECT_EQ(key->hardware_id, uuid_from(1));
    EXPECT_EQ(key->ordinal, 0u);
}

/** @brief Tests that the HIP source names a HIP device by its UUID. */
TEST(Small_UUIDDeviceKeySource, TheHIPSourceNamesAHIPDeviceByItsUUID)
{
    const std::optional<DeviceKey> key =
        HIPUUIDDeviceKeySource().key_of(identity_on(DeviceBackend::HIP, uuid_from(2)));

    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->backend, DeviceBackend::HIP);
    EXPECT_EQ(key->hardware_id, uuid_from(2));
}

/** @brief Tests that the Level Zero source names a Level Zero device by its UUID. */
TEST(Small_UUIDDeviceKeySource, TheLevelZeroSourceNamesALevelZeroDeviceByItsUUID)
{
    const std::optional<DeviceKey> key = LevelZeroUUIDDeviceKeySource().key_of(
        identity_on(DeviceBackend::LevelZero, uuid_from(3)));

    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(key->backend, DeviceBackend::LevelZero);
    EXPECT_EQ(key->hardware_id, uuid_from(3));
}

/** @brief Tests that a source declines a device of another backend, whatever UUID it has. */
TEST(Small_UUIDDeviceKeySource, ASourceDeclinesADeviceOfAnotherBackend)
{
    EXPECT_FALSE(CUDAUUIDDeviceKeySource()
                     .key_of(identity_on(DeviceBackend::OpenCL, uuid_from(1)))
                     .has_value());
    EXPECT_FALSE(HIPUUIDDeviceKeySource()
                     .key_of(identity_on(DeviceBackend::CUDA, uuid_from(1)))
                     .has_value());
}

/** @brief Tests that a source declines a device that reports no UUID. */
TEST(Small_UUIDDeviceKeySource, ASourceDeclinesADeviceWithNoUUID)
{
    EXPECT_FALSE(CUDAUUIDDeviceKeySource()
                     .key_of(identity_on(DeviceBackend::CUDA, std::nullopt))
                     .has_value());
}
