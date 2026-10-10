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
 * @brief Tests the numbering and names of Errc and the shape of SushiException.
 * @author Mustafa Garip
 */

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <gtest/gtest.h>
#include <SushiRuntime/core/error.hpp>

using SushiRuntime::Core::Errc;
using SushiRuntime::Core::Error;
using SushiRuntime::Core::SushiException;

namespace
{
    /** @brief Returns the number an enumerator is stored as. */
    constexpr std::uint8_t number_of(Errc code) noexcept
    {
        return static_cast<std::uint8_t>(code);
    }

    // A consumer compiled against an older header compares these numbers; see README.md.
    TEST(Unit_Error, EveryEnumeratorKeepsItsNumber)
    {
        EXPECT_EQ(number_of(Errc::ok), 0);
        EXPECT_EQ(number_of(Errc::invalid_graph), 1);
        EXPECT_EQ(number_of(Errc::compile_failed), 2);
        EXPECT_EQ(number_of(Errc::allocation_failed), 3);
        EXPECT_EQ(number_of(Errc::kernel_failed), 4);
        EXPECT_EQ(number_of(Errc::device_lost), 5);
        EXPECT_EQ(number_of(Errc::invalid_access), 6);
        EXPECT_EQ(number_of(Errc::dangling_resource), 7);
        EXPECT_EQ(number_of(Errc::unsupported), 8);
        EXPECT_EQ(number_of(Errc::invalid_state), 9);
    }

    TEST(Unit_Error, EveryEnumeratorHasItsOwnName)
    {
        EXPECT_STREQ(to_string(Errc::ok), "ok");
        EXPECT_STREQ(to_string(Errc::invalid_graph), "invalid_graph");
        EXPECT_STREQ(to_string(Errc::compile_failed), "compile_failed");
        EXPECT_STREQ(to_string(Errc::allocation_failed), "allocation_failed");
        EXPECT_STREQ(to_string(Errc::kernel_failed), "kernel_failed");
        EXPECT_STREQ(to_string(Errc::device_lost), "device_lost");
        EXPECT_STREQ(to_string(Errc::invalid_access), "invalid_access");
        EXPECT_STREQ(to_string(Errc::dangling_resource), "dangling_resource");
        EXPECT_STREQ(to_string(Errc::unsupported), "unsupported");
        EXPECT_STREQ(to_string(Errc::invalid_state), "invalid_state");
    }

    TEST(Unit_Error, AValueOutsideTheEnumIsNamedUnknown)
    {
        EXPECT_STREQ(to_string(static_cast<Errc>(250)), "unknown");
    }

    TEST(Unit_Error, AHandlerForRuntimeErrorCatchesSushiException)
    {
        static_assert(std::is_base_of<std::runtime_error, SushiException>::value,
                      "SushiException must stay a std::runtime_error");

        try
        {
            throw SushiException(Error{Errc::invalid_state, "the call came too late"});
        }
        catch (const std::runtime_error& caught)
        {
            EXPECT_STREQ(caught.what(), "the call came too late");

            const auto* typed = dynamic_cast<const SushiException*>(&caught);
            ASSERT_NE(typed, nullptr);
            EXPECT_EQ(typed->error().code, Errc::invalid_state);
            EXPECT_EQ(typed->error().message, "the call came too late");
        }
    }
} // namespace
