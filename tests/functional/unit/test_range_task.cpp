/**************************************************************************/
/* test_range_task.cpp                                                   */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/* All Rights Reserved.                                                   */
/*                                                                        */
/* CONFIDENTIAL: This software is the proprietary information of          */
/* Mustafa Garip & Sushi Systems. Unauthorized copying of this file,      */
/* via any medium is strictly prohibited.                                 */
/*                                                                        */
/* This source code and the intellectual property contained herein        */
/* is confidential and may not be disclosed, copied, or used without      */
/* explicit written permission from the copyright holders.                */
/**************************************************************************/

/**
 * @file test_range_task.cpp
 * @brief Unit coverage for the divisible-workload primitive (RangeTask::split).
 *
 * RangeTask is the splittable Task the work-stealing scheduler uses to rebalance
 * a large divisible range: an idle worker that picks up a splittable task carves
 * off a chunk (split()) and pushes it back for another worker to grab. These
 * tests pin the split CONTRACT, which is what the scheduler relies on:
 *   - the splittability threshold (a range must exceed the minimum grain),
 *   - split() yields a real, distinct sub-task while there is enough work,
 *   - repeated splitting strictly shrinks the owner and terminates,
 *   - once below the grain, split() reports "nothing to hand off" (nullopt).
 *
 * All checks are deterministic and host-only: split() is pure atomic bookkeeping
 * and never touches a device, so no RuntimeContext or SYCL queue is needed.
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
    // The split logic does no real compute; a no-op body keeps the test focused
    // on the partitioning bookkeeping rather than any kernel behaviour.
    auto noop_kernel()
    {
        return [](size_t, size_t, sycl::handler&) {};
    }
}

/**
 * @test SplittableOnlyAboveGrain
 * @brief A range is splittable only when it is strictly larger than the minimum
 * grain (1024). At or below the grain it must report not-splittable, so the
 * scheduler stops subdividing tiny ranges.
 */
TEST(RangeTaskTest, SplittableOnlyAboveGrain)
{
    RangeTask big(size_t{0}, size_t{4096}, noop_kernel());
    EXPECT_TRUE(big.is_splittable()) << "A range well above the grain must be splittable.";

    // Exactly the grain is NOT splittable: the contract is strictly greater-than.
    RangeTask at_grain(size_t{0}, size_t{1024}, noop_kernel());
    EXPECT_FALSE(at_grain.is_splittable());

    RangeTask tiny(size_t{0}, size_t{100}, noop_kernel());
    EXPECT_FALSE(tiny.is_splittable());

    // A below-grain range must hand off nothing.
    EXPECT_FALSE(tiny.split().has_value())
        << "split() on a below-grain range must return nullopt (nothing to steal).";
}

/**
 * @test SplitHandsOffDistinctChunksUntilExhausted
 * @brief Repeatedly splitting a large range must keep producing real, distinct
 * sub-tasks while the owner stays above the grain, strictly shrink the owner on
 * every successful split, and finally converge to a non-splittable state where
 * split() returns nullopt. This is exactly the loop a worker drives to rebalance
 * a divisible task.
 */
TEST(RangeTaskTest, SplitHandsOffDistinctChunksUntilExhausted)
{
    sushi_ptr<Task> owner = make_sushi<RangeTask>(size_t{0}, size_t{1u << 16}, noop_kernel());
    ASSERT_TRUE(owner->is_splittable());

    // Keep every chunk alive so freed-then-reused addresses can't masquerade as
    // duplicates; this lets us assert genuine distinctness.
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
