/**************************************************************************/
/* test_completion_signal.cpp                                             */
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
 * @file test_completion_signal.cpp
 * @brief Tests CompletionSignal: a wait times out, a raise wakes the waiter, and reset clears it.
 * @see tests/foundation/measurement/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include <SushiRuntime/measurement/completion_signal.hpp>

using namespace SushiRuntime::Test;

/** @brief Tests that a wait with nothing raised returns false. */
TEST(Medium_CompletionSignal, AWaitWithNoRaiseTimesOut)
{
    CompletionSignal signal;
    EXPECT_FALSE(signal.wait_for(std::chrono::seconds(0)));
}

/** @brief Tests that a raise on another thread wakes the waiter. */
TEST(Medium_CompletionSignal, ARaiseWakesTheWaiter)
{
    CompletionSignal signal;
    std::thread raiser([&signal] { signal.raise(); });
    EXPECT_TRUE(signal.wait_for(std::chrono::seconds(30)));
    raiser.join();
}

/** @brief Tests that a raised signal stays raised until reset() clears it. */
TEST(Medium_CompletionSignal, ResetClearsARaisedSignal)
{
    CompletionSignal signal;
    signal.raise();
    EXPECT_TRUE(signal.wait_for(std::chrono::seconds(0))) << "a raised signal stays raised";

    signal.reset();
    EXPECT_FALSE(signal.wait_for(std::chrono::seconds(0)));
}
