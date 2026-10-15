/**************************************************************************/
/* test_dispatch_width.cpp                                                */
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
 * @file test_dispatch_width.cpp
 * @brief Tests K_WIDTH_UNKNOWN: the sentinel no real launch width reaches.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <SushiRuntime/report/dispatch_width.hpp>

using namespace SushiRuntime;

/** @brief Tests that the unknown width is the largest 32-bit value. */
TEST(Small_DispatchWidth, UnknownIsTheLargestWidth)
{
    static_assert(K_WIDTH_UNKNOWN == std::numeric_limits<std::uint32_t>::max(),
                  "the unknown width is the one value no launch counts to");
    SUCCEED();
}
