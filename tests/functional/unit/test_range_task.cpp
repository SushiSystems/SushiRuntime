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
 * @brief Tests RangeTask::split for its grain threshold and distinct chunks until exhaustion.
 * @see tests/functional/unit/README.md
 * @author Mustafa Garip
 */

#include <cstddef>
#include <optional>
#include <unordered_set>
#include <vector>
#include <gtest/gtest.h>
#include <sycl/sycl.hpp>
#include <SushiRuntime/scheduler/task.hpp>

using namespace SushiRuntime;
using SushiRuntime::Scheduler::Task;
using SushiRuntime::Scheduler::RangeTask;

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
 * @brief Verifies that a range task is splittable only when strictly larger than its grain size.
 *
 * Asserts that sizes above the grain (1024) are splittable, while exact grain or smaller sizes are not, and return nullopt when split.
 */
TEST(Unit_RangeTaskTest, SplittableOnlyAboveGrain)
{
    RangeTask big(size_t{0}, size_t{4096}, noop_kernel());
    EXPECT_TRUE(big.is_splittable()) << "A range well above the grain must be splittable.";

    // Exactly the grain is NOT splittable: the contract is strictly greater-than.
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
TEST(Unit_RangeTaskTest, SplitHandsOffDistinctChunksUntilExhausted)
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
