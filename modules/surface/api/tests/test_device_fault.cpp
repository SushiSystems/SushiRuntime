/**************************************************************************/
/* test_device_fault.cpp                                                  */
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
 * @file test_device_fault.cpp
 * @brief Tests that a device fault record stores a value past a uint32 as UINT32_MAX.
 * @see modules/surface/api/tests/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/SushiRuntime.h>
#include <SushiRuntime/api/detail/device_fault.hpp>

using namespace SushiRuntime;

static_assert(API::detail::saturated_fault_word(7) == 7u,
              "a value that fits a fault word is stored as it is");
static_assert(API::detail::saturated_fault_word(std::size_t{UINT32_MAX} + 1u) == UINT32_MAX,
              "a value past a fault word is stored as the largest word");

/** @brief Tests that each field of a record saturates and the message shows the saturated value. */
TEST(Integration_DeviceFault, AFieldPastAUint32IsStoredSaturated)
{
    if (sizeof(std::size_t) <= sizeof(std::uint32_t))
        GTEST_SKIP() << "std::size_t holds no value past a uint32 on this platform";

    const std::size_t over = std::size_t{UINT32_MAX} + 5u;
    std::array<std::uint32_t, API::detail::DEVICE_FAULT_WORDS> record{};

    API::detail::record_device_fault(record.data(), API::detail::DEVICE_FAULT_WINDOW, over, 7,
                                     over);

    EXPECT_EQ(record[0], API::detail::DEVICE_FAULT_WINDOW);
    EXPECT_EQ(record[1], UINT32_MAX);
    EXPECT_EQ(record[2], 7u);
    EXPECT_EQ(record[3], UINT32_MAX);

    try
    {
        API::detail::throw_if_device_fault(record.data());
        ADD_FAILURE() << "a record that holds a fault did not throw";
    }
    catch (const Core::SushiException& fault)
    {
        const std::string& message = fault.error().message;
        EXPECT_NE(message.find("starts at " + std::to_string(UINT32_MAX)), std::string::npos)
            << message;
    }
    EXPECT_EQ(record[0], API::detail::DEVICE_FAULT_NONE);
}
