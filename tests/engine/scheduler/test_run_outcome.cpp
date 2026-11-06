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

namespace
{
    /** @brief Records a failure named "first" and then one named "second" on @p outcome. */
    void record_two_failures(RunOutcome& outcome)
    {
        outcome.record(Error{ErrorCode::KernelFailed, "first"});
        outcome.record(Error{ErrorCode::DeviceLost, "second"});
    }
} // namespace

/** @brief Tests that the first recorded failure is kept and a later one is not. */
TEST(Small_RunOutcome, TheFirstFailureWins)
{
    RunOutcome outcome;

    record_two_failures(outcome);

    ASSERT_TRUE(outcome.error().has_value());
    EXPECT_EQ(outcome.error()->message, "first");
}

/** @brief Tests that a recorded failure aborts the run. */
TEST(Small_RunOutcome, AFailureAbortsTheRun)
{
    RunOutcome outcome;
    EXPECT_FALSE(outcome.is_aborting());

    outcome.record(Error{ErrorCode::KernelFailed, "first"});

    EXPECT_TRUE(outcome.has_failed());
    EXPECT_TRUE(outcome.is_aborting());
}

/** @brief Tests that clear_failure resets the failure and the abort it caused. */
TEST(Small_RunOutcome, ClearingTheFailureResetsTheFailureAndTheAbort)
{
    RunOutcome outcome;
    record_two_failures(outcome);

    outcome.clear_failure();

    EXPECT_FALSE(outcome.has_failed());
    EXPECT_FALSE(outcome.error().has_value());
    EXPECT_FALSE(outcome.is_aborting());
}

/** @brief Tests that cancellation aborts the run and records no failure. */
TEST(Small_RunOutcome, CancellationAbortsWithoutAFailure)
{
    RunOutcome outcome;

    outcome.cancel();

    EXPECT_TRUE(outcome.is_cancelled());
    EXPECT_TRUE(outcome.is_aborting());
    EXPECT_FALSE(outcome.has_failed());
}

/** @brief Tests that clearing the cancellation lifts the abort. */
TEST(Small_RunOutcome, ClearingTheCancellationLiftsTheAbort)
{
    RunOutcome outcome;
    outcome.cancel();

    outcome.clear_cancellation();

    EXPECT_FALSE(outcome.is_cancelled());
    EXPECT_FALSE(outcome.is_aborting());
}
