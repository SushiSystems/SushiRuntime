/**************************************************************************/
/* test_manual_heartbeat_clock.cpp                                        */
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
 * @file test_manual_heartbeat_clock.cpp
 * @brief Tests ManualHeartbeatClock, the clock the heartbeat tests advance by hand.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <chrono>
#include <functional>
#include <thread>

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/manual_heartbeat_clock.hpp>

using SushiRuntime::Internal::Scheduler::ManualHeartbeatClock;

namespace
{
    /** @brief Pauses once on @p clock for one heartbeat interval. */
    void pause_once(ManualHeartbeatClock& clock)
    {
        clock.pause(std::chrono::milliseconds(5));
    }
} // namespace

/** @brief Tests that a pause returns once the clock is advanced by one beat, and counts the pause. */
TEST(Medium_ManualHeartbeatClock, AdvancingByOneBeatEndsOnePause)
{
    ManualHeartbeatClock clock;
    std::thread pauser(pause_once, std::ref(clock));
    clock.wait_for_pauses(1);

    clock.advance(1);
    pauser.join();

    EXPECT_EQ(clock.pauses_entered(), 1u);
}

/** @brief Tests that release_all ends a pause that no beat was granted to. */
TEST(Medium_ManualHeartbeatClock, ReleaseAllEndsAPauseNoBeatWasGrantedTo)
{
    ManualHeartbeatClock clock;
    std::thread pauser(pause_once, std::ref(clock));
    clock.wait_for_pauses(1);

    clock.release_all();
    pauser.join();

    EXPECT_EQ(clock.pauses_entered(), 1u);
}
