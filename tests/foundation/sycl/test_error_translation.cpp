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
TEST(Small_SYCLErrorTranslation, ASYCLExceptionTakesTheDeviceCode)
{
    const Result<void> result = Internal::SYCL::translate_exceptions(
        ErrorCode::DeviceLost, ErrorCode::InvalidAccess,
        [] { throw sycl::exception(sycl::make_error_code(sycl::errc::runtime), "queue lost"); });
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::DeviceLost);
    EXPECT_NE(result.error().message.find("queue lost"), std::string::npos);
}

/** @brief Tests that a SYCL memory allocation failure is AllocationFailed. */
TEST(Small_SYCLErrorTranslation, AMemoryAllocationErrorIsAllocationFailed)
{
    const sycl::exception failure(sycl::make_error_code(sycl::errc::memory_allocation), "full");
    EXPECT_EQ(Internal::SYCL::error_code_of(failure, ErrorCode::DeviceLost),
              ErrorCode::AllocationFailed);
}

/** @brief Tests that a standard exception takes the other code, and success a value. */
TEST(Small_SYCLErrorTranslation, AStandardExceptionTakesTheOtherCode)
{
    const Result<void> failed = Internal::SYCL::translate_exceptions(
        ErrorCode::DeviceLost, ErrorCode::InvalidAccess, [] { throw std::logic_error("bad"); });
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, ErrorCode::InvalidAccess);
    EXPECT_TRUE(Internal::SYCL::translate_exceptions([] {}).has_value());
}
