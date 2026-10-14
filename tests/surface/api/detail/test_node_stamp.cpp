/**************************************************************************/
/* test_node_stamp.cpp                                                    */
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
 * @file test_node_stamp.cpp
 * @brief Tests NodeStamp and parameters_refusal(): what a stamp copies and the parameter limit.
 * @see tests/surface/api/detail/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/sushiruntime.hpp>
#include <SushiRuntime/api/detail/node_stamp.hpp>

#include "description_runner.hpp"

using namespace SushiRuntime;

/** @brief Tests that a stamp copies the operation, the width and the parameter count. */
TEST(Unit_NodeStamp, CopiesWhatTheOptionsState)
{
    const std::uint64_t parameters[3] = {1, 2, 3};
    NodeOptions options;
    options.operation = OperationID(42);
    options.width = 12;
    options.parameters = span<const std::uint64_t>(parameters, 3);
    const Detail::NodeStamp stamp(options, "named");
    EXPECT_STREQ(stamp.name, "named");
    EXPECT_EQ(stamp.operation, OperationID(42));
    EXPECT_EQ(stamp.width, 12u);
    EXPECT_EQ(stamp.parameter_count, 3u);
    EXPECT_EQ(Detail::NodeStamp().width, K_WIDTH_UNKNOWN);
}

/** @brief Tests that parameters within the limit pass and one past it is refused. */
TEST(Unit_NodeStamp, OneParameterPastTheLimitIsRefused)
{
    const std::uint64_t parameters[Detail::K_MAX_NODE_PARAMETERS + 1] = {};
    NodeOptions options;
    options.parameters = span<const std::uint64_t>(parameters, Detail::K_MAX_NODE_PARAMETERS);
    EXPECT_FALSE(Detail::parameters_refusal(options, 0).has_value());
    options.parameters = span<const std::uint64_t>(parameters, Detail::K_MAX_NODE_PARAMETERS + 1);
    const std::optional<Error> refusal = Detail::parameters_refusal(options, 0);
    ASSERT_TRUE(refusal.has_value());
    EXPECT_EQ(refusal->code, ErrorCode::InvalidGraph);
}
