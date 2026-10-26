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
 * @file test_node_cost.cpp
 * @brief Tests NodeCost and NodeDType, the work a caller states one dispatch of a node does.
 * @see tests/foundation/core/README.md
 * @author Mustafa Garip
 */

#include <cstring>
#include <optional>

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

/** @brief Tests that a default cost states no element type and holds zeros. */
TEST(Small_NodeCost, ADefaultCostHoldsZerosAndNoDtype)
{
    const NodeCost cost;

    EXPECT_EQ(cost.flops, 0u);
    EXPECT_EQ(cost.bytes, 0u);
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

/** @brief Tests that an optional cost tells an unstated cost from a stated zero. */
TEST(Small_NodeCost, AnOptionalCostTellsAnUnstatedCostFromAStatedZero)
{
    std::optional<NodeCost> unstated;
    std::optional<NodeCost> zero = NodeCost{};

    EXPECT_FALSE(unstated.has_value());
    ASSERT_TRUE(zero.has_value());
    EXPECT_EQ(zero->flops, 0u);
    EXPECT_EQ(zero->bytes, 0u);
}
