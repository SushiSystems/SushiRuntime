/**************************************************************************/
/* test_descriptor_refusal.cpp                                            */
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
 * @file test_descriptor_refusal.cpp
 * @brief Tests descriptor_refusal and device_refusal, the checks an import runs on its descriptor.
 * @see tests/surface/memory_exchange/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>
#include <SushiRuntime/memory_exchange/descriptor_refusal.hpp>

using namespace SushiRuntime;
using Internal::MemoryExchange::descriptor_refusal;
using Internal::MemoryExchange::device_refusal;

namespace
{
    /** @brief Returns the capabilities of a GPU that imports an opaque file descriptor and nothing else. */
    InteropCapabilities fd_importer()
    {
        InteropCapabilities capabilities;
        capabilities.device_import = true;
        capabilities.importable_kinds.insert(ExternalHandleKind::OpaqueFd);
        return capabilities;
    }

    /** @brief Returns a descriptor of an opaque file descriptor over @p allocation_bytes bytes. */
    ExternalMemoryDescriptor fd_descriptor(std::size_t allocation_bytes)
    {
        ExternalMemoryDescriptor descriptor;
        descriptor.kind = ExternalHandleKind::OpaqueFd;
        descriptor.handle = 3;
        descriptor.allocation_bytes = allocation_bytes;
        return descriptor;
    }

    /** @brief Returns the capabilities of a GPU that imports an opaque Win32 handle and reports @p uuid. */
    InteropCapabilities win32_importer(const DeviceUUID& uuid)
    {
        InteropCapabilities capabilities;
        capabilities.device_import = true;
        capabilities.importable_kinds.insert(ExternalHandleKind::OpaqueWin32);
        capabilities.device_uuid = uuid;
        return capabilities;
    }
} // namespace

/** @brief Tests that a descriptor naming no handle kind is refused with InvalidAccess, naming Unspecified. */
TEST(Small_DescriptorRefusal, ADescriptorWithNoKindIsRefused)
{
    ExternalMemoryDescriptor descriptor;
    descriptor.allocation_bytes = 4096;

    const std::optional<Error> refused = descriptor_refusal(fd_importer(), descriptor, 4096, 0);

    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->code, ErrorCode::InvalidAccess);
    EXPECT_NE(refused->message.find("Unspecified"), std::string::npos) << refused->message;
}

/** @brief Tests that a kind the device does not import is refused with InvalidAccess, naming the kind. */
TEST(Small_DescriptorRefusal, AKindTheDeviceDoesNotImportIsRefusedByName)
{
    ExternalMemoryDescriptor descriptor;
    descriptor.kind = ExternalHandleKind::OpaqueWin32;
    descriptor.handle = 1;
    descriptor.allocation_bytes = 4096;

    const std::optional<Error> refused = descriptor_refusal(fd_importer(), descriptor, 4096, 0);

    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->code, ErrorCode::InvalidAccess);
    EXPECT_NE(refused->message.find("OpaqueWin32"), std::string::npos) << refused->message;
}

/** @brief Tests that a region ending past the allocation is refused with InvalidAccess. */
TEST(Small_DescriptorRefusal, ARegionPastTheAllocationIsRefused)
{
    const std::optional<Error> refused =
        descriptor_refusal(fd_importer(), fd_descriptor(4096), 8192, 0);

    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->code, ErrorCode::InvalidAccess);
}

/** @brief Tests that a descriptor of a kind the device imports, inside its allocation, is not refused. */
TEST(Small_DescriptorRefusal, ADescriptorTheDeviceImportsIsNotRefused)
{
    EXPECT_FALSE(descriptor_refusal(fd_importer(), fd_descriptor(4096), 4096, 0).has_value());
}

/** @brief Tests that a device reporting no UUID is refused with InvalidAccess. */
TEST(Small_DeviceRefusal, ADeviceWithNoUuidIsRefused)
{
    const std::optional<Error> refused = device_refusal(fd_importer(), fd_descriptor(4096), 0);

    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->code, ErrorCode::InvalidAccess);
}

