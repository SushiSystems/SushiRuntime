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
 * @brief Tests the value and error accessors of Result and their SushiException contract.
 * @author Mustafa Garip
 */

#include <stdexcept>
#include <string>
#include <gtest/gtest.h>
#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/core/result.hpp>

using SushiRuntime::Errc;
using SushiRuntime::Error;
using SushiRuntime::make_error;
using SushiRuntime::Result;
using SushiRuntime::SushiException;

namespace
{
    /** @brief Runs an access that must throw and returns the SushiException it threw. */
    template <typename Access>
    SushiException exception_from(Access access)
    {
        try
        {
            access();
        }
        catch (const SushiException& caught)
        {
            return caught;
        }
        throw std::logic_error("the access did not throw SushiException");
    }

    /** @brief Expects the exception to carry the device-lost error the tests store. */
    void expect_device_lost(const SushiException& caught)
    {
        EXPECT_EQ(caught.error().code, Errc::device_lost);
        EXPECT_EQ(caught.error().message, "lost the device");
        EXPECT_STREQ(caught.what(), "lost the device");
    }

    /** @brief Expects the exception to report a value read on an error of a non-Error type. */
    void expect_value_read_on_error(const SushiException& caught)
    {
        EXPECT_EQ(caught.error().code, Errc::invalid_state);
        EXPECT_EQ(caught.error().message, "Result::value(): the result holds an error.");
    }

    /** @brief Expects the exception to report an error read on a success value. */
    void expect_error_read_on_value(const SushiException& caught)
    {
        EXPECT_EQ(caught.error().code, Errc::invalid_state);
        EXPECT_EQ(caught.error().message, "Result::error(): the result holds a value.");
    }

    TEST(Unit_Result, SuccessValueIsReadThroughEveryValueAccessor)
    {
        Result<int, Error> number(41);
        EXPECT_EQ(number.value(), 41);
        EXPECT_EQ(*number, 41);

        number.value() = 42;
        EXPECT_EQ(*number, 42);

        const Result<int, Error>& number_view = number;
        EXPECT_EQ(number_view.value(), 42);
        EXPECT_EQ(*number_view, 42);

        Result<std::string, Error> text(std::string("sushi"));
        EXPECT_EQ(text->size(), 5u);

        const Result<std::string, Error>& text_view = text;
        EXPECT_EQ(text_view->size(), 5u);
    }

    TEST(Unit_Result, ValueOnErrorThrowsTheStoredErrorUnchanged)
    {
        Result<int, Error> result(make_error(Error{Errc::device_lost, "lost the device"}));

        expect_device_lost(exception_from([&] { return result.value(); }));
    }

    TEST(Unit_Result, EveryValueAccessorOnErrorThrowsTheStoredError)
    {
        Result<int, Error> number(make_error(Error{Errc::device_lost, "lost the device"}));
        const Result<int, Error>& number_view = number;
        Result<std::string, Error> text(make_error(Error{Errc::device_lost, "lost the device"}));
        const Result<std::string, Error>& text_view = text;

        expect_device_lost(exception_from([&] { return number.value(); }));
        expect_device_lost(exception_from([&] { return number_view.value(); }));
        expect_device_lost(exception_from([&] { return *number; }));
        expect_device_lost(exception_from([&] { return *number_view; }));
        expect_device_lost(exception_from([&] { return text->size(); }));
        expect_device_lost(exception_from([&] { return text_view->size(); }));
    }

    TEST(Unit_Result, ValueOnNonErrorErrorTypeThrowsInvalidState)
    {
        Result<int, int> number(make_error(7));
        const Result<int, int>& number_view = number;
        Result<std::string, int> text(make_error(7));
        const Result<std::string, int>& text_view = text;

        expect_value_read_on_error(exception_from([&] { return number.value(); }));
        expect_value_read_on_error(exception_from([&] { return number_view.value(); }));
        expect_value_read_on_error(exception_from([&] { return *number; }));
        expect_value_read_on_error(exception_from([&] { return *number_view; }));
        expect_value_read_on_error(exception_from([&] { return text->size(); }));
        expect_value_read_on_error(exception_from([&] { return text_view->size(); }));
    }

    TEST(Unit_Result, ErrorOnSuccessThrowsInvalidState)
    {
        Result<int, Error> result(5);
        const Result<int, Error>& result_view = result;

        expect_error_read_on_value(exception_from([&] { return result.error(); }));
        expect_error_read_on_value(exception_from([&] { return result_view.error(); }));
    }

    TEST(Unit_Result, ErrorOnErrorReturnsTheStoredError)
    {
        Result<int, Error> result(make_error(Error{Errc::kernel_failed, "kernel faulted"}));
        const Result<int, Error>& result_view = result;

        EXPECT_EQ(result.error().code, Errc::kernel_failed);
        EXPECT_EQ(result_view.error().message, "kernel faulted");
    }
} // namespace
