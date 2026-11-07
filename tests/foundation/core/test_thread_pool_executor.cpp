/**************************************************************************/
/* test_thread_pool_executor.cpp                                          */
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
 * @file test_thread_pool_executor.cpp
 * @brief Tests ThreadPoolExecutor's own promises: who runs a job, and when.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <thread>

#include <gtest/gtest.h>

#include <SushiRuntime/core/thread_pool_executor.hpp>

using SushiRuntime::ThreadPoolExecutor;

/** @brief Tests that a pool of no workers runs a posted job only when run_one is called. */
TEST(Small_ThreadPoolExecutor, APoolOfNoWorkersRunsAJobOnlyInsideRunOne)
{
    ThreadPoolExecutor executor(0);
    int ran = 0;
    executor.post([&ran]() { ++ran; });
    const int before = ran;

    const bool stepped = executor.run_one();

    EXPECT_EQ(before, 0);
    EXPECT_TRUE(stepped);
    EXPECT_EQ(ran, 1);
}

/** @brief Tests that a pool's worker, not the posting thread, runs a job nobody steps. */
TEST(Medium_ThreadPoolExecutor, AJobRunsOnAWorkerThread)
{
    ThreadPoolExecutor executor(1);
    std::thread::id runner;
    executor.post([&runner]() { runner = std::this_thread::get_id(); });

    executor.wait_idle();

    EXPECT_NE(runner, std::this_thread::get_id());
}
