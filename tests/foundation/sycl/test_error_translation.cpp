/**************************************************************************/
/* test_error_translation.cpp                                             */
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
 * @file test_error_translation.cpp
 * @brief Tests the SYCL error translation: device codes, allocation failures and other throws.
 * @see tests/foundation/sycl/README.md
 * @author Mustafa Garip
 */

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>
#include <SushiRuntime/sycl/error_translation.hpp>

using namespace SushiRuntime;

/** @brief Tests that a SYCL exception takes the device code with its text. */
TEST(Unit_SYCLErrorTranslation, ASYCLExceptionTakesTheDeviceCode)
{
    const Result<void> result = Internal::SYCL::translate_exceptions(
        ErrorCode::DeviceLost, ErrorCode::InvalidAccess,
        [] { throw sycl::exception(sycl::make_error_code(sycl::errc::runtime), "queue lost"); });
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::DeviceLost);
    EXPECT_NE(result.error().message.find("queue lost"), std::string::npos);
}

/** @brief Tests that a SYCL memory allocation failure is AllocationFailed. */
TEST(Unit_SYCLErrorTranslation, AMemoryAllocationErrorIsAllocationFailed)
{
    const sycl::exception failure(sycl::make_error_code(sycl::errc::memory_allocation), "full");
    EXPECT_EQ(Internal::SYCL::error_code_of(failure, ErrorCode::DeviceLost),
              ErrorCode::AllocationFailed);
}

/** @brief Tests that a standard exception takes the other code, and success a value. */
TEST(Unit_SYCLErrorTranslation, AStandardExceptionTakesTheOtherCode)
{
    const Result<void> failed = Internal::SYCL::translate_exceptions(
        ErrorCode::DeviceLost, ErrorCode::InvalidAccess, [] { throw std::logic_error("bad"); });
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, ErrorCode::InvalidAccess);
    EXPECT_TRUE(Internal::SYCL::translate_exceptions([] {}).has_value());
}
