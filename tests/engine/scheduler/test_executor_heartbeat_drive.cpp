/**************************************************************************/
/* test_executor_heartbeat_drive.cpp                                      */
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
 * @file test_executor_heartbeat_drive.cpp
 * @brief Tests ExecutorHeartbeatDrive on a SeededExecutor and a clock the test advances.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/core/clock.hpp>
#include <SushiRuntime/core/seeded_executor.hpp>

#include <SushiRuntime/scheduler/executor_heartbeat_drive.hpp>

using SushiRuntime::Internal::Scheduler::ExecutorHeartbeatDrive;
using SushiRuntime::Internal::Scheduler::K_HEARTBEAT_INTERVAL;

namespace
{
    /** @brief The heartbeat interval in clock nanoseconds. */
    constexpr std::uint64_t K_INTERVAL_NS =
        static_cast<std::uint64_t>(std::chrono::nanoseconds(K_HEARTBEAT_INTERVAL).count());

    /** @brief Runs @p jobs jobs of @p executor, one at a time. */
    void run_jobs(SushiRuntime::SeededExecutor& executor, int jobs)
    {
        for (int job = 0; job < jobs; ++job)
            static_cast<void>(executor.run_one());
    }
} // namespace

/** @brief Holds a drive over a seeded executor and a manual clock, and counts its beats. */
class Small_ExecutorHeartbeatDrive : public ::testing::Test
{
    protected:
        /// @brief Runs the drive's job.
        SushiRuntime::SeededExecutor executor_;
        /// @brief The clock the job reads.
        SushiRuntime::ManualClock clock_;
        /// @brief The drive under test.
        ExecutorHeartbeatDrive drive_{executor_, clock_};
        /// @brief Beats the drive has made.
        int beats_ = 0;
};

/** @brief Tests that the job beats once, then once more for each interval the clock moves on. */
TEST_F(Small_ExecutorHeartbeatDrive, BeatsOnceForEachIntervalTheClockMoves)
{
    drive_.start([this]() { ++beats_; });
    run_jobs(executor_, 3);
    ASSERT_EQ(beats_, 1) << "no interval has passed after the first beat";

    clock_.advance(K_INTERVAL_NS);
    run_jobs(executor_, 3);

    EXPECT_EQ(beats_, 2);
    drive_.stop();
}

/** @brief Tests that the job neither beats nor posts itself again after stop(). */
TEST_F(Small_ExecutorHeartbeatDrive, NoBeatFollowsStop)
{
    drive_.start([this]() { ++beats_; });
    drive_.stop();

    clock_.advance(10 * K_INTERVAL_NS);
    run_jobs(executor_, 3);

    EXPECT_EQ(beats_, 0);
    EXPECT_EQ(executor_.pending(), 0u);
}

/** @brief Tests that a second start(), and a restart while the job is still posted, leave one job. */
TEST_F(Small_ExecutorHeartbeatDrive, ASecondStartLeavesOneJob)
{
    drive_.start([this]() { ++beats_; });
    drive_.start([this]() { ++beats_; });
    ASSERT_EQ(executor_.pending(), 1u);
    drive_.stop();

    drive_.start([this]() { ++beats_; });

    EXPECT_EQ(executor_.pending(), 1u) << "a restart reuses the job still posted";
    drive_.stop();
}
