/**************************************************************************/
/* test_before_step_hooks.cpp                                             */
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
 * @file test_before_step_hooks.cpp
 * @brief Tests BeforeStepHooks: hooks run in order with the step count, small and large alike.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/api/detail/before_step_hooks.hpp>

using namespace SushiRuntime;

/** @brief Tests that every hook runs once per call, in the order added, with the count. */
TEST(Small_BeforeStepHooks, HooksRunInOrderWithTheCount)
{
    std::vector<std::size_t> seen;
    std::array<char, 256> large{};
    Detail::BeforeStepHooks hooks;
    hooks.add([&seen](std::size_t steps) { seen.push_back(steps); });
    hooks.add([&seen, large](std::size_t steps) { seen.push_back(steps + 100 + large[0]); });

    hooks.run(3);

    EXPECT_EQ(seen, (std::vector<std::size_t>{3, 103}));
}

/** @brief Tests that a moved BeforeStepHooks runs the hooks it was handed. */
TEST(Small_BeforeStepHooks, AMovedObjectKeepsItsHooks)
{
    std::vector<std::size_t> seen;
    std::array<char, 256> large{};
    Detail::BeforeStepHooks hooks;
    hooks.add([&seen](std::size_t steps) { seen.push_back(steps); });
    hooks.add([&seen, large](std::size_t steps) { seen.push_back(steps + 100 + large[0]); });

    Detail::BeforeStepHooks moved(std::move(hooks));
    moved.run(4);

    EXPECT_EQ(seen, (std::vector<std::size_t>{4, 104}));
}
