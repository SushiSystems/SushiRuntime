/**************************************************************************/
/* test_placement_decision.cpp                                            */
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
 * @file test_placement_decision.cpp
 * @brief Tests PlacementDecision: its default and what each outcome carries.
 * @author Mustafa Garip
 * @see tests/extension/cluster_load/README.md
 */

#include <chrono>
#include <variant>

#include <gtest/gtest.h>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/cluster_load/placement_decision.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that a default decision names no worker and device 0. */
TEST(Small_PlacementDecision, DefaultIsAWorkerOnDeviceZeroWithNoId)
{
    const PlacementDecision decision;
    ASSERT_TRUE(std::holds_alternative<RunOnWorker>(decision));
    EXPECT_EQ(std::get<RunOnWorker>(decision).worker_id, -1);
    EXPECT_EQ(std::get<RunOnWorker>(decision).device_index, 0u);
}

/** @brief Tests that a wait carries its end and a failure its error. */
TEST(Small_PlacementDecision, WaitAndFailCarryTheirFacts)
{
    const auto until = std::chrono::steady_clock::time_point(std::chrono::seconds(5));
    const PlacementDecision wait = WaitForTarget{until};
    EXPECT_EQ(std::get<WaitForTarget>(wait).until, until);

    const PlacementDecision fail = FailRun{Error{ErrorCode::InvalidState, "absent"}};
    EXPECT_EQ(std::get<FailRun>(fail).error.code, ErrorCode::InvalidState);
    EXPECT_EQ(std::get<FailRun>(fail).error.message, "absent");
}
