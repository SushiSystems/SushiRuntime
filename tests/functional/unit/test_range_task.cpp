/**************************************************************************/
/* test_range_task.cpp                                                   */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                        https://sushisystems.io                         */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License");        */
/* you may not use this file except in compliance with the License.       */
/* You may obtain a copy of the License at                                */
/*                                                                        */
/*     http://www.apache.org/licenses/LICENSE-2.0                         */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

// Unit coverage for the divisible-workload primitive (RangeTask::split): the
// grain threshold, that split() yields a distinct sub-task while work remains,
// that repeated splitting shrinks the owner and terminates, and that it returns
// nullopt below the grain. All deterministic and host-only.

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
    // No-op body: the test is about partitioning bookkeeping, not compute.
    auto noop_kernel()
    {
        return [](size_t, size_t, sycl::handler&) {};
    }
}

// A range is splittable only when strictly larger than the grain (1024).
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

// Repeated splitting yields distinct sub-tasks, shrinks the owner each time, and
// converges to a non-splittable state — the loop a worker drives to rebalance.
TEST(Unit_RangeTaskTest, SplitHandsOffDistinctChunksUntilExhausted)
{
    sushi_ptr<Task> owner = make_sushi<RangeTask>(size_t{0}, size_t{1u << 16}, noop_kernel());
    ASSERT_TRUE(owner->is_splittable());

    // Keep every chunk alive so a freed-then-reused address can't look like a
    // duplicate; this lets us assert genuine distinctness.
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
