/**************************************************************************/
/* test_quiescence_latch.cpp                                              */
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
 * @file test_quiescence_latch.cpp
 * @brief Tests QuiescenceLatch: a wait returns once every added unit retired.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

#include <SushiRuntime/scheduler/quiescence_latch.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Scheduler;

/** @brief Tests that an empty latch's wait returns at once. */
TEST(Small_QuiescenceLatch, AnEmptyLatchDoesNotWait)
{
    QuiescenceLatch latch;
    bool returned = false;

    latch.wait();
    returned = true;

    EXPECT_TRUE(returned);
}

/** @brief Tests that the waiter wakes only after the last of three units retires. */
TEST(Medium_QuiescenceLatch, TheWaiterWakesAfterTheLastRetire)
{
    QuiescenceLatch latch;
    latch.add(3);
    std::atomic<int> retired{0};
    std::thread worker([&]
    {
        retired.fetch_add(1);
        latch.retire();
        retired.fetch_add(1);
        latch.retire();
        retired.fetch_add(1);
        latch.retire();
    });

    latch.wait();

    EXPECT_EQ(retired.load(), 3) << "the wait returned before every unit retired";
    worker.join();
}
