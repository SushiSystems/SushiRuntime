/**************************************************************************/
/* test_cluster_monitor_step.cpp                                          */
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
 * @file test_cluster_monitor_step.cpp
 * @brief Tests plan_pass(), pass_period() and next_pass_at(): what a monitor pass decides.
 * @see tests/extension/cluster_monitor/README.md
 * @author Mustafa Garip
 */

#include <chrono>

#include <gtest/gtest.h>

#include <SushiRuntime/cluster_monitor/cluster_monitor_step.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;
using std::chrono::milliseconds;

namespace
{
    /** @brief Returns a fixed point of the steady clock plus @p offset. */
    std::chrono::steady_clock::time_point at(milliseconds offset)
    {
        return std::chrono::steady_clock::time_point(std::chrono::hours(1)) + offset;
    }

    /** @brief Returns a reaped task of @p worker with @p cause. */
    ReapedTask reaped(std::uint64_t task_id, std::uint32_t worker, ReapCause cause)
    {
        return ReapedTask{task_id, PeerID{worker}, cause};
    }
} // namespace

/** @brief Tests that a pass that found nothing does nothing. */
TEST(Small_ClusterMonitorStep, APassThatFoundNothingDoesNothing)
{
    MonitorState state;

    const MonitorActions actions = plan_pass(state, MonitorFindings{}, at(milliseconds(0)));

    EXPECT_TRUE(actions.goodbye_and_close.empty());
    EXPECT_TRUE(actions.close.empty());
    EXPECT_TRUE(actions.warnings.empty());
    EXPECT_FALSE(actions.reoffer_held);
}

/** @brief Tests that a silent worker gets a GOODBYE and a close, and a late handshake only a close. */
TEST(Small_ClusterMonitorStep, ASilentWorkerGetsAGoodbyeAndALateHandshakeOnlyAClose)
{
    MonitorState state;
    MonitorFindings findings;
    findings.silent = {PeerID{3}, PeerID{4}};
    findings.handshake_overdue = {PeerID{9}};

    const MonitorActions actions = plan_pass(state, findings, at(milliseconds(0)));

    ASSERT_EQ(actions.goodbye_and_close.size(), 2u);
    EXPECT_EQ(actions.goodbye_and_close[0], PeerID{3});
    EXPECT_EQ(actions.goodbye_and_close[1], PeerID{4});
    ASSERT_EQ(actions.close.size(), 1u);
    EXPECT_EQ(actions.close[0], PeerID{9});
    EXPECT_FALSE(actions.reoffer_held) << "no task was reaped";
}

/** @brief Tests that every reaped task is counted under its own cause. */
TEST(Small_ClusterMonitorStep, EveryReapedTaskIsCountedUnderItsCause)
{
    MonitorState state;
    MonitorFindings findings;
    findings.reaped = {reaped(1, 1, ReapCause::Deadline), reaped(2, 2, ReapCause::Unacknowledged),
                       reaped(3, 3, ReapCause::Unacknowledged), reaped(4, 4, ReapCause::LostResult)};

    const MonitorActions actions = plan_pass(state, findings, at(milliseconds(0)));

    EXPECT_EQ(actions.reaped.deadline, 1u);
    EXPECT_EQ(actions.reaped.unacknowledged, 2u);
    EXPECT_EQ(actions.reaped.lost_result, 1u);
    EXPECT_TRUE(actions.reoffer_held) << "the reaped tasks freed slots a held node may take";
}

/** @brief Tests that one worker gets one warning for all its reaped tasks in a pass. */
TEST(Small_ClusterMonitorStep, OneWorkerGetsOneWarningPerPass)
{
    MonitorState state;
    MonitorFindings findings;
    findings.reaped = {reaped(1, 5, ReapCause::Deadline), reaped(2, 5, ReapCause::LostResult),
                       reaped(3, 6, ReapCause::Deadline)};

    const MonitorActions actions = plan_pass(state, findings, at(milliseconds(0)));

    ASSERT_EQ(actions.warnings.size(), 2u);
    EXPECT_EQ(actions.warnings[0].task_id, 1u);
    EXPECT_EQ(actions.warnings[0].worker, PeerID{5});
    EXPECT_EQ(actions.warnings[1].task_id, 3u);
    EXPECT_EQ(actions.reaped.deadline, 2u) << "every task is counted, the warning is only throttled";
}

