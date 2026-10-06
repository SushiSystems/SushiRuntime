/**************************************************************************/
/* test_interop_vocabulary.cpp                                            */
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
