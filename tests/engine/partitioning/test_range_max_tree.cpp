/**************************************************************************/
/* test_range_max_tree.cpp                                                */
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

/**
 * @file test_range_max_tree.cpp
 * @brief Tests RangeMaxTree against a plain row of slots.
 * @see tests/engine/partitioning/README.md
 * @author Mustafa Garip
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <SushiRuntime/partitioning/range_max_tree.hpp>

#include "random_partition_case.hpp"

using namespace SushiRuntime;
using namespace SushiRuntime::Internal::Coherence;

namespace
{
    /** @brief Returns the largest slot of row from first to last. */
    [[nodiscard]] std::int64_t row_max(const std::vector<std::int64_t>& row, std::size_t first, std::size_t last)
    {
        return *std::max_element(row.begin() + static_cast<std::ptrdiff_t>(first),
                                 row.begin() + static_cast<std::ptrdiff_t>(last) + 1);
    }

    /** @brief Returns a delta over slots with first and last drawn from draw. */
    [[nodiscard]] RangeMaxTree::Delta draw_delta(SeededDraw& draw, std::size_t slots)
    {
        std::size_t first = static_cast<std::size_t>(draw.between(0, slots - 1));
        std::size_t last = static_cast<std::size_t>(draw.between(0, slots - 1));
        if (last < first)
            std::swap(first, last);
        return RangeMaxTree::Delta{first, last, static_cast<std::int64_t>(draw.between(0, 200)) - 100};
    }

    TEST(Small_RangeMaxTree, AdditionsAndQueriesMatchAPlainRow)
    {
        constexpr std::size_t K_SLOTS = 37;
        SeededDraw draw(5);
        RangeMaxTree tree(K_SLOTS);
        std::vector<std::int64_t> row(K_SLOTS, 0);
        for (int step = 0; step < 200; ++step)
        {
            const RangeMaxTree::Delta delta = draw_delta(draw, K_SLOTS);
            tree.add(delta);
            for (std::size_t slot = delta.first; slot <= delta.last; ++slot)
                row[slot] += delta.value;
            const RangeMaxTree::Delta query = draw_delta(draw, K_SLOTS);
            ASSERT_EQ(tree.max(query.first, query.last), row_max(row, query.first, query.last));
            ASSERT_EQ(tree.max_all(), row_max(row, 0, K_SLOTS - 1));
        }
    }

    TEST(Small_RangeMaxTree, PeakWithAnswersWithoutChangingTheRow)
    {
        constexpr std::size_t K_SLOTS = 23;
        SeededDraw draw(11);
        RangeMaxTree tree(K_SLOTS);
        std::vector<std::int64_t> row(K_SLOTS, 0);
        for (int step = 0; step < 100; ++step)
        {
            std::vector<RangeMaxTree::Delta> deltas;
            std::vector<std::int64_t> trial = row;
            for (int count = 0; count < 4; ++count)
            {
                deltas.push_back(draw_delta(draw, K_SLOTS));
                for (std::size_t slot = deltas.back().first; slot <= deltas.back().last; ++slot)
                    trial[slot] += deltas.back().value;
            }
            ASSERT_EQ(tree.peak_with(deltas), row_max(trial, 0, K_SLOTS - 1));
            ASSERT_EQ(tree.max_all(), row_max(row, 0, K_SLOTS - 1));
            tree.add(deltas.front());
            for (std::size_t slot = deltas.front().first; slot <= deltas.front().last; ++slot)
                row[slot] += deltas.front().value;
        }
    }
} // namespace
