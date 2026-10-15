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
 * @file test_range_task.cpp
 * @brief Tests RangeTask for its split threshold, the coverage of its parts and its range check.
 * @see tests/engine/scheduler/README.md
 * @author Mustafa Garip
 */

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
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
}

/**
 * @brief Verifies that a range task is splittable only when the half it keeps reaches the grain.
 *
 * Asserts that a range well above the grain (1024) is splittable, while a range of exactly the
 * grain or less is not, and returns nullopt when split.
 */
TEST(Medium_RangeTaskTest, SplittableOnlyAboveGrain)
{
    RangeTask big(size_t{0}, size_t{4096}, noop_kernel());
    EXPECT_TRUE(big.is_splittable()) << "A range well above the grain must be splittable.";

    // A range of exactly the grain would keep half a grain, so it does not split.
    RangeTask at_grain(size_t{0}, size_t{1024}, noop_kernel());
    EXPECT_FALSE(at_grain.is_splittable());

    RangeTask tiny(size_t{0}, size_t{100}, noop_kernel());
    EXPECT_FALSE(tiny.is_splittable());

    EXPECT_FALSE(tiny.split().has_value())
        << "split() on a below-grain range must return nullopt (nothing to steal).";
}

/**
 * @brief Verifies that repeated splitting yields distinct sub-tasks and eventually converges.
 *
 * Repeatedly splits a large range task, tracking each yielded chunk to ensure they are distinct, and asserts the owner becomes non-splittable once exhausted.
 */
TEST(Medium_RangeTaskTest, SplitHandsOffDistinctChunksUntilExhausted)
{
    sushi_ptr<Task> owner = make_sushi<RangeTask>(size_t{0}, size_t{1u << 16}, noop_kernel());
    ASSERT_TRUE(owner->is_splittable());

    // Every chunk stays alive, so a reused address cannot pass for a duplicate; see README.md.
    std::vector<sushi_ptr<Task>> chunks;
    std::unordered_set<void*> seen;

    int splits = 0;
    while (owner->is_splittable())
    {
        std::optional<sushi_ptr<Task>> chunk = owner->split();
        ASSERT_TRUE(chunk.has_value()) << "A splittable range must yield a chunk.";
        ASSERT_NE(chunk->get(), nullptr) << "Handed-off chunk must be a real task, not null.";

        EXPECT_TRUE(seen.insert(chunk->get()).second)
            << "split() handed out the same sub-task twice (live aliasing).";

        chunks.push_back(*chunk);
        ASSERT_LT(++splits, 1000) << "Splitting failed to converge — possible infinite subdivision.";
    }

    EXPECT_GT(splits, 1) << "A 64K range should split several times before reaching the grain.";
    EXPECT_FALSE(owner->is_splittable());
    EXPECT_FALSE(owner->split().has_value())
        << "Once below the grain, split() must report nothing to hand off.";
}

/**
 * @brief is_splittable() and split() give the same answer on both sides of the threshold.
 */
TEST(Medium_RangeTaskTest, IsSplittableAgreesWithSplitAroundTheThreshold)
{
    // The offset keeps the answer a function of the size, not of where the range starts.
    constexpr size_t K_OFFSETS[] = {0, 4096};
    constexpr size_t K_SIZES[] = {1024, 1025, 2046, 2047, 2048};
    constexpr bool K_EXPECTED[] = {false, false, false, true, true};

    for (const size_t offset : K_OFFSETS)
    {
        for (size_t i = 0; i < std::size(K_SIZES); ++i)
        {
            RangeTask task(offset, offset + K_SIZES[i], noop_kernel());
            const bool splittable = task.is_splittable();
            const bool split = task.split().has_value();

            EXPECT_EQ(splittable, K_EXPECTED[i]) << "size " << K_SIZES[i] << ", offset " << offset;
            EXPECT_EQ(splittable, split) << "size " << K_SIZES[i] << ", offset " << offset;
        }
    }
}

/**
 * @brief The parts a range splits into cover it once, with no gap and no overlap.
 */
TEST(Medium_RangeTaskTest, SplitPartsCoverTheRangeExactlyOnce)
{
    constexpr size_t K_BEGIN = 37;
    constexpr size_t K_END = K_BEGIN + 10007;

    std::vector<std::pair<size_t, size_t>> ranges;
    const auto recording_kernel = [&ranges](size_t start, size_t end, sycl::handler& handler)
    {
        ranges.emplace_back(start, end);
        handler.single_task([] {});
    };

    // A chunk is split in turn, so every part is taken as far down as split() allows.
    std::vector<sushi_ptr<Task>> parts;
    parts.push_back(make_sushi<RangeTask>(K_BEGIN, K_END, recording_kernel));
    for (size_t i = 0; i < parts.size(); ++i)
    {
        while (parts[i]->is_splittable())
        {
            std::optional<sushi_ptr<Task>> chunk = parts[i]->split();
            ASSERT_TRUE(chunk.has_value()) << "A splittable range must yield a chunk.";
            parts.push_back(*chunk);
            ASSERT_LT(parts.size(), 1000u) << "Splitting failed to converge.";
        }
    }
    ASSERT_GT(parts.size(), 1u);

    sycl::queue queue;
    for (const sushi_ptr<Task>& part : parts)
        queue.submit([&part](sycl::handler& handler) { part->execute(handler); }).wait();

    ASSERT_EQ(ranges.size(), parts.size()) << "A part executed no range or more than one.";
    std::sort(ranges.begin(), ranges.end());

    size_t cursor = K_BEGIN;
    for (const std::pair<size_t, size_t>& range : ranges)
    {
        EXPECT_EQ(range.first, cursor) << "A gap or an overlap starts at " << cursor;
        EXPECT_LT(range.first, range.second);
        cursor = range.second;
    }
    EXPECT_EQ(cursor, K_END);
}

/**
 * @brief A reversed range stops the process at construction.
 */
TEST(Medium_RangeTaskDeathTest, AReversedRangeStopsAtConstruction)
{
    EXPECT_DEATH(RangeTask(size_t{8}, size_t{4}, noop_kernel()), "the reversed range .8, 4.");
}

/**
 * @brief An empty range is accepted and does not split.
 */
TEST(Medium_RangeTaskTest, AnEmptyRangeIsAcceptedAndDoesNotSplit)
{
    RangeTask empty(size_t{8}, size_t{8}, noop_kernel());
    EXPECT_FALSE(empty.is_splittable());
    EXPECT_FALSE(empty.split().has_value());
}
