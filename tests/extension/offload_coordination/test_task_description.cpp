/**************************************************************************/
/* test_task_description.cpp                                              */
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
 * @file test_task_description.cpp
 * @brief Tests describe_task(): the name, the operation and the parameters up to the last non-zero one.
 * @see tests/extension/offload_coordination/README.md
 * @author Mustafa Garip
 */

#include <cstdint>

#include <gtest/gtest.h>

#include <SushiRuntime/api/graph/node_options.hpp>
#include <SushiRuntime/core/operation_id.hpp>
#include <SushiRuntime/task/task_types.hpp>

#include <SushiRuntime/offload_coordination/task_description.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Distributed;

/** @brief Tests that trailing zero parameters are not sent and a zero inside the list is. */
TEST(Medium_TaskDescription, TrailingZerosAreDropped)
{
    Internal::Graph::TaskMetadata metadata;
    metadata.name = "advect";
    metadata.op_id = OperationID(9);
    metadata.params[0] = 5;
    metadata.params[2] = 7;
    EXPECT_EQ(used_parameter_count(metadata), 3u);

    const NodeOptions options = describe_task(metadata);
    EXPECT_EQ(options.name, "advect");
    EXPECT_EQ(options.operation, OperationID(9));
    ASSERT_EQ(options.parameters.size(), 3u);
    EXPECT_EQ(options.parameters[1], 0u);
    EXPECT_EQ(options.parameters[2], 7u);
}

/** @brief Tests that a task with no parameter sends none. */
TEST(Medium_TaskDescription, NoParameterSendsNone)
{
    const Internal::Graph::TaskMetadata metadata;
    EXPECT_EQ(used_parameter_count(metadata), 0u);
    EXPECT_TRUE(describe_task(metadata).parameters.empty());
}
