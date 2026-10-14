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
 * @file test_completion_signal.cpp
 * @brief Tests CompletionSignal: a wait times out, a raise wakes it, and reset clears it.
 * @see tests/foundation/measurement/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include <SushiRuntime/measurement/completion_signal.hpp>

using namespace SushiRuntime::Test;

/** @brief Tests that a wait with nothing raised returns false. */
TEST(Unit_CompletionSignal, AWaitWithNoRaiseTimesOut)
{
    CompletionSignal signal;
    EXPECT_FALSE(signal.wait_for(std::chrono::seconds(0)));
}

/** @brief Tests that a raise on another thread wakes the waiter, and reset() clears it. */
TEST(Unit_CompletionSignal, ARaiseWakesTheWaiterAndResetClearsIt)
{
    CompletionSignal signal;
    std::thread raiser([&signal] { signal.raise(); });
    EXPECT_TRUE(signal.wait_for(std::chrono::seconds(30)));
    raiser.join();

    EXPECT_TRUE(signal.wait_for(std::chrono::seconds(0))) << "a raised signal stays raised";
    signal.reset();
    EXPECT_FALSE(signal.wait_for(std::chrono::seconds(0)));
}
