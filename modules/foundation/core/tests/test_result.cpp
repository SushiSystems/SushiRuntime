/**************************************************************************/
/* test_result.cpp                                                        */
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
 * @file test_result.cpp
 * @brief Tests the accessors of Result on a value and on an error, and Result<void>.
 * @author Mustafa Garip
 */

#include <memory>
#include <string>
#include <type_traits>
#include <gtest/gtest.h>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>

using SushiRuntime::Error;
using SushiRuntime::ErrorCode;
using SushiRuntime::make_error;
using SushiRuntime::Result;

namespace
{
    /** @brief The pattern of the line error() makes the handler write on a value. */
    const char* const K_ERROR_OF_VALUE = "Result::error\\(\\) needs a Result that holds an error";

    /** @brief The text a value accessor hands the handler when the error type is not Error. */
    const char* const K_VALUE_OF_ERROR = "the Result holds an error";

    /** @brief Returns the error the death tests of an Error-typed Result store. */
    Error lost_device()
    {
        return Error{ErrorCode::DeviceLost, "lost the device"};
    }

    TEST(Unit_Result, SuccessValueIsReadThroughEveryValueAccessor)
    {
        Result<int> number(41);
        EXPECT_TRUE(number.has_value());
        EXPECT_TRUE(static_cast<bool>(number));
        EXPECT_EQ(number.value(), 41);
        EXPECT_EQ(*number, 41);

        number.value() = 42;
        EXPECT_EQ(*number, 42);

        const Result<int>& number_view = number;
        EXPECT_EQ(number_view.value(), 42);
        EXPECT_EQ(*number_view, 42);

        Result<std::string> text(std::string("sushi"));
        EXPECT_EQ(text->size(), 5u);

        const Result<std::string>& text_view = text;
        EXPECT_EQ(text_view->size(), 5u);
    }

    TEST(Unit_Result, AnExpiringResultGivesItsValueAway)
    {
        const std::unique_ptr<int> taken =
            Result<std::unique_ptr<int>>(std::make_unique<int>(7)).value();
        ASSERT_NE(taken, nullptr);
        EXPECT_EQ(*taken, 7);

        const std::unique_ptr<int> dereferenced =
            *Result<std::unique_ptr<int>>(std::make_unique<int>(8));
        ASSERT_NE(dereferenced, nullptr);
        EXPECT_EQ(*dereferenced, 8);
    }

    TEST(Unit_Result, ErrorOnErrorReturnsTheStoredError)
    {
        Result<int> result(make_error(Error{ErrorCode::KernelFailed, "kernel faulted"}));
        const Result<int>& result_view = result;

        EXPECT_FALSE(result.has_value());
        EXPECT_FALSE(static_cast<bool>(result));
        EXPECT_EQ(result.error().code, ErrorCode::KernelFailed);
        EXPECT_EQ(result_view.error().message, "kernel faulted");

        result.error().message = "rewritten";
        EXPECT_EQ(result_view.error().message, "rewritten");
    }

    TEST(Unit_Result, TheErrorTypeDefaultsToError)
    {
        static_assert(std::is_same_v<Result<int>, Result<int, Error>>,
                      "Result<T> must name Result<T, Error>");
        static_assert(std::is_same_v<Result<void>, Result<void, Error>>,
                      "Result<void> must name Result<void, Error>");
    }

    TEST(Unit_ResultDeathTest, EveryValueAccessorOnAnErrorStopsWithTheStoredMessage)
    {
        Result<int> number(make_error(lost_device()));
        const Result<int>& number_view = number;
        Result<std::string> text(make_error(lost_device()));
        const Result<std::string>& text_view = text;

        EXPECT_DEATH(static_cast<void>(number.value()), "lost the device");
        EXPECT_DEATH(static_cast<void>(number_view.value()), "lost the device");
        EXPECT_DEATH(static_cast<void>(*number), "lost the device");
        EXPECT_DEATH(static_cast<void>(*number_view), "lost the device");
        EXPECT_DEATH(static_cast<void>(text->size()), "lost the device");
        EXPECT_DEATH(static_cast<void>(text_view->size()), "lost the device");
    }

    TEST(Unit_ResultDeathTest, EveryValueAccessorOnAnErrorOfAnotherTypeStops)
    {
        Result<int, int> number(make_error(7));
        const Result<int, int>& number_view = number;
        Result<std::string, int> text(make_error(7));
        const Result<std::string, int>& text_view = text;

        EXPECT_DEATH(static_cast<void>(number.value()), K_VALUE_OF_ERROR);
        EXPECT_DEATH(static_cast<void>(number_view.value()), K_VALUE_OF_ERROR);
        EXPECT_DEATH(static_cast<void>(*number), K_VALUE_OF_ERROR);
        EXPECT_DEATH(static_cast<void>(*number_view), K_VALUE_OF_ERROR);
        EXPECT_DEATH(static_cast<void>(text->size()), K_VALUE_OF_ERROR);
        EXPECT_DEATH(static_cast<void>(text_view->size()), K_VALUE_OF_ERROR);
    }

    TEST(Unit_ResultDeathTest, AValueAccessorOnAnExpiringErrorStops)
    {
        EXPECT_DEATH(static_cast<void>(Result<int>(make_error(lost_device())).value()),
                     "lost the device");
        EXPECT_DEATH(static_cast<void>(*Result<int>(make_error(lost_device()))),
                     "lost the device");
    }

    TEST(Unit_ResultDeathTest, ErrorOnAValueStops)
    {
        Result<int> result(5);
        const Result<int>& result_view = result;

        EXPECT_DEATH(static_cast<void>(result.error()), K_ERROR_OF_VALUE);
        EXPECT_DEATH(static_cast<void>(result_view.error()), K_ERROR_OF_VALUE);
    }

    TEST(Unit_Result, VoidSuccessHoldsNoErrorAndValueReturns)
    {
        const Result<void> done;

        EXPECT_TRUE(done.has_value());
        EXPECT_TRUE(static_cast<bool>(done));
        done.value();
    }

    TEST(Unit_Result, VoidErrorReturnsTheStoredError)
    {
        Result<void> failed(make_error(lost_device()));
        const Result<void>& failed_view = failed;

        EXPECT_FALSE(failed.has_value());
        EXPECT_FALSE(static_cast<bool>(failed));
        EXPECT_EQ(failed.error().code, ErrorCode::DeviceLost);
        EXPECT_EQ(failed_view.error().message, "lost the device");
    }

    TEST(Unit_ResultDeathTest, VoidValueOnAnErrorStopsWithTheStoredMessage)
    {
        const Result<void> failed(make_error(lost_device()));
        const Result<void, int> numbered(make_error(7));

        EXPECT_DEATH(failed.value(), "lost the device");
        EXPECT_DEATH(numbered.value(), K_VALUE_OF_ERROR);
    }

    TEST(Unit_ResultDeathTest, VoidErrorOnSuccessStops)
    {
        Result<void> done;
        const Result<void>& done_view = done;

        EXPECT_DEATH(static_cast<void>(done.error()), K_ERROR_OF_VALUE);
        EXPECT_DEATH(static_cast<void>(done_view.error()), K_ERROR_OF_VALUE);
    }
} // namespace
