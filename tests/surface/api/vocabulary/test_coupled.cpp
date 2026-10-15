/**************************************************************************/
/* test_coupled.cpp                                                       */
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
 * @file test_coupled.cpp
 * @brief Tests Coupled: the state written and the states read, in argument order.
 * @see tests/surface/api/vocabulary/README.md
 * @author Mustafa Garip
 */

#include <tuple>
#include <type_traits>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/vocabulary/coupled.hpp>

using namespace SushiRuntime;

/** @brief Tests that Coupled refers to the states it was given, in order. */
TEST(Medium_Coupled, NamesTheStatesItWasGiven)
{
    auto runtime = Runtime::create().value();
    auto out = runtime.state<int>(4).value();
    auto first = runtime.state<int>(4).value();
    auto second = runtime.state<float>(4).value();
    const Coupled coupled(out, first, second);
    static_assert(std::is_same_v<decltype(coupled), const Coupled<int, int, float>>, "deduced");
    EXPECT_EQ(&coupled.out, &out);
    EXPECT_EQ(&std::get<0>(coupled.inputs), &first);
    EXPECT_EQ(&std::get<1>(coupled.inputs), &second);
}
