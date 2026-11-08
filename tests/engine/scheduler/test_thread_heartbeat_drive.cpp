/**************************************************************************/
/* test_thread_heartbeat_drive.cpp                                        */
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
 * @file test_thread_heartbeat_drive.cpp
 * @brief Tests ThreadHeartbeatDrive on a ManualHeartbeatClock the test steps.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <atomic>

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/manual_heartbeat_clock.hpp>

#include <SushiRuntime/scheduler/thread_heartbeat_drive.hpp>

using SushiRuntime::Internal::Scheduler::ManualHeartbeatClock;
using SushiRuntime::Internal::Scheduler::ThreadHeartbeatDrive;

/** @brief Holds a drive pausing on a manual clock, and counts its beats. */
class Medium_ThreadHeartbeatDrive : public ::testing::Test
{
    protected:
        /// @brief The clock the thread pauses on; declared first, so it outlives the drive.
        ManualHeartbeatClock clock_;
        /// @brief Beats the drive has made, on its thread.
        std::atomic<int> beats_{0};
        /// @brief The drive under test.
        ThreadHeartbeatDrive drive_{clock_};
};

/** @brief Tests that the thread beats once, then once more for each beat the clock grants. */
TEST_F(Medium_ThreadHeartbeatDrive, BeatsOnceForEachBeatTheClockGrants)
{
    drive_.start([this]() { beats_.fetch_add(1); });
    clock_.wait_for_pauses(1);
    clock_.advance(1);
    clock_.wait_for_pauses(2);

    EXPECT_EQ(beats_.load(), 2);
    clock_.release_all();
    drive_.stop();
}

/** @brief Tests that no beat follows the return of stop(), however far the clock advances. */
TEST_F(Medium_ThreadHeartbeatDrive, NoBeatFollowsStop)
{
    drive_.start([this]() { beats_.fetch_add(1); });
    clock_.wait_for_pauses(1);
    clock_.release_all();
    drive_.stop();
    const int at_stop = beats_.load();

    clock_.advance(3);

    EXPECT_EQ(beats_.load(), at_stop);
}

/** @brief Tests that a second start() leaves one beating thread, not two. */
TEST_F(Medium_ThreadHeartbeatDrive, ASecondStartLeavesOneThread)
{
    drive_.start([this]() { beats_.fetch_add(1); });
    drive_.start([this]() { beats_.fetch_add(1); });
    clock_.wait_for_pauses(1);
    clock_.advance(1);
    clock_.wait_for_pauses(2);

    EXPECT_EQ(beats_.load(), 2);
    clock_.release_all();
    drive_.stop();
}