/** @brief Tests that a null Win32 handle is refused with InvalidAccess, saying the handle is null. */
TEST(Small_DescriptorRefusal, ANullWin32HandleIsRefusedByName)
{
    ExternalMemoryDescriptor descriptor;
    descriptor.kind = ExternalHandleKind::OpaqueWin32;
    descriptor.handle = 0;
    descriptor.allocation_bytes = 64;

    const std::optional<Error> refused = descriptor_refusal(win32_importer(DeviceUUID{}), descriptor, 16, 0);

    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->code, ErrorCode::InvalidAccess);
    EXPECT_NE(refused->message.find("descriptor.handle is null"), std::string::npos) << refused->message;
}

/** @brief Tests that a file descriptor above the largest int is refused, naming its value. */
TEST(Small_DescriptorRefusal, AFileDescriptorAboveIntIsRefusedWithItsValue)
{
    ExternalMemoryDescriptor descriptor = fd_descriptor(64);
    descriptor.handle = static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1;

    const std::optional<Error> refused = descriptor_refusal(fd_importer(), descriptor, 16, 0);

    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->code, ErrorCode::InvalidAccess);
    EXPECT_NE(refused->message.find("descriptor.handle is " + std::to_string(descriptor.handle)),
              std::string::npos)
        << refused->message;
}

/** @brief Tests that a region past the allocation is refused with the bytes, the offset and the allocation size. */
TEST(Small_DescriptorRefusal, ARegionPastTheAllocationIsRefusedWithItsNumbers)
{
    ExternalMemoryDescriptor descriptor = fd_descriptor(64);
    descriptor.offset = 60;

    const std::optional<Error> refused = descriptor_refusal(fd_importer(), descriptor, 16, 0);

    ASSERT_TRUE(refused.has_value());
    EXPECT_NE(refused->message.find("16 bytes at offset 60"), std::string::npos) << refused->message;
    EXPECT_NE(refused->message.find("allocation of 64 bytes"), std::string::npos) << refused->message;
}

/** @brief Tests that an offset past the allocation is refused without the subtraction wrapping. */
TEST(Small_DescriptorRefusal, AnOffsetPastTheAllocationIsRefusedWithoutWrapping)
{
    ExternalMemoryDescriptor descriptor = fd_descriptor(64);
    descriptor.offset = 65;

    const std::optional<Error> refused = descriptor_refusal(fd_importer(), descriptor, 0, 0);

    ASSERT_TRUE(refused.has_value());
    EXPECT_NE(refused->message.find("offset 65"), std::string::npos) << refused->message;
}

/** @brief Tests that another device's UUID is refused with both UUIDs in the message. */
TEST(Small_DeviceRefusal, AnotherDevicesUUIDIsRefusedWithBothUUIDs)
{
    DeviceUUID device;
    ExternalMemoryDescriptor descriptor = fd_descriptor(64);
    descriptor.device = device;
    descriptor.device.bytes[15] = static_cast<std::uint8_t>(descriptor.device.bytes[15] ^ 0xFFu);
    InteropCapabilities capabilities = fd_importer();
    capabilities.device_uuid = device;

    const std::optional<Error> refused = device_refusal(capabilities, descriptor, 0);

    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->code, ErrorCode::InvalidAccess);
    EXPECT_NE(refused->message.find("descriptor.device is"), std::string::npos) << refused->message;
    EXPECT_NE(refused->message.find("and device"), std::string::npos) << refused->message;
}

/** @brief Tests that a descriptor naming the device's own UUID is not refused. */
TEST(Small_DeviceRefusal, TheDevicesOwnUUIDIsNotRefused)
{
    ExternalMemoryDescriptor descriptor = fd_descriptor(64);
    InteropCapabilities capabilities = fd_importer();
    capabilities.device_uuid = descriptor.device;

    EXPECT_FALSE(device_refusal(capabilities, descriptor, 0).has_value());
}
