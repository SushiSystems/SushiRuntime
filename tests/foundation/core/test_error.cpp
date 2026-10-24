/**************************************************************************/
/* test_error.cpp                                                         */
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
 * @file test_error.cpp
 * @brief Tests the numbering and names of ErrorCode.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>

using SushiRuntime::ErrorCode;
using SushiRuntime::Error;

namespace
{
    /** @brief Returns the number an enumerator is stored as. */
    constexpr std::uint8_t number_of(ErrorCode code) noexcept
    {
        return static_cast<std::uint8_t>(code);
    }

    // A consumer compiled against an older header compares these numbers; see README.md.
    TEST(Small_Error, EveryEnumeratorKeepsItsNumber)
    {
        EXPECT_EQ(number_of(ErrorCode::InvalidGraph), 1);
        EXPECT_EQ(number_of(ErrorCode::CompileFailed), 2);
        EXPECT_EQ(number_of(ErrorCode::AllocationFailed), 3);
        EXPECT_EQ(number_of(ErrorCode::KernelFailed), 4);
        EXPECT_EQ(number_of(ErrorCode::DeviceLost), 5);
        EXPECT_EQ(number_of(ErrorCode::InvalidAccess), 6);
        EXPECT_EQ(number_of(ErrorCode::DanglingResource), 7);
        EXPECT_EQ(number_of(ErrorCode::Unsupported), 8);
        EXPECT_EQ(number_of(ErrorCode::InvalidState), 9);
        EXPECT_EQ(number_of(ErrorCode::ProtocolMismatch), 10);
        EXPECT_EQ(number_of(ErrorCode::InvalidArgument), 11);
    }

    TEST(Small_Error, EveryEnumeratorHasItsOwnName)
    {
        EXPECT_STREQ(to_string(ErrorCode::InvalidGraph), "InvalidGraph");
        EXPECT_STREQ(to_string(ErrorCode::CompileFailed), "CompileFailed");
        EXPECT_STREQ(to_string(ErrorCode::AllocationFailed), "AllocationFailed");
        EXPECT_STREQ(to_string(ErrorCode::KernelFailed), "KernelFailed");
        EXPECT_STREQ(to_string(ErrorCode::DeviceLost), "DeviceLost");
        EXPECT_STREQ(to_string(ErrorCode::InvalidAccess), "InvalidAccess");
        EXPECT_STREQ(to_string(ErrorCode::DanglingResource), "DanglingResource");
        EXPECT_STREQ(to_string(ErrorCode::Unsupported), "Unsupported");
        EXPECT_STREQ(to_string(ErrorCode::InvalidState), "InvalidState");
        EXPECT_STREQ(to_string(ErrorCode::ProtocolMismatch), "ProtocolMismatch");
        EXPECT_STREQ(to_string(ErrorCode::InvalidArgument), "InvalidArgument");
    }

    TEST(Small_Error, AValueOutsideTheEnumIsNamedUnknown)
    {
        EXPECT_STREQ(to_string(static_cast<ErrorCode>(250)), "Unknown");
    }

} // namespace
