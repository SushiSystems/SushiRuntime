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
 * @brief Tests the exception type and code a caller of the library catches.
 * @author Mustafa Garip
 */

#include <stdexcept>
#include <string>
#include <gtest/gtest.h>
#include <SushiRuntime/SushiRuntime.h>

using namespace SushiRuntime;

namespace
{
    /** @brief Returns the filter that admits no device class. */
    Topology::HardwareFilterConfig filter_admitting_nothing()
    {
        Topology::HardwareFilterConfig config;
        config.include_cpu = false;
        config.include_gpu = false;
        config.include_accelerator = false;
        return config;
    }
} // namespace

/** @brief Tests that a filter excluding every device throws SushiException with unsupported. */
TEST(Integration_ErrorTypes, AFilterThatExcludesEveryDeviceThrowsUnsupported)
{
    try
    {
        static_cast<void>(Runtime::create(filter_admitting_nothing()));
        FAIL() << "a runtime was built over no device";
    }
    catch (const SushiException& refusal)
    {
        EXPECT_EQ(refusal.error().code, Errc::unsupported);
        EXPECT_EQ(refusal.error().message, refusal.what());
    }
}

/** @brief Tests that a handler written for std::runtime_error still receives that failure. */
TEST(Integration_ErrorTypes, AHandlerForRuntimeErrorReceivesTheSameFailure)
{
    try
    {
        static_cast<void>(Runtime::create(filter_admitting_nothing()));
        FAIL() << "a runtime was built over no device";
    }
    catch (const std::runtime_error& caught)
    {
        // The exception crossed the library boundary with its dynamic type; see README.md.
        const auto* typed = dynamic_cast<const SushiException*>(&caught);
        ASSERT_NE(typed, nullptr) << caught.what();
        EXPECT_EQ(typed->error().code, Errc::unsupported);
    }
}
