/**************************************************************************/
/* test_steady_time.cpp                                                   */
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
 * @file test_steady_time.cpp
 * @brief Tests steady_ns(), the steady reading a dispatch record and a clock pairing are made of.
 * @see tests/platform/observability/README.md
 * @author Mustafa Garip
 */

#include <chrono>

#include <gtest/gtest.h>

#include <SushiRuntime/observability/steady_time.hpp>

using SushiRuntime::Internal::Observability::steady_ns;

/** @brief Tests that steady_ns() counts the nanoseconds of a time point since the steady clock's epoch. */
TEST(Small_SteadyTime, SteadyNsCountsNanosecondsSinceTheEpoch)
{
    const std::chrono::steady_clock::time_point point{std::chrono::microseconds(3)};

    EXPECT_EQ(steady_ns(point), 3000u);
}
