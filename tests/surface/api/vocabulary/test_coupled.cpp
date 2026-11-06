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
 * @file test_coupled.cpp
 * @brief Tests Coupled: the state written and the states read, in argument order.
 * @see tests/surface/api/vocabulary/README.md
 * @author Mustafa Garip
 */

#include <tuple>
#include <type_traits>
#include <utility>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/vocabulary/coupled.hpp>

#include "support/runtime_environment.hpp"

using namespace SushiRuntime;

static_assert(std::is_same_v<decltype(Coupled(std::declval<State<int>&>(), std::declval<State<int>&>(),
                                              std::declval<State<float>&>())),
                             Coupled<int, int, float>>,
              "the element types of the states are deduced");

/** @brief Fixture of the Coupled test, which allocates on the program's runtime. */
class Medium_Coupled : public TestSupport::RuntimeTest
{
};

/** @brief Tests that Coupled refers to the states it was given, in order. */
TEST_F(Medium_Coupled, NamesTheStatesItWasGiven)
{
    auto out = runtime().state<int>(4).value();
    auto first = runtime().state<int>(4).value();
    auto second = runtime().state<float>(4).value();

    const Coupled coupled(out, first, second);

    EXPECT_EQ(&coupled.out, &out);
    EXPECT_EQ(&std::get<0>(coupled.inputs), &first);
    EXPECT_EQ(&std::get<1>(coupled.inputs), &second);
}
