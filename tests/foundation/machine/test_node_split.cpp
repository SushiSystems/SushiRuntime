/**************************************************************************/
/* test_node_split.cpp                                                    */
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
 * @file test_node_split.cpp
 * @brief Tests NodeSplit, how a node states that it may run in parts.
 * @see tests/foundation/machine/README.md
 * @author Mustafa Garip
 */

#include <vector>

#include <gtest/gtest.h>

#include <SushiRuntime/machine/node_split.hpp>

using namespace SushiRuntime;

/** @brief Tests that a default split names axis zero, no extent and no kernel. */
TEST(Small_NodeSplit, ADefaultSplitHoldsNoKernel)
{
    const NodeSplit split;

    EXPECT_EQ(split.axis, 0u);
    EXPECT_EQ(split.extent, 0u);
    EXPECT_FALSE(split.regions);
    EXPECT_FALSE(split.slice);
    EXPECT_FALSE(split.combine);
}

/**
 * @brief Tests that the slice takes a part, a queue and the bound pointers, and the combine sees
 *        the parts in order.
 */
TEST(Small_NodeSplit, HoldsTheConsumersKernels)
{
    std::vector<SplitPart> combined;
    NodeSplit split;
    split.axis = 1;
    split.extent = 10;
    split.slice = [](const SplitPart&, sycl::queue&, span<const sycl::event>, BoundOperands) { return sycl::event(); };
    split.combine = [&combined](span<const SplitPart> parts)
    {
        combined.assign(parts.begin(), parts.end());
    };

    const std::vector<SplitPart> parts = {{1, 0, 6, 0, 2}, {1, 6, 10, 1, 2}};
    split.combine(span<const SplitPart>(parts.data(), parts.size()));

    EXPECT_TRUE(split.slice);
    EXPECT_EQ(combined, parts);
}
