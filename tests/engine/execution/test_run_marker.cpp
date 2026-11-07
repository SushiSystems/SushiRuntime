/**************************************************************************/
/* test_run_marker.cpp                                                   */
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
 * @file test_run_marker.cpp
 * @brief Tests RunMarker: which runs show as in progress, and nested runs.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/execution/run_marker.hpp>

using SushiRuntime::Internal::Execution::RunMarker;

/** @brief Tests that a fresh marker shows no run. */
TEST(Small_RunMarker, AFreshMarkerShowsNoRun)
{
    const RunMarker marker;

    EXPECT_FALSE(marker.in_progress());
}

/** @brief Tests that a chained run is in progress from its begin until its end. */
TEST(Small_RunMarker, AChainedRunIsInProgressUntilItEnds)
{
    RunMarker marker;

    marker.begin_chained();
    EXPECT_TRUE(marker.in_progress());
    marker.end_chained();

    EXPECT_FALSE(marker.in_progress());
}

/** @brief Tests that a scheduled run inside a chained run leaves the chained run showing. */
TEST(Small_RunMarker, AScheduledRunInsideAChainedRunLeavesTheChainedRunShowing)
{
    RunMarker marker;
    marker.begin_chained();

    ASSERT_TRUE(marker.try_begin_scheduled());
    marker.end_scheduled();

    EXPECT_TRUE(marker.in_progress()) << "the scheduled run's end cleared the chained run's mark";
    marker.end_chained();
    EXPECT_FALSE(marker.in_progress());
}

/** @brief Tests that the end of one chained run leaves another that is still open showing. */
TEST(Small_RunMarker, OneChainedRunsEndLeavesTheOtherShowing)
{
    RunMarker marker;
    marker.begin_chained();
    marker.begin_chained();

    marker.end_chained();

    EXPECT_TRUE(marker.in_progress()) << "one chained run's end cleared the other's";
    marker.end_chained();
    EXPECT_FALSE(marker.in_progress());
}

/** @brief Tests that a second scheduled run is refused while the first holds the marker. */
TEST(Small_RunMarker, ASecondScheduledRunIsRefused)
{
    RunMarker marker;
    ASSERT_TRUE(marker.try_begin_scheduled());

    EXPECT_FALSE(marker.try_begin_scheduled());
    marker.end_scheduled();
    EXPECT_FALSE(marker.in_progress());
}

/** @brief Tests that an idle change is taken only from an idle marker and is no run. */
TEST(Small_RunMarker, AnIdleChangeIsTakenOnlyWhenIdleAndIsNoRun)
{
    RunMarker marker;
    marker.begin_chained();
    EXPECT_FALSE(marker.begin_idle_change()) << "a run is in progress";
    marker.end_chained();

    ASSERT_TRUE(marker.begin_idle_change());
    EXPECT_FALSE(marker.in_progress());
    EXPECT_FALSE(marker.begin_idle_change()) << "the change already holds the bit";
    marker.end_idle_change();
    EXPECT_TRUE(marker.begin_idle_change());
}