/** @brief Tests that a worker warned a moment ago is not warned again, and is after a second. */
TEST(Small_ClusterMonitorStep, AWorkerIsWarnedAgainOnlyAfterASecond)
{
    MonitorState state;
    MonitorFindings findings;
    findings.reaped = {reaped(1, 5, ReapCause::Deadline)};

    ASSERT_EQ(plan_pass(state, findings, at(milliseconds(0))).warnings.size(), 1u);
    EXPECT_TRUE(plan_pass(state, findings, at(milliseconds(999))).warnings.empty())
        << "999 ms after the first warning is inside the window";
    EXPECT_EQ(plan_pass(state, findings, at(milliseconds(1000))).warnings.size(), 1u)
        << "at one second the first warning has expired";
}

/** @brief Tests that the period is the configured one while the timeouts allow it. */
TEST(Small_ClusterMonitorStep, ThePeriodIsTheConfiguredOneWhileTheTimeoutsAllowIt)
{
    EXPECT_EQ(pass_period(milliseconds(250), milliseconds(5000), milliseconds(30000)),
              milliseconds(250));
}

/** @brief Tests that a quarter of the liveness timeout caps the period. */
TEST(Small_ClusterMonitorStep, AQuarterOfTheLivenessTimeoutCapsThePeriod)
{
    EXPECT_EQ(pass_period(milliseconds(250), milliseconds(100), milliseconds(30000)),
              milliseconds(25));
}

/** @brief Tests that a tighter task deadline caps the period, and a deadline of zero does not. */
TEST(Small_ClusterMonitorStep, ATighterTaskDeadlineCapsThePeriodAndZeroDoesNot)
{
    EXPECT_EQ(pass_period(milliseconds(250), milliseconds(5000), milliseconds(40)),
              milliseconds(10));
    EXPECT_EQ(pass_period(milliseconds(250), milliseconds(5000), milliseconds(0)),
              milliseconds(250))
        << "a deadline of 0 sets no bound";
}

/** @brief Tests that the period never falls below one millisecond. */
TEST(Small_ClusterMonitorStep, ThePeriodNeverFallsBelowOneMillisecond)
{
    EXPECT_EQ(pass_period(milliseconds(250), milliseconds(2), milliseconds(0)), milliseconds(1));
}

/** @brief Tests that the next pass is the earliest of the period, the hold and the deadline. */
TEST(Small_ClusterMonitorStep, TheNextPassIsTheEarliestOfThePeriodTheHoldAndTheDeadline)
{
    const auto never = std::chrono::steady_clock::time_point::max();

    EXPECT_EQ(next_pass_at(at(milliseconds(0)), milliseconds(250), never, never),
              at(milliseconds(250)));
    EXPECT_EQ(next_pass_at(at(milliseconds(0)), milliseconds(250), at(milliseconds(30)), never),
              at(milliseconds(30)));
    EXPECT_EQ(next_pass_at(at(milliseconds(0)), milliseconds(250), at(milliseconds(30)),
                           at(milliseconds(20))),
              at(milliseconds(20)));
}

/** @brief Tests that each cause has its own words. */
TEST(Small_ClusterMonitorStep, EachCauseHasItsOwnWords)
{
    EXPECT_STRNE(reap_cause_words(ReapCause::Deadline), reap_cause_words(ReapCause::Unacknowledged));
    EXPECT_STRNE(reap_cause_words(ReapCause::Unacknowledged), reap_cause_words(ReapCause::LostResult));
    EXPECT_STRNE(reap_cause_words(ReapCause::Deadline), reap_cause_words(ReapCause::LostResult));
}
