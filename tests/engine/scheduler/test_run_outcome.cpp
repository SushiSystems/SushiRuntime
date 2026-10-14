/**************************************************************************/
/* test_run_outcome.cpp                                                   */
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
 * @file test_run_outcome.cpp
 * @brief Tests RunOutcome: the first failure wins, cancellation, clearing and aborting.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <optional>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>

#include <SushiRuntime/scheduler/run_outcome.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Scheduler;

/** @brief Tests that the first recorded failure is kept and a later one is not. */
TEST(Unit_RunOutcome, TheFirstFailureWins)
{
    RunOutcome outcome;
    EXPECT_FALSE(outcome.has_failed());
    EXPECT_FALSE(outcome.error().has_value());
    outcome.record(Error{ErrorCode::KernelFailed, "first"});
    outcome.record(Error{ErrorCode::DeviceLost, "second"});
    ASSERT_TRUE(outcome.error().has_value());
    EXPECT_EQ(outcome.error()->message, "first");
    EXPECT_TRUE(outcome.is_aborting());

    outcome.clear_failure();
    EXPECT_FALSE(outcome.has_failed());
    EXPECT_FALSE(outcome.error().has_value());
    EXPECT_FALSE(outcome.is_aborting());
}

/** @brief Tests that cancellation aborts on its own and clears on its own. */
TEST(Unit_RunOutcome, CancellationAbortsWithoutAFailure)
{
    RunOutcome outcome;
    outcome.cancel();
    EXPECT_TRUE(outcome.is_cancelled());
    EXPECT_TRUE(outcome.is_aborting());
    EXPECT_FALSE(outcome.has_failed());
    outcome.clear_cancellation();
    EXPECT_FALSE(outcome.is_aborting());
}
