/**************************************************************************/
/* test_text_format.cpp                                                   */
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
 * @file test_text_format.cpp
 * @brief Tests format_text: placeholders, doubled braces and a count that does not match.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/core/text_format.hpp>

#include <gtest/gtest.h>

using namespace SushiRuntime;

/** @brief Tests that each {} takes the next argument through operator<<, in order. */
TEST(Small_TextFormat, EachPlaceholderTakesTheNextArgument)
{
    EXPECT_EQ(format_text("{} of {} at {}", 3, "seven", 0.5), "3 of seven at 0.5");
}

/** @brief Tests that doubled braces are written once, with and without arguments. */
TEST(Small_TextFormat, DoubledBracesAreLiteral)
{
    EXPECT_EQ(format_text("{{}} and }}"), "{} and }");
    EXPECT_EQ(format_text("{{{}}}", 4), "{4}");
}

/** @brief Tests that extra arguments and extra placeholders are left as they are. */
TEST(Small_TextFormat, AMismatchedCountIsIgnored)
{
    EXPECT_EQ(format_text("one {}", 1, 2), "one 1");
    EXPECT_EQ(format_text("{} and {}", 1), "1 and {}");
}
