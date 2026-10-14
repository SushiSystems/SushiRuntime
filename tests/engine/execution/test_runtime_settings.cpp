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
 * @file test_runtime_settings.cpp
 * @brief Tests RuntimeSettings: the defaults and the pool settings it hands the scheduler.
 * @see tests/engine/execution/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/machine/pin_policy.hpp>
#include <SushiRuntime/scheduler/worker_pool_settings.hpp>

#include <SushiRuntime/execution/runtime_settings.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Execution;

/** @brief Tests the defaults: no profiling, one worker per core, pinned, no rebalancer. */
TEST(Unit_RuntimeSettings, DefaultsAskForNothingExtra)
{
    const RuntimeSettings settings;
    EXPECT_FALSE(settings.profiling);
    EXPECT_EQ(settings.worker_count, 0u);
    EXPECT_EQ(settings.first_core, 0u);
    EXPECT_EQ(settings.pinning, PinPolicy::Pin);
    EXPECT_FALSE(settings.rebalancer);
    EXPECT_FALSE(settings.force_scheduler);
}

/** @brief Tests that the pool settings carry the three fields of the pool, unchanged. */
TEST(Unit_RuntimeSettings, ThePoolSettingsCarryThePoolFields)
{
    RuntimeSettings settings;
    settings.worker_count = 6;
    settings.first_core = 2;
    settings.pinning = PinPolicy::Float;
    settings.profiling = true;
    const Internal::Scheduler::WorkerPoolSettings pool = worker_pool_settings(settings);
    EXPECT_EQ(pool.worker_count, 6u);
    EXPECT_EQ(pool.first_core, 2u);
    EXPECT_EQ(pool.pinning, PinPolicy::Float);
}
