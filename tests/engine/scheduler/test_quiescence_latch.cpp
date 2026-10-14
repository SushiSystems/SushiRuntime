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

/** @brief Tests that an empty latch does not wait. */
TEST(Unit_QuiescenceLatch, AnEmptyLatchDoesNotWait)
{
    QuiescenceLatch latch;
    latch.wait();
    SUCCEED();
}

/** @brief Tests that the waiter wakes only after the last of three units retires. */
TEST(Unit_QuiescenceLatch, TheWaiterWakesAfterTheLastRetire)
{
    QuiescenceLatch latch;
    latch.add(3);
    std::atomic<int> retired{0};
    std::thread worker([&]
    {
        for (int unit = 0; unit < 3; ++unit)
        {
            retired.fetch_add(1);
            latch.retire();
        }
    });
    latch.wait();
    EXPECT_EQ(retired.load(), 3) << "the wait returned before every unit retired";
    worker.join();
}
