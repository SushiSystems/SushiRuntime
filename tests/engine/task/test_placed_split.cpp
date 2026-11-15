/**************************************************************************/
/* test_placed_split.cpp                                                  */
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
 * @file test_placed_split.cpp
 * @brief Tests that a node holds no placed split until one is given, and keeps the parts in slice order.
 * @see tests/engine/task/README.md
 * @author Mustafa Garip
 */

#include <memory>

#include <gtest/gtest.h>

#include <SushiRuntime/task/node.hpp>
#include <SushiRuntime/task/placed_split.hpp>

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Graph;

namespace
{
    TEST(Small_PlacedSplit, ANodeRunsWholeUntilItIsGivenPartsWhichItKeepsInSliceOrder)
    {
        Node node;
        EXPECT_EQ(node.placed_split, nullptr);
        EXPECT_EQ(node.kernel_pointers(), nullptr);

        auto placed = std::make_shared<PlacedSplit>();
        placed->parts.push_back(SplitDispatchPart{1, SplitPart{0, 0, 3, 0, 2}});
        placed->parts.push_back(SplitDispatchPart{0, SplitPart{0, 3, 8, 1, 2}});
        placed->access.resize(2);
        placed->access[1].writes.emplace_back(GlobalHandle::from_words(1, 1), 12, 20);
        node.placed_split = placed;

        ASSERT_NE(node.placed_split, nullptr);
        EXPECT_EQ(node.placed_split->parts[0].device, 1u);
        EXPECT_EQ(node.placed_split->parts[1].part.begin, 3u);
        EXPECT_EQ(node.placed_split->access[1].writes[0].offset, 12u);
    }
} // namespace
