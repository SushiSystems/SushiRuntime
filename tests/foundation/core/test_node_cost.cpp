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

#include <type_traits>

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

/** @brief Tests that a default cost states no element type, the generic kind and zeros. */
TEST(Small_NodeCost, ADefaultCostStatesNoDtype)
{
    const NodeCost cost;

    EXPECT_EQ(cost.dtype, NodeDType::Unstated);
    EXPECT_EQ(cost.kind, OperationKinds::K_GENERIC);
    EXPECT_EQ(cost.operations, 0u);
    EXPECT_EQ(cost.read_bytes, 0u);
    EXPECT_EQ(cost.written_bytes, 0u);
}

/** @brief Tests that read and written bytes are held apart and that moved bytes is their sum. */
TEST(Small_NodeCost, HoldsReadAndWrittenBytesApart)
{
    const NodeCost cost{100, 48, 16, OperationKinds::K_COMPUTE_BOUND, NodeDType::F32};

    EXPECT_EQ(cost.operations, 100u);
    EXPECT_EQ(cost.read_bytes, 48u);
    EXPECT_EQ(cost.written_bytes, 16u);
    EXPECT_EQ(cost.moved_bytes(), 64u);
    EXPECT_EQ(cost.kind, OperationKinds::K_COMPUTE_BOUND);
}

/** @brief Tests that a cost is a trivially copyable value. */
TEST(Small_NodeCost, IsATriviallyCopyableValue)
{
    EXPECT_TRUE(std::is_trivially_copyable<NodeCost>::value);
}

/** @brief Tests that equality compares every field, the kind and the dtype included. */
TEST(Small_NodeCost, EqualityComparesEveryField)
{
    const NodeCost base{10, 20, 30, OperationKinds::K_MEMORY_BOUND, NodeDType::F16};

    EXPECT_TRUE(base == (NodeCost{10, 20, 30, OperationKinds::K_MEMORY_BOUND, NodeDType::F16}));
    EXPECT_TRUE(base != (NodeCost{11, 20, 30, OperationKinds::K_MEMORY_BOUND, NodeDType::F16}));
    EXPECT_TRUE(base != (NodeCost{10, 21, 30, OperationKinds::K_MEMORY_BOUND, NodeDType::F16}));
    EXPECT_TRUE(base != (NodeCost{10, 20, 31, OperationKinds::K_MEMORY_BOUND, NodeDType::F16}));
    EXPECT_TRUE(base != (NodeCost{10, 20, 30, OperationKinds::K_GENERIC, NodeDType::F16}));
    EXPECT_TRUE(base != (NodeCost{10, 20, 30, OperationKinds::K_MEMORY_BOUND, NodeDType::BF16}));
}
