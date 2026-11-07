/**************************************************************************/
/* test_link_tick.cpp                                                     */
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
 * @file test_link_tick.cpp
 * @brief Tests plan_link_tick(): what the link's thread does at a given time.
 * @see tests/extension/master_link/README.md
 * @author Mustafa Garip
 */

#include <chrono>

#include <gtest/gtest.h>

#include <SushiRuntime/master_link/link_tick.hpp>

using namespace SushiRuntime::Distributed;
using std::chrono::milliseconds;

namespace
{
    /** @brief Returns a fixed point of the steady clock plus @p offset. */
    std::chrono::steady_clock::time_point at(milliseconds offset)
    {
        return std::chrono::steady_clock::time_point(std::chrono::hours(1)) + offset;
    }

    /** @brief Returns the state of a registered link that heard from the master at time 0. */
    LinkTickInput registered(milliseconds liveness)
    {
        LinkTickInput input;
        input.running = true;
        input.phase = LinkPhase::Registered;
        input.last_frame = at(milliseconds(0));
        input.liveness = liveness;
        input.heartbeat_at = at(milliseconds(10));
        input.heartbeat_interval = milliseconds(10);
        return input;
    }
} // namespace

/** @brief Tests that an idle link has nothing due and no wake. */
TEST(Small_LinkTick, AnIdleLinkWaitsForAChange)
{
    LinkTickInput input;
    input.running = true;

    const LinkTick tick = plan_link_tick(input, at(milliseconds(0)));

    EXPECT_EQ(tick.action, LinkAction::Wait);
    EXPECT_FALSE(tick.wake.has_value());
}

/** @brief Tests that a registered link wakes at the earlier of its heartbeat and its watch. */
TEST(Small_LinkTick, ARegisteredLinkWakesAtTheEarlierOfTheHeartbeatAndTheWatch)
{
    const LinkTick watched = plan_link_tick(registered(milliseconds(40)), at(milliseconds(1)));
    EXPECT_EQ(watched.action, LinkAction::Wait);
    EXPECT_EQ(watched.wake, at(milliseconds(10)));

    const LinkTick near_watch = plan_link_tick(registered(milliseconds(5)), at(milliseconds(1)));
    EXPECT_EQ(near_watch.wake, at(milliseconds(5)));

    const LinkTick unwatched = plan_link_tick(registered(milliseconds(0)), at(milliseconds(1)));
    EXPECT_EQ(unwatched.wake, at(milliseconds(10))) << "a liveness of 0 sets no watch";
}

/** @brief Tests that a due heartbeat is sent and the next one is an interval away. */
TEST(Small_LinkTick, ADueHeartbeatIsSentAndTheNextIsAnIntervalAway)
{
    const LinkTick tick = plan_link_tick(registered(milliseconds(0)), at(milliseconds(10)));

    EXPECT_EQ(tick.action, LinkAction::Heartbeat);
    EXPECT_EQ(tick.heartbeat_at, at(milliseconds(20)));
    EXPECT_EQ(tick.phase, LinkPhase::Registered);
}

/** @brief Tests that a master silent for the liveness timeout is closed, and the link waits for the loss. */
TEST(Small_LinkTick, ASilentMasterIsClosedAtTheLivenessTimeout)
{
    const LinkTick before = plan_link_tick(registered(milliseconds(40)), at(milliseconds(39)));
    EXPECT_NE(before.action, LinkAction::CloseSilent);

    const LinkTick tick = plan_link_tick(registered(milliseconds(40)), at(milliseconds(41)));

    EXPECT_EQ(tick.action, LinkAction::CloseSilent);
    EXPECT_EQ(tick.phase, LinkPhase::Closing);
    EXPECT_EQ(tick.silence, milliseconds(41));
    EXPECT_EQ(tick.limit, milliseconds(40));
}

/** @brief Tests that a link closing a silent master takes no second action. */
TEST(Small_LinkTick, AClosingLinkTakesNoSecondAction)
{
    LinkTickInput input = registered(milliseconds(40));
    input.phase = LinkPhase::Closing;

    const LinkTick tick = plan_link_tick(input, at(milliseconds(500)));

    EXPECT_EQ(tick.action, LinkAction::Wait);
    EXPECT_FALSE(tick.wake.has_value());
}

/** @brief Tests that a handshake with no WELCOME in an interval sends HELLO again. */
TEST(Small_LinkTick, AHandshakeWithNoWelcomeRestarts)
{
    LinkTickInput input;
    input.running = true;
    input.phase = LinkPhase::HelloSent;
    input.hello_at = at(milliseconds(0));
    input.heartbeat_interval = milliseconds(10);

    const LinkTick waiting = plan_link_tick(input, at(milliseconds(9)));
    EXPECT_EQ(waiting.action, LinkAction::Wait);
    EXPECT_EQ(waiting.wake, at(milliseconds(10)));

    const LinkTick restart = plan_link_tick(input, at(milliseconds(10)));
    EXPECT_EQ(restart.action, LinkAction::Restart);
    EXPECT_EQ(restart.hello_at, at(milliseconds(10)));

    input.phase = LinkPhase::ProofSent;
    EXPECT_EQ(plan_link_tick(input, at(milliseconds(10))).action, LinkAction::Restart)
        << "a PROOF with no WELCOME restarts too";
}

/** @brief Tests that an ended link stops its transport once. */
TEST(Small_LinkTick, AnEndedLinkStopsItsTransportOnce)
{
    LinkTickInput input;
    input.ended = true;

    const LinkTick first = plan_link_tick(input, at(milliseconds(0)));
    EXPECT_EQ(first.action, LinkAction::StopTransport);
    EXPECT_TRUE(first.transport_stopped);

    input.transport_stopped = true;
    EXPECT_EQ(plan_link_tick(input, at(milliseconds(1))).action, LinkAction::Wait);
}

/** @brief Tests that a link that is leaving does nothing at all. */
TEST(Small_LinkTick, ALeavingLinkDoesNothing)
{
    LinkTickInput input = registered(milliseconds(40));
    input.running = false;

    const LinkTick tick = plan_link_tick(input, at(milliseconds(500)));

    EXPECT_EQ(tick.action, LinkAction::Wait);
    EXPECT_FALSE(tick.wake.has_value());
}
