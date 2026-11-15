/**************************************************************************/
/* test_run_description.cpp                                               */
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
 * @file test_run_description.cpp
 * @brief Tests RunDescription: every count starts at zero and keeps what it is given.
 * @see tests/foundation/placement/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

#include <SushiRuntime/placement/run_description.hpp>

using namespace SushiRuntime;

/** @brief Tests that a description made with no values describes an empty run. */
TEST(Small_RunDescription, ADefaultDescriptionCountsNothing)
{
    const RunDescription run;

    EXPECT_EQ(run.node_count, 0u);
    EXPECT_EQ(run.steps, 0u);
    EXPECT_EQ(run.launched_items, 0u);
    EXPECT_EQ(run.unsized_nodes, 0u);
    EXPECT_EQ(run.declared_operations, 0u);
    EXPECT_EQ(run.declared_bytes, 0u);
    EXPECT_EQ(run.costed_nodes, 0u);
}

/** @brief Tests that a description is a plain value a policy may copy. */
TEST(Small_RunDescription, ADescriptionIsATriviallyCopyableValue)
{
    EXPECT_TRUE(std::is_trivially_copyable<RunDescription>::value);

    RunDescription run;
    run.node_count = 2;
    run.steps = 3;
    run.launched_items = std::uint64_t{1} << 40;
    run.unsized_nodes = 1;
    run.declared_operations = 5;
    run.declared_bytes = 7;
    run.costed_nodes = 1;
    const RunDescription copied = run;

    EXPECT_EQ(copied.node_count, 2u);
    EXPECT_EQ(copied.steps, 3u);
    EXPECT_EQ(copied.launched_items, std::uint64_t{1} << 40);
    EXPECT_EQ(copied.unsized_nodes, 1u);
    EXPECT_EQ(copied.declared_operations, 5u);
    EXPECT_EQ(copied.declared_bytes, 7u);
    EXPECT_EQ(copied.costed_nodes, 1u);
}
