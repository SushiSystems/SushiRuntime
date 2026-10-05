/****************************************************************/
/* test_interop_vocabulary.cpp                                  */
/* SushiRuntime - https://github.com/SushiSystems/SushiRuntime  */
/* Copyright (c) 2026 Sushi Systems                             */
/* Licensed under PolyForm Noncommercial 1.0.0. See LICENSE.    */
/* Commercial use requires a licence from Sushi Systems.        */
/****************************************************************/

/**
 * @file test_interop_vocabulary.cpp
 * @brief Tests the interop vocabulary's UUID equality and default capabilities.
 * @author Sushi (mustafagarip@sushisystems.io)
 */

#include <gtest/gtest.h>
#include <SushiRuntime/interop/device_uuid.hpp>
#include <SushiRuntime/interop/interop_capabilities.hpp>

using SushiRuntime::Interop::DeviceUuid;
using SushiRuntime::Interop::InteropCapabilities;

TEST(Unit_InteropVocabulary, UuidsCompareByEveryByte)
{
    DeviceUuid left;
    DeviceUuid right;
    left.bytes.fill(0x11);
    right.bytes.fill(0x11);

    EXPECT_TRUE(left == right);
    EXPECT_FALSE(left != right);

    // Byte 15 is the only one that differs, so this is not a prefix check.
    right.bytes[15] = 0x12;

    EXPECT_FALSE(left == right);
    EXPECT_TRUE(left != right);
}

TEST(Unit_InteropVocabulary, DefaultCapabilitiesOfferNothing)
{
    InteropCapabilities capabilities;

    EXPECT_FALSE(capabilities.host_export);
    EXPECT_FALSE(capabilities.device_import);
    EXPECT_FALSE(capabilities.device_uuid.has_value());
}
