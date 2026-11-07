/**************************************************************************/
/* test_clock.cpp                                                         */
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
 * @file test_clock.cpp
 * @brief Tests the Clock contract on both adapters, and ManualClock's own promises.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/core/clock.hpp>

#include "support/clock_contract.hpp"

using SushiRuntime::ManualClock;

namespace TestSupport
{
    /** @brief Runs the contract on the clock a test advances. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Small_ManualClock, ClockContract, ::SushiRuntime::ManualClock, );

    /** @brief Runs the contract on the machine's steady clock, the boundary ManualClock stands in for. */
    INSTANTIATE_TYPED_TEST_SUITE_P(Medium_SteadyClock, ClockContract, ::SushiRuntime::SteadyClock, );
} // namespace TestSupport

/** @brief Tests that a manual clock reads the time it was started at. */
TEST(Small_ManualClock, ReadsItsStartTime)
{
    const ManualClock clock(42);

    EXPECT_EQ(clock.now_ns(), 42u);
}

/** @brief Tests that advancing a manual clock moves its reading by the step. */
TEST(Small_ManualClock, AdvanceMovesTheReadingByTheStep)
{
    ManualClock clock(10);

    clock.advance(5);

    EXPECT_EQ(clock.now_ns(), 15u);
}
