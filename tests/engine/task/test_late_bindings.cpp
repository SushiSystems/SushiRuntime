/**************************************************************************/
/* test_late_bindings.cpp                                                 */
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
 * @file test_late_bindings.cpp
 * @brief Tests LateBindings: an enabled binding that uses the baked width and base 0.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>

#include <gtest/gtest.h>

#include <SushiRuntime/task/late_bindings.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

/** @brief Tests the defaults of the bindings. */
TEST(Unit_LateBindings, DefaultsUseTheBakedWidth)
{
    const LateBindings bindings;
    EXPECT_TRUE(bindings.enabled.load());
    EXPECT_EQ(bindings.count.load(), LateBindings::K_USE_BAKED);
    EXPECT_EQ(bindings.base.load(), 0u);
    EXPECT_EQ(bindings.device_count, nullptr);
    EXPECT_EQ(bindings.device_base, nullptr);
    static_assert(LateBindings::K_USE_BAKED == ~std::size_t{0}, "the sentinel is the largest size");
}
