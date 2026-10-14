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
 * @brief Tests translate_exceptions(): success, a standard exception, bad_alloc and an unnamed throw.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <new>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/error_translation.hpp>
#include <SushiRuntime/core/result.hpp>

using namespace SushiRuntime;

/** @brief Tests that a function that returns gives a value and runs once. */
TEST(Unit_ErrorTranslation, AReturningFunctionGivesAValue)
{
    int calls = 0;
    const Result<void> result = Detail::translate_exceptions([&calls] { ++calls; });
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(calls, 1);
}

/** @brief Tests that a standard exception becomes the given code with its text. */
TEST(Unit_ErrorTranslation, AStandardExceptionKeepsItsText)
{
    const Result<void> result = Detail::translate_exceptions(
        ErrorCode::InvalidAccess, [] { throw std::runtime_error("the kernel refused"); });
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidAccess);
    EXPECT_NE(result.error().message.find("the kernel refused"), std::string::npos);
}

/** @brief Tests that bad_alloc is AllocationFailed whatever code the caller gave. */
TEST(Unit_ErrorTranslation, BadAllocIsAllocationFailed)
{
    const Result<void> result =
        Detail::translate_exceptions(ErrorCode::InvalidAccess, [] { throw std::bad_alloc(); });
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::AllocationFailed);
}

/** @brief Tests that a thrown non-exception is named, and the default code is KernelFailed. */
TEST(Unit_ErrorTranslation, AnUnnamedThrowGetsTheFixedMessage)
{
    const Result<void> result = Detail::translate_exceptions([] { throw 7; });
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::KernelFailed);
    EXPECT_EQ(result.error().message, Detail::K_UNNAMED_EXCEPTION_MESSAGE);
}
