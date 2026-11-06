/**************************************************************************/
/* test_runtime_settings.cpp                                              */
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
 * @file test_runtime_settings.cpp
 * @brief Tests RuntimeSettings: the pool settings it hands the scheduler.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/machine/pin_policy.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>

#include <SushiRuntime/execution/runtime_settings.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Execution;

/** @brief Tests that settings nobody touched ask the pool for one worker per core, from core 0, pinned. */
TEST(Small_RuntimeSettings, UntouchedSettingsAskThePoolForOneWorkerPerCorePinned)
{
    const Internal::Scheduler::WorkerPoolSettings pool = worker_pool_settings(RuntimeSettings{});

    EXPECT_EQ(pool.worker_count, 0u);
    EXPECT_EQ(pool.first_core, 0u);
    EXPECT_EQ(pool.pinning, PinPolicy::Pin);
}

/** @brief Tests that the pool settings carry the three fields of the pool, unchanged. */
TEST(Small_RuntimeSettings, ThePoolSettingsCarryThePoolFields)
{
    RuntimeSettings settings;
    settings.worker_count = 6;
    settings.first_core = 2;
    settings.pinning = PinPolicy::Float;

    const Internal::Scheduler::WorkerPoolSettings pool = worker_pool_settings(settings);

    EXPECT_EQ(pool.worker_count, 6u);
    EXPECT_EQ(pool.first_core, 2u);
    EXPECT_EQ(pool.pinning, PinPolicy::Float);
}
