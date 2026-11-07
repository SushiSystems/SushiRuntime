/**************************************************************************/
/* test_range_task.cpp                                                    */
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
 * @file test_range_task.cpp
 * @brief Tests RangeTask for its split threshold, the coverage of its parts and its range check.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <sycl/sycl.hpp>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/scheduler/range_task.hpp>
#include <SushiRuntime/task/task.hpp>


using namespace SushiRuntime;
using SushiRuntime::Internal::Scheduler::Task;
using SushiRuntime::Internal::Scheduler::RangeTask;

namespace
{
    /** @brief The most chunks a test lets a range split into, so a defect fails the test and does not hang it. */
    constexpr std::size_t K_MAX_PARTS = 1000;

    /**
     * @brief Creates a no-operation kernel for range task testing.
     *
     * Used for tests focusing on partitioning bookkeeping where compute logic is unnecessary.
     *
     * @return A callable lambda that performs no operation.
     */
    auto noop_kernel()
    {
        return [](size_t, size_t, sycl::handler&) {};
    }

    /** @brief The chunks a range handed off, and the address of each. */
    struct HandedOff
    {
        /** @brief Every chunk, kept alive so a reused address cannot pass for a duplicate. */
        std::vector<sushi_ptr<Task>> chunks;
        /** @brief The distinct addresses of the chunks. */
        std::unordered_set<void*> addresses;
    };

    /** @brief Splits @p owner until it is no longer splittable, or K_MAX_PARTS chunks were handed off. */
    HandedOff hand_off_until_exhausted(Task& owner)
    {
        HandedOff handed;
        while (owner.is_splittable() && handed.chunks.size() < K_MAX_PARTS)
        {
            std::optional<sushi_ptr<Task>> chunk = owner.split();
            if (!chunk.has_value())
                break;
            handed.addresses.insert(chunk->get());
            handed.chunks.push_back(*chunk);
        }
        return handed;
    }

    /** @brief Splits every part in turn, as far as split() allows, and returns all the parts, the first one included. */
    std::vector<sushi_ptr<Task>> split_into_parts(sushi_ptr<Task> whole)
    {
        std::vector<sushi_ptr<Task>> parts;
        parts.push_back(std::move(whole));
        for (std::size_t i = 0; i < parts.size() && parts.size() < K_MAX_PARTS; ++i)
        {
            HandedOff handed = hand_off_until_exhausted(*parts[i]);
            parts.insert(parts.end(), handed.chunks.begin(), handed.chunks.end());
        }
        return parts;
    }

    /** @brief Describes the first gap or overlap of @p ranges over [@p begin, @p end), or returns an empty text. */
    std::string coverage_defect(std::vector<std::pair<size_t, size_t>> ranges, size_t begin, size_t end)
    {
        std::sort(ranges.begin(), ranges.end());
        size_t cursor = begin;
        for (const std::pair<size_t, size_t>& range : ranges)
        {
            if (range.first != cursor)
                return "a gap or an overlap starts at " + std::to_string(cursor);
            if (range.first >= range.second)
                return "an empty part starts at " + std::to_string(range.first);
            cursor = range.second;
        }
        return cursor == end ? std::string{} : "the parts stop at " + std::to_string(cursor);
    }

    /** @brief One range, whether it splits, and the answer is_splittable and split must both give. */
    struct ThresholdRow
    {
        /** @brief The first index of the range. */
        size_t offset;
        /** @brief The size of the range. */
        size_t size;
        /** @brief Whether the range splits. */
        bool splits;
    };
} // namespace

/** @brief Fixture of the rows of the split threshold. */
class Small_RangeTaskThreshold : public ::testing::TestWithParam<ThresholdRow>
{
};

/**
 * @brief Tests that a range splits only when the half it keeps reaches the grain of 1024, from any offset.
 *
 * The first size that splits is 2047. The offset keeps the answer a function of the size, not of
 * where the range starts, and a range that does not split hands off nothing.
 */
TEST_P(Small_RangeTaskThreshold, IsSplittableAgreesWithSplitAroundTheThreshold)
{
    const ThresholdRow row = GetParam();
    RangeTask task(row.offset, row.offset + row.size, noop_kernel());

    EXPECT_EQ(task.is_splittable(), row.splits);
    EXPECT_EQ(task.split().has_value(), row.splits);
}

INSTANTIATE_TEST_SUITE_P(Sizes, Small_RangeTaskThreshold,
                         ::testing::Values(ThresholdRow{0, 100, false}, ThresholdRow{0, 1024, false},
                                           ThresholdRow{0, 1025, false}, ThresholdRow{0, 2046, false},
                                           ThresholdRow{0, 2047, true}, ThresholdRow{0, 2048, true},
                                           ThresholdRow{0, 4096, true}, ThresholdRow{4096, 1024, false},
                                           ThresholdRow{4096, 1025, false}, ThresholdRow{4096, 2046, false},
                                           ThresholdRow{4096, 2047, true}, ThresholdRow{4096, 2048, true}));

/** @brief Tests that repeated splitting hands off a distinct chunk each time, until the owner is below the grain. */
TEST(Small_RangeTask, SplitHandsOffDistinctChunksUntilExhausted)
{
    sushi_ptr<Task> owner = make_sushi<RangeTask>(size_t{0}, size_t{1u << 16}, noop_kernel());

    const HandedOff handed = hand_off_until_exhausted(*owner);

    EXPECT_GT(handed.chunks.size(), 1u) << "A 64K range should split several times before reaching the grain.";
    EXPECT_LT(handed.chunks.size(), K_MAX_PARTS) << "Splitting failed to converge.";
    EXPECT_EQ(handed.addresses.size(), handed.chunks.size()) << "split() handed out the same sub-task twice.";
    EXPECT_FALSE(owner->is_splittable());
}

/** @brief Tests that an owner below the grain has nothing left to hand off. */
TEST(Small_RangeTask, AnExhaustedOwnerHandsOffNothing)
{
    sushi_ptr<Task> owner = make_sushi<RangeTask>(size_t{0}, size_t{1u << 16}, noop_kernel());
    static_cast<void>(hand_off_until_exhausted(*owner));

    EXPECT_FALSE(owner->split().has_value());
}

/** @brief Tests that an empty range is accepted and does not split. */
TEST(Small_RangeTask, AnEmptyRangeIsAcceptedAndDoesNotSplit)
{
    RangeTask empty(size_t{8}, size_t{8}, noop_kernel());

    EXPECT_FALSE(empty.is_splittable());
    EXPECT_FALSE(empty.split().has_value());
}

/** @brief Tests that the parts a range splits into cover it once, with no gap and no overlap. */
TEST(Small_RangeTask, SplitPartsCoverTheRangeExactlyOnce)
{
    constexpr size_t K_BEGIN = 37;
    constexpr size_t K_END = K_BEGIN + 10007;
    const std::vector<sushi_ptr<Task>> parts =
        split_into_parts(make_sushi<RangeTask>(K_BEGIN, K_END, noop_kernel()));
    ASSERT_GT(parts.size(), 1u);

    std::vector<std::pair<size_t, size_t>> ranges;
    for (const sushi_ptr<Task>& part : parts)
        ranges.push_back(static_cast<const RangeTask&>(*part).range());

    EXPECT_EQ(coverage_defect(ranges, K_BEGIN, K_END), "");
}

/** @brief Tests that a reversed range stops the process at construction. A death test starts a child process. */
TEST(Medium_RangeTaskDeathTest, AReversedRangeStopsAtConstruction)
{
    EXPECT_DEATH(RangeTask(size_t{8}, size_t{4}, noop_kernel()), "the reversed range .8, 4.");
}
