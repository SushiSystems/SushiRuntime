/**************************************************************************/
/* test_error_types.cpp                                                   */
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
 * @file test_error_types.cpp
 * @brief Tests the code a caller of the library reads from a failed factory.
 * @author Mustafa Garip
 */

#include <string>
#include <gtest/gtest.h>
#include <SushiRuntime/sushiruntime.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Returns the config whose device filter admits no device class. */
    RuntimeConfig config_admitting_nothing()
    {
        RuntimeConfig config;
        config.devices.include_cpu = false;
        config.devices.include_gpu = false;
        config.devices.include_accelerator = false;
        return config;
    }
} // namespace

/** @brief Tests that a filter excluding every device returns Unsupported and throws nothing. */
TEST(Integration_ErrorTypes, AFilterThatExcludesEveryDeviceReturnsUnsupported)
{
    EXPECT_NO_THROW({
        const auto refused = Runtime::create(config_admitting_nothing());
        ASSERT_FALSE(refused.has_value()) << "a runtime was built over no device";
        EXPECT_EQ(refused.error().code, ErrorCode::Unsupported);
        EXPECT_FALSE(refused.error().message.empty());
    });
}
