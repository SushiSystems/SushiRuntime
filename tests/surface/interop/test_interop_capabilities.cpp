/**************************************************************************/
/* test_interop_capabilities.cpp                                          */
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
 * @file test_interop_capabilities.cpp
 * @brief Tests the interop vocabulary: device UUIDs, capabilities and memory descriptors.
 * @see tests/surface/interop/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/interop/device_uuid.hpp>
#include <SushiRuntime/interop/external_handle_kind_set.hpp>
#include <SushiRuntime/interop/external_memory.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>

using SushiRuntime::DeviceUUID;
using SushiRuntime::ExternalHandleKind;
using SushiRuntime::ExternalMemoryDescriptor;
using SushiRuntime::InteropCapabilities;

TEST(Small_InteropVocabulary, UUIDsCompareByEveryByte)
{
    DeviceUUID left;
    DeviceUUID right;
    left.bytes.fill(0x11);
    right.bytes.fill(0x11);

    EXPECT_TRUE(left == right);
    EXPECT_FALSE(left != right);

    // Byte 15 is the only one that differs, so this is not a prefix check.
    right.bytes[15] = 0x12;

    EXPECT_FALSE(left == right);
    EXPECT_TRUE(left != right);
}

TEST(Small_InteropVocabulary, DefaultCapabilitiesOfferNothing)
{
    InteropCapabilities capabilities;

    EXPECT_FALSE(capabilities.host_export);
    EXPECT_FALSE(capabilities.device_import);
    EXPECT_FALSE(capabilities.device_uuid.has_value());
    EXPECT_TRUE(capabilities.importable_kinds.is_empty());
}

TEST(Small_InteropVocabulary, ADefaultDescriptorStatesNoHandleKind)
{
    ExternalMemoryDescriptor descriptor;

    EXPECT_EQ(descriptor.kind, ExternalHandleKind::Unspecified);
    EXPECT_EQ(descriptor.handle, 0u);
    EXPECT_EQ(descriptor.allocation_bytes, 0u);
    EXPECT_EQ(descriptor.offset, 0u);
}
