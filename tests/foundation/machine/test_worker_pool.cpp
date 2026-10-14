/**************************************************************************/
/* test_worker_pool.cpp                                                   */
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
 * @file test_worker_pool.cpp
 * @brief Tests WorkerPool: an empty pool that pins by default.
 * @see tests/foundation/machine/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <SushiRuntime/machine/worker_pool.hpp>

using namespace SushiRuntime;

/** @brief Tests the defaults: no worker, core 0, pinned. */
TEST(Unit_WorkerPool, DefaultsToAnEmptyPinnedPool)
{
    const WorkerPool pool;
    EXPECT_EQ(pool.count, 0u);
    EXPECT_EQ(pool.first_core, 0u);
    EXPECT_EQ(pool.pinning, PinPolicy::Pin);
}
