/**************************************************************************/
/* test_node_table.cpp                                                    */
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
 * @file test_node_table.cpp
 * @brief Tests NodeTable, the names, operations and costs of a plan's nodes by registration index.
 * @see tests/foundation/report/README.md
 * @author Mustafa Garip
 */

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/core/node_cost.hpp>
#include <SushiRuntime/report/node_table.hpp>

using namespace SushiRuntime;

namespace
{
    /** @brief Builds a row for registration index @p index named @p name. */
    NodeTableRow row_of(std::uint32_t index, const char* name)
    {
        NodeTableRow row;
        row.index = index;
        row.name = name;
        row.multiplicity = 1;
        return row;
    }
} // namespace

/** @brief Tests that a row is found by its registration index whatever order the rows came in. */
TEST(Small_NodeTable, FindsARowByRegistrationIndex)
{
    const NodeTable table(std::vector<NodeTableRow>{row_of(5, "e"), row_of(1, "b"), row_of(3, "d")});

    ASSERT_EQ(table.size(), 3u);
    ASSERT_NE(table.find(3), nullptr);
    EXPECT_EQ(table.find(3)->name, "d");
    EXPECT_EQ(table.find(1)->name, "b");
    EXPECT_EQ(table.find(5)->name, "e");
    EXPECT_EQ(table.rows().front().index, 1u) << "the rows are kept in index order";
    EXPECT_EQ(table.rows().back().index, 5u);
}

/** @brief Tests that an index no row holds, before, between and past the rows, has none. */
TEST(Small_NodeTable, AnIndexPastTheTableHasNoRow)
{
    const NodeTable table(std::vector<NodeTableRow>{row_of(1, "b"), row_of(3, "d")});

    EXPECT_EQ(table.find(0), nullptr);
    EXPECT_EQ(table.find(2), nullptr);
    EXPECT_EQ(table.find(4), nullptr);
    EXPECT_EQ(table.find(0xFFFFFFFFu), nullptr);

    const NodeTable empty;
    EXPECT_EQ(empty.size(), 0u);
    EXPECT_EQ(empty.find(0), nullptr);
}

/** @brief Tests that a cost a caller never stated stays apart from a stated cost of zeros. */
TEST(Small_NodeTable, KeepsAnAbsentCostApartFromAStatedZero)
{
    NodeTableRow none = row_of(0, "none");
    NodeTableRow zero = row_of(1, "zero");
    zero.cost = NodeCost{};
    NodeTableRow stated = row_of(2, "stated");
    stated.cost = NodeCost{1024, 64, NodeDType::F32};
    const NodeTable table(std::vector<NodeTableRow>{none, zero, stated});

    EXPECT_FALSE(table.find(0)->cost.has_value());
    ASSERT_TRUE(table.find(1)->cost.has_value());
    EXPECT_EQ(table.find(1)->cost->flops, 0u);
    EXPECT_EQ(table.find(1)->cost->bytes, 0u);
    ASSERT_TRUE(table.find(2)->cost.has_value());
    EXPECT_EQ(table.find(2)->cost->flops, 1024u);
    EXPECT_EQ(table.find(2)->cost->dtype, NodeDType::F32);
}
