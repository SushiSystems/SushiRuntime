/**************************************************************************/
/* test_node_widths.cpp                                                   */
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
 * @file test_node_widths.cpp
 * @brief Tests snapshot_widths(): the baked width, the late count, and widths too wide for 32 bits.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/report/dispatch_width.hpp>
#include <SushiRuntime/task/late_bindings.hpp>
#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/node_widths.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    /** @brief Gives @p node the baked width 64 and late bindings that hold their defaults. */
    void bake_with_default_bindings(Node& node)
    {
        node.baked_width = 64;
        node.bindings_ = std::make_shared<LateBindings>();
    }
} // namespace

/** @brief Tests that a node with a baked width and no bindings reports it as launched. */
TEST(Small_NodeWidths, ABakedNodeReportsItsWidthAsLaunched)
{
    Node node;
    node.baked_width = 64;

    EXPECT_EQ(snapshot_widths(node).launched, 64u);
}

/** @brief Tests that a node with a baked width and no bindings reports it as live. */
TEST(Small_NodeWidths, ABakedNodeReportsItsWidthAsLive)
{
    Node node;
    node.baked_width = 64;

    EXPECT_EQ(snapshot_widths(node).live, 64u);
}

/** @brief Tests that a node with no baked width reports an unknown width. */
TEST(Small_NodeWidths, ANodeWithNoBakedWidthReportsUnknown)
{
    EXPECT_EQ(snapshot_widths(Node()).launched, K_WIDTH_UNKNOWN);
}

/** @brief Tests that default bindings report the baked width as live. */
TEST(Small_NodeWidths, DefaultBindingsReportTheBakedWidthAsLive)
{
    Node node;
    bake_with_default_bindings(node);

    EXPECT_EQ(snapshot_widths(node).live, 64u) << "the baked width was not used by default";
}

/** @brief Tests that a late count sets the live width. */
TEST(Small_NodeWidths, ALateCountSetsTheLiveWidth)
{
    Node node;
    bake_with_default_bindings(node);
    node.bindings_->count.store(24);

    EXPECT_EQ(snapshot_widths(node).live, 24u);
}

/** @brief Tests that a late count leaves the launched width alone. */
TEST(Small_NodeWidths, ALateCountLeavesTheLaunchedWidthAlone)
{
    Node node;
    bake_with_default_bindings(node);
    node.bindings_->count.store(24);

    EXPECT_EQ(snapshot_widths(node).launched, 64u);
}

/** @brief Tests that a width beyond 32 bits is reported as unknown. */
TEST(Small_NodeWidths, AWidthBeyondThirtyTwoBitsIsUnknown)
{
    Node node;
    node.baked_width = std::size_t{1} << 40;
    EXPECT_EQ(snapshot_widths(node).launched, K_WIDTH_UNKNOWN);
}
