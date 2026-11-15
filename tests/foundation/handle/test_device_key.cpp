/**************************************************************************/
/* test_device_key.cpp                                                    */
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
 * @file test_device_key.cpp
 * @brief Tests DeviceKey's order, hash and text round trip.
 * @see tests/foundation/handle/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/handle/device_key.hpp>

using SushiRuntime::DeviceBackend;
using SushiRuntime::DeviceKey;
using SushiRuntime::HardwareIDKind;

namespace
{
    /** @brief Returns a key with the given fields and every id byte set to fill. */
    DeviceKey make_key(DeviceBackend backend, HardwareIDKind id_kind, std::uint8_t fill,
                       std::uint32_t ordinal)
    {
        DeviceKey key;
        key.backend = backend;
        key.id_kind = id_kind;
        key.hardware_id.fill(fill);
        key.ordinal = ordinal;
        return key;
    }
}

TEST(Small_DeviceKey, TextRoundTripsEveryKind)
{
    DeviceKey uuid = make_key(DeviceBackend::CUDA, HardwareIDKind::UUID, 0, 0);
    for (std::size_t index = 0; index < uuid.hardware_id.size(); ++index)
        uuid.hardware_id[index] = static_cast<std::uint8_t>(index * 17);
    EXPECT_EQ(SushiRuntime::to_text(uuid), "cuda:uuid:00112233445566778899aabbccddeeff:0");

    const std::vector<DeviceKey> keys = {
        uuid,
        make_key(DeviceBackend::Host, HardwareIDKind::None, 0, 0),
        make_key(DeviceBackend::LevelZero, HardwareIDKind::PCIAddress, 0xAB, 3),
        make_key(DeviceBackend::HIP, HardwareIDKind::UUID, 0xFF, UINT32_MAX),
        make_key(DeviceBackend::OpenCL, HardwareIDKind::None, 0, 12)};
    for (const DeviceKey& key : keys)
    {
        const std::string text = SushiRuntime::to_text(key);
        SCOPED_TRACE(text);
        const std::optional<DeviceKey> parsed = SushiRuntime::parse_device_key(text);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, key);
    }
}

TEST(Small_DeviceKey, MalformedTextDoesNotParse)
{
    const std::vector<std::string> bad = {
        "",
        "cuda",
        "vulkan:uuid:00112233445566778899aabbccddeeff:0",
        "cuda:serial:00112233445566778899aabbccddeeff:0",
        "cuda:uuid:00112233445566778899aabbccddeef:0",
        "cuda:uuid:00112233445566778899AABBCCDDEEFF:0",
        "cuda:uuid:00112233445566778899aabbccddeeff:",
        "cuda:uuid:00112233445566778899aabbccddeeff:-1",
        "cuda:uuid:00112233445566778899aabbccddeeff:4294967296"};
    for (const std::string& text : bad)
    {
        SCOPED_TRACE(text);
        EXPECT_FALSE(SushiRuntime::parse_device_key(text).has_value());
    }
}

TEST(Small_DeviceKey, OrderComparesBackendKindIDOrdinal)
{
    const std::vector<DeviceKey> ascending = {
        make_key(DeviceBackend::Host, HardwareIDKind::None, 0, 0),
        make_key(DeviceBackend::Host, HardwareIDKind::None, 0, 1),
        make_key(DeviceBackend::Host, HardwareIDKind::UUID, 0, 0),
        make_key(DeviceBackend::Host, HardwareIDKind::UUID, 1, 0),
        make_key(DeviceBackend::CUDA, HardwareIDKind::None, 0, 0)};
    for (std::size_t lower = 0; lower < ascending.size(); ++lower)
    {
        for (std::size_t upper = 0; upper < ascending.size(); ++upper)
        {
            SCOPED_TRACE(testing::Message() << lower << " against " << upper);
            EXPECT_EQ(ascending[lower] < ascending[upper], lower < upper);
            EXPECT_EQ(ascending[lower] > ascending[upper], lower > upper);
            EXPECT_EQ(ascending[lower] == ascending[upper], lower == upper);
        }
    }
    std::vector<DeviceKey> shuffled(ascending.rbegin(), ascending.rend());
    std::sort(shuffled.begin(), shuffled.end());
    EXPECT_EQ(shuffled, ascending);
}

TEST(Small_DeviceKey, EqualKeysHashEqualAndFieldsChangeTheHash)
{
    const std::hash<DeviceKey> hash;
    const DeviceKey key = make_key(DeviceBackend::HIP, HardwareIDKind::UUID, 0x5A, 2);
    EXPECT_EQ(hash(key), hash(make_key(DeviceBackend::HIP, HardwareIDKind::UUID, 0x5A, 2)));
    EXPECT_NE(hash(key), hash(make_key(DeviceBackend::CUDA, HardwareIDKind::UUID, 0x5A, 2)));
    EXPECT_NE(hash(key), hash(make_key(DeviceBackend::HIP, HardwareIDKind::PCIAddress, 0x5A, 2)));
    EXPECT_NE(hash(key), hash(make_key(DeviceBackend::HIP, HardwareIDKind::UUID, 0x5B, 2)));
    EXPECT_NE(hash(key), hash(make_key(DeviceBackend::HIP, HardwareIDKind::UUID, 0x5A, 3)));
}
