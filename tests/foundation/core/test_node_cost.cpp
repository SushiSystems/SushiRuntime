/**************************************************************************/
/* test_node_cost.cpp                                                     */
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
 * @file test_node_cost.cpp
 * @brief Tests NodeCost and NodeDType, the work a caller states one dispatch of a node does.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstring>

#include <gtest/gtest.h>

#include <SushiRuntime/core/node_cost.hpp>

using namespace SushiRuntime;

/** @brief Tests that each element type has its name and that an unstated one has none. */
TEST(Small_NodeCost, NamesItsDtype)
{
    EXPECT_STREQ(dtype_name(NodeDType::F32), "f32");
    EXPECT_STREQ(dtype_name(NodeDType::F16), "f16");
    EXPECT_STREQ(dtype_name(NodeDType::BF16), "bf16");
    EXPECT_STREQ(dtype_name(NodeDType::I8), "i8");
    EXPECT_EQ(dtype_name(NodeDType::Unstated), nullptr);
}

/** @brief Tests that a default cost states no element type. */
TEST(Small_NodeCost, ADefaultCostStatesNoDtype)
{
    const NodeCost cost;

    EXPECT_EQ(cost.dtype, NodeDType::Unstated);
}

/** @brief Tests that equality compares every field, the dtype included. */
TEST(Small_NodeCost, EqualityComparesEveryField)
{
    const NodeCost base{10, 20, NodeDType::F16};

    EXPECT_TRUE(base == (NodeCost{10, 20, NodeDType::F16}));
    EXPECT_TRUE(base != (NodeCost{11, 20, NodeDType::F16}));
    EXPECT_TRUE(base != (NodeCost{10, 21, NodeDType::F16}));
    EXPECT_TRUE(base != (NodeCost{10, 20, NodeDType::BF16}));
}
