/**************************************************************************/
/* test_stop_signal.cpp                                                   */
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
 * @file test_stop_signal.cpp
 * @brief Tests StopSignal: the flag starts clear, request() sets it.
 * @see tests/extension/process_environment/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/process_environment/stop_signal.hpp>

using namespace SushiRuntime::Distributed;

/** @brief Tests that the flag starts clear, request() sets it and a later object clears it. */
TEST(Medium_StopSignalTest, RequestSetsTheFlagAndANewObjectClearsIt)
{
    {
        StopSignal stop_signal;
        EXPECT_FALSE(stop_signal.is_requested());

        stop_signal.request();

        EXPECT_TRUE(stop_signal.is_requested());
    }

    const StopSignal next;
    EXPECT_FALSE(next.is_requested());
}
